#include <gtest/gtest.h>

#include "common/Config.hpp"
#include "common/Errors.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace optinspect;

namespace {

fs::path writeTempConfig(const std::string& contents, const std::string& name) {
    const auto path = fs::temp_directory_path() / name;
    std::ofstream file(path);
    file << contents;
    return path;
}

}  // namespace

TEST(ConfigTest, LoadsValidMinimalConfig) {
    const auto path = writeTempConfig(R"({"preprocessing": {"gaussian_kernel_size": 7}})",
                                       "optinspect_valid_config.json");
    InspectionConfig config = InspectionConfig::loadFromFile(path.string());
    EXPECT_EQ(config.preprocessing.gaussian_kernel_size, 7);
    fs::remove(path);
}

TEST(ConfigTest, UsesDefaultsForMissingFields) {
    const auto path = writeTempConfig(R"({})", "optinspect_empty_config.json");
    InspectionConfig config = InspectionConfig::loadFromFile(path.string());
    EXPECT_EQ(config.alignment.method, AlignmentMethod::ECC);
    EXPECT_EQ(config.threshold.method, ThresholdMethod::OTSU);
    fs::remove(path);
}

TEST(ConfigTest, ThrowsOnMissingFile) {
    EXPECT_THROW(InspectionConfig::loadFromFile("nonexistent_config_file.json"), ConfigurationError);
}

TEST(ConfigTest, ThrowsOnMalformedJson) {
    const auto path = writeTempConfig("{ this is not valid json", "optinspect_malformed_config.json");
    EXPECT_THROW(InspectionConfig::loadFromFile(path.string()), ConfigurationError);
    fs::remove(path);
}

TEST(ConfigTest, ThrowsOnInvalidDetectionAreaRange) {
    const auto path = writeTempConfig(
        R"({"detection": {"min_defect_area_px": 100, "max_defect_area_px": 50}})",
        "optinspect_invalid_area_config.json");
    EXPECT_THROW(InspectionConfig::loadFromFile(path.string()), ConfigurationError);
    fs::remove(path);
}

TEST(ConfigTest, ThrowsOnInvalidAlignmentMethod) {
    const auto path = writeTempConfig(R"({"alignment": {"method": "NOT_A_METHOD"}})",
                                       "optinspect_invalid_alignment_config.json");
    EXPECT_THROW(InspectionConfig::loadFromFile(path.string()), ConfigurationError);
    fs::remove(path);
}
