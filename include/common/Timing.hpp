#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace optinspect {

/// A single named timing measurement for one pipeline stage, expressed in
/// milliseconds. Used to populate the "stage timings" section of the
/// inspection report (see ReportWriter).
struct StageTiming {
    std::string stage_name;
    double milliseconds = 0.0;
};

/// Lightweight RAII/manual stopwatch used to measure the duration of
/// individual pipeline stages (load, align, compare, detect, decide, etc.)
/// without scattering std::chrono calls throughout the business logic.
class Stopwatch {
public:
    Stopwatch() { reset(); }

    void reset() { start_ = std::chrono::steady_clock::now(); }

    /// Returns elapsed time since construction/reset, in milliseconds.
    double elapsedMilliseconds() const {
        const auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(now - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_;
};

/// Accumulates named stage timings during a single inspection run.
class PipelineTimer {
public:
    /// Records a stage duration. Call this immediately after a stage
    /// completes, using a Stopwatch started at the stage's beginning.
    void record(const std::string& stage_name, double milliseconds) {
        timings_.push_back(StageTiming{stage_name, milliseconds});
    }

    const std::vector<StageTiming>& timings() const { return timings_; }

    double totalMilliseconds() const {
        double total = 0.0;
        for (const auto& t : timings_) total += t.milliseconds;
        return total;
    }

private:
    std::vector<StageTiming> timings_;
};

}  // namespace optinspect
