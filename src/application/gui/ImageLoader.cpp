#include "ImageLoader.hpp"

#include "common/Timing.hpp"

#include <opencv2/imgcodecs.hpp>

#include <filesystem>

namespace optinspect::gui {

namespace fs = std::filesystem;

std::string makeImageCacheKey(const std::string& path) {
    std::error_code ec;
    if (path.empty() || !fs::is_regular_file(path, ec) || ec) return {};
    const fs::path absolute = fs::absolute(path, ec).lexically_normal();
    const auto mtime = fs::last_write_time(path, ec);
    const auto size = fs::file_size(path, ec);
    return absolute.string() + "|" + std::to_string(mtime.time_since_epoch().count()) + "|" +
           std::to_string(static_cast<unsigned long long>(size));
}

ImageSlot loadImageSlot(const std::string& path, const std::unordered_set<std::string>& cached_keys) {
    ImageSlot slot;
    slot.path = path;
    if (path.empty()) {
        slot.error = "No file path recorded for this image.";
        return slot;
    }
    slot.cache_key = makeImageCacheKey(path);
    if (slot.cache_key.empty()) {
        slot.error = "File not found: " + path;
        return slot;
    }
    if (cached_keys.count(slot.cache_key) != 0) {
        slot.already_cached = true;
        return slot;
    }

    Stopwatch timer;
    try {
        cv::Mat image = cv::imread(path, cv::IMREAD_UNCHANGED);
        if (image.empty()) {
            slot.cache_key.clear();
            slot.error = "Could not decode image (unsupported or corrupted file): " + path;
            return slot;
        }
        if (image.depth() == CV_16U) {
            cv::Mat reduced;
            image.convertTo(reduced, CV_8U, 1.0 / 257.0);
            image = reduced;
        } else if (image.depth() != CV_8U) {
            slot.cache_key.clear();
            slot.error = "Unsupported pixel depth in image: " + path;
            return slot;
        }
        slot.pixels = std::make_shared<const cv::Mat>(std::move(image));
    } catch (const std::exception& e) {
        slot.cache_key.clear();
        slot.error = std::string("Image loading failed: ") + e.what();
    }
    slot.load_ms = timer.elapsedMilliseconds();
    return slot;
}

}  // namespace optinspect::gui
