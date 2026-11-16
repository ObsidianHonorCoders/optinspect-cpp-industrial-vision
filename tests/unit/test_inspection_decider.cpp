#include <gtest/gtest.h>

#include "detection/InspectionDecider.hpp"
#include "processing/ImageAligner.hpp"

using namespace optinspect;

namespace {

DecisionConfig defaultConfig() {
    DecisionConfig config;
    config.fail_defect_count_threshold = 1;
    config.fail_area_px = 400.0;
    config.min_alignment_confidence = 0.5;
    return config;
}

AlignmentResult successfulAlignment(double confidence = 0.9) {
    AlignmentResult result;
    result.success = true;
    result.confidence = confidence;
    result.message = "ok";
    return result;
}

}  // namespace

TEST(InspectionDeciderTest, PassesWhenNoDefectsAndAlignmentGood) {
    InspectionDecider decider(defaultConfig());
    InspectionResult result = decider.decide(successfulAlignment(), {});
    EXPECT_EQ(result.decision, InspectionDecision::PASS);
}

TEST(InspectionDeciderTest, FailsWhenDefectCountMeetsThreshold) {
    InspectionDecider decider(defaultConfig());
    DefectMeasurement defect;
    defect.area_px = 50.0;  // below fail_area_px but count threshold is 1
    InspectionResult result = decider.decide(successfulAlignment(), {defect});
    EXPECT_EQ(result.decision, InspectionDecision::FAIL);
}

TEST(InspectionDeciderTest, FailsWhenDefectAreaExceedsThreshold) {
    InspectionDecider decider(defaultConfig());
    DefectMeasurement defect;
    defect.area_px = 1000.0;
    InspectionResult result = decider.decide(successfulAlignment(), {defect});
    EXPECT_EQ(result.decision, InspectionDecision::FAIL);
}

TEST(InspectionDeciderTest, ReviewsWhenAlignmentFailed) {
    InspectionDecider decider(defaultConfig());
    AlignmentResult alignment;
    alignment.success = false;
    alignment.message = "diverged";
    InspectionResult result = decider.decide(alignment, {});
    EXPECT_EQ(result.decision, InspectionDecision::REVIEW);
}

TEST(InspectionDeciderTest, ReviewsWhenAlignmentConfidenceBelowMinimum) {
    InspectionDecider decider(defaultConfig());
    InspectionResult result = decider.decide(successfulAlignment(0.2), {});
    EXPECT_EQ(result.decision, InspectionDecision::REVIEW);
}
