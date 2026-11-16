#include "InspectionPipeline.hpp"

#include "common/Timing.hpp"
#include "detection/DefectDetector.hpp"
#include "detection/InspectionDecider.hpp"
#include "processing/ImageAligner.hpp"

namespace optinspect {

InspectionPipeline::InspectionPipeline(InspectionConfig config) : config_(std::move(config)) {}

PipelineOutput InspectionPipeline::run(const cv::Mat& reference, const cv::Mat& inspected) const {
    PipelineOutput output;

    Stopwatch stage_timer;

    const ImageAligner aligner(config_.alignment);
    stage_timer.reset();
    const AlignmentResult alignment = aligner.align(reference, inspected);
    output.timer.record("alignment", stage_timer.elapsedMilliseconds());
    output.aligned_inspected = alignment.aligned_image;

    std::vector<DefectMeasurement> defects;
    if (alignment.success) {
        DefectDetector detector(config_.preprocessing, config_.threshold, config_.morphology,
                                 config_.detection);
        stage_timer.reset();
        defects = detector.detect(reference, alignment.aligned_image);
        output.timer.record("detection", stage_timer.elapsedMilliseconds());

        output.difference_image = detector.lastComparison().difference_image;
        output.binary_mask = detector.lastComparison().binary_mask;
    }

    const InspectionDecider decider(config_.decision);
    stage_timer.reset();
    output.result = decider.decide(alignment, defects);
    output.timer.record("decision", stage_timer.elapsedMilliseconds());

    return output;
}

}  // namespace optinspect
