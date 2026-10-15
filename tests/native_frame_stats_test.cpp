#include "native_frame_stats.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <clocale>
#include <new>
#include <stdexcept>

namespace {
bool countAllocations = false;
int allocations = 0, assertions = 0;
void expect(bool pass, const char* why) {
    ++assertions;
    if (!pass) throw std::runtime_error(why);
}
void warm(lt::NativeFrameStats& stats) {
    for (int64_t i = 1; i <= 30; ++i) {
        stats.poll(i * 20000000);
        stats.presented(i * 20000000 + 1000000);
    }
}
void readReport(FILE* file, char* text, size_t size) {
    std::rewind(file);
    const size_t got = std::fread(text, 1, size - 1, file);
    text[got] = 0;
}
}
void* operator new(size_t size) {
    if (countAllocations) ++allocations;
    if (void* p = std::malloc(size)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

int main() {
    try {
        lt::NativeFrameStats stats;
        static_assert(sizeof(stats) < 64 * 1024);
        FILE* file = std::tmpfile();
        expect(file != nullptr, "temporary report stream");
        stats.poll(0); stats.presented(1000000); stats.report(file, "dummy", "software");
        expect(stats.count() == 0 && std::ftell(file) == 0, "default off emits nothing");
        for (const char* bad : {"", "0", "00", "01", "3601", "99999999999999999999", "+1", "-1", "1 ", " 1", "1x", "1.0"}) {
            expect(!stats.configure(bad), "strict bounded sample parser");
            expect(!stats.collecting() && !stats.reportPending(), "invalid setting cannot collect/report");
        }
        for (const char* good : {"1", "3600"})
            expect(stats.configure(good) && stats.collecting(), "inclusive sample bounds");
        expect(stats.configure("4"), "configure known duration samples");
        stats.presented(1); // unpaired presentation cannot count or warm up
        expect(stats.warmup() == 0, "unpaired presentation ignored");
        warm(stats);
        expect(stats.warmup() == 30 && stats.count() == 0, "exact completed warmup bound");
        const int64_t cadence[] = {20, 30, 50, 300}, duration[] = {1, 5, 40, 250};
        int64_t end = 601000000;
        for (int i = 0; i < 4; ++i) {
            end += cadence[i] * 1000000;
            stats.poll(end - duration[i] * 1000000);
            stats.presented(end);
        }
        expect(stats.count() == 4 && stats.complete() && !stats.collecting(), "target stops collection");
        stats.poll(end + 1); stats.presented(end + 1000000000);
        expect(stats.count() == 4, "extra frames cannot grow collection");
        char originalLocale[128];
        std::snprintf(originalLocale, sizeof(originalLocale), "%s", std::setlocale(LC_NUMERIC, nullptr));
        bool commaLocale = false;
        for (const char* name : {"fr_FR.UTF-8", "fr_FR.utf8", "de_DE.UTF-8", "de_DE.utf8"}) {
            if (std::setlocale(LC_NUMERIC, name) && std::strcmp(std::localeconv()->decimal_point, ",") == 0) {
                commaLocale = true;
                break;
            }
        }
        stats.report(file, "du\"m\\my\n", "software");
        std::setlocale(LC_NUMERIC, originalLocale);
        const long reportSize = std::ftell(file);
        stats.report(file, "dummy", "software");
        expect(std::ftell(file) == reportSize, "only one report");
        char text[4096]; readReport(file, text, sizeof(text));
        expect(std::strstr(text, "\"reason\":\"target\"") != nullptr, "target reason");
        expect(std::strstr(text, "\"median\":22.500000,\"p95\":250.000000") != nullptr, "unclamped poll duration distribution");
        expect(std::strstr(text, "\"median\":40.000000,\"p95\":300.000000") != nullptr, "distinct presentation cadence distribution");
        expect(std::strstr(text, "du\\\"m\\\\my\\u000a") != nullptr, "JSON escaping");
        // The Python SDL contract test parses this report as JSON too.
        std::fputs(text, stdout);
        std::printf("native_frame_stats_comma_locale: %s\n", commaLocale ? "verified" : "unavailable");
        std::fclose(file);

        file = std::tmpfile(); expect(file != nullptr, "partial report stream");
        stats.configure("10"); warm(stats);
        stats.poll(700000000); stats.presented(701000000);
        stats.poll(701000001); stats.presented(701000000); // invalid fake clock
        expect(stats.count() == 1, "backwards fake clock ignored, never clamped");
        stats.report(file, "dummy", "software"); readReport(file, text, sizeof(text));
        expect(std::strstr(text, "\"reason\":\"shutdown\"") && std::strstr(text, "\"samples\":1"), "partial shutdown report");
        std::fclose(file);

        file = std::tmpfile(); expect(file != nullptr, "bounded report stream");
        allocations = 0; countAllocations = true;
        stats.configure("3600");
        for (int64_t i = 1; i < 10000; ++i) {
            stats.poll(i * 20000000);
            stats.presented(i * 20000000 + 1000000);
        }
        stats.report(file, "dummy", "software");
        countAllocations = false;
        expect(stats.count() == 3600 && allocations == 0, "fixed memory at and beyond sample cap, including report");
        std::fclose(file);
        expect(stats.configure(nullptr) && !stats.collecting(), "reset returns to default off");
        std::printf("native_frame_stats_test: %d assertions passed\n", assertions);
        return 0;
    } catch (const std::exception& error) {
        countAllocations = false;
        std::fprintf(stderr, "native_frame_stats_test: %s\n", error.what());
        return 1;
    }
}
