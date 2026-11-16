#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

namespace optinspect::testing {

/// A unique database path inside the system temp directory. The database
/// file and any SQLite side files (-wal, -shm, -journal) are removed on
/// destruction, so tests never touch a real or shared database.
class TempDatabaseFile {
public:
    explicit TempDatabaseFile(const std::string& tag = "db") {
        static std::atomic<unsigned> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = (std::filesystem::temp_directory_path() /
                 ("optinspect_test_" + tag + "_" + std::to_string(stamp) + "_" +
                  std::to_string(counter++) + ".db"))
                    .string();
    }

    ~TempDatabaseFile() {
        std::error_code ec;
        for (const char* suffix : {"", "-wal", "-shm", "-journal"}) {
            std::filesystem::remove(path_ + suffix, ec);
        }
    }

    TempDatabaseFile(const TempDatabaseFile&) = delete;
    TempDatabaseFile& operator=(const TempDatabaseFile&) = delete;

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

/// A fresh temporary directory removed (recursively) on destruction.
class TempDirectory {
public:
    explicit TempDirectory(const std::string& tag = "dir") {
        static std::atomic<unsigned> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("optinspect_test_" + tag + "_" + std::to_string(stamp) + "_" +
                 std::to_string(counter++));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace optinspect::testing
