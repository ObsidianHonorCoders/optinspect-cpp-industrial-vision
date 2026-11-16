#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace optinspect {

/// One reference/inspected image pair sharing the same identifier.
struct ImagePair {
    std::string id;  ///< Shared prefix, e.g. "00041000".
    std::filesystem::path reference_path;
    std::filesystem::path inspected_path;
};

struct BatchDiscovery {
    std::vector<ImagePair> pairs;  ///< Sorted by id.
    std::vector<std::filesystem::path> unmatched_references;
    std::vector<std::filesystem::path> unmatched_inspected;
};

/// Pairs "<id>_temp.<ext>" files in `reference_dir` with "<id>_test.<ext>"
/// files in `inspected_dir` (DeepPCB naming convention). The two directories
/// may be the same. Only common image extensions are considered; extensions
/// of the two files in a pair need not match. Files without a partner are
/// reported in the unmatched lists rather than silently ignored.
/// Throws std::invalid_argument if either path is not a directory.
BatchDiscovery discoverPairs(const std::filesystem::path& reference_dir,
                             const std::filesystem::path& inspected_dir);

}  // namespace optinspect
