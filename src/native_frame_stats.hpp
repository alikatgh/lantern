// native_frame_stats.hpp — bounded, opt-in native framework timing diagnostics.
// SDL enables collection; CPU/headless backends never enable it implicitly.
#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace lt {

class NativeFrameStats {
public:
    static constexpr int maxSamples = 3600;
    static constexpr int warmupFrames = 30;

    bool configure(const char* value) {
        target_ = count_ = warmup_ = 0;
        reported_ = pending_ = havePrevious_ = false;
        if (!value) return true;
        if (value[0] < '1' || value[0] > '9') return false;
        int target = 0;
        for (const char* p = value; *p; ++p) {
            if (*p < '0' || *p > '9' || target > maxSamples / 10) return false;
            target = target * 10 + (*p - '0');
            if (target > maxSamples) return false;
        }
        target_ = target;
        return true;
    }

    bool collecting() const { return target_ > 0 && !reported_ && count_ < target_; }
    bool reportPending() const { return target_ > 0 && !reported_; }
    bool complete() const { return target_ > 0 && count_ == target_; }
    int count() const { return count_; }
    int warmup() const { return warmup_; }

    static int64_t now() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void poll(int64_t nowNs) {
        if (!collecting()) return;
        start_ = nowNs;
        pending_ = true;
    }

    // Called immediately after a successful SDL_RenderPresent return. The
    // durations are independent of the game dt clamp and fixed-dt overrides.
    void presented(int64_t nowNs) {
        if (!collecting() || !pending_) return;
        pending_ = false;
        if (nowNs < start_ || (havePrevious_ && nowNs < previous_)) return;
        if (warmup_ < warmupFrames) ++warmup_;
        else {
            pollMs_[count_] = double(nowNs - start_) / 1e6;
            cadenceMs_[count_] = double(nowNs - previous_) / 1e6;
            ++count_;
        }
        previous_ = nowNs;
        havePrevious_ = true;
    }

    void report(FILE* out, const char* video, const char* renderer) {
        if (!reportPending()) return;
        reported_ = true;
        std::fprintf(out, "{\"schema\":\"lantern.native-frame-stats.v1\","
            "\"reason\":\"%s\",\"target_samples\":%d,\"samples\":%d,"
            "\"warmup_target\":%d,\"warmup_completed\":%d,\"clock\":\"steady_clock\","
            "\"boundary\":\"lt_frame_poll entry to SDL_RenderPresent return\","
            "\"cadence_boundary\":\"consecutive SDL_RenderPresent returns\","
            "\"display_scanout_measured\":false,\"video_driver\":",
            complete() ? "target" : "shutdown", target_, count_, warmupFrames, warmup_);
        jsonString(out, video);
        std::fputs(",\"renderer\":", out);
        jsonString(out, renderer);
        std::fputs(",\"poll_to_present_ms\":", out);
        summary(out, pollMs_);
        std::fputs(",\"cadence_ms\":", out);
        summary(out, cadenceMs_);
        std::fputs("}\n", out);
    }

private:
    static void jsonNumber(FILE* out, double value) {
        // Durations originate as signed 64-bit nanoseconds. Split nonnegative
        // milliseconds into integer parts around a literal decimal point:
        // floating printf follows LC_NUMERIC, while floating to_chars is not
        // available on the supported iOS 15 deployment target.
        if (!std::isfinite(value) || value < 0 || value > double(INT64_MAX) / 1e6) {
            std::fputs("null", out);
            return;
        }
        auto whole = static_cast<uint64_t>(value);
        auto fraction = static_cast<unsigned>((value - double(whole)) * 1e6 + 0.5);
        if (fraction == 1000000) { ++whole; fraction = 0; }
        std::fprintf(out, "%llu.%06u", static_cast<unsigned long long>(whole), fraction);
    }

    static void jsonString(FILE* out, const char* text) {
        if (!text) { std::fputs("null", out); return; }
        std::fputc('"', out);
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
            if (*p == '"' || *p == '\\') std::fputc('\\', out);
            if (*p < 32) std::fprintf(out, "\\u%04x", unsigned(*p));
            else std::fputc(*p, out);
        }
        std::fputc('"', out);
    }

    void summary(FILE* out, const std::array<double, maxSamples>& samples) const {
        if (!count_) { std::fputs("null", out); return; }
        // Fixed stack workspace; collection and reporting have no heap growth.
        std::array<double, maxSamples> sorted;
        std::copy_n(samples.begin(), count_, sorted.begin());
        std::sort(sorted.begin(), sorted.begin() + count_);
        double sum = 0;
        int over60Hz = 0;
        for (int i = 0; i < count_; ++i) {
            sum += sorted[i];
            if (sorted[i] > 1000.0 / 60.0) ++over60Hz;
        }
        const double median = count_ % 2 ? sorted[count_ / 2]
            : (sorted[count_ / 2 - 1] + sorted[count_ / 2]) / 2;
        std::fputs("{\"min\":", out); jsonNumber(out, sorted[0]);
        std::fputs(",\"median\":", out); jsonNumber(out, median);
        std::fputs(",\"p95\":", out); jsonNumber(out, sorted[(count_ * 95 + 99) / 100 - 1]);
        std::fputs(",\"p99\":", out); jsonNumber(out, sorted[(count_ * 99 + 99) / 100 - 1]);
        std::fputs(",\"max\":", out); jsonNumber(out, sorted[count_ - 1]);
        std::fputs(",\"mean\":", out); jsonNumber(out, sum / count_);
        std::fprintf(out, ",\"over_60hz_budget\":%d}", over60Hz);
    }

    int target_ = 0, count_ = 0, warmup_ = 0;
    bool reported_ = false, pending_ = false, havePrevious_ = false;
    int64_t start_ = 0, previous_ = 0;
    std::array<double, maxSamples> pollMs_{};
    std::array<double, maxSamples> cadenceMs_{};
};

inline NativeFrameStats& nativeFrameStats() {
    static NativeFrameStats stats;
    return stats;
}

} // namespace lt
