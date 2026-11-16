#include <gtest/gtest.h>

#include "InspectionPipeline.hpp"
#include "ReportWriter.hpp"
#include "ResultAnnotator.hpp"
#include "SyntheticImages.hpp"

#include <opencv2/imgcodecs.hpp>

#include <filesystem>

namespace fs = std::filesystem;
using namespace optinspect;
using namespace optinspect::testing;

namespace {

InspectionConfig fastTestConfig() {
    InspectionConfig config;
    config.alignment.method = AlignmentMethod::NONE;  // synthetic images are already aligned
    config.threshold.method = ThresholdMethod::FIXED;
    config.threshold.fixed_threshold = 15.0;
    config.detection.min_defect_area_px = 10.0;
    config.detection.max_defect_area_px = 50000.0;
    config.decision.fail_area_px = 50.0;
    config.decision.fail_defect_count_threshold = 1;
    return config;
}

}  // namespace

TEST(FullInspectionIntegrationTest, IdenticalImagesProduceNoDefectsAndPass) {
    cv::Mat reference = makeSyntheticReferenceImage();
    InspectionPipeline pipeline(fastTestConfig());

    PipelineOutput output = pipeline.run(reference, reference.clone());

    EXPECT_EQ(output.result.decision, InspectionDecision::PASS);
    EXPECT_TRUE(output.result.defects.empty());
}

TEST(FullInspectionIntegrationTest, SyntheticDefectIsDetectedAndFails) {
    cv::Mat reference = makeSyntheticReferenceImage();
    cv::Mat defective = withSyntheticDefect(reference, cv::Rect(60, 60, 25, 25));

    InspectionPipeline pipeline(fastTestConfig());
    PipelineOutput output = pipeline.run(reference, defective);

    EXPECT_EQ(output.result.decision, InspectionDecision::FAIL);
    EXPECT_FALSE(output.result.defects.empty());
}

TEST(FullInspectionIntegrationTest, AlignmentFailureProducesReview) {
    InspectionConfig config = fastTestConfig();
    config.alignment.method = AlignmentMethod::ECC;
    config.alignment.ecc_max_iterations = 1;   // force non-convergence
    config.alignment.ecc_epsilon = 1e-12;
    config.alignment.max_translation_px = 0.0001;  // force rejection even if it converges

    cv::Mat reference = makeSyntheticReferenceImage();
    // Featureless noise image is unlikely to align reliably against the
    // structured synthetic reference within a single ECC iteration.
    cv::Mat noisy(reference.size(), reference.type());
    cv::randu(noisy, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));

    InspectionPipeline pipeline(config);
    PipelineOutput output = pipeline.run(reference, noisy);

    EXPECT_EQ(output.result.decision, InspectionDecision::REVIEW);
}

TEST(FullInspectionIntegrationTest, AnnotatedImageIsGeneratedWithCorrectSize) {
    cv::Mat reference = makeSyntheticReferenceImage();
    cv::Mat defective = withSyntheticDefect(reference, cv::Rect(60, 60, 25, 25));

    InspectionPipeline pipeline(fastTestConfig());
    PipelineOutput output = pipeline.run(reference, defective);

    cv::Mat annotated = ResultAnnotator::annotate(defective, output.result);
    EXPECT_EQ(annotated.size(), defective.size());
    EXPECT_EQ(annotated.channels(), 3);
}

TEST(FullInspectionIntegrationTest, ReportGenerationWritesValidJsonFile) {
    cv::Mat reference = makeSyntheticReferenceImage();
    cv::Mat defective = withSyntheticDefect(reference, cv::Rect(60, 60, 25, 25));

    InspectionPipeline pipeline(fastTestConfig());
    PipelineOutput output = pipeline.run(reference, defective);

    InspectionMetadata metadata;
    metadata.inspection_id = "test-001";
    metadata.reference_path = "reference.png";
    metadata.inspected_path = "defective.png";
    metadata.reference_size = reference.size();
    metadata.inspected_size = defective.size();

    const auto report_path = fs::temp_directory_path() / "optinspect_test_report.json";
    ReportWriter::writeJson(report_path.string(), metadata, output.result, output.timer,
                            fastTestConfig());

    ASSERT_TRUE(fs::exists(report_path));
    EXPECT_GT(fs::file_size(report_path), 0u);

    fs::remove(report_path);
}

TEST(FullInspectionIntegrationTest, OutputImageWritesToDisk) {
    cv::Mat reference = makeSyntheticReferenceImage();
    InspectionPipeline pipeline(fastTestConfig());
    PipelineOutput output = pipeline.run(reference, reference.clone());

    cv::Mat annotated = ResultAnnotator::annotate(reference, output.result);
    const auto image_path = fs::temp_directory_path() / "optinspect_test_annotated.png";
    ASSERT_TRUE(cv::imwrite(image_path.string(), annotated));
    EXPECT_TRUE(fs::exists(image_path));

    fs::remove(image_path);
}
