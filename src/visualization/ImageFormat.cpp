#include "visualization/ImageFormat.hpp"

#include <algorithm>
#include <string>

namespace optinspect::viz {

const char* toString(PixelLayout layout) {
    switch (layout) {
        case PixelLayout::Gray8: return "Gray8 (GL_R8)";
        case PixelLayout::BGR8: return "BGR8 (GL_RGB8)";
        case PixelLayout::BGRA8: return "BGRA8 (GL_RGBA8)";
    }
    return "?";
}

FormatCheck inspectImage(const cv::Mat& image, int max_dimension) {
    FormatCheck check;
    if (image.empty() || image.data == nullptr) {
        check.status = FormatStatus::Empty;
        check.message = "image is empty";
        return check;
    }
    if (image.dims != 2) {
        check.status = FormatStatus::InvalidDimensions;
        check.message = "only 2-D images are supported";
        return check;
    }
    if (image.cols <= 0 || image.rows <= 0) {
        check.status = FormatStatus::InvalidDimensions;
        check.message = "image has non-positive dimensions";
        return check;
    }
    if (image.depth() != CV_8U) {
        check.status = FormatStatus::UnsupportedDepth;
        check.message = "only 8-bit images are supported (OpenCV depth " +
                        std::to_string(image.depth()) + ")";
        return check;
    }
    if (image.cols > max_dimension || image.rows > max_dimension) {
        check.status = FormatStatus::TooLarge;
        check.message = "image " + std::to_string(image.cols) + "x" + std::to_string(image.rows) +
                        " exceeds the maximum texture size " + std::to_string(max_dimension);
        return check;
    }

    PixelLayout layout;
    switch (image.channels()) {
        case 1: layout = PixelLayout::Gray8; break;
        case 3: layout = PixelLayout::BGR8; break;
        case 4: layout = PixelLayout::BGRA8; break;
        default:
            check.status = FormatStatus::UnsupportedChannels;
            check.message = "unsupported channel count " + std::to_string(image.channels());
            return check;
    }

    check.status = FormatStatus::Ok;
    check.format.layout = layout;
    check.format.width = image.cols;
    check.format.height = image.rows;
    check.format.channels = image.channels();
    return check;
}

UploadPlan planUpload(const cv::Mat& image) {
    UploadPlan plan;
    const std::size_t pixel = image.elemSize();
    if (image.isContinuous()) {
        plan.source = image;
        plan.row_length_px = 0;
        return plan;
    }
    if (pixel > 0 && image.step[0] % pixel == 0) {
        plan.source = image;
        plan.row_length_px = static_cast<int>(image.step[0] / pixel);
        return plan;
    }
    plan.source = image.clone();  // clone() of a strided Mat yields a continuous matrix
    plan.row_length_px = 0;
    plan.copied = true;
    return plan;
}

}  // namespace optinspect::viz
