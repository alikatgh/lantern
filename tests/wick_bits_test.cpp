// Standalone compiler/VM tests: no renderer, assets, SDL, or window needed.
#include "wick.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

static int passed = 0, failed = 0;
static void test(const std::string& source, const std::string& error = "") {
    wick::VM* vm = wick::create();
    std::string got;
    const bool ok = wick::load(vm, source, "bits.wick", got);
    const bool expected = error.empty() ? ok : !ok && got.find(error) != std::string::npos
                                               && got.find("bits.wick:") != std::string::npos;
    if (expected) ++passed;
    else { ++failed; std::cerr << "FAIL: " << source << "\n" << got << "\n"; }
    wick::collect(vm);
    wick::destroy(vm);
}
int main(int argc, char** argv) {
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            std::ifstream input(argv[i]);
            if (!input) { std::cerr << "cannot read " << argv[i] << '\n'; return 1; }
            std::ostringstream source; source << input.rdbuf();
            test(source.str());
        }
        std::cout << "Wick snippets: " << passed << " passed, " << failed << " failed\n";
        return failed ? 1 : 0;
    }
    test(R"(
check(0xFF == 255 and 0Xff == 255, "hex")
check(0b10101010 == 170 and 0B11 == 3, "binary")
check(0xFFFFFFFF == 4294967295, "max")
check(0b11111111111111111111111111111111 == 0xFFFFFFFF, "binary max")
let n = 0
for i in 0x0..0x10 { n = n + i }
check(n == 120, "hex ranges")
check(1.25 + 2.5 == 3.75, "decimal unchanged")
check(bit_and(0xA5, 0x0F) == 5, "mask")
check(bit_or(0xA0, 0x05) == 0xA5, "or")
check(bit_xor(0xAA, 0xFF) == 0x55, "xor")
check(bit_not(0) == 0xFFFFFFFF, "not is 32 bit")
check(bit_not(0xFFFFFFFF) == 0, "not max")
check(bit_shl(1, 31) == 0x80000000, "high bit")
check(bit_shl(0xFFFFFFFF, 1) == 0xFFFFFFFE, "discard overflow")
check(bit_shl(0xFFFFFFFF, 31) == 0x80000000, "max shift")
check(bit_shr(0x80000000, 31) == 1, "logical right shift")
check(bit_shl(7, 0) == 7 and bit_shr(7, 0) == 7, "zero shift")
check(u8(255 + 1) == 0 and u8(-1) == 255, "byte wrap")
check(u16(65535 + 1) == 0 and u16(-1) == 65535, "word wrap")
check(u8(-256) == 0 and str(u8(-256)) == "0", "negative zero")
check(u8(9007199254740991) == 255, "safe upper")
check(u16(-9007199254740991) == 1, "safe lower")
check(hex(0) == "0" and hex(0x8080) == "8080", "hex default")
check(hex(10, 2) == "0A" and hex(256, 2) == "100", "padding not truncation")
check(hex(0xFFFFFFFF, 8) == "FFFFFFFF", "hex max")
check(bin(5, 8) == "00000101" and bin(0) == "0", "binary format")
check(len(bin(0xFFFFFFFF, 32)) == 32, "binary max")
record Register { value: num, label: str }
let regs: list<Register> = []
push(regs, Register { value: 0xFF, label: "A" })
regs[0].value = u8(regs[0].value + 1)
check(regs[0].value == 0, "record register wraps")
// Real address-space workload: 64 Ki entries, last address, PC rollover.
let memory: list<num> = []
for i in 0..65536 { push(memory, 0) }
memory[0xFFFF] = 0x76
check(memory[65535] == 118 and u16(0xFFFF + 1) == 0, "memory bus")
// Exhaustive byte-domain identities used by gates and displays.
for i in 0..256 {
  check(u8(bit_not(i)) == 255 - i, "complement")
  check(bit_xor(i, i) == 0 and bit_and(i, 0xFF) == i, "gates")
  check(u8(bit_or(bit_shl(i, 1), bit_shr(i, 7))) == (i * 2) % 256 + floor(i / 128), "rotate")
}
)");
    for (auto source : {"let x = 0x", "let x = 0B"}) test(source, "needs digits");
    for (auto source : {"let x = 0b2", "let x = 0xGG", "let x = 0b102", "let x = 0xFFoops", "let x = 0x_FF"}) test(source, "invalid digit");
    for (auto source : {"let x = 0x100000000", "let x = 0b100000000000000000000000000000000"}) test(source, "exceeds 32 bits");
    test("let x = 0x1.5", "fractional part");
    test("let x = bit_and(true, 1)", "argument");
    test("let x = bit_or(1)", "at least");
    test("let x = bit_not(1, 2)", "at most");
    test("let x = hex(1, true)", "argument");
    test("let x = bit_and(num(\"1\"), 1)", "num?");
    for (const std::string bad : {"-1", "1.5", "4294967296", "sqrt(-1)", "1 / 0"}) {
        for (const std::string fn : {"bit_and", "bit_or", "bit_xor"}) {
            test(fn + "(" + bad + ", 1)", "bit value");
            test(fn + "(1, " + bad + ")", "bit value");
        }
        for (const std::string fn : {"bit_not", "hex", "bin"}) test(fn + "(" + bad + ")", "bit value");
        for (const std::string fn : {"bit_shl", "bit_shr"}) test(fn + "(" + bad + ", 0)", "bit value");
    }
    for (const std::string bad : {"-1", "32", "0.5", "sqrt(-1)", "1 / 0"})
        for (const std::string fn : {"bit_shl", "bit_shr"}) test(fn + "(1, " + bad + ")", "shift count");
    for (const std::string bad : {"1.5", "9007199254740992", "-9007199254740992", "sqrt(-1)", "1 / 0"})
        for (const std::string fn : {"u8", "u16"}) test(fn + "(" + bad + ")", "safe integer");
    for (const std::string bad : {"0", "-1", "33", "1.5", "sqrt(-1)", "1 / 0"})
        for (const std::string fn : {"hex", "bin"}) test(fn + "(1, " + bad + ")", "format width");
    test("hex(1, 9)", "format width");
    test("fn hex(): num { return 1 }", "already defined");
    // Builtins remain installed and callable after hot reload.
    auto* vm = wick::create();
    std::string err;
    bool ok = wick::load(vm, "check(u8(256) == 0, \"first\")", "reload", err);
    wick::reset(vm);
    ok = ok && wick::load(vm, "check(hex(0xFF) == \"FF\", \"second\")", "reload", err);
    if (ok) ++passed; else { ++failed; std::cerr << err << '\n'; }
    wick::destroy(vm);
    std::cout << "wick bits: " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
