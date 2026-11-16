#include "common/Config.hpp"

#include "common/Errors.hpp"
#include "third_party/nlohmann/json.hpp"

#include <fstream>
#include <sstream>

namespace optinspect {

namespace {

using json = nlohmann::json;

AlignmentMethod parseAlignmentMethod(const std::string& value) {
    if (value == "ECC") return AlignmentMethod::ECC;
    if (value == "ORB") return AlignmentMethod::ORB;
    if (value == "NONE") return AlignmentMethod::NONE;
    throw ConfigurationError("Unknown alignment.method: '" + value + "' (expected ECC, ORB, or NONE).");
}

ThresholdMethod parseThresholdMethod(const std::string& value) {
    if (value == "FIXED") return ThresholdMethod::FIXED;
    if (value == "OTSU") return ThresholdMethod::OTSU;
    if (value == "ADAPTIVE_MEAN") return ThresholdMethod::ADAPTIVE_MEAN;
    if (value == "ADAPTIVE_GAUSSIAN") return ThresholdMethod::ADAPTIVE_GAUSSIAN;
    throw ConfigurationError("Unknown threshold.method: '" + value +
                              "' (expected FIXED, OTSU, ADAPTIVE_MEAN, or ADAPTIVE_GAUSSIAN).");
}

template <typename T>
T getOr(const json& j, const std::string& key, T fallback) {
    if (j.contains(key)) {
        return j.at(key).get<T>();
    }
    return fallback;
}

}  // namespace

InspectionConfig InspectionConfig::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw ConfigurationError("Could not open configuration file: " + path);
    }

    json root;
    try {
        file >> root;
    } catch (const json::parse_error& e) {
        throw ConfigurationError("Malformed JSON in configuration file '" + path + "': " + e.what());
    }

    InspectionConfig config;

    try {
        if (root.contains("preprocessing")) {
            const auto& p = root.at("preprocessing");
            config.preprocessing.gaussian_kernel_size = getOr(p, "gaussian_kernel_size", config.preprocessing.gaussian_kernel_size);
            config.preprocessing.gaussian_sigma = getOr(p, "gaussian_sigma", config.preprocessing.gaussian_sigma);
        }

        if (root.contains("alignment")) {
            const auto& a = root.at("alignment");
            if (a.contains("method")) {
                config.alignment.method = parseAlignmentMethod(a.at("method").get<std::string>());
            }
            config.alignment.ecc_max_iterations = getOr(a, "ecc_max_iterations", config.alignment.ecc_max_iterations);
            config.alignment.ecc_epsilon = getOr(a, "ecc_epsilon", config.alignment.ecc_epsilon);
            config.alignment.orb_max_features = getOr(a, "orb_max_features", config.alignment.orb_max_features);
            config.alignment.orb_good_match_ratio = getOr(a, "orb_good_match_ratio", config.alignment.orb_good_match_ratio);
            config.alignment.orb_min_good_matches = getOr(a, "orb_min_good_matches", config.alignment.orb_min_good_matches);
            config.alignment.max_translation_px = getOr(a, "max_translation_px", config.alignment.max_translation_px);
        }

        if (root.contains("threshold")) {
            const auto& t = root.at("threshold");
            if (t.contains("method")) {
                config.threshold.method = parseThresholdMethod(t.at("method").get<std::string>());
            }
            config.threshold.fixed_threshold = getOr(t, "fixed_threshold", config.threshold.fixed_threshold);
            config.threshold.adaptive_block_size = getOr(t, "adaptive_block_size", config.threshold.adaptive_block_size);
            config.threshold.adaptive_c = getOr(t, "adaptive_c", config.threshold.adaptive_c);
        }

        if (root.contains("morphology")) {
            const auto& m = root.at("morphology");
            config.morphology.kernel_size = getOr(m, "kernel_size", config.morphology.kernel_size);
            config.morphology.close_iterations = getOr(m, "close_iterations", config.morphology.close_iterations);
            config.morphology.open_iterations = getOr(m, "open_iterations", config.morphology.open_iterations);
        }

        if (root.contains("detection")) {
            const auto& d = root.at("detection");
            config.detection.min_defect_area_px = getOr(d, "min_defect_area_px", config.detection.min_defect_area_px);
            config.detection.max_defect_area_px = getOr(d, "max_defect_area_px", config.detection.max_defect_area_px);
            config.detection.min_confidence = getOr(d, "min_confidence", config.detection.min_confidence);
        }

        if (root.contains("decision")) {
            const auto& dec = root.at("decision");
            config.decision.fail_defect_count_threshold = getOr(dec, "fail_defect_count_threshold", config.decision.fail_defect_count_threshold);
            config.decision.fail_area_px = getOr(dec, "fail_area_px", config.decision.fail_area_px);
            config.decision.min_alignment_confidence = getOr(dec, "min_alignment_confidence", config.decision.min_alignment_confidence);
        }

        if (root.contains("database")) {
            const auto& db = root.at("database");
            config.database.enabled = getOr(db, "enabled", config.database.enabled);
            config.database.path = getOr(db, "path", config.database.path);
            config.database.auto_initialize = getOr(db, "auto_initialize", config.database.auto_initialize);
        }

        config.default_output_dir = getOr(root, "default_output_dir", config.default_output_dir);
    } catch (const json::exception& e) {
        throw ConfigurationError("Error reading configuration file '" + path + "': " + e.what());
    }

    config.validate();
    return config;
}

void InspectionConfig::validate() const {
    std::ostringstream err;

    if (preprocessing.gaussian_kernel_size < 1) {
        throw ConfigurationError("preprocessing.gaussian_kernel_size must be >= 1.");
    }
    if (preprocessing.gaussian_sigma < 0.0) {
        throw ConfigurationError("preprocessing.gaussian_sigma must be >= 0.");
    }

    if (alignment.ecc_max_iterations < 1) {
        throw ConfigurationError("alignment.ecc_max_iterations must be >= 1.");
    }
    if (alignment.ecc_epsilon <= 0.0) {
        throw ConfigurationError("alignment.ecc_epsilon must be > 0.");
    }
    if (alignment.orb_max_features < 1) {
        throw ConfigurationError("alignment.orb_max_features must be >= 1.");
    }
    if (alignment.orb_good_match_ratio <= 0.0 || alignment.orb_good_match_ratio >= 1.0) {
        throw ConfigurationError("alignment.orb_good_match_ratio must be in (0, 1).");
    }
    if (alignment.orb_min_good_matches < 4) {
        // Homography estimation requires at least 4 point correspondences.
        throw ConfigurationError("alignment.orb_min_good_matches must be >= 4.");
    }
    if (alignment.max_translation_px <= 0.0) {
        throw ConfigurationError("alignment.max_translation_px must be > 0.");
    }

    if (threshold.fixed_threshold < 0.0 || threshold.fixed_threshold > 255.0) {
        throw ConfigurationError("threshold.fixed_threshold must be in [0, 255].");
    }
    if (threshold.adaptive_block_size < 3) {
        throw ConfigurationError("threshold.adaptive_block_size must be >= 3.");
    }

    if (morphology.kernel_size < 1) {
        throw ConfigurationError("morphology.kernel_size must be >= 1.");
    }
    if (morphology.close_iterations < 0 || morphology.open_iterations < 0) {
        throw ConfigurationError("morphology iteration counts must be >= 0.");
    }

    if (detection.min_defect_area_px < 0.0) {
        throw ConfigurationError("detection.min_defect_area_px must be >= 0.");
    }
    if (detection.max_defect_area_px <= detection.min_defect_area_px) {
        throw ConfigurationError("detection.max_defect_area_px must be > detection.min_defect_area_px.");
    }
    if (detection.min_confidence < 0.0 || detection.min_confidence > 1.0) {
        throw ConfigurationError("detection.min_confidence must be in [0, 1].");
    }

    if (decision.fail_defect_count_threshold < 1) {
        throw ConfigurationError("decision.fail_defect_count_threshold must be >= 1.");
    }
    if (decision.fail_area_px <= 0.0) {
        throw ConfigurationError("decision.fail_area_px must be > 0.");
    }
    if (decision.min_alignment_confidence < 0.0 || decision.min_alignment_confidence > 1.0) {
        throw ConfigurationError("decision.min_alignment_confidence must be in [0, 1].");
    }

    if (database.enabled && database.path.empty()) {
        throw ConfigurationError("database.path must not be empty when database.enabled is true.");
    }
}

}  // namespace optinspect
