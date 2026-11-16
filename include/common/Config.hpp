#pragma once

#include <string>

namespace optinspect {

/// Alignment algorithm selection. ECC is a dense, intensity-based method
/// well-suited to PCB images that are already nearly aligned (e.g. DeepPCB
/// image pairs which are captured from the same fixture); ORB is a sparse
/// feature-based method that is more tolerant of larger geometric offsets
/// but can fail on repetitive, low-texture PCB copper patterns.
enum class AlignmentMethod { ECC, ORB, NONE };

/// Thresholding strategy used when building the defect difference mask.
enum class ThresholdMethod { FIXED, OTSU, ADAPTIVE_MEAN, ADAPTIVE_GAUSSIAN };

struct PreprocessingConfig {
    int gaussian_kernel_size = 5;     ///< Must be odd and >= 1.
    double gaussian_sigma = 0.0;      ///< 0 => derived from kernel size by OpenCV.
};

struct AlignmentConfig {
    AlignmentMethod method = AlignmentMethod::ECC;
    int ecc_max_iterations = 100;
    double ecc_epsilon = 1e-6;
    int orb_max_features = 1000;
    double orb_good_match_ratio = 0.75;
    int orb_min_good_matches = 10;
    /// Maximum acceptable mean pixel displacement introduced by the
    /// estimated transform (sanity bound against degenerate homographies).
    double max_translation_px = 200.0;
};

struct ThresholdConfig {
    ThresholdMethod method = ThresholdMethod::OTSU;
    double fixed_threshold = 30.0;
    int adaptive_block_size = 25;     ///< Must be odd and > 1.
    double adaptive_c = 5.0;
};

struct MorphologyConfig {
    int kernel_size = 3;              ///< Must be odd and >= 1.
    int close_iterations = 2;
    int open_iterations = 1;
};

struct DetectionConfig {
    double min_defect_area_px = 20.0;
    double max_defect_area_px = 50000.0;
    double min_confidence = 0.0;
};

struct DecisionConfig {
    /// Any detected defect at or above this severity fails the inspection.
    int fail_defect_count_threshold = 1;
    /// Area (px) above which a single defect is considered severe enough to fail
    /// even if fail_defect_count_threshold has not been reached.
    double fail_area_px = 400.0;
    /// Minimum alignment confidence (e.g. ECC correlation coefficient
    /// normalized to [0,1], or inlier ratio for ORB) required to trust the
    /// comparison; below this, the result is REVIEW instead of PASS/FAIL.
    double min_alignment_confidence = 0.5;
};

/// All inspection parameters loaded from config/inspection_config.json.
/// Grouped by pipeline stage so each module only depends on the sub-struct
/// it actually needs, rather than the entire configuration object.
struct InspectionConfig {
    PreprocessingConfig preprocessing;
    AlignmentConfig alignment;
    ThresholdConfig threshold;
    MorphologyConfig morphology;
    DetectionConfig detection;
    DecisionConfig decision;

    std::string default_output_dir = "results";

    /// Loads and validates configuration from a JSON file.
    /// Throws ConfigurationError on missing file, malformed JSON, or
    /// invalid parameter values (e.g. negative sizes, even kernel sizes).
    static InspectionConfig loadFromFile(const std::string& path);

    /// Validates the in-memory configuration, throwing ConfigurationError
    /// with a descriptive message on the first violation found.
    void validate() const;
};

}  // namespace optinspect
