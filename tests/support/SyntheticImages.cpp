#include "SyntheticImages.hpp"

#include <opencv2/imgproc.hpp>

namespace optinspect::testing {

cv::Mat makeSyntheticReferenceImage(int width, int height) {
    cv::Mat image(height, width, CV_8UC3, cv::Scalar(30, 30, 30));  // dark PCB substrate

    // Draw a few deterministic "copper trace" rectangles to give the image
    // texture and structure, similar in spirit to PCB trace patterns.
    cv::rectangle(image, cv::Rect(20, 20, 160, 10), cv::Scalar(40, 140, 200), cv::FILLED);
    cv::rectangle(image, cv::Rect(20, 50, 10, 120), cv::Scalar(40, 140, 200), cv::FILLED);
    cv::rectangle(image, cv::Rect(170, 50, 10, 120), cv::Scalar(40, 140, 200), cv::FILLED);
    cv::rectangle(image, cv::Rect(20, 170, 160, 10), cv::Scalar(40, 140, 200), cv::FILLED);
    cv::circle(image, cv::Point(width / 2, height / 2), 15, cv::Scalar(200, 200, 200), cv::FILLED);

    return image;
}

cv::Mat withSyntheticDefect(const cv::Mat& reference, cv::Rect defect_rect) {
    cv::Mat defective = reference.clone();
    cv::rectangle(defective, defect_rect, cv::Scalar(0, 0, 0), cv::FILLED);
    return defective;
}

}  // namespace optinspect::testing
