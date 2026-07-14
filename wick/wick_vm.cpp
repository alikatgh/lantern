// wick_vm.cpp — the wick stack VM, mark/sweep GC (host-triggered at frame
// boundaries), typed-native signature parsing, and the public embed API.
#include "wick_internal.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace wick {

// ------------------------------------------------------------------- heap

Obj* VM::newObj(Obj::K k) {
    Obj* o = new Obj();
    o->k = k;
    o->next = objects;
    objects = o;
    return o;
}

static void markValue(const Value& v) {
    if ((v.tag == Value::STR || v.tag == Value::LIST || v.tag == Value::MAP) &&
        v.o && !v.o->mark) {
        v.o->mark = true;
        if (v.o->k == Obj::LIST)
            for (const Value& e : v.o->list) markValue(e);
        else if (v.o->k == Obj::MAP)
            for (auto& kv : v.o->map) markValue(kv.second);
    }
}

void collect(VM* vm) {
    for (Value& v : vm->consts) markValue(v);
    for (Value& v : vm->globals) markValue(v);
    for (Value& v : vm->stack) markValue(v);
    for (Native& n : vm->natives)
        for (Value& d : n.defaults) markValue(d);
    Obj** p = &vm->objects;
    while (*p) {
        Obj* o = *p;
        if (o->mark) {
            o->mark = false;
            p = &o->next;
        } else {
            *p = o->next;
            delete o;
        }
    }
}

// -------------------------------------------------------------- lifecycle

VM* create() { return new VM(); }

void destroy(VM* vm) {
    Obj* o = vm->objects;
    while (o) {
        Obj* n = o->next;
        delete o;
        o = n;
    }
    delete vm;
}

void reset(VM* vm) {
    vm->consts.clear();
    vm->protos.clear();
    vm->fnByName.clear();
    vm->globals.clear();
    vm->gtypes.clear();
    vm->gByName.clear();
    vm->stack.clear();
    vm->rerr.clear();
    collect(vm); // consts/globals emptied: everything unreferenced dies
}

// ---------------------------------------------------- native registration

static bool parseSigType(const char*& p, Type& out, std::string& err) {
    while (*p == ' ') p++;
    const char* s = p;
    while (isalpha((unsigned char)*p)) p++;
    std::string n(s, p - s);
    if (n == "num") out = {Type::NUM, false, Type::NUM};
    else if (n == "bool") out = {Type::BOOL, false, Type::NUM};
    else if (n == "str") out = {Type::STR, false, Type::NUM};
    else if (n == "void") out = {Type::VOID, false, Type::NUM};
    else if (n == "list") out = {Type::LIST, false, Type::NUM}; // list<num>
    else { err = "bad type '" + n + "' in native signature"; return false; }
    if (*p == '?') { out.opt = true; p++; }
    return true;
}

bool addNative(VM* vm, const char* ns, const char* sig, NativeFn fn,
               std::string& err) {
    const char* p = sig;
    while (*p == ' ') p++;
    const char* s = p;
    while (isalnum((unsigned char)*p) || *p == '_') p++;
    Native n;
    n.name = (ns && *ns) ? std::string(ns) + "." + std::string(s, p - s)
                         : std::string(s, p - s);
    n.fn = fn;
    if (*p++ != '(') { err = "expected '(' in signature: " + std::string(sig); return false; }
    while (*p == ' ') p++;
    bool sawDefault = false;
    if (*p != ')') {
        for (;;) {
            Type t;
            if (!parseSigType(p, t, err)) return false;
            n.params.push_back(t);
            Value dflt = Value::nil();
            bool hasD = false;
            while (*p == ' ') p++;
            if (*p == '=') {
                p++;
                dflt = Value::num(std::strtod(p, const_cast<char**>(&p)));
                hasD = true;
                sawDefault = true;
            } else if (sawDefault) {
                err = "required parameter after a default in: " + n.name;
                return false;
            }
            n.defaults.push_back(hasD ? dflt : Value::nil());
            if (!hasD) n.minArgs = (int)n.params.size();
            while (*p == ' ') p++;
            if (*p == ',') { p++; continue; }
            break;
        }
    }
    if (*p++ != ')') { err = "expected ')' in signature: " + n.name; return false; }
    while (*p == ' ') p++;
    if (*p == ':') {
        p++;
        if (!parseSigType(p, n.ret, err)) return false;
    }
    if (vm->natByName.count(n.name)) { err = "duplicate native " + n.name; return false; }
    vm->natByName[n.name] = (int)vm->natives.size();
    vm->natives.push_back(std::move(n));
    return true;
}

void addConst(VM* vm, const char* ns, const char* name, double v) {
    std::string full = (ns && *ns) ? std::string(ns) + "." + name : name;
    vm->numConsts[full] = v;
}

// ------------------------------------------------------------ interpreter

static std::string valToStr(const Value& v) {
    switch (v.tag) {
    case Value::NIL: return "nil";
    case Value::BOOL: return v.b ? "true" : "false";
    case Value::NUM: {
        char buf[32];
        if (v.d == (long long)v.d && std::fabs(v.d) < 1e15)
            std::snprintf(buf, sizeof buf, "%lld", (long long)v.d);
        else
            std::snprintf(buf, sizeof buf, "%.14g", v.d);
        return buf;
    }
    case Value::STR: return v.o->s;
    case Value::LIST: return "<list>";
    case Value::MAP: return "<map>";
    }
    return "?";
}

static bool valEq(const Value& a, const Value& b) {
    if (a.tag != b.tag) return false;
    switch (a.tag) {
    case Value::NIL: return true;
    case Value::NUM: return a.d == b.d;
    case Value::BOOL: return a.b == b.b;
    case Value::STR: return a.o->s == b.o->s;
    default: return a.o == b.o;
    }
}

bool run(VM& vm, int protoIdx, const Value* args, int argc, Value& out,
         std::string& err) {
    struct Frame {
        int proto;
        size_t ip;
        size_t base;
    };
    std::vector<Frame> frames;
    std::vector<Value>& st = vm.stack;
    size_t stackFloor = st.size();

    auto pushFrame = [&](int pIdx, int nargs) {
        // args are already on the stack; locals live above them
        Frame f{pIdx, 0, st.size() - (size_t)nargs};
        st.resize(f.base + (size_t)vm.protos[pIdx].nlocals);
        frames.push_back(f);
    };
    for (int i = 0; i < argc; i++) st.push_back(args[i]);
    pushFrame(protoIdx, argc);

    auto rt = [&](const std::string& m, int line) {
        char buf[512];
        std::snprintf(buf, sizeof buf, "%s:%d: %s", vm.chunkName.c_str(), line,
                      m.c_str());
        err = buf;
        st.resize(stackFloor);
        return false;
    };

    while (!frames.empty()) {
        Frame& fr = frames.back();
        Proto& p = vm.protos[fr.proto];
        const uint8_t* code = p.code.data();
        #define LINE ((int)p.lines[fr.ip - 1])
        uint8_t op = code[fr.ip++];
        auto u16 = [&]() {
            uint16_t v = (uint16_t)(code[fr.ip] | (code[fr.ip + 1] << 8));
            fr.ip += 2;
            return v;
        };
        switch (op) {
        case OP_CONST: st.push_back(vm.consts[u16()]); break;
        case OP_NIL: st.push_back(Value::nil()); break;
        case OP_TRUE: st.push_back(Value::boolean(true)); break;
        case OP_FALSE: st.push_back(Value::boolean(false)); break;
        case OP_POP: st.pop_back(); break;
        case OP_LOADL: st.push_back(st[fr.base + code[fr.ip++]]); break;
        case OP_STOREL: {
            uint8_t slot = code[fr.ip++];
            st[fr.base + slot] = st.back();
            st.pop_back();
            break;
        }
        case OP_LOADG: st.push_back(vm.globals[u16()]); break;
        case OP_STOREG: {
            uint16_t slot = u16();
            vm.globals[slot] = st.back();
            st.pop_back();
            break;
        }
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: {
            double b = st.back().d; st.pop_back();
            double a = st.back().d;
            double r = op == OP_ADD ? a + b : op == OP_SUB ? a - b
                     : op == OP_MUL ? a * b : op == OP_DIV ? a / b
                     : std::fmod(a, b);
            st.back() = Value::num(r);
            break;
        }
        case OP_NEG: st.back() = Value::num(-st.back().d); break;
        case OP_CONCAT: {
            Value b = st.back(); st.pop_back();
            Value a = st.back();
            Obj* o = vm.newObj(Obj::STR);
            o->s = a.o->s + b.o->s;
            Value v; v.tag = Value::STR; v.o = o;
            st.back() = v;
            break;
        }
        case OP_EQ: case OP_NE: {
            Value b = st.back(); st.pop_back();
            Value a = st.back();
            bool eq = valEq(a, b);
            st.back() = Value::boolean(op == OP_EQ ? eq : !eq);
            break;
        }
        case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
            double b = st.back().d; st.pop_back();
            double a = st.back().d;
            bool r = op == OP_LT ? a < b : op == OP_LE ? a <= b
                   : op == OP_GT ? a > b : a >= b;
            st.back() = Value::boolean(r);
            break;
        }
        case OP_NOT: st.back() = Value::boolean(!st.back().b); break;
        case OP_JMP: { uint16_t o = u16(); fr.ip += o; break; }
        case OP_JF: {
            uint16_t o = u16();
            bool c = st.back().b;
            st.pop_back();
            if (!c) fr.ip += o;
            break;
        }
        case OP_JB: { uint16_t o = u16(); fr.ip -= o; break; }
        case OP_JNN: {
            uint16_t o = u16();
            if (st.back().tag != Value::NIL) fr.ip += o;
            else st.pop_back();
            break;
        }
        case OP_ISNIL: {
            bool n = st.back().tag == Value::NIL;
            st.back() = Value::boolean(n);
            break;
        }
        case OP_CALL: {
            uint16_t fnIdx = u16();
            uint8_t nargs = code[fr.ip++];
            pushFrame(fnIdx, nargs);
            break;
        }
        case OP_NCALL: {
            uint16_t ni = u16();
            uint8_t nargs = code[fr.ip++];
            Native& n = vm.natives[ni];
            vm.rerr.clear();
            Value r = n.fn(vm, st.data() + (st.size() - nargs), nargs);
            st.resize(st.size() - nargs);
            if (!vm.rerr.empty()) return rt(vm.rerr, LINE);
            st.push_back(r);
            break;
        }
        case OP_RET: case OP_RETV: {
            Value rv = op == OP_RETV ? st.back() : Value::nil();
            st.resize(frames.back().base);
            frames.pop_back();
            if (frames.empty()) {
                out = rv;
                st.resize(stackFloor);
                return true;
            }
            st.push_back(rv);
            break;
        }
        case OP_LIST: {
            uint8_t n = code[fr.ip++];
            Obj* o = vm.newObj(Obj::LIST);
            o->list.assign(st.end() - n, st.end());
            st.resize(st.size() - n);
            Value v; v.tag = Value::LIST; v.o = o;
            st.push_back(v);
            break;
        }
        case OP_MAP: {
            uint8_t n = code[fr.ip++];
            Obj* o = vm.newObj(Obj::MAP);
            for (int i = 0; i < n; i++) {
                Value val = st.back(); st.pop_back();
                Value key = st.back(); st.pop_back();
                o->map[key.o->s] = val;
            }
            Value v; v.tag = Value::MAP; v.o = o;
            st.push_back(v);
            break;
        }
        case OP_IGET: {
            int i = (int)st.back().d; st.pop_back();
            Obj* o = st.back().o;
            if (i < 0 || i >= (int)o->list.size())
                return rt("list index " + std::to_string(i) +
                          " out of range (len " +
                          std::to_string(o->list.size()) + ")", LINE);
            st.back() = o->list[(size_t)i];
            break;
        }
        case OP_ISET: {
            Value v = st.back(); st.pop_back();
            int i = (int)st.back().d; st.pop_back();
            Obj* o = st.back().o; st.pop_back();
            if (i < 0 || i >= (int)o->list.size())
                return rt("list index " + std::to_string(i) +
                          " out of range (len " +
                          std::to_string(o->list.size()) + ")", LINE);
            o->list[(size_t)i] = v;
            break;
        }
        case OP_MGET: {
            std::string k = st.back().o->s; st.pop_back();
            Obj* o = st.back().o;
            auto it = o->map.find(k);
            st.back() = it == o->map.end() ? Value::nil() : it->second;
            break;
        }
        case OP_MSET: {
            Value v = st.back(); st.pop_back();
            std::string k = st.back().o->s; st.pop_back();
            Obj* o = st.back().o; st.pop_back();
            o->map[k] = v;
            break;
        }
        case OP_LEN: {
            Value a = st.back();
            double n = a.tag == Value::STR ? (double)a.o->s.size()
                     : a.tag == Value::LIST ? (double)a.o->list.size()
                     : (double)a.o->map.size();
            st.back() = Value::num(n);
            break;
        }
        case OP_TOSTR: {
            Value a = st.back();
            if (a.tag == Value::STR) break;
            Obj* o = vm.newObj(Obj::STR);
            o->s = valToStr(a);
            Value v; v.tag = Value::STR; v.o = o;
            st.back() = v;
            break;
        }
        case OP_TONUM: {
            const std::string& s = st.back().o->s;
            char* end = nullptr;
            double d = std::strtod(s.c_str(), &end);
            while (end && *end == ' ') end++;
            if (s.empty() || !end || *end != '\0')
                st.back() = Value::nil();      // parse failure -> nil (num?)
            else
                st.back() = Value::num(d);
            break;
        }
        case OP_LPUSH: {
            Value v = st.back(); st.pop_back();
            st.back().o->list.push_back(v);
            st.back() = Value::nil(); // push() is void; caller POPs
            break;
        }
        case OP_LPOP: {
            Obj* o = st.back().o;
            if (o->list.empty()) st.back() = Value::nil();
            else {
                st.back() = o->list.back();
                o->list.pop_back();
            }
            break;
        }
        case OP_HALT:
            st.resize(stackFloor);
            out = Value::nil();
            return true;
        default:
            return rt("bad opcode", LINE);
        }
        #undef LINE
    }
    st.resize(stackFloor);
    out = Value::nil();
    return true;
}

// ------------------------------------------------------------- public API

// flat math/util natives every wick program gets
static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static Value nFloor(VM&, const Value* a, int) { return Value::num(std::floor(a[0].d)); }
static Value nCeil(VM&, const Value* a, int) { return Value::num(std::ceil(a[0].d)); }
static Value nEnv(VM& vm, const Value* a, int) {
    const char* v = std::getenv(getStr(a[0]).c_str());
    return v ? makeStr(vm, v) : Value::nil();
}
static Value nAbs(VM&, const Value* a, int) { return Value::num(std::fabs(a[0].d)); }
static Value nMin(VM&, const Value* a, int) { return Value::num(std::fmin(a[0].d, a[1].d)); }
static Value nMax(VM&, const Value* a, int) { return Value::num(std::fmax(a[0].d, a[1].d)); }
static Value nSqrt(VM&, const Value* a, int) { return Value::num(std::sqrt(a[0].d)); }
static Value nSin(VM&, const Value* a, int) { return Value::num(std::sin(a[0].d)); }
static Value nCos(VM&, const Value* a, int) { return Value::num(std::cos(a[0].d)); }
static Value nAtan(VM&, const Value* a, int) { return Value::num(std::atan2(a[0].d, a[1].d)); }
static Value nRand(VM&, const Value*, int) {
    // deterministic xorshift64* — same sequence on every machine
    g_rng ^= g_rng >> 12; g_rng ^= g_rng << 25; g_rng ^= g_rng >> 27;
    return Value::num((double)((g_rng * 0x2545F4914F6CDD1Dull) >> 11) /
                      9007199254740992.0);
}
static Value nSrand(VM&, const Value* a, int) {
    g_rng = (uint64_t)a[0].d | 1;
    return Value::nil();
}
static Value nCheck(VM& vm, const Value* a, int) {
    if (!a[0].b) setError(vm, "check failed: " + getStr(a[1]));
    return Value::nil();
}

static void installBuiltins(VM* vm) {
    std::string e;
    addNative(vm, nullptr, "floor(num): num", nFloor, e);
    addNative(vm, nullptr, "ceil(num): num", nCeil, e);
    addNative(vm, nullptr, "env(str): str?", nEnv, e);
    addNative(vm, nullptr, "abs(num): num", nAbs, e);
    addNative(vm, nullptr, "min(num, num): num", nMin, e);
    addNative(vm, nullptr, "max(num, num): num", nMax, e);
    addNative(vm, nullptr, "sqrt(num): num", nSqrt, e);
    addNative(vm, nullptr, "sin(num): num", nSin, e);
    addNative(vm, nullptr, "cos(num): num", nCos, e);
    addNative(vm, nullptr, "atan(num, num): num", nAtan, e);
    addNative(vm, nullptr, "rand(): num", nRand, e);
    addNative(vm, nullptr, "srand(num)", nSrand, e);
    addNative(vm, nullptr, "check(bool, str)", nCheck, e);
    addConst(vm, nullptr, "pi", 3.14159265358979323846);
}

bool load(VM* vm, const std::string& source, const std::string& chunkName,
          std::string& err) {
    if (vm->natByName.find("floor") == vm->natByName.end())
        installBuiltins(vm);
    if (!compile(*vm, source, chunkName, err)) return false;
    Value out;
    return run(*vm, 0, nullptr, 0, out, err);
}

bool call(VM* vm, const char* name, double arg, bool hasArg,
          std::string& err) {
    auto it = vm->fnByName.find(name);
    if (it == vm->fnByName.end()) return true; // optional callback
    Proto& p = vm->protos[it->second];
    Value a = Value::num(arg);
    Value out;
    int argc = (hasArg && p.nparams >= 1) ? 1 : 0;
    return run(*vm, it->second, argc ? &a : nullptr, argc, out, err);
}

void setError(VM& vm, const std::string& msg) { vm.rerr = msg; }

Value makeStr(VM& vm, const std::string& s) {
    Obj* o = vm.newObj(Obj::STR);
    o->s = s;
    Value v; v.tag = Value::STR; v.o = o;
    return v;
}
std::string getStr(const Value& v) {
    return v.tag == Value::STR && v.o ? v.o->s : std::string();
}
int listLen(const Value& v) {
    return v.tag == Value::LIST && v.o ? (int)v.o->list.size() : 0;
}
Value listGet(const Value& v, int i) {
    if (v.tag != Value::LIST || !v.o || i < 0 || i >= (int)v.o->list.size())
        return Value::nil();
    return v.o->list[(size_t)i];
}

} // namespace wick
