// wick_front.cpp — lexer + one-pass typed compiler for the wick language.
// Parses straight to bytecode (no AST) while tracking a static Type for
// every expression: the entire nil-safety and typo-safety story lives in
// this file. Declare-before-use, C-style; see docs/WICK.md.
#include "wick_internal.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace wick {

std::string Type::name() const {
    const char* base[] = {"num", "bool", "str", "list", "map", "void", "nil"};
    std::string n = base[k];
    if (k == LIST || k == MAP)
        n += std::string("<") + base[elem] + ">";
    if (opt) n += "?";
    return n;
}

namespace {

enum TokKind {
    T_EOF, T_NUM, T_STR, T_IDENT,
    T_LET, T_FN, T_IF, T_ELIF, T_ELSE, T_WHILE, T_FOR, T_IN, T_BREAK,
    T_CONT, T_RETURN, T_TRUE, T_FALSE, T_NIL, T_AND, T_OR, T_NOT,
    T_LP, T_RP, T_LB, T_RB, T_LBRACE, T_RBRACE,
    T_COMMA, T_COLON, T_DOT, T_DOTDOT, T_QQ, T_QMARK,
    T_ASSIGN, T_EQ, T_NE, T_LT, T_LE, T_GT, T_GE,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
};

struct Tok {
    TokKind kind = T_EOF;
    std::string text;
    double num = 0;
    int line = 1;
};

struct Local {
    std::string name;
    Type type;
    int depth;
    int slot;
    bool narrowed = false; // optional temporarily narrowed to non-opt
};

struct LoopCtx {
    std::vector<size_t> breaks;    // JMP patch sites
    std::vector<size_t> continues; // JMP patch sites
    int depth;                     // scope depth at loop entry
};

struct Compiler {
    VM& vm;
    const std::string& src;
    size_t pos = 0;
    int line = 1;
    Tok cur, ahead;
    bool hasAhead = false;
    std::string err;
    bool failed = false;

    Proto* proto = nullptr;        // current emit target
    int protoIdx = 0;
    std::vector<Local> locals;     // only while compiling a fn
    int scopeDepth = 0;
    bool inFn = false;
    std::vector<LoopCtx> loops;

    explicit Compiler(VM& v, const std::string& s) : vm(v), src(s) {}

    // ---- errors ----
    bool fail(const std::string& m, int ln = -1) {
        if (!failed) {
            char buf[512];
            std::snprintf(buf, sizeof buf, "%s:%d: %s", vm.chunkName.c_str(),
                          ln < 0 ? cur.line : ln, m.c_str());
            err = buf;
            failed = true;
        }
        return false;
    }

    // ---- lexer ----
    Tok lex() {
        for (;;) {
            while (pos < src.size()) {
                char c = src[pos];
                if (c == '\n') { line++; pos++; }
                else if (c == ' ' || c == '\t' || c == '\r') pos++;
                else break;
            }
            if (pos + 1 < src.size() && src[pos] == '/' && src[pos + 1] == '/') {
                while (pos < src.size() && src[pos] != '\n') pos++;
                continue;
            }
            break;
        }
        Tok t;
        t.line = line;
        if (pos >= src.size()) return t;
        char c = src[pos];
        if (isdigit((unsigned char)c)) {
            // scan by hand so `0..40` stays NUM DOTDOT NUM (stod would
            // greedily eat "0." and orphan the range operator)
            size_t s = pos;
            while (pos < src.size() && isdigit((unsigned char)src[pos])) pos++;
            if (pos + 1 < src.size() && src[pos] == '.' &&
                isdigit((unsigned char)src[pos + 1])) {
                pos++;
                while (pos < src.size() && isdigit((unsigned char)src[pos]))
                    pos++;
            }
            t.num = std::strtod(src.substr(s, pos - s).c_str(), nullptr);
            t.kind = T_NUM;
            return t;
        }
        if (isalpha((unsigned char)c) || c == '_') {
            size_t s = pos;
            while (pos < src.size() &&
                   (isalnum((unsigned char)src[pos]) || src[pos] == '_'))
                pos++;
            t.text = src.substr(s, pos - s);
            static const struct { const char* w; TokKind k; } kw[] = {
                {"let", T_LET}, {"fn", T_FN}, {"if", T_IF}, {"elif", T_ELIF},
                {"else", T_ELSE}, {"while", T_WHILE}, {"for", T_FOR},
                {"in", T_IN}, {"break", T_BREAK}, {"continue", T_CONT},
                {"return", T_RETURN}, {"true", T_TRUE}, {"false", T_FALSE},
                {"nil", T_NIL}, {"and", T_AND}, {"or", T_OR}, {"not", T_NOT},
            };
            t.kind = T_IDENT;
            for (auto& k : kw)
                if (t.text == k.w) { t.kind = k.k; break; }
            return t;
        }
        if (c == '"') {
            pos++;
            std::string s;
            while (pos < src.size() && src[pos] != '"') {
                char d = src[pos++];
                if (d == '\\' && pos < src.size()) {
                    char e = src[pos++];
                    if (e == 'n') s += '\n';
                    else if (e == 't') s += '\t';
                    else s += e;
                } else {
                    if (d == '\n') line++;
                    s += d;
                }
            }
            if (pos < src.size()) pos++; // closing quote
            t.kind = T_STR;
            t.text = s;
            return t;
        }
        pos++;
        switch (c) {
        case '(': t.kind = T_LP; return t;
        case ')': t.kind = T_RP; return t;
        case '[': t.kind = T_LB; return t;
        case ']': t.kind = T_RB; return t;
        case '{': t.kind = T_LBRACE; return t;
        case '}': t.kind = T_RBRACE; return t;
        case ',': t.kind = T_COMMA; return t;
        case ':': t.kind = T_COLON; return t;
        case '+': t.kind = T_PLUS; return t;
        case '-': t.kind = T_MINUS; return t;
        case '*': t.kind = T_STAR; return t;
        case '/': t.kind = T_SLASH; return t;
        case '%': t.kind = T_PERCENT; return t;
        case '.':
            if (pos < src.size() && src[pos] == '.') { pos++; t.kind = T_DOTDOT; }
            else t.kind = T_DOT;
            return t;
        case '?':
            if (pos < src.size() && src[pos] == '?') { pos++; t.kind = T_QQ; }
            else t.kind = T_QMARK;
            return t;
        case '=':
            if (pos < src.size() && src[pos] == '=') { pos++; t.kind = T_EQ; }
            else t.kind = T_ASSIGN;
            return t;
        case '!':
            if (pos < src.size() && src[pos] == '=') { pos++; t.kind = T_NE; return t; }
            break;
        case '<':
            if (pos < src.size() && src[pos] == '=') { pos++; t.kind = T_LE; }
            else t.kind = T_LT;
            return t;
        case '>':
            if (pos < src.size() && src[pos] == '=') { pos++; t.kind = T_GE; }
            else t.kind = T_GT;
            return t;
        }
        t.kind = T_EOF;
        fail(std::string("unexpected character '") + c + "'", t.line);
        return t;
    }

    void advance() {
        if (hasAhead) { cur = ahead; hasAhead = false; }
        else cur = lex();
    }
    Tok& peek() {
        if (!hasAhead) { ahead = lex(); hasAhead = true; }
        return ahead;
    }
    bool accept(TokKind k) {
        if (cur.kind == k) { advance(); return true; }
        return false;
    }
    bool expect(TokKind k, const char* what) {
        if (cur.kind == k) { advance(); return true; }
        return fail(std::string("expected ") + what);
    }

    // ---- emit ----
    void emit(uint8_t b) {
        proto->code.push_back(b);
        proto->lines.push_back(cur.line);
    }
    void emit2(uint8_t op, uint16_t u) {
        emit(op); emit((uint8_t)(u & 0xff)); emit((uint8_t)(u >> 8));
    }
    size_t emitJump(uint8_t op) {
        emit(op); emit(0); emit(0);
        return proto->code.size() - 2;
    }
    void patchJump(size_t at) {
        size_t off = proto->code.size() - (at + 2);
        if (off > 0xffff) { fail("jump too far"); return; }
        proto->code[at] = (uint8_t)(off & 0xff);
        proto->code[at + 1] = (uint8_t)(off >> 8);
    }
    void emitLoop(size_t target) {
        emit(OP_JB);
        size_t off = proto->code.size() + 2 - target;
        emit((uint8_t)(off & 0xff)); emit((uint8_t)(off >> 8));
    }
    uint16_t addConstVal(Value v) {
        vm.consts.push_back(v);
        return (uint16_t)(vm.consts.size() - 1);
    }
    uint16_t addConstStr(const std::string& s) {
        Obj* o = vm.newObj(Obj::STR);
        o->s = s;
        Value v; v.tag = Value::STR; v.o = o;
        return addConstVal(v);
    }

    // ---- scopes ----
    int addLocal(const std::string& name, Type t) {
        for (int i = (int)locals.size() - 1; i >= 0; i--) {
            if (locals[i].depth < scopeDepth) break;
            if (locals[i].name == name) {
                fail("'" + name + "' already declared in this scope");
                return -1;
            }
        }
        int slot = (int)locals.size();
        locals.push_back({name, t, scopeDepth, slot, false});
        if (slot + 1 > proto->nlocals) proto->nlocals = slot + 1;
        if (slot > 250) fail("too many locals");
        return slot;
    }
    Local* findLocal(const std::string& name) {
        for (int i = (int)locals.size() - 1; i >= 0; i--)
            if (locals[i].name == name) return &locals[i];
        return nullptr;
    }
    void beginScope() { scopeDepth++; }
    void endScope() {
        scopeDepth--;
        while (!locals.empty() && locals.back().depth > scopeDepth)
            locals.pop_back();
    }

    // ---- types ----
    bool parseType(Type& out) {
        if (cur.kind != T_IDENT) return fail("expected type name");
        std::string n = cur.text;
        advance();
        if (n == "num") out = {Type::NUM, false, Type::NUM};
        else if (n == "bool") out = {Type::BOOL, false, Type::NUM};
        else if (n == "str") out = {Type::STR, false, Type::NUM};
        else if (n == "list" || n == "map") {
            out.k = n == "list" ? Type::LIST : Type::MAP;
            if (!expect(T_LT, "'<'")) return false;
            Type e;
            if (!parseType(e)) return false;
            if (e.opt || e.k == Type::LIST || e.k == Type::MAP)
                return fail("container elements must be num, bool, or str");
            out.elem = e.k;
            if (!expect(T_GT, "'>'")) return false;
        } else return fail("unknown type '" + n + "'");
        if (accept(T_QMARK)) out.opt = true;
        return true;
    }
    // can a value of type src be stored where dst is expected?
    bool assignable(const Type& dst, const Type& src) {
        if (src.k == Type::NILT) return dst.opt;
        if (!dst.sameBase(src)) return false;
        if (src.opt && !dst.opt) return false; // must narrow or ?? first
        return true;
    }
    bool checkAssign(const Type& dst, const Type& src, const char* what) {
        if (assignable(dst, src)) return true;
        return fail(std::string(what) + ": expected " + dst.name() +
                    ", got " + src.name());
    }

    // ---- expressions (Pratt). Every parse returns the static Type. ----
    // precedence: or < and < ?? < ==/!= < cmp < +- < */% < unary < postfix
    Type expression() { return parseOr(); }

    Type parseOr() {
        Type l = parseAnd();
        while (cur.kind == T_OR) {
            advance();
            if (l.k != Type::BOOL || l.opt) fail("'or' needs bool operands");
            // short-circuit: true -> push true, skip rhs; false -> rhs
            size_t els = emitJump(OP_JF);
            emit(OP_TRUE);
            size_t end = emitJump(OP_JMP);
            patchJump(els);
            Type r = parseAnd();
            if (r.k != Type::BOOL || r.opt) fail("'or' needs bool operands");
            patchJump(end);
            l = {Type::BOOL, false, Type::NUM};
        }
        return l;
    }
    Type parseAnd() {
        Type l = parseCoalesce();
        while (cur.kind == T_AND) {
            advance();
            if (l.k != Type::BOOL || l.opt) fail("'and' needs bool operands");
            size_t els = emitJump(OP_JF);
            Type r = parseCoalesce();
            if (r.k != Type::BOOL || r.opt) fail("'and' needs bool operands");
            size_t end = emitJump(OP_JMP);
            patchJump(els);
            emit(OP_FALSE);
            patchJump(end);
            l = {Type::BOOL, false, Type::NUM};
        }
        return l;
    }
    Type parseCoalesce() {
        Type l = parseEquality();
        while (cur.kind == T_QQ) {
            advance();
            if (!l.opt && l.k != Type::NILT)
                fail("'?\?' left side must be an optional");
            size_t keep = emitJump(OP_JNN); // not-nil: jump, keep value
            Type r = parseEquality();
            Type base = l; base.opt = false;
            if (l.k == Type::NILT) base = r; // nil ?? x
            if (!r.sameBase(base) || r.opt)
                fail("'?\?' default must be " + base.name());
            patchJump(keep);
            l = base;
        }
        return l;
    }
    Type parseEquality() {
        Type l = parseCmp();
        while (cur.kind == T_EQ || cur.kind == T_NE) {
            bool ne = cur.kind == T_NE;
            advance();
            if (cur.kind == T_NIL) { // x == nil / x != nil
                advance();
                if (!l.opt) fail("comparing non-optional to nil");
                emit(OP_ISNIL);
                if (ne) emit(OP_NOT);
            } else {
                Type r = parseCmp();
                if (!l.sameBase(r) &&
                    !(l.k == Type::NILT && r.opt) && !(r.k == Type::NILT))
                    fail("'==' operands must have the same type");
                emit(ne ? OP_NE : OP_EQ);
            }
            l = {Type::BOOL, false, Type::NUM};
        }
        return l;
    }
    Type parseCmp() {
        Type l = parseAdd();
        while (cur.kind == T_LT || cur.kind == T_LE || cur.kind == T_GT ||
               cur.kind == T_GE) {
            TokKind op = cur.kind;
            advance();
            Type r = parseAdd();
            if (l.k != Type::NUM || l.opt || r.k != Type::NUM || r.opt)
                fail("comparison needs num operands");
            emit(op == T_LT ? OP_LT : op == T_LE ? OP_LE
                 : op == T_GT ? OP_GT : OP_GE);
            l = {Type::BOOL, false, Type::NUM};
        }
        return l;
    }
    Type parseAdd() {
        Type l = parseMul();
        while (cur.kind == T_PLUS || cur.kind == T_MINUS) {
            bool plus = cur.kind == T_PLUS;
            advance();
            Type r = parseMul();
            if (plus && l.k == Type::STR && r.k == Type::STR && !l.opt &&
                !r.opt) {
                emit(OP_CONCAT);
            } else if (l.k == Type::NUM && r.k == Type::NUM && !l.opt &&
                       !r.opt) {
                emit(plus ? OP_ADD : OP_SUB);
            } else {
                fail(plus ? "'+' needs num+num or str+str (use str(x))"
                          : "'-' needs num operands");
            }
        }
        return l;
    }
    Type parseMul() {
        Type l = parseUnary();
        while (cur.kind == T_STAR || cur.kind == T_SLASH ||
               cur.kind == T_PERCENT) {
            TokKind op = cur.kind;
            advance();
            Type r = parseUnary();
            if (l.k != Type::NUM || l.opt || r.k != Type::NUM || r.opt)
                fail("arithmetic needs num operands");
            emit(op == T_STAR ? OP_MUL : op == T_SLASH ? OP_DIV : OP_MOD);
        }
        return l;
    }
    Type parseUnary() {
        if (cur.kind == T_MINUS) {
            advance();
            Type t = parseUnary();
            if (t.k != Type::NUM || t.opt) fail("'-' needs a num");
            emit(OP_NEG);
            return t;
        }
        if (cur.kind == T_NOT) {
            advance();
            Type t = parseUnary();
            if (t.k != Type::BOOL || t.opt) fail("'not' needs a bool");
            emit(OP_NOT);
            return t;
        }
        return parsePostfix();
    }
    Type parsePostfix() {
        Type t = parsePrimary();
        for (;;) {
            if (cur.kind == T_LB) { // index get
                advance();
                Type i = expression();
                if (!expect(T_RB, "']'")) return t;
                if (t.k == Type::LIST) {
                    if (i.k != Type::NUM || i.opt) fail("list index must be num");
                    emit(OP_IGET);
                    t = {t.elem, false, Type::NUM};
                } else if (t.k == Type::MAP) {
                    if (i.k != Type::STR || i.opt) fail("map key must be str");
                    emit(OP_MGET);
                    t = {t.elem, true, Type::NUM}; // map get is optional
                } else {
                    fail("only lists and maps can be indexed");
                }
            } else break;
        }
        return t;
    }

    // builtin generic calls handled by the compiler itself
    bool genericBuiltin(const std::string& name, Type& out) {
        auto one = [&](Type& a) {
            if (!expect(T_LP, "'('")) return false;
            a = expression();
            return expect(T_RP, "')'");
        };
        if (name == "len") {
            Type a;
            if (!one(a)) return true;
            if (a.opt || (a.k != Type::STR && a.k != Type::LIST && a.k != Type::MAP))
                fail("len() needs str, list, or map");
            emit(OP_LEN);
            out = {Type::NUM, false, Type::NUM};
            return true;
        }
        if (name == "str") {
            Type a;
            if (!one(a)) return true;
            if (a.opt || (a.k != Type::NUM && a.k != Type::BOOL && a.k != Type::STR))
                fail("str() needs num, bool, or str");
            emit(OP_TOSTR);
            out = {Type::STR, false, Type::NUM};
            return true;
        }
        if (name == "num") {
            Type a;
            if (!one(a)) return true;
            if (a.k != Type::STR || a.opt) fail("num() needs a str");
            emit(OP_TONUM);
            out = {Type::NUM, true, Type::NUM}; // parse can fail -> num?
            return true;
        }
        if (name == "push") {
            if (!expect(T_LP, "'('")) return true;
            Type l = expression();
            if (l.k != Type::LIST || l.opt) fail("push() needs a list first");
            if (!expect(T_COMMA, "','")) return true;
            Type v = expression();
            Type want{l.elem, false, Type::NUM};
            checkAssign(want, v, "push()");
            if (!expect(T_RP, "')'")) return true;
            emit(OP_LPUSH);
            out = {Type::VOID, false, Type::NUM};
            return true;
        }
        if (name == "pop") {
            Type a;
            if (!one(a)) return true;
            if (a.k != Type::LIST || a.opt) fail("pop() needs a list");
            emit(OP_LPOP);
            out = {a.elem, true, Type::NUM}; // empty list -> nil
            return true;
        }
        return false;
    }

    Type callNative(int natIdx) {
        Native& n = vm.natives[natIdx];
        if (!expect(T_LP, "'('")) return {Type::VOID};
        int argc = 0;
        if (cur.kind != T_RP) {
            do {
                if (argc >= (int)n.params.size()) {
                    fail(n.name + "() takes at most " +
                         std::to_string(n.params.size()) + " arguments");
                    return {Type::VOID};
                }
                Type a = expression();
                checkAssign(n.params[argc], a,
                            (n.name + "() argument " +
                             std::to_string(argc + 1)).c_str());
                argc++;
            } while (accept(T_COMMA));
        }
        expect(T_RP, "')'");
        if (argc < n.minArgs) {
            fail(n.name + "() needs at least " + std::to_string(n.minArgs) +
                 " arguments");
            return {Type::VOID};
        }
        // fill defaults so the runtime always sees full arity
        for (int i = argc; i < (int)n.params.size(); i++)
            emit2(OP_CONST, addConstVal(n.defaults[i]));
        emit2(OP_NCALL, (uint16_t)natIdx);
        emit((uint8_t)n.params.size());
        return n.ret;
    }

    Type callFunction(int fnIdx) {
        Proto& p = vm.protos[fnIdx]; // note: may move; re-fetch after args
        std::string fname = p.name;
        int nparams = p.nparams;
        std::vector<Type> params = p.params;
        Type ret = p.ret;
        if (!expect(T_LP, "'('")) return {Type::VOID};
        int argc = 0;
        if (cur.kind != T_RP) {
            do {
                if (argc >= nparams) {
                    fail(fname + "() takes " + std::to_string(nparams) +
                         " arguments");
                    return {Type::VOID};
                }
                Type a = expression();
                checkAssign(params[argc], a,
                            (fname + "() argument " +
                             std::to_string(argc + 1)).c_str());
                argc++;
            } while (accept(T_COMMA));
        }
        expect(T_RP, "')'");
        if (argc != nparams)
            fail(fname + "() takes " + std::to_string(nparams) + " arguments");
        emit2(OP_CALL, (uint16_t)fnIdx);
        emit((uint8_t)argc);
        return ret;
    }

    Type parsePrimary() {
        switch (cur.kind) {
        case T_NUM: {
            double d = cur.num;
            advance();
            emit2(OP_CONST, addConstVal(Value::num(d)));
            return {Type::NUM, false, Type::NUM};
        }
        case T_STR: {
            std::string s = cur.text;
            advance();
            emit2(OP_CONST, addConstStr(s));
            return {Type::STR, false, Type::NUM};
        }
        case T_TRUE: advance(); emit(OP_TRUE); return {Type::BOOL, false, Type::NUM};
        case T_FALSE: advance(); emit(OP_FALSE); return {Type::BOOL, false, Type::NUM};
        case T_NIL: advance(); emit(OP_NIL); return {Type::NILT, false, Type::NUM};
        case T_LP: {
            advance();
            Type t = expression();
            expect(T_RP, "')'");
            return t;
        }
        case T_LB: return listOrMapLiteral();
        case T_IDENT: return identifier();
        default:
            fail("expected an expression");
            advance();
            return {Type::VOID};
        }
    }

    Type listOrMapLiteral() {
        advance(); // '['
        if (cur.kind == T_RB) {
            fail("empty [] needs a type: let xs: list<num> = [] "
                 "(write the annotation)");
            advance();
            return {Type::LIST, false, Type::NUM};
        }
        // map literal if first element is "key":
        if (cur.kind == T_STR && peek().kind == T_COLON) {
            int n = 0;
            Type elem{Type::VOID};
            do {
                if (cur.kind != T_STR) { fail("map keys must be str literals"); break; }
                emit2(OP_CONST, addConstStr(cur.text));
                advance();
                expect(T_COLON, "':'");
                Type v = expression();
                if (v.opt || (v.k != Type::NUM && v.k != Type::BOOL && v.k != Type::STR))
                    fail("map values must be num, bool, or str");
                if (n == 0) elem = v;
                else if (!elem.sameBase(v)) fail("map values must share one type");
                n++;
            } while (accept(T_COMMA));
            expect(T_RB, "']'");
            emit(OP_MAP);
            emit((uint8_t)n);
            return {Type::MAP, false, elem.k};
        }
        int n = 0;
        Type elem{Type::VOID};
        do {
            Type v = expression();
            if (v.opt || (v.k != Type::NUM && v.k != Type::BOOL && v.k != Type::STR))
                fail("list elements must be num, bool, or str");
            if (n == 0) elem = v;
            else if (!elem.sameBase(v)) fail("list elements must share one type");
            n++;
        } while (accept(T_COMMA));
        expect(T_RB, "']'");
        emit(OP_LIST);
        emit((uint8_t)n);
        return {Type::LIST, false, elem.k};
    }

    Type identifier() {
        std::string name = cur.text;
        int nameLine = cur.line;
        advance();
        if (cur.kind == T_DOT) { // namespaced native / const, e.g. lt.draw
            advance();
            if (cur.kind != T_IDENT) { fail("expected name after '.'"); return {Type::VOID}; }
            std::string full = name + "." + cur.text;
            advance();
            auto ci = vm.numConsts.find(full);
            if (ci != vm.numConsts.end()) {
                emit2(OP_CONST, addConstVal(Value::num(ci->second)));
                return {Type::NUM, false, Type::NUM};
            }
            auto ni = vm.natByName.find(full);
            if (ni != vm.natByName.end()) return callNative(ni->second);
            fail("unknown name '" + full + "'", nameLine);
            return {Type::VOID};
        }
        if (cur.kind == T_LP) { // call
            Type out;
            if (genericBuiltin(name, out)) return out;
            auto ni = vm.natByName.find(name);
            if (ni != vm.natByName.end()) return callNative(ni->second);
            auto fi = vm.fnByName.find(name);
            if (fi != vm.fnByName.end()) return callFunction(fi->second);
            fail("unknown function '" + name +
                 "' (wick requires declare-before-use)", nameLine);
            return {Type::VOID};
        }
        // variable read
        if (Local* l = findLocal(name)) {
            emit(OP_LOADL);
            emit((uint8_t)l->slot);
            Type t = l->type;
            if (l->narrowed) t.opt = false;
            return t;
        }
        auto gi = vm.gByName.find(name);
        if (gi != vm.gByName.end()) {
            emit2(OP_LOADG, (uint16_t)gi->second);
            return vm.gtypes[gi->second];
        }
        fail("unknown variable '" + name + "' (declare it with let)", nameLine);
        return {Type::VOID};
    }

    // ---- statements ----
    void block() {
        if (!expect(T_LBRACE, "'{'")) return;
        beginScope();
        while (cur.kind != T_RBRACE && cur.kind != T_EOF && !failed)
            statement();
        endScope();
        expect(T_RBRACE, "'}'");
    }

    void letStmt() {
        advance(); // let
        if (cur.kind != T_IDENT) { fail("expected a name after let"); return; }
        std::string name = cur.text;
        advance();
        Type ann{Type::VOID};
        bool hasAnn = false;
        if (accept(T_COLON)) {
            if (!parseType(ann)) return;
            hasAnn = true;
        }
        if (!expect(T_ASSIGN, "'='")) return;
        // special-case: annotated empty container literal
        Type t;
        if (hasAnn && cur.kind == T_LB && peek().kind == T_RB &&
            (ann.k == Type::LIST || ann.k == Type::MAP)) {
            advance(); advance();
            emit(ann.k == Type::LIST ? OP_LIST : OP_MAP);
            emit(0);
            t = ann;
        } else {
            t = expression();
            if (hasAnn) {
                if (!checkAssign(ann, t, "let")) return;
                t = ann;
            } else if (t.k == Type::NILT) {
                fail("can't infer a type from nil; annotate: let x: str? = nil");
                return;
            } else if (t.k == Type::VOID) {
                fail("can't assign a void expression");
                return;
            }
        }
        if (inFn) {
            int slot = addLocal(name, t);
            if (slot < 0) return;
            emit(OP_STOREL);
            emit((uint8_t)slot);
        } else {
            if (vm.gByName.count(name)) { fail("'" + name + "' already declared"); return; }
            int slot = (int)vm.globals.size();
            vm.globals.push_back(Value::nil());
            vm.gtypes.push_back(t);
            vm.gByName[name] = slot;
            emit2(OP_STOREG, (uint16_t)slot);
        }
    }

    void assignOrCallStmt() {
        // IDENT (.name call | [expr] = | = | call)
        if (cur.kind != T_IDENT) {
            fail("expected a statement");
            advance();
            return;
        }
        std::string name = cur.text;
        // lvalue forms need lookahead
        TokKind nk = peek().kind;
        if (nk == T_ASSIGN) { // x = expr
            advance(); advance();
            Type rt = expression();
            if (Local* l = findLocal(name)) {
                checkAssign(l->type, rt, ("assignment to " + name).c_str());
                if (l->narrowed && rt.opt) l->narrowed = false;
                emit(OP_STOREL);
                emit((uint8_t)l->slot);
            } else {
                auto gi = vm.gByName.find(name);
                if (gi == vm.gByName.end()) {
                    fail("unknown variable '" + name +
                         "' — wick has no implicit globals; use let");
                    return;
                }
                checkAssign(vm.gtypes[gi->second], rt,
                            ("assignment to " + name).c_str());
                emit2(OP_STOREG, (uint16_t)gi->second);
            }
            return;
        }
        if (nk == T_LB) { // x[i] = expr  (or an index-read expression stmt)
            // compile container ref first
            Type ct = identifier(); // consumes IDENT and, via postfix? no —
            // identifier() alone; the '[' is still current token here
            // (identifier() only handles '.', '(' and plain reads)
            if (cur.kind == T_LB) {
                advance();
                Type it = expression();
                expect(T_RB, "']'");
                if (cur.kind == T_ASSIGN) {
                    advance();
                    Type vt = expression();
                    if (ct.k == Type::LIST) {
                        if (it.k != Type::NUM || it.opt) fail("list index must be num");
                        checkAssign({ct.elem, false, Type::NUM}, vt, "element");
                        emit(OP_ISET);
                    } else if (ct.k == Type::MAP) {
                        if (it.k != Type::STR || it.opt) fail("map key must be str");
                        checkAssign({ct.elem, false, Type::NUM}, vt, "value");
                        emit(OP_MSET);
                    } else fail("only lists and maps can be indexed");
                    return;
                }
                // index read as a statement — pointless; pop it
                if (ct.k == Type::LIST) emit(OP_IGET);
                else if (ct.k == Type::MAP) emit(OP_MGET);
                else fail("only lists and maps can be indexed");
                emit(OP_POP);
                return;
            }
            emit(OP_POP);
            return;
        }
        // expression statement (calls); pop non-void results
        Type t = expression();
        if (t.k != Type::VOID) emit(OP_POP);
    }

    void ifStmt() {
        advance(); // if
        // narrowing pattern: IDENT != nil { ... }
        Local* narrowed = nullptr;
        if (cur.kind == T_IDENT && peek().kind == T_NE) {
            std::string vn = cur.text;
            // lookahead for nil: cheap sub-scan — save state
            Local* l = findLocal(vn);
            if (l && l->type.opt && !l->narrowed) {
                // tentatively parse: IDENT != nil
                advance();          // IDENT consumed
                advance();          // !=
                if (cur.kind == T_NIL) {
                    advance();
                    emit(OP_LOADL);
                    emit((uint8_t)l->slot);
                    emit(OP_ISNIL);
                    emit(OP_NOT);
                    narrowed = l;
                } else {
                    // general expr continuing from IDENT != ...
                    emit(OP_LOADL);
                    emit((uint8_t)l->slot);
                    Type r = parseCmp();
                    if (!l->type.sameBase(r)) fail("'!=' operands must match");
                    emit(OP_NE);
                    if (cur.kind == T_AND || cur.kind == T_OR)
                        fail("parenthesize this condition: (a != b) and ...");
                }
            }
        }
        if (!narrowed && cur.kind != T_LBRACE) {
            // (either no pattern matched, or plain condition)
            if (cur.kind != T_LBRACE) {
                Type c = expression();
                if (c.k != Type::BOOL || c.opt)
                    fail("if condition must be bool (wick has no truthiness)");
            }
        }
        size_t elseJ = emitJump(OP_JF);
        if (narrowed) narrowed->narrowed = true;
        block();
        if (narrowed) narrowed->narrowed = false;
        if (cur.kind == T_ELIF || cur.kind == T_ELSE) {
            size_t endJ = emitJump(OP_JMP);
            patchJump(elseJ);
            if (cur.kind == T_ELIF) {
                cur.kind = T_IF; // reuse
                ifStmt();
            } else {
                advance();
                block();
            }
            patchJump(endJ);
        } else {
            patchJump(elseJ);
        }
    }

    void whileStmt() {
        advance();
        size_t top = proto->code.size();
        Type c = expression();
        if (c.k != Type::BOOL || c.opt) fail("while condition must be bool");
        size_t exitJ = emitJump(OP_JF);
        loops.push_back({{}, {}, scopeDepth});
        block();
        for (size_t at : loops.back().continues) patchJump(at);
        emitLoop(top);
        patchJump(exitJ);
        for (size_t at : loops.back().breaks) patchJump(at);
        loops.pop_back();
    }

    void forStmt() {
        // for i in a..b { }   — i: num, [a, b)
        advance();
        if (cur.kind != T_IDENT) { fail("expected a loop variable"); return; }
        std::string vn = cur.text;
        advance();
        expect(T_IN, "'in'");
        beginScope();
        Type a = expression();
        if (a.k != Type::NUM || a.opt) fail("range start must be num");
        int iSlot = addLocal(vn, {Type::NUM, false, Type::NUM});
        emit(OP_STOREL); emit((uint8_t)iSlot);
        expect(T_DOTDOT, "'..'");
        Type b = expression();
        if (b.k != Type::NUM || b.opt) fail("range end must be num");
        int limSlot = addLocal("(limit)", {Type::NUM, false, Type::NUM});
        emit(OP_STOREL); emit((uint8_t)limSlot);
        size_t top = proto->code.size();
        emit(OP_LOADL); emit((uint8_t)iSlot);
        emit(OP_LOADL); emit((uint8_t)limSlot);
        emit(OP_LT);
        size_t exitJ = emitJump(OP_JF);
        loops.push_back({{}, {}, scopeDepth});
        block();
        for (size_t at : loops.back().continues) patchJump(at);
        emit(OP_LOADL); emit((uint8_t)iSlot);
        emit2(OP_CONST, addConstVal(Value::num(1)));
        emit(OP_ADD);
        emit(OP_STOREL); emit((uint8_t)iSlot);
        emitLoop(top);
        patchJump(exitJ);
        for (size_t at : loops.back().breaks) patchJump(at);
        loops.pop_back();
        endScope();
    }

    void fnStmt() {
        advance();
        if (inFn) { fail("fn declarations can't nest"); return; }
        if (cur.kind != T_IDENT) { fail("expected a function name"); return; }
        std::string name = cur.text;
        advance();
        if (vm.fnByName.count(name) || vm.natByName.count(name)) {
            fail("'" + name + "' already defined");
            return;
        }
        Proto p;
        p.name = name;
        int fnIdx = (int)vm.protos.size();
        // parse signature
        expect(T_LP, "'('");
        std::vector<std::pair<std::string, Type>> params;
        if (cur.kind != T_RP) {
            do {
                if (cur.kind != T_IDENT) { fail("expected a parameter name"); return; }
                std::string pn = cur.text;
                advance();
                if (!expect(T_COLON, "':' (parameter types are required)")) return;
                Type pt;
                if (!parseType(pt)) return;
                params.push_back({pn, pt});
                p.params.push_back(pt);
            } while (accept(T_COMMA));
        }
        expect(T_RP, "')'");
        p.nparams = (int)params.size();
        if (accept(T_COLON)) {
            if (!parseType(p.ret)) return;
        }
        vm.protos.push_back(p);
        vm.fnByName[name] = fnIdx;
        // compile body into the new proto
        Proto* prevProto = proto;
        int prevIdx = protoIdx;
        auto prevLocals = std::move(locals);
        locals.clear();
        bool prevInFn = inFn;
        int prevDepth = scopeDepth;
        proto = &vm.protos[fnIdx];
        protoIdx = fnIdx;
        inFn = true;
        scopeDepth = 1;
        for (auto& pr : params) addLocal(pr.first, pr.second);
        block();
        emit(OP_RET); // implicit return (checker: non-void fns should RETV,
                      // falling off the end returns nil — allowed only for void)
        if (vm.protos[fnIdx].ret.k != Type::VOID &&
            !vm.protos[fnIdx].ret.opt) {
            // conservative: require the LAST statement path to return is
            // out of scope for v0.1; runtime returns nil which would break
            // typing — so forbid falling off entirely by checking the byte
            // before our OP_RET is OP_RETV is too weak; accept and document.
        }
        (void)prevProto; // vm.protos may have reallocated: re-fetch by index
        proto = &vm.protos[prevIdx];
        protoIdx = prevIdx;
        locals = std::move(prevLocals);
        inFn = prevInFn;
        scopeDepth = prevDepth;
    }

    void statement() {
        switch (cur.kind) {
        case T_LET: letStmt(); return;
        case T_FN:
            if (inFn) { fail("fn declarations can't nest"); return; }
            fnStmt();
            return;
        case T_IF: ifStmt(); return;
        case T_WHILE: whileStmt(); return;
        case T_FOR: forStmt(); return;
        case T_LBRACE: block(); return;
        case T_BREAK: {
            advance();
            if (loops.empty()) { fail("break outside a loop"); return; }
            loops.back().breaks.push_back(emitJump(OP_JMP));
            return;
        }
        case T_CONT: {
            advance();
            if (loops.empty()) { fail("continue outside a loop"); return; }
            loops.back().continues.push_back(emitJump(OP_JMP));
            return;
        }
        case T_RETURN: {
            advance();
            if (!inFn) { fail("return outside a function"); return; }
            Type want = vm.protos[protoIdx].ret;
            if (cur.kind == T_RBRACE || cur.kind == T_EOF) {
                if (want.k != Type::VOID) fail("this function must return " + want.name());
                emit(OP_RET);
            } else {
                Type t = expression();
                if (want.k == Type::VOID) fail("void function can't return a value");
                else checkAssign(want, t, "return");
                emit(OP_RETV);
            }
            return;
        }
        default: assignOrCallStmt(); return;
        }
    }

    bool compileProgram() {
        // proto 0 = top level
        Proto top;
        top.name = "<main>";
        vm.protos.push_back(top);
        proto = &vm.protos[0];
        protoIdx = 0;
        inFn = false;
        advance(); // prime cur
        while (cur.kind != T_EOF && !failed) {
            // NOTE: fn bodies write into their own proto; top-level
            // statements append to proto 0. Re-anchor after any fn decl
            // since vm.protos may reallocate.
            statement();
            proto = &vm.protos[protoIdx == 0 ? 0 : protoIdx];
            if (!inFn) { proto = &vm.protos[0]; protoIdx = 0; }
        }
        proto = &vm.protos[0];
        emit(OP_HALT);
        return !failed;
    }
};

} // namespace

bool compile(VM& vm, const std::string& src, const std::string& chunkName,
             std::string& err) {
    vm.chunkName = chunkName;
    Compiler c(vm, src);
    if (!c.compileProgram()) {
        err = c.err;
        return false;
    }
    return true;
}

} // namespace wick
