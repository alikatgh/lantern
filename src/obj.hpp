// obj.hpp — internal interface for the OBJ loader (see obj.cpp).
#pragma once
#include <vector>

namespace lt {
// Appends lantern-format vertices (12 floats each) to `out`.
bool loadObj(const char* path, std::vector<float>& out);
} // namespace lt
