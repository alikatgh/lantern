// Real compiler/API tests; no engine, SDL, window, or source fixture copies.
#include "wick_internal.hpp"
#include <algorithm>
#include <bit>
#include <iostream>
#include <memory>
#include <sstream>

using namespace wick;
static int assertions = 0;
static void require(bool condition, const char* message) {
    ++assertions;
    if (!condition) { std::cerr << "FAIL " << message << '\n'; std::exit(1); }
}
static int traps = 0;
static double captured = 0;
static Value trap(VM&, const Value*, int) { ++traps; return Value::nil(); }
static Value capture(VM&, const Value* a, int) { captured = a[0].d; return Value::nil(); }
using OwnedVM = std::unique_ptr<VM, decltype(&destroy)>;
static OwnedVM fresh() { return OwnedVM(create(), destroy); }
static void type(std::ostream& s, const Type& t) { s << int(t.k) << ':' << t.opt << ':' << int(t.elem) << ':' << t.recId << ';'; }
static void value(std::ostream& s, const Value& v) {
    s << int(v.tag) << ':';
    if (v.tag == Value::NUM) s << std::bit_cast<uint64_t>(v.d);
    else if (v.tag == Value::BOOL) s << v.b;
    else s << v.o;
    s << ';';
}
template<class M> static void map(std::ostream& s, const M& m) {
    std::vector<std::string> keys;
    for (auto& entry : m) keys.push_back(entry.first);
    std::sort(keys.begin(), keys.end());
    for (auto& key : keys) s << key.size() << ':' << key << '=' << m.at(key) << ';';
}
static std::string snapshot(const VM& vm) {
    // Include program, environment, stack, runtime errors and every heap node;
    // container addresses/capacities also catch hidden resets/reallocation.
    std::ostringstream s;
    auto values = [&](const std::vector<Value>& v) {
        s << v.data() << ':' << v.capacity() << ':' << v.size() << ';';
        for (auto& x : v) value(s, x);
    };
    values(vm.consts); values(vm.globals); values(vm.stack);
    for (auto& t : vm.gtypes) type(s, t);
    for (auto& p : vm.protos) {
        s << p.name << ':' << p.nparams << ':' << p.nlocals << ';'; type(s, p.ret);
        for (auto& t : p.params) type(s, t);
        s << static_cast<const void*>(p.code.data()) << ':' << p.code.capacity() << ';';
        for (auto x : p.code) s << int(x) << ',';
        for (auto x : p.lines) s << x << ',';
    }
    map(s, vm.fnByName); map(s, vm.gByName); map(s, vm.recByName);
    map(s, vm.natByName); map(s, vm.numConsts);
    for (auto& r : vm.records) {
        s << r.name << ';';
        for (auto& f : r.fields) s << f << ';';
        for (auto& t : r.fieldTypes) type(s, t);
    }
    for (auto& n : vm.natives) {
        s << n.name << ':' << n.minArgs << ':' << reinterpret_cast<uintptr_t>(n.fn) << ';';
        type(s, n.ret);
        for (auto& t : n.params) type(s, t);
        for (auto& v : n.defaults) value(s, v);
    }
    for (Obj* o = vm.objects; o; o = o->next) {
        s << o << ':' << o->next << ':' << int(o->k) << ':' << o->mark << ':' << o->recId << ':' << o->s.size() << ':' << o->s << ';';
        values(o->list); values(o->fields);
        std::vector<std::string> keys;
        for (auto& entry : o->map) keys.push_back(entry.first);
        std::sort(keys.begin(), keys.end());
        for (auto& key : keys) { s << key << ';'; value(s, o->map.at(key)); }
    }
    s << vm.rerr << ':' << vm.rline << ':' << vm.chunkName;
    return s.str();
}
static void diagnostic(const std::string& source, const char* code, int line,
                       const char* message) {
    Diagnostic d;
    require(!check(nullptr, source, "true\"file\nСаха.wick", d), "invalid source rejected");
    require(d.file == "true\"file\nСаха.wick", "diagnostic exact file");
    require(d.line == line, "diagnostic exact line");
    require(d.phase == "compile" && d.code == code, "diagnostic phase/code");
    require(d.message.find(message) != std::string::npos, "diagnostic exact message metadata");
}
int main() {
    Diagnostic d;
    require(check(nullptr, "let n = abs(-2)\nfn draw() {}", "valid.wick", d), "valid builtins compile");
    require(d.file.empty() && d.message.empty() && d.line == 0 && d.code.empty(), "success clears diagnostic");
    diagnostic("// first\n\nlet x: num = true\n", "WICK_COMPILE_ERROR", 3, "expected num");
    diagnostic("// first\nlet x = (1 + )", "WICK_COMPILE_ERROR", 2, "expected an expression");
    diagnostic("// first\nlet x = \"unterminated", "WICK_COMPILE_ERROR", 2, "unterminated string literal");
    diagnostic("let x = \"trailing\\", "WICK_COMPILE_ERROR", 1, "unterminated string literal");
    {
        auto vm = fresh(); std::string err;
        require(addNative(vm.get(), nullptr, "trap()", trap, err), "register trap");
        require(addNative(vm.get(), nullptr, "capture(num)", capture, err), "register capture");
        require(addNative(vm.get(), "host", "defaulted(num=7): num", nullptr, err), "register default signature");
        addConst(vm.get(), "host", "WIDTH", 400);
        require(load(vm.get(), R"(
record Pair { n: num, text: str }
let pair = Pair { n: 4, text: "kept" }
let list = [1, 2]
let dict = ["k": "v"]
fn update() { pair.n = pair.n + 1 }
fn seed() { srand(12345) }
fn next() { capture(rand()) }
)", "caller.wick", err), "caller state setup");
        vm->stack.push_back(makeStr(*vm, "rooted-stack"));
        vm->rerr = "retained error"; vm->rline = 77;
        const auto before = snapshot(*vm);
        require(check(vm.get(), "while true {}\ntrap()\nlet x = host.defaulted() + host.WIDTH\nlet allocated = \"temporary\"", "checked.wick", d), "infinite top level compile only");
        require(traps == 0 && snapshot(*vm) == before, "success preserves all caller state and native trap");
        require(!check(vm.get(), "trap()\nlet x: num = false", "bad.wick", d), "invalid caller-environment source rejected");
        require(traps == 0 && snapshot(*vm) == before, "failure preserves all caller state and native trap");
        require(!check(vm.get(), "update()", "isolation.wick", d), "caller functions are not a check environment");
        require(snapshot(*vm) == before, "isolation failure preserves caller");
        vm->rerr.clear(); vm->stack.clear();
        require(call(vm.get(), "update", 0, false, err), "original compiled program still callable");
        require(vm->globals[0].o->fields[0].d == 5, "original globals still work");
        require(call(vm.get(), "seed", 0, false, err) && call(vm.get(), "next", 0, false, err), "RNG reference");
        const double expected = captured;
        require(call(vm.get(), "seed", 0, false, err), "RNG reseed");
        require(check(vm.get(), "srand(999)\nlet n = rand()\ntrap()", "rng.wick", d), "RNG source compile only");
        require(call(vm.get(), "next", 0, false, err) && captured == expected, "check preserves process RNG");
        require(traps == 0, "no native executed");
        // Public registrations cannot contain heap defaults; reject a malformed
        // internal environment safely, without copying that pointer.
        vm->natives.back().defaults.push_back(makeStr(*vm, "unsafe default"));
        const auto unsafeBefore = snapshot(*vm);
        require(!check(vm.get(), "", "unsafe.wick", d) && d.phase == "host", "unsafe environment rejected");
        require(snapshot(*vm) == unsafeBefore, "unsafe environment rejection preserves caller");
    }
    {
        std::string source = "//"; source.resize(SOURCE_LIMIT_BYTES, ' ');
        require(check(nullptr, source, "limit.wick", d), "exact 8MiB accepted");
        source += ' ';
        require(!check(nullptr, source, "limit.wick", d) && d.code == "WICK_SOURCE_LIMIT" && d.line == 0, "8MiB plus one rejected");
    }
    {
        // Each abs(0) adds one real constant and no declaration or jump whose
        // independent operand limits could obscure the constant boundary.
        std::string source;
        for (int i = 0; i < 65536; ++i) source += "abs(0)\n";
        require(check(nullptr, source, "constants.wick", d), "check accepts 65536 constants");
        auto vm = fresh(); std::string err;
        require(load(vm.get(), source, "constants.wick", err), "load accepts 65536 constants");
        require(vm->consts.size() == 65536, "full u16 constant pool retained");
        const auto& code = vm->protos[0].code;
        require(code[code.size()-8] == 255 && code[code.size()-7] == 255, "constant index 65535 encoded without wrap");
        // Every other emitter must retain its origin after consuming a token
        // or compiling a body. The API takes real source, not a prefilled VM.
        auto environment = fresh();
        require(addNative(environment.get(), nullptr, "defaulted(num=7): num", trap, err), "register capacity default");
        addConst(environment.get(), "host", "WIDTH", 400);
        for (const std::string suffix : {"defaulted()\n", "let s = \"x\"\n",
                 "let m = [\"k\":true]\n", "let width = host.WIDTH\n"}) {
            require(!check(environment.get(), source + suffix, "origins.wick", d) &&
                    d.code == "WICK_CONSTANT_LIMIT" && d.line == 65537,
                    "all constant emitters retain source origin");
        }
        std::string loopSource = "let start = 0\nlet end = 0\n";
        for (int i = 0; i < 65534; ++i) loopSource += "abs(0)\n";
        loopSource += "for i in start..end {}\n";
        require(!check(nullptr, loopSource, "loop-origin.wick", d) &&
                d.code == "WICK_CONSTANT_LIMIT" && d.line == 65537,
                "implicit loop constant retains for origin");
        auto defaultOverflow = fresh();
        require(addNative(defaultOverflow.get(), nullptr, "defaulted(num=7): num", trap, err), "register legacy default capacity");
        require(!load(defaultOverflow.get(), source + "defaulted()\n", "default-origin.wick", err) &&
                err.find("default-origin.wick:65537:") != std::string::npos && traps == 0,
                "legacy load default constant has true line and never executes");
        auto loopOverflow = fresh();
        require(!load(loopOverflow.get(), loopSource, "loop-origin.wick", err) &&
                err.find("loop-origin.wick:65537:") != std::string::npos,
                "legacy load implicit constant has true line");
        source += "abs(0)\n";
        require(!check(nullptr, source, "constants.wick", d) && d.code == "WICK_CONSTANT_LIMIT" && d.line == 65537, "check rejects constant 65537 at true line");
        auto overflow = fresh();
        require(addNative(overflow.get(), nullptr, "trap()", trap, err), "register load overflow trap");
        require(!load(overflow.get(), "trap()\n" + source, "overflow.wick", err), "load rejects constant 65537");
        require(err.find("constant pool exceeds 65536") != std::string::npos && overflow->consts.size() == 65536 && traps == 0, "load rejects before insertion or top-level execution");
    }
    {
        // The shared depth budget counts parser entries, not just punctuation.
        // These boundaries include the enclosing statement/expression/unary.
        auto parentheses = [](int n) { return "let x = " + std::string(n, '(') + "0" + std::string(n, ')'); };
        require(check(nullptr, parentheses(126), "depth.wick", d), "parentheses below recursive limit");
        require(!check(nullptr, parentheses(127), "depth.wick", d) && d.code == "WICK_COMPLEXITY_LIMIT", "parentheses beyond recursive limit");
        require(check(nullptr, "let x = " + std::string(253, '-') + "0", "depth.wick", d), "unary at recursive limit");
        require(!check(nullptr, "let x = " + std::string(254, '-') + "0", "depth.wick", d) && d.code == "WICK_COMPLEXITY_LIMIT", "unary beyond recursive limit");
        require(check(nullptr, std::string(256, '{') + std::string(256, '}'), "depth.wick", d), "blocks at recursive limit");
        require(!check(nullptr, std::string(257, '{') + std::string(257, '}'), "depth.wick", d) && d.code == "WICK_COMPLEXITY_LIMIT", "blocks beyond recursive limit");
        std::string chain = "if true {}";
        for (int i = 0; i < 300; ++i) chain += " elif true {}";
        require(!check(nullptr, chain, "depth.wick", d) && d.code == "WICK_COMPLEXITY_LIMIT", "elif recursion bounded");
        std::string nestedType = "let x: ";
        for (int i = 0; i < 300; ++i) nestedType += "list<";
        nestedType += "num" + std::string(300, '>');
        require(!check(nullptr, nestedType, "type.wick", d) && d.code == "WICK_COMPLEXITY_LIMIT", "nested type recursion bounded");
    }
    std::cout << "Wick check API: " << assertions << " assertions passed\n";
}
