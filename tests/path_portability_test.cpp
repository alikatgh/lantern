// Filesystem portability and confinement checks; no SDL, Lua or window needed.
#include "package.hpp"
#include "path_sandbox.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#include <cstddef>
#include <cstring>
#endif

namespace fs = std::filesystem;

static void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

#ifdef _WIN32
// GCC 13 cannot create/detect directory symlinks via std::filesystem. Junctions
// need no Developer Mode/admin permission and exercise native reparse handling.
static bool createJunction(const fs::path& target, const fs::path& link) {
    if (!CreateDirectoryW(link.c_str(), nullptr)) return false;
    HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) { RemoveDirectoryW(link.c_str()); return false; }
    struct MountPointData {
        DWORD tag;
        WORD dataLength, reserved;
        WORD substituteOffset, substituteLength, printOffset, printLength;
        wchar_t path[1];
    };
    const auto absolute = fs::absolute(target).make_preferred().native();
    const std::wstring substitute = L"\\??\\" + absolute;
    const size_t bytes = offsetof(MountPointData, path) +
        (substitute.size() + absolute.size() + 2) * sizeof(wchar_t);
    std::vector<unsigned char> buffer(bytes, 0);
    auto* data = reinterpret_cast<MountPointData*>(buffer.data());
    data->tag = IO_REPARSE_TAG_MOUNT_POINT;
    data->dataLength = static_cast<WORD>(bytes - 8);
    data->substituteLength = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    data->printOffset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t));
    data->printLength = static_cast<WORD>(absolute.size() * sizeof(wchar_t));
    std::memcpy(data->path, substitute.c_str(), (substitute.size() + 1) * sizeof(wchar_t));
    std::memcpy(reinterpret_cast<unsigned char*>(data->path) + data->printOffset,
                absolute.c_str(), (absolute.size() + 1) * sizeof(wchar_t));
    DWORD returned = 0;
    const bool ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(),
        static_cast<DWORD>(buffer.size()), nullptr, 0, &returned, nullptr);
    CloseHandle(handle);
    if (!ok) RemoveDirectoryW(link.c_str());
    return ok;
}
#endif

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const fs::path fixture = fs::absolute(argv[1]);
    // Only clean a fresh fixture created by this invocation.
    if (!fs::create_directory(fixture)) return 2;
    std::vector<fs::path> junctions;
    struct Cleanup {
        fs::path path;
        std::vector<fs::path>& junctions;
        ~Cleanup() {
#ifdef _WIN32
            // Remove links explicitly: GCC 13 remove_all may follow junctions.
            for (const auto& link : junctions)
                if (!RemoveDirectoryW(link.c_str())) return;
#endif
            std::error_code ec; fs::remove_all(path, ec);
        }
    } cleanup{fixture, junctions};
    try {
        junctions.reserve(2);
        const auto game = fixture / "game with spaces";
        const auto sibling = fixture / "game with spaces-outside";
        fs::create_directories(game / "assets" / "nested");
        fs::create_directory(sibling);
        std::ofstream(game / "assets" / "nested" / "texture.bmp") << "asset";
        std::ofstream(sibling / "secret.bmp") << "outside";
        std::string out, err;
        auto resolve = [&](const std::string& root, const std::string& rel) {
            out.clear(); err.clear();
            return lt::pathResolveUnder(root, rel, out, err);
        };
        const auto asset = game / "assets" / "nested" / "texture.bmp";
        require(resolve(game.string(), "assets/nested/texture.bmp"), err);
        require(out == fs::canonical(asset).generic_string(), "canonical separators");
        require(resolve(fs::relative(game).string(), "./assets/nested/texture.bmp"), err);
        require(out == fs::canonical(asset).generic_string(), "relative game root");
        require(resolve(game.string() + "/", "assets/nested/texture.bmp"), err);
        require(resolve(game.string(), "assets/nested/missing.bmp"), err);
        require(resolve(game.string(), "missing/subdir/texture.bmp"), err);
        for (const auto* rel : {"../secret.bmp", "/secret.bmp", "\\secret.bmp",
                                "assets/../secret.bmp", "assets\\texture.bmp",
                                "assets//texture.bmp", ".hidden/texture.bmp"})
            require(!resolve(game.string(), rel), std::string("accepted ") + rel);
        require(resolve(game.root_path().string(),
                        asset.lexically_relative(game.root_path()).generic_string()),
                "filesystem root must contain its descendants");
        require(resolve(game.root_path().generic_string(),
                        asset.lexically_relative(game.root_path()).generic_string()),
                "forward-slash filesystem root must contain its descendants");
        require(!resolve((fixture / "nonexistent-game").string(), "texture.bmp"),
                "unresolved game root accepted");

#ifdef _WIN32
        require(createJunction(sibling, game / "escape"), "cannot create escape junction");
        junctions.push_back(game / "escape");
        require(!resolve(game.string(), "escape/secret.bmp"), "existing junction escape");
        require(!resolve(game.string(), "escape/missing.bmp"), "missing leaf junction escape");
        require(!resolve(game.string(), "escape/missing/deeper.bmp"), "missing parent junction escape");
        require(createJunction(game / "assets", game / "alias"), "cannot create internal junction");
        junctions.push_back(game / "alias");
        require(resolve(game.string(), "alias/nested/texture.bmp"), err);
#else
        std::error_code ec;
        fs::create_directory_symlink(sibling, game / "escape", ec);
        if (!ec) {
            require(!resolve(game.string(), "escape/secret.bmp"), "existing link escape");
            require(!resolve(game.string(), "escape/missing.bmp"), "missing leaf link escape");
            require(!resolve(game.string(), "escape/missing/deeper.bmp"), "missing parent link escape");
            fs::create_directory_symlink(game / "assets", game / "alias");
            require(resolve(game.string(), "alias/nested/texture.bmp"), err);
        } else {
            std::cout << "SKIP: directory symlinks unavailable: " << ec.message() << '\n';
        }
#endif

#ifdef _WIN32
        require(out.find('\\') == std::string::npos, "Windows separators leaked");
        require(!resolve(game.string(), "C:/secret.bmp"), "drive-qualified path accepted");
        require(!resolve(game.string(), "assets/texture.bmp:stream"), "alternate data stream accepted");
#endif

        const std::vector<lt::PkgFile> files = {
            {"game.info", {'i', 'n', 'f', 'o'}},
            {"main.wick", {'f', 'n'}},
            {"assets/nested/texture.bmp", {'a', 's', 's', 'e', 't'}}};
        const auto package = fixture / "game.lant";
        require(lt::pkgWrite(package.string(), files, err), err);
        const auto extract = fixture / "extract with spaces";
        fs::create_directory(extract);
        require(lt::pkgExtract(package.string(), extract.string(), err), err);
        require(lt::pkgExtract(package.string(), extract.string(), err), "existing parents: " + err);
        std::ifstream input(extract / "assets" / "nested" / "texture.bmp", std::ios::binary);
        const std::string contents((std::istreambuf_iterator<char>(input)), {});
        require(contents == "asset", "nested package contents differ");
        require(resolve(extract.string(), "assets/nested/texture.bmp"), err);
        const auto blocked = fixture / "blocked";
        fs::create_directory(blocked);
        std::ofstream(blocked / "assets") << "must stay a file";
        require(!lt::pkgExtract(package.string(), blocked.string(), err), "file accepted as parent directory");
#ifdef _WIN32
        auto invalid = files;
        invalid.push_back({"assets/texture.bmp:stream", {'x'}});
        require(!lt::pkgWrite((fixture / "invalid.lant").string(), invalid, err),
                "package alternate data stream accepted");
#endif
        std::cout << "path portability: asset loading, nested packages and confinement passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
