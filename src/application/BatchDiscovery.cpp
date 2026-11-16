#include "BatchDiscovery.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>

namespace optinspect {

namespace fs = std::filesystem;

namespace {

const std::string kReferenceSuffix = "_temp";
const std::string kInspectedSuffix = "_test";

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool isImageFile(const fs::path& p) {
    const std::string ext = lower(p.extension().string());
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp" ||
           ext == ".tif" || ext == ".tiff";
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() > suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// Maps id -> path for files in `dir` whose stem ends with `suffix`.
std::map<std::string, fs::path> collect(const fs::path& dir, const std::string& suffix) {
    std::map<std::string, fs::path> found;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file() || !isImageFile(entry.path())) {
            continue;
        }
        const std::string stem = entry.path().stem().string();
        if (endsWith(stem, suffix)) {
            found.emplace(stem.substr(0, stem.size() - suffix.size()), entry.path());
        }
    }
    return found;
}

}  // namespace

BatchDiscovery discoverPairs(const fs::path& reference_dir, const fs::path& inspected_dir) {
    if (!fs::is_directory(reference_dir)) {
        throw std::invalid_argument("Not a directory: " + reference_dir.string());
    }
    if (!fs::is_directory(inspected_dir)) {
        throw std::invalid_argument("Not a directory: " + inspected_dir.string());
    }

    const auto references = collect(reference_dir, kReferenceSuffix);
    const auto inspected = collect(inspected_dir, kInspectedSuffix);

    BatchDiscovery result;
    for (const auto& [id, ref_path] : references) {
        const auto it = inspected.find(id);
        if (it == inspected.end()) {
            result.unmatched_references.push_back(ref_path);
        } else {
            result.pairs.push_back({id, ref_path, it->second});
        }
    }
    for (const auto& [id, insp_path] : inspected) {
        if (references.find(id) == references.end()) {
            result.unmatched_inspected.push_back(insp_path);
        }
    }
    return result;
}

}  // namespace optinspect
