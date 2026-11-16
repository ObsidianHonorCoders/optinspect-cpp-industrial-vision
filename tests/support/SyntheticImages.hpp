#pragma once

#include <opencv2/core.hpp>

namespace optinspect::testing {

/// Generates a deterministic synthetic "PCB-like" reference image: a plain
/// background with a few rectangular "trace" shapes, used as a stand-in for
/// a real PCB photo so tests do not depend on the DeepPCB dataset being
/// downloaded.
cv::Mat makeSyntheticReferenceImage(int width = 200, int height = 200);

/// Returns a copy of `reference` with a small synthetic defect (a filled
/// rectangle of the given size) stamped at the given location, simulating
/// e.g. a copper smear or missing-trace defect for integration testing.
cv::Mat withSyntheticDefect(const cv::Mat& reference, cv::Rect defect_rect);

}  // namespace optinspect::testing
