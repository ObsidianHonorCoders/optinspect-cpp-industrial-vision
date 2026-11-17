#pragma once

#include <cstdint>

namespace optinspect::viz {

/// GPU time measurement with OpenGL timer queries (GL_TIME_ELAPSED, core since 3.3).
/// Two query objects are alternated so reading a result never stalls the pipeline:
/// the value returned by lastMs() belongs to a frame that finished 1-2 frames ago.
/// Requires a current OpenGL context.
class GpuTimer {
public:
    GpuTimer();
    ~GpuTimer();
    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;

    void begin();
    void end();

    /// Most recent completed measurement in milliseconds (0 until one is available).
    double lastMs() const { return last_ms_; }
    std::uint64_t samples() const { return samples_; }

private:
    unsigned int queries_[2] = {0, 0};
    int current_ = 0;
    bool pending_[2] = {false, false};
    bool active_ = false;
    double last_ms_ = 0.0;
    std::uint64_t samples_ = 0;
};

}  // namespace optinspect::viz
