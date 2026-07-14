// pack_main.cpp — `lantern_pack <game_dir> <out.lant>`: package a wick game
// folder for the store. The inverse lives in the engine host: `lantern
// game.lant` extracts and runs it. Store packages are wick-only — main.wick
// must exist (see docs/PACKAGE.md for why).
#include "../src/package.hpp"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

static bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(f);
        return false;
    }
    out.resize((size_t)size);
    size_t got = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: lantern_pack <game_dir> <out.lant>\n");
        return 2;
    }
    const std::string dir = argv[1], out = argv[2];

    struct stat st{};
    if (stat((dir + "/main.wick").c_str(), &st) != 0) {
        std::fprintf(stderr,
                     "lantern_pack: %s has no main.wick — store packages "
                     "are wick-only (wick games are pure data; Lua games "
                     "can reach os/io and cannot be packaged)\n",
                     dir.c_str());
        return 1;
    }

    std::vector<lt::PkgFile> files;

    // game.info first — synthesized if the folder doesn't carry one yet
    lt::PkgFile info;
    info.name = "game.info";
    if (!readAll(dir + "/game.info", info.data)) {
        std::string base = dir;
        while (!base.empty() && base.back() == '/') base.pop_back();
        size_t slash = base.find_last_of('/');
        if (slash != std::string::npos) base = base.substr(slash + 1);
        std::string s = "id=local." + base + "\ntitle=" + base +
                        "\nversion=0.0.0\nauthor=unknown\nentry=main.wick\n";
        info.data.assign(s.begin(), s.end());
        std::fprintf(stderr,
                     "lantern_pack: no game.info in %s — synthesized one "
                     "(add a real one before submitting to the store)\n",
                     dir.c_str());
    }
    files.push_back(std::move(info));

    DIR* d = opendir(dir.c_str());
    if (!d) {
        std::fprintf(stderr, "lantern_pack: cannot read %s\n", dir.c_str());
        return 1;
    }
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n.empty() || n[0] == '.' || n == "game.info") continue;
        lt::PkgFile pf;
        pf.name = n;
        if (!readAll(dir + "/" + n, pf.data)) {
            std::fprintf(stderr, "lantern_pack: skipping unreadable %s\n",
                         n.c_str());
            continue; // subdirectories land here too — packages are flat
        }
        files.push_back(std::move(pf));
    }
    closedir(d);

    std::string err;
    if (!lt::pkgWrite(out, files, err)) {
        std::fprintf(stderr, "lantern_pack: %s\n", err.c_str());
        return 1;
    }
    size_t total = 0;
    for (const lt::PkgFile& pf : files) total += pf.data.size();
    std::printf("lantern_pack: %s — %zu files, %.1f KB\n", out.c_str(),
                files.size(), total / 1024.0);
    return 0;
}
