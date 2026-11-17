#pragma once

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace optinspect::gui {

/// Outcome of a background job: either a value or an error message.
template <typename T>
struct TaskResult {
    std::optional<T> value;
    std::string error;
    double elapsed_ms = 0.0;

    bool ok() const { return value.has_value(); }
};

/// Runs one job at a time on an owned std::thread and hands the result back to
/// the polling (GUI) thread. There are no detached threads: the destructor
/// joins, so a running job always finishes before the owner goes away.
///
/// Rules for the job function: it must not touch ImGui or OpenGL and must only
/// use data it owns or copied (capture by value). Exceptions are caught and
/// returned as TaskResult::error.
template <typename T>
class AsyncTask {
public:
    AsyncTask() = default;
    AsyncTask(const AsyncTask&) = delete;
    AsyncTask& operator=(const AsyncTask&) = delete;
    ~AsyncTask() { join(); }

    bool busy() const { return running_.load(); }

    /// Starts `job` unless one is still running (returns false then). An unread
    /// previous result is discarded.
    bool start(std::function<T()> job) {
        if (busy()) return false;
        join();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result_.reset();
        }
        running_ = true;
        thread_ = std::thread([this, job = std::move(job)]() {
            TaskResult<T> r;
            const auto begin = std::chrono::steady_clock::now();
            try {
                r.value = job();
            } catch (const std::exception& e) {
                r.error = e.what();
            } catch (...) {
                r.error = "unknown error";
            }
            r.elapsed_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                result_ = std::move(r);
                running_ = false;  // published together with the result
            }
        });
        return true;
    }

    /// Returns the finished result once, or nullopt while running / when nothing is pending.
    std::optional<TaskResult<T>> poll() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!result_) return std::nullopt;
        std::optional<TaskResult<T>> out = std::move(result_);
        result_.reset();
        return out;
    }

    /// Blocks until the running job (if any) has finished.
    void join() {
        if (thread_.joinable()) thread_.join();
    }

private:
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::optional<TaskResult<T>> result_;
};

}  // namespace optinspect::gui
