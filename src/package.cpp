// package.cpp — read/write/extract .lant packages (see package.hpp).
#include "package.hpp"
#include <cstdio>
#include <cstring>

namespace lt {

namespace {

constexpr char kMagic[8] = {'L', 'A', 'N', 'T', 'P', 'K', 'G', '1'};
constexpr uint32_t kMaxFiles = 1024;
constexpr uint32_t kMaxFileSize = 64u * 1024 * 1024;  // one asset
constexpr size_t kMaxPkgSize = 256u * 1024 * 1024;    // whole package

void putU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back((uint8_t)(v & 0xff));
    b.push_back((uint8_t)(v >> 8));
}
void putU32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (i * 8)));
}
uint16_t getU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t getU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

// A name is a flat file name: utf-8, no path separators at all, no "..",
// nothing hidden. Games are flat folders; packages stay flat too.
bool nameOk(const std::string& n) {
    if (n.empty() || n.size() > 255) return false;
    if (n[0] == '.') return false; // no hidden files, no "." / ".."
    for (char c : n)
        if (c == '/' || c == '\\' || c == 0) return false;
    return true;
}

} // namespace

uint32_t pkgCrc32(const uint8_t* data, size_t len, uint32_t seed) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t crc = seed ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

bool pkgRead(const std::string& path, std::vector<PkgFile>& out,
             std::string& err) {
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        err = "cannot open " + path;
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < (long)(sizeof kMagic + 8) || (size_t)size > kMaxPkgSize) {
        std::fclose(f);
        err = "not a .lant package (bad size)";
        return false;
    }
    std::vector<uint8_t> d((size_t)size);
    size_t got = std::fread(d.data(), 1, d.size(), f);
    std::fclose(f);
    if (got != d.size() || std::memcmp(d.data(), kMagic, sizeof kMagic) != 0) {
        err = "not a .lant package (bad magic)";
        return false;
    }
    const uint32_t storedCrc = getU32(&d[d.size() - 4]);
    if (pkgCrc32(d.data(), d.size() - 4) != storedCrc) {
        err = "package is corrupt (CRC mismatch)";
        return false;
    }
    size_t pos = sizeof kMagic;
    const uint32_t count = getU32(&d[pos]);
    pos += 4;
    if (count == 0 || count > kMaxFiles) {
        err = "package has a bad file count";
        return false;
    }
    const size_t end = d.size() - 4;
    for (uint32_t i = 0; i < count; i++) {
        if (pos + 2 > end) { err = "package is truncated"; return false; }
        const uint16_t nlen = getU16(&d[pos]);
        pos += 2;
        if (nlen == 0 || pos + nlen + 4 > end) {
            err = "package is truncated";
            return false;
        }
        PkgFile pf;
        pf.name.assign((const char*)&d[pos], nlen);
        pos += nlen;
        if (!nameOk(pf.name)) {
            err = "package contains a bad file name: " + pf.name;
            return false;
        }
        const uint32_t dlen = getU32(&d[pos]);
        pos += 4;
        if (dlen > kMaxFileSize || pos + dlen > end) {
            err = "package is truncated";
            return false;
        }
        pf.data.assign(&d[pos], &d[pos] + dlen);
        pos += dlen;
        out.push_back(std::move(pf));
    }
    if (pos != end) {
        err = "package has trailing garbage";
        return false;
    }
    if (out[0].name != "game.info") {
        err = "first package entry must be game.info";
        return false;
    }
    return true;
}

bool pkgWrite(const std::string& path, const std::vector<PkgFile>& files,
              std::string& err) {
    if (files.empty() || files.size() > kMaxFiles) {
        err = "bad file count";
        return false;
    }
    if (files[0].name != "game.info") {
        err = "first package entry must be game.info";
        return false;
    }
    std::vector<uint8_t> b;
    b.insert(b.end(), kMagic, kMagic + sizeof kMagic);
    putU32(b, (uint32_t)files.size());
    for (const PkgFile& pf : files) {
        if (!nameOk(pf.name)) {
            err = "bad file name: " + pf.name;
            return false;
        }
        if (pf.data.size() > kMaxFileSize) {
            err = pf.name + " is too large";
            return false;
        }
        putU16(b, (uint16_t)pf.name.size());
        b.insert(b.end(), pf.name.begin(), pf.name.end());
        putU32(b, (uint32_t)pf.data.size());
        b.insert(b.end(), pf.data.begin(), pf.data.end());
    }
    putU32(b, pkgCrc32(b.data(), b.size()));
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        err = "cannot write " + path;
        return false;
    }
    size_t wrote = std::fwrite(b.data(), 1, b.size(), f);
    std::fclose(f);
    if (wrote != b.size()) {
        err = "short write to " + path;
        return false;
    }
    return true;
}

bool pkgExtract(const std::string& path, const std::string& dir,
                std::string& err) {
    std::vector<PkgFile> files;
    if (!pkgRead(path, files, err)) return false;
    for (const PkgFile& pf : files) {
        const std::string p = dir + "/" + pf.name;
        FILE* f = std::fopen(p.c_str(), "wb");
        if (!f) {
            err = "cannot write " + p;
            return false;
        }
        size_t wrote = pf.data.empty()
                           ? 0
                           : std::fwrite(pf.data.data(), 1, pf.data.size(), f);
        std::fclose(f);
        if (wrote != pf.data.size()) {
            err = "short write to " + p;
            return false;
        }
    }
    return true;
}

} // namespace lt
