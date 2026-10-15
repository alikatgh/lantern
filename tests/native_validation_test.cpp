#include "lantern.h"
#include "save_storage.hpp"
#include "platform.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <sys/stat.h>

namespace {
int assertions = 0;
void expect(bool pass, const char* why) {
    ++assertions;
    if (!pass) throw std::runtime_error(why);
}
std::string contents(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void saves(const char* scratch) {
    namespace fs = std::filesystem;
    const mode_t mask = umask(0);
    umask(mask);
    const fs::path base = fs::path(scratch) / "native-save-fixture";
    expect(!fs::exists(base), "fixture must be new");
    fs::create_directories(base / "private");
    fs::create_directories(base / "outside-dir");
    std::ofstream(base / "outside") << "preserve";
    fs::create_directory_symlink(base / "private", base / "root-link");
    try {
        unsetenv("LANTERN_FRAME_STATS");
        setenv("LANTERN_SAVE_ROOT", (base / "private").c_str(), 1);
        // These checks run before any boot. The platform path guard is checked
        // first, so a failure never proceeds to a default-storage write.
        expect(lt::platSavePath("private-probe").empty(), "preboot explicit root cannot resolve ordinary storage");
        expect(!lt_save_write("private-probe", "x", 1), "preboot explicit root fails closed");
        for (const char* setting : {"LANTERN_RENDER_SCALE", "LANTERN_RENDER_NATIVE", "LANTERN_PAINTED_2D", "LANTERN_RENDER_WORKERS"}) {
            setenv(setting, "invalid", 1);
            expect(!lt_boot("invalid rendering with selected root", 1), "early render validation rejects boot");
            expect(lt::platSavePath("private-probe").empty(), "early boot failure cannot resolve ordinary storage");
            expect(!lt_save_write("private-probe", "x", 1), "early boot failure cannot fall back on write");
            char buffer[16];
            expect(lt_save_read("private-probe", buffer, sizeof(buffer)) == -1, "early boot failure cannot fall back on read");
            unsetenv(setting);
        }
        for (const std::string& root : {std::string{}, std::string{"relative"},
                (base / "missing").string(), (base / "outside").string(),
                (base / "root-link").string(), (base / "root-link").string() + "/"}) {
            setenv("LANTERN_SAVE_ROOT", root.c_str(), 1);
            expect(!lt_boot("invalid private save root", 1), "invalid selected root rejects boot");
            expect(!lt_save_write("private-probe", "x", 1), "invalid root never falls back on write");
            char buffer[16];
            expect(lt_save_read("private-probe", buffer, sizeof(buffer)) == -1, "invalid root never falls back on read");
        }
        setenv("LANTERN_SAVE_ROOT", (base / "private").c_str(), 1);
        setenv("LANTERN_FRAME_STATS", "0", 1);
        expect(!lt_boot("invalid stats with selected root", 1), "invalid stats rejects boot with private root selected");
        expect(!lt_save_write("private-probe", "x", 1), "combined settings failure cannot fall back on write");
        unsetenv("LANTERN_FRAME_STATS");
        expect(lt_boot("private saves", 1), "valid private root boots");
        const char data[] = {'a', 0, 'b'};
        char buffer[16]{};
        expect(lt_save_read("slot", buffer, sizeof(buffer)) == -1, "missing save remains missing");
        expect(lt_save_write("slot", data, sizeof(data)) == 1, "private binary-safe write");
        expect(lt_save_read("slot", buffer, sizeof(buffer)) == 3 && !std::memcmp(buffer, data, 3), "private binary-safe read");
        expect(contents(base / "private/slot") == std::string(data, sizeof(data)), "file only in selected root");
        struct stat st{};
        expect(stat((base / "private/slot").c_str(), &st) == 0 && (st.st_mode & 0777) == (0600 & ~mask), "private saves have owner-only creation permissions");
        expect(lt_save_write("slot", "z", 1) == 1 && contents(base / "private/slot") == "z", "regular overwrite truncates correctly");
        expect(lt_save_write("empty", nullptr, 0) == 1 && lt_save_read("empty", nullptr, 0) == 0, "empty save remains distinct from missing");
        for (const char* name : {"", ".", "..", "../outside", "/outside", "a/b", "a\\b", "a:b"}) {
            expect(!lt_save_write(name, "x", 1), "unsafe save name rejects write");
            expect(lt_save_read(name, buffer, sizeof(buffer)) == -1, "unsafe save name rejects read");
        }
        fs::create_symlink(base / "outside", base / "private/link");
        fs::create_hard_link(base / "outside", base / "private/hardlink");
        fs::create_directory(base / "private/directory");
        expect(mkfifo((base / "private/fifo").c_str(), 0600) == 0, "FIFO fixture");
        for (const char* name : {"link", "hardlink", "directory", "fifo"}) {
            expect(!lt_save_write(name, "x", 1), "non-owned/nonregular leaf rejects write");
            expect(lt_save_read(name, buffer, sizeof(buffer)) == -1, "non-owned/nonregular leaf rejects read");
        }
        expect(contents(base / "outside") == "preserve", "outside original preserved");
        expect(!lt_save_write("null", nullptr, 1) && lt_save_read("slot", nullptr, 1) == -1, "invalid buffers rejected");
        fs::rename(base / "private", base / "pinned");
        fs::create_directory_symlink(base / "outside-dir", base / "private");
        expect(lt_save_write("pinned-slot", "p", 1) == 1, "directory descriptor survives path replacement");
        expect(contents(base / "pinned/pinned-slot") == "p" && !fs::exists(base / "outside-dir/pinned-slot"), "replacement symlink cannot redirect selected saves");
        lt_shutdown();
        expect(!lt_save_write("closed-slot", "x", 1), "closed selected root never falls back");

        unsetenv("LANTERN_SAVE_ROOT");
        expect(lt_boot("ordinary save selection", 1), "unset environment boots normally");
        expect(!lt::desktopSaveRoot().selected(), "unset root preserves ordinary platform path selection");
        setenv("LANTERN_SAVE_ROOT", (base / "private").c_str(), 1);
        expect(lt::platSavePath("private-probe").empty(), "late explicit selection cannot resolve ordinary storage");
        expect(!lt_save_write("private-probe", "x", 1), "late explicit selection refuses fallback");
        unsetenv("LANTERN_SAVE_ROOT");
        // Exercise the unchanged default-path routing on a fixture, never user data.
        FILE* file = lt::desktopSaveRoot().openFile("default-slot", (base / "default-slot").string(), true);
        expect(file && std::fwrite("d", 1, 1, file) == 1, "unselected root uses supplied ordinary path");
        expect(std::fclose(file) == 0 && contents(base / "default-slot") == "d", "ordinary path write verified in fixture");
        expect(stat((base / "default-slot").c_str(), &st) == 0 && (st.st_mode & 0777) == (0666 & ~mask), "ordinary save creation permissions remain unchanged");
        lt_shutdown();
        fs::remove_all(base);
        std::printf("native_validation_saves: %d assertions passed\n", assertions);
    } catch (...) {
        lt_shutdown();
        fs::remove_all(base);
        throw;
    }
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::strcmp(argv[1], "saves") == 0) {
            saves(argv[2]);
            return 0;
        }
        if (argc == 3 && std::strcmp(argv[1], "frames") == 0) {
            if (!lt_boot("native frame-stat fixture", 1)) return 2;
            int frames = 0;
            const int target = std::atoi(argv[2]);
            while (frames < target && lt_frame_poll()) {
                lt_frame_begin(); lt_clear(0, 0, 0); lt_frame_end();
                ++frames;
            }
            lt_shutdown();
            std::printf("{\"presented_frames\":%d}\n", frames);
            return frames == target ? 0 : 1;
        }
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "native_validation_test: %s\n", error.what());
        return 1;
    }
}
