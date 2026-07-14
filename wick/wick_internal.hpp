// wick_internal.hpp — shared guts of the wick compiler and VM.
// Nothing here is public API; embedders use wick.hpp only.
#pragma once
#include "wick.hpp"
#include <unordered_map>

namespace wick {

// ---- static types --------------------------------------------------------
struct Type {
    enum K : uint8_t { NUM, BOOL, STR, LIST, MAP, VOID, NILT } k = VOID;
    bool opt = false;              // T?
    K elem = NUM;                  // element type for LIST/MAP
    bool sameBase(const Type& o) const {
        if (k != o.k) return false;
        if (k == LIST || k == MAP) return elem == o.elem;
        return true;
    }
    std::string name() const;
};

// ---- bytecode -------------------------------------------------------------
enum Op : uint8_t {
    OP_CONST,   // u16: push consts[i]
    OP_NIL, OP_TRUE, OP_FALSE, OP_POP,
    OP_LOADL, OP_STOREL,           // u8 frame slot
    OP_LOADG, OP_STOREG,           // u16 global slot
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG, // num
    OP_CONCAT,                     // str + str
    OP_EQ, OP_NE,                  // generic (tag + value)
    OP_LT, OP_LE, OP_GT, OP_GE,    // num
    OP_NOT,
    OP_JMP, OP_JF,                 // u16 forward offset (JF pops a bool)
    OP_JB,                         // u16 backward offset
    OP_JNN,                        // if top != nil jump (keep); else pop
    OP_ISNIL,                      // pop, push bool(top is nil)
    OP_CALL,                       // u16 proto idx, u8 argc
    OP_NCALL,                      // u16 native idx, u8 argc
    OP_RET,                        // return nil (void)
    OP_RETV,                       // return top of stack
    OP_LIST,                       // u8 n: build list from top n values
    OP_MAP,                        // u8 n: build map from top n (key,value)*
    OP_IGET, OP_ISET,              // list[i] get / set
    OP_MGET, OP_MSET,              // map[k]  get (-> V or nil) / set
    OP_LEN, OP_TOSTR, OP_TONUM,    // generic builtins
    OP_LPUSH, OP_LPOP,             // push(list, v) / pop(list) -> V or nil
    OP_HALT,
};

struct Proto {
    std::string name;
    int nparams = 0;
    int nlocals = 0;               // params + locals (frame size)
    Type ret{Type::VOID};
    std::vector<Type> params;
    std::vector<uint8_t> code;
    std::vector<int> lines;        // one entry per code byte
};

struct Native {
    std::string name;              // "lt.draw" or "floor"
    std::vector<Type> params;
    std::vector<Value> defaults;   // aligned to params tail; NIL = required
    int minArgs = 0;
    Type ret{Type::VOID};
    NativeFn fn = nullptr;
};

// ---- heap objects ---------------------------------------------------------
struct Obj {
    enum K : uint8_t { STR, LIST, MAP } k = STR;
    bool mark = false;
    Obj* next = nullptr;
    std::string s;
    std::vector<Value> list;
    std::unordered_map<std::string, Value> map;
};

// ---- the VM ---------------------------------------------------------------
struct VM {
    // program (rebuilt by load(); cleared by reset())
    std::vector<Value> consts;
    std::vector<Proto> protos;                       // [0] = top level
    std::unordered_map<std::string, int> fnByName;
    std::vector<Value> globals;
    std::vector<Type> gtypes;
    std::unordered_map<std::string, int> gByName;
    // environment (survives reset)
    std::vector<Native> natives;
    std::unordered_map<std::string, int> natByName;
    std::unordered_map<std::string, double> numConsts; // "lt.W" -> 400
    // heap
    Obj* objects = nullptr;
    std::vector<Value> stack;
    // runtime error state
    std::string rerr;
    int rline = 0;
    std::string chunkName;

    Obj* newObj(Obj::K k);
};

bool compile(VM& vm, const std::string& src, const std::string& chunkName,
             std::string& err);                      // wick_front.cpp
bool run(VM& vm, int protoIdx, const Value* args, int argc, Value& out,
         std::string& err);                          // wick_vm.cpp

} // namespace wick
