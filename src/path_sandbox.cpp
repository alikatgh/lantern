// path_sandbox.cpp — lexical + realpath confinement under a game root.
#include "path_sandbox.hpp"
#include <cctype>
#include <cstdlib>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace lt {

namespace {

bool g_packageMode = false;
std::string g_shotDir;

// Collapse "." and refuse ".." while building a normalized relative path.
// Segments may not be empty, start with '.', or contain backslash.
bool normalizeRel(const std::string& rel, std::string& out, std::string& err) {
    if (rel.empty()) {
        err = "empty path";
        return false;
    }
    if (rel[0] == '/' || rel[0] == '\\') {
        err = "absolute paths are not allowed";
        return false;
    }
    // Reject Windows-style drive or any '\\'
    for (char c : rel) {
        if (c == '\\' || c == 0) {
            err = "invalid path character";
            return false;
        }
    }
    std::vector<std::string> segs;
    size_t i = 0;
    while (i < rel.size()) {
        size_t j = rel.find('/', i);
        if (j == std::string::npos) j = rel.size();
        std::string seg = rel.substr(i, j - i);
        i = j + 1;
        if (seg.empty()) {
            err = "empty path segment";
            return false;
        }
        if (seg == "..") {
            err = "path escape ('..') is not allowed";
            return false;
        }
        if (seg == ".") continue;
        if (seg[0] == '.') {
            err = "hidden path segments are not allowed";
            return false;
        }
        segs.push_back(seg);
    }
    if (segs.empty()) {
        err = "empty path";
        return false;
    }
    out.clear();
    for (size_t k = 0; k < segs.size(); k++) {
        if (k) out += '/';
        out += segs[k];
    }
    return true;
}

std::string absPath(const std::string& p) {
    char buf[PATH_MAX];
    if (realpath(p.c_str(), buf)) return std::string(buf);
    return {};
}

} // namespace

bool pathIsSafeRel(const std::string& rel) {
    std::string n, err;
    return normalizeRel(rel, n, err);
}

bool pathResolveUnder(const std::string& root, const std::string& rel,
                      std::string& out, std::string& err) {
    std::string norm;
    if (!normalizeRel(rel, norm, err)) return false;
    // Ensure root has no trailing slash for clean join (except "/")
    std::string r = root;
    while (r.size() > 1 && r.back() == '/') r.pop_back();
    out = r + "/" + norm;

    // If the joined path already exists, realpath both and require prefix.
    std::string aRoot = absPath(r);
    std::string aOut = absPath(out);
    if (!aRoot.empty() && !aOut.empty()) {
        if (aOut.size() < aRoot.size() ||
            aOut.compare(0, aRoot.size(), aRoot) != 0 ||
            (aOut.size() > aRoot.size() && aOut[aRoot.size()] != '/')) {
            err = "path escapes game directory";
            out.clear();
            return false;
        }
        out = aOut;
        return true;
    }
    // File may not exist yet (load will fail later) — still reject if any
    // parent that does exist escapes. Walk up creating a candidate check.
    std::string cand = out;
    while (!cand.empty()) {
        std::string a = absPath(cand);
        if (!a.empty()) {
            if (!aRoot.empty()) {
                if (a.size() < aRoot.size() ||
                    a.compare(0, aRoot.size(), aRoot) != 0) {
                    err = "path escapes game directory";
                    out.clear();
                    return false;
                }
            }
            break;
        }
        size_t slash = cand.find_last_of('/');
        if (slash == std::string::npos || slash == 0) break;
        cand = cand.substr(0, slash);
    }
    return true;
}

void pathSetPackageMode(bool on) { g_packageMode = on; }
bool pathIsPackageMode() { return g_packageMode; }

void pathSetScreenshotDir(const std::string& dir) { g_shotDir = dir; }
const std::string& pathScreenshotDir() { return g_shotDir; }

bool pathResolveScreenshot(const std::string& gameDir, const std::string& req,
                           std::string& out, std::string& err) {
    if (g_packageMode) {
        // Basename only under host-owned shot dir (or game temp extract dir).
        std::string base = req;
        size_t slash = base.find_last_of("/\\");
        if (slash != std::string::npos) base = base.substr(slash + 1);
        if (base.empty() || base[0] == '.' || base.find("..") != std::string::npos) {
            err = "bad screenshot name";
            return false;
        }
        for (char c : base) {
            if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' ||
                  c == '.')) {
                err = "bad screenshot name";
                return false;
            }
        }
        std::string dir = g_shotDir.empty() ? gameDir : g_shotDir;
        while (!dir.empty() && dir.back() == '/') dir.pop_back();
        out = dir + "/" + base;
        return true;
    }
    // Folder mode: under game dir only, same rules as load paths.
    return pathResolveUnder(gameDir, req, out, err);
}

} // namespace lt
