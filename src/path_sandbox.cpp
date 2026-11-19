// path_sandbox.cpp — lexical + canonical confinement under a game root.
#include "path_sandbox.hpp"
#include <cctype>
#include <filesystem>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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
        if (c == '\\' || c == 0
#ifdef _WIN32
            || c == ':' // drive-qualified names and alternate data streams
#endif
        ) {
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

std::string absPath(const std::string& p, std::error_code& ec) {
#ifdef _WIN32
    // MinGW 13's filesystem::canonical does not resolve Windows junctions.
    // Follow reparse points when opening, then query the actual target path.
    const std::filesystem::path requested(p);
    HANDLE handle = CreateFileW(requested.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        ec.assign(GetLastError(), std::system_category());
        return {};
    }
    struct CloseHandleOnExit {
        HANDLE handle;
        ~CloseHandleOnExit() { CloseHandle(handle); }
    } close{handle};
    const DWORD size = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
    if (!size) {
        // The handle exists: even ERROR_PATH_NOT_FOUND here is a resolution
        // failure (for example a volume with no DOS name), not a missing file.
        ec = std::make_error_code(std::errc::io_error);
        return {};
    }
    std::vector<wchar_t> buffer(size);
    const DWORD count = GetFinalPathNameByHandleW(handle, buffer.data(), size,
                                                 FILE_NAME_NORMALIZED);
    if (!count || count >= size) {
        ec = std::make_error_code(std::errc::io_error);
        return {};
    }
    std::wstring path(buffer.data(), count);
    // Convert extended DOS/UNC prefixes back to paths usable by CRT asset I/O.
    if (path.compare(0, 8, L"\\\\?\\UNC\\") == 0)
        path = L"\\\\" + path.substr(8);
    else if (path.compare(0, 4, L"\\\\?\\") == 0 && path.size() > 5 && path[5] == L':')
        path.erase(0, 4);
    ec.clear();
    return std::filesystem::path(path).generic_string();
#else
    const auto canonical = std::filesystem::canonical(p, ec);
    // Resolve links as realpath did, and use '/' on Windows as well as POSIX.
    return ec ? std::string{} : canonical.generic_string();
#endif
}

bool isMissingPath(const std::error_code& ec) {
#ifdef _WIN32
    return ec.category() == std::system_category() &&
           (ec.value() == ERROR_FILE_NOT_FOUND || ec.value() == ERROR_PATH_NOT_FOUND);
#else
    return ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory;
#endif
}

bool isUnder(const std::string& root, const std::string& path) {
    return path.size() >= root.size() &&
           path.compare(0, root.size(), root) == 0 &&
           (path.size() == root.size() || root.back() == '/' ||
            path[root.size()] == '/');
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
    // Keep filesystem roots intact (C:/ must never become drive-relative C:).
    std::filesystem::path r(root);
    while (r != r.root_path() && !r.has_filename()) r = r.parent_path();
    out = (r / std::filesystem::path(norm)).generic_string();

    // If the joined path already exists, canonicalize both and require prefix.
    std::error_code ec;
    std::string aRoot = absPath(r.string(), ec);
    if (aRoot.empty()) {
        err = "cannot resolve game directory";
        out.clear();
        return false;
    }
    std::string aOut = absPath(out, ec);
    if (!aOut.empty()) {
        if (!isUnder(aRoot, aOut)) {
            err = "path escapes game directory";
            out.clear();
            return false;
        }
        out = aOut;
        return true;
    }
    if (!isMissingPath(ec)) {
        err = "cannot resolve game path";
        out.clear();
        return false;
    }
    // File may not exist yet (load will fail later) — still reject if any
    // parent that does exist escapes. Walk up creating a candidate check.
    std::string cand = out;
    while (!cand.empty()) {
        std::string a = absPath(cand, ec);
        if (!a.empty()) {
            if (!isUnder(aRoot, a)) {
                err = "path escapes game directory";
                out.clear();
                return false;
            }
            break;
        }
        if (!isMissingPath(ec)) {
            err = "cannot resolve game path";
            out.clear();
            return false;
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
