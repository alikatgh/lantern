// Real native compiler/VM probe, including transitive CMake target definitions.
#ifndef WICK_HOST_BITS
#error "The wick CMake target must publish WICK_HOST_BITS to its consumers"
#endif
#include "wick.hpp"
#include <cstdio>
#include <limits>
#include <type_traits>

static_assert(wick::HOST_BITS == WICK_TEST_EXPECTED_HOST_BITS);
static_assert(wick::HOST_BITS == sizeof(void*) * CHAR_BIT);
static_assert(std::is_same_v<decltype(wick::Value::d), double>);

static double observed = 0;
static wick::Value capture(wick::VM&, const wick::Value* args, int) {
    observed = args[0].d;
    return wick::Value::nil();
}

int main() {
    auto* vm = wick::create();
    std::string error;
    const bool ok = wick::addNative(vm, nullptr, "capture(num)", capture, error) &&
        wick::load(vm, R"(
check(1.25 + 2.5 == 3.75, "fractional num arithmetic")
check(4294967295 + 1 == 4294967296, "num remains double beyond u32")
check(u8(256) == 0 and bit_not(0) == 4294967295, "u32 helpers unchanged")
fn sample() { capture(1.25 + 2.5) }
)", "host-bits.wick", error) &&
        wick::call(vm, "sample", 0, false, error) && observed == 3.75;
    wick::destroy(vm);
    if (!ok) {
        std::fprintf(stderr, "native Wick arithmetic/API failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("{\"host_bits\":%zu,\"target_bits\":%u,"
                "\"num\":{\"storage_bits\":%zu,\"mantissa_bits\":%d,\"radix\":%d}}\n",
                sizeof(void*) * CHAR_BIT, wick::HOST_BITS,
                sizeof(double) * CHAR_BIT, std::numeric_limits<double>::digits,
                std::numeric_limits<double>::radix);
    return 0;
}
