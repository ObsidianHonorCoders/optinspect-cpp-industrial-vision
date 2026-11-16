#include "processing/ImageComparator.hpp"

#include "common/Errors.hpp"
#include "processing/ImageProcessor.hpp"
#include "processing/MorphologicalProcessor.hpp"
#include "processing/ThresholdProcessor.hpp"

namespace optinspect {

ImageComparator::ImageComparator(PreprocessingConfig preprocessing, ThresholdConfig threshold,
                                  MorphologyConfig morphology)
    : preprocessing_(preprocessing), threshold_(threshold), morphology_(morphology) {}

ComparisonResult ImageComparator::compare(const cv::Mat& reference, const cv::Mat& inspected) const {
    if (reference.empty() || inspected.empty()) {
        throw OptInspectError("ImageComparator::compare requires non-empty images.");
    }
    if (reference.size() != inspected.size()) {
        throw OptInspectError("ImageComparator::compare requires images of identical size "
                               "(expected alignment to have already resolved size mismatches).");
    }

    const cv::Mat ref_gray = ImageProcessor::toGrayscale(reference);
    const cv::Mat insp_gray = ImageProcessor::toGrayscale(inspected);

    const cv::Mat ref_blurred =
        ImageProcessor::gaussianBlur(ref_gray, preprocessing_.gaussian_kernel_size, preprocessing_.gaussian_sigma);
    const cv::Mat insp_blurred =
        ImageProcessor::gaussianBlur(insp_gray, preprocessing_.gaussian_kernel_size, preprocessing_.gaussian_sigma);

    const cv::Mat diff = ImageProcessor::absoluteDifference(ref_blurred, insp_blurred);

    const ThresholdProcessor thresholder(threshold_);
    const cv::Mat mask = thresholder.apply(diff);

    const MorphologicalProcessor morph(morphology_);
    const cv::Mat cleaned_mask = morph.clean(mask);

    ComparisonResult result;
    result.difference_image = diff;
    result.binary_mask = cleaned_mask;
    return result;
}

}  // namespace optinspect
