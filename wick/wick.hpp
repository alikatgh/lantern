// wick.hpp — the wick language embed API. wick is lantern's own scripting
// language: statically typed, Lua-small, compiled in one pass to bytecode
// and run on a stack VM. No dependencies beyond the C++ standard library.
// See docs/WICK.md for the language itself.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace wick {

struct Obj; // Str / List / Map (GC-managed, defined internally)

struct Value {
    enum Tag : uint8_t { NIL, NUM, BOOL, STR, LIST, MAP } tag = NIL;
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

// Compile + run top-level statements. On failure err is "name:line: msg".
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
