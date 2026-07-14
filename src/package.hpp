// package.hpp — the .lant game package: lantern's own archive format.
// One file, whole game: manifest + scripts + assets, CRC-checked. Nothing
// borrowed (no zip/tar) — ~150 lines we fully control, like everything else.
//
// Layout (all integers little-endian):
//   8   magic "LANTPKG1"
//   u32 file count
//   per file:  u16 name length, name bytes (utf-8, '/' separators,
//              validated: no "..", no leading '/', no '\'),
//              u32 data length, data bytes
//   u32 CRC-32 (IEEE) of every byte before it
//
// The first file MUST be "game.info" — plain key=value lines:
//   id=com.author.game   title=...   version=1.0.0   author=...
//   entry=main.wick      engine=0.7
// Store packages are wick-only (entry must be main.wick): wick games are
// pure data — the VM's only exits are the typed lt.* natives — so a
// package can't reach the filesystem or network. See docs/PACKAGE.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace lt {

struct PkgFile {
    std::string name;
    std::vector<uint8_t> data;
};

// Read + validate a package. On failure returns false and sets err.
bool pkgRead(const std::string& path, std::vector<PkgFile>& out,
             std::string& err);
// Write a package from files (caller puts game.info first).
bool pkgWrite(const std::string& path, const std::vector<PkgFile>& files,
              std::string& err);
// Extract into dir (created if needed; subdirs are rejected by the name
// validator, so games stay flat — same rule as game folders).
bool pkgExtract(const std::string& path, const std::string& dir,
                std::string& err);

uint32_t pkgCrc32(const uint8_t* data, size_t len, uint32_t seed = 0);

} // namespace lt
