#include "visualization/GpuTimer.hpp"

#include <glad/glad.h>

namespace optinspect::viz {

GpuTimer::GpuTimer() { glGenQueries(2, queries_); }

GpuTimer::~GpuTimer() { glDeleteQueries(2, queries_); }

void GpuTimer::begin() {
    if (active_) return;
    // Collect the older query (the one we are about to reuse) if it finished.
    const int idx = current_;
    if (pending_[idx]) {
        GLuint available = 0;
        glGetQueryObjectuiv(queries_[idx], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available) {
            GLuint64 ns = 0;
            glGetQueryObjectui64v(queries_[idx], GL_QUERY_RESULT, &ns);
            last_ms_ = static_cast<double>(ns) / 1.0e6;
            ++samples_;
            pending_[idx] = false;
        } else {
            return;  // previous measurement still in flight; skip this frame
        }
    }
    glBeginQuery(GL_TIME_ELAPSED, queries_[idx]);
    active_ = true;
}

void GpuTimer::end() {
    if (!active_) return;
    glEndQuery(GL_TIME_ELAPSED);
    pending_[current_] = true;
    current_ ^= 1;
    active_ = false;
}

}  // namespace optinspect::viz
