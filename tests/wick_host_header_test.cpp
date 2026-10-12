// Objective-C++ system headers define nil before including the host API.
#define nil nullptr
#include "../src/wick_host.hpp"
#include <type_traits>

static_assert(nil == nullptr);
using CheckSource = bool (*)(const std::string&, const std::string&,
                             wick::Diagnostic&);
static_assert(std::is_same_v<decltype(&checkWickSource), CheckSource>);

int main() { return 0; }
