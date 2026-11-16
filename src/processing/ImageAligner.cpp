#include "processing/ImageAligner.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

#include <cmath>

#include "common/Errors.hpp"

namespace optinspect {

namespace {

cv::Mat toGray(const cv::Mat& image) {
    if (image.channels() == 1) return image;
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    return gray;
}

}  // namespace

ImageAligner::ImageAligner(AlignmentConfig config) : config_(config) {}

AlignmentResult ImageAligner::align(const cv::Mat& reference, const cv::Mat& inspected) const {
    if (reference.empty() || inspected.empty()) {
        throw AlignmentError("Cannot align empty image(s).");
    }

    if (config_.method == AlignmentMethod::NONE) {
        AlignmentResult result;
        result.aligned_image = inspected.clone();
        result.success = true;
        result.confidence = 1.0;
        result.message = "Alignment disabled by configuration; images assumed pre-aligned.";
        return result;
    }

    if (reference.size() != inspected.size()) {
        // Resize the inspected image to the reference resolution before
        // attempting registration; a transform cannot be estimated reliably
        // across mismatched resolutions with these methods.
        cv::Mat resized;
        cv::resize(inspected, resized, reference.size());
        AlignmentResult result = (config_.method == AlignmentMethod::ECC)
                                      ? alignEcc(reference, resized)
                                      : alignOrb(reference, resized);
        result.message += " (input resized from " + std::to_string(inspected.cols) + "x" +
                           std::to_string(inspected.rows) + " to match reference)";
        return result;
    }

    return (config_.method == AlignmentMethod::ECC) ? alignEcc(reference, inspected)
                                                      : alignOrb(reference, inspected);
}

AlignmentResult ImageAligner::alignEcc(const cv::Mat& reference, const cv::Mat& inspected) const {
    AlignmentResult result;
    const cv::Mat ref_gray = toGray(reference);
    const cv::Mat insp_gray = toGray(inspected);

    cv::Mat warp_matrix = cv::Mat::eye(2, 3, CV_32F);
    const cv::TermCriteria criteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS,
                                     config_.ecc_max_iterations, config_.ecc_epsilon);

    double correlation = 0.0;
    try {
        correlation = cv::findTransformECC(ref_gray, insp_gray, warp_matrix, cv::MOTION_EUCLIDEAN,
                                            criteria);
    } catch (const cv::Exception& e) {
        result.success = false;
        result.confidence = 0.0;
        result.message = std::string("ECC alignment failed to converge: ") + e.what();
        result.aligned_image = inspected.clone();
        return result;
    }

    // Sanity-check the estimated translation to reject degenerate warps.
    const double dx = warp_matrix.at<float>(0, 2);
    const double dy = warp_matrix.at<float>(1, 2);
    const double translation = std::sqrt(dx * dx + dy * dy);
    if (translation > config_.max_translation_px) {
        result.success = false;
        result.confidence = correlation;
        result.message = "ECC alignment rejected: estimated translation (" +
                          std::to_string(translation) + "px) exceeds configured bound (" +
                          std::to_string(config_.max_translation_px) + "px).";
        result.aligned_image = inspected.clone();
        return result;
    }

    cv::Mat warped;
    cv::warpAffine(inspected, warped, warp_matrix, reference.size(), cv::INTER_LINEAR + cv::WARP_INVERSE_MAP);

    result.success = true;
    result.confidence = std::max(0.0, std::min(1.0, correlation));
    result.aligned_image = warped;
    result.message = "ECC alignment succeeded with correlation coefficient " +
                      std::to_string(correlation) + ".";
    return result;
}

AlignmentResult ImageAligner::alignOrb(const cv::Mat& reference, const cv::Mat& inspected) const {
    AlignmentResult result;
    const cv::Mat ref_gray = toGray(reference);
    const cv::Mat insp_gray = toGray(inspected);

    auto orb = cv::ORB::create(config_.orb_max_features);
    std::vector<cv::KeyPoint> kp_ref, kp_insp;
    cv::Mat desc_ref, desc_insp;
    orb->detectAndCompute(ref_gray, cv::noArray(), kp_ref, desc_ref);
    orb->detectAndCompute(insp_gray, cv::noArray(), kp_insp, desc_insp);

    if (desc_ref.empty() || desc_insp.empty()) {
        result.success = false;
        result.message = "ORB alignment failed: no descriptors could be extracted from one or both images.";
        result.aligned_image = inspected.clone();
        return result;
    }

    cv::BFMatcher matcher(cv::NORM_HAMMING);
    std::vector<std::vector<cv::DMatch>> knn_matches;
    matcher.knnMatch(desc_insp, desc_ref, knn_matches, 2);

    std::vector<cv::DMatch> good_matches;
    for (const auto& pair : knn_matches) {
        if (pair.size() == 2 && pair[0].distance < config_.orb_good_match_ratio * pair[1].distance) {
            good_matches.push_back(pair[0]);
        }
    }

    if (static_cast<int>(good_matches.size()) < config_.orb_min_good_matches) {
        result.success = false;
        result.confidence = 0.0;
        result.message = "ORB alignment failed: only " + std::to_string(good_matches.size()) +
                          " good matches found (minimum " +
                          std::to_string(config_.orb_min_good_matches) + " required).";
        result.aligned_image = inspected.clone();
        return result;
    }

    std::vector<cv::Point2f> pts_insp, pts_ref;
    for (const auto& m : good_matches) {
        pts_insp.push_back(kp_insp[m.queryIdx].pt);
        pts_ref.push_back(kp_ref[m.trainIdx].pt);
    }

    std::vector<uchar> inlier_mask;
    cv::Mat homography = cv::findHomography(pts_insp, pts_ref, cv::RANSAC, 3.0, inlier_mask);
    if (homography.empty()) {
        result.success = false;
        result.message = "ORB alignment failed: homography estimation did not converge (RANSAC).";
        result.aligned_image = inspected.clone();
        return result;
    }

    const int inlier_count = static_cast<int>(cv::countNonZero(inlier_mask));
    const double inlier_ratio =
        static_cast<double>(inlier_count) / static_cast<double>(good_matches.size());

    cv::Mat warped;
    cv::warpPerspective(inspected, warped, homography, reference.size());

    result.success = true;
    result.confidence = inlier_ratio;
    result.aligned_image = warped;
    result.message = "ORB alignment succeeded with " + std::to_string(inlier_count) + "/" +
                      std::to_string(good_matches.size()) + " inliers (ratio " +
                      std::to_string(inlier_ratio) + ").";
    return result;
}

}  // namespace optinspect
