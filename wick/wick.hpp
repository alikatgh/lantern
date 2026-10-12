// wick.hpp — the wick language embed API. wick is lantern's own scripting
// language: statically typed, Lua-small, compiled in one pass to bytecode
// and run on a stack VM. No dependencies beyond the C++ standard library.
// See docs/WICK.md for the language itself.
#pragma once
#include <climits>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Standalone embedders use the same native ABI policy as the CMake wick target.
// Set WICK_HOST_BITS=32 for every translation unit only with a 32-bit toolchain.
#ifndef WICK_HOST_BITS
#define WICK_HOST_BITS 64
#endif
#if WICK_HOST_BITS != 64 && WICK_HOST_BITS != 32
#error "WICK_HOST_BITS must be 64 or 32"
#endif
static_assert(sizeof(void*) * CHAR_BIT == WICK_HOST_BITS,
              "WICK_HOST_BITS must match the compiler's native pointer width");

namespace wick {

inline constexpr unsigned HOST_BITS = WICK_HOST_BITS;

struct Obj; // Str / List / Map (GC-managed, defined internally)

struct Value {
    enum Tag : uint8_t { NIL, NUM, BOOL, STR, LIST, MAP, REC } tag = NIL;
    union {
        double d;
        bool b;
        Obj* o;
    };
    Value() : tag(NIL), o(nullptr) {}
    static Value nil() { return Value(); }
    static Value num(double v) { Value x; x.tag = NUM; x.d = v; return x; }
    static Value boolean(bool v) { Value x; x.tag = BOOL; x.b = v; return x; }
};

struct VM;

VM*  create();
void destroy(VM* vm);

// Natives are registered BEFORE load() under a namespace ("lt") or the
// global namespace (ns = nullptr). The signature string is checked at
// compile time and dispatched by id at run time. Grammar:
//   name(type[, type...][, type = default...]) [: type]
//   type := num | bool | str | T?          (defaults: numeric literals only)
// e.g. "draw(num, num, num, num, num=0, num=0, num=0): num"
//      "load_save(str): str?"
using NativeFn = Value (*)(VM& vm, const Value* args, int argc);
bool addNative(VM* vm, const char* ns, const char* sig, NativeFn fn,
               std::string& err);
// A compile-time numeric constant, e.g. lt.W = 400.
void addConst(VM* vm, const char* ns, const char* name, double v);

// Compile-only validation. A fresh temporary VM uses the environment's native
// signatures and numeric constants, never its program or heap. No Wick code or
// native is executed, and the environment (or process RNG) is not modified.
// nullptr selects the builtin environment. line == 0 means unavailable.
inline constexpr size_t SOURCE_LIMIT_BYTES = 8 * 1024 * 1024;
inline constexpr unsigned COMPILE_DEPTH_LIMIT = 256;
struct Diagnostic {
    std::string file;
    int line = 0;
    std::string message;
    std::string phase;
    std::string code;
};
bool check(VM* environment, const std::string& source,
           const std::string& chunkName, Diagnostic& diagnostic);

// Compile + run top-level statements. On failure err is "name:line: msg".
// Compilation shares the source, nesting and constant-pool limits of check().
bool load(VM* vm, const std::string& source, const std::string& chunkName,
          std::string& err);
// Call a global fn by name (no-op true if absent — update/draw optional).
bool call(VM* vm, const char* name, double arg, bool hasArg, std::string& err);
// Mark & sweep. The host calls this at frame boundaries only.
void collect(VM* vm);
// Reset all program state (globals, functions) — hot-reload support.
void reset(VM* vm);

// Report a runtime error from inside a native (aborts the current run
// with file:line pointing at the call site).
void setError(VM& vm, const std::string& msg);

// Value helpers for natives.
Value       makeStr(VM& vm, const std::string& s);
std::string getStr(const Value& v);          // valid while the Value lives
int         listLen(const Value& v);
Value       listGet(const Value& v, int i);

} // namespace wick
