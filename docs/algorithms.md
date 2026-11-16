# Algorithms

This document describes the image-processing and decision algorithms
implemented in Phase 1, and the configuration parameters that control them
(all under `config/inspection_config.json`, mapped to `common/Config.hpp`
structs).

## 1. Preprocessing

Before alignment and comparison, images are converted to grayscale and
smoothed with a Gaussian blur (`preprocessing.gaussian_kernel_size`,
`preprocessing.gaussian_sigma`) to reduce sensor/compression noise that
would otherwise register as false-positive difference regions. The kernel
size must be odd; `gaussian_sigma = 0.0` lets OpenCV derive sigma from the
kernel size automatically.

## 2. Alignment (`ImageAligner`)

Two reference images and inspected images of the same physical board are
rarely pixel-perfectly registered (camera jitter, fixture tolerance, etc.),
so alignment is a prerequisite for a meaningful pixel difference. Two
methods are supported, selected via `alignment.method`:

### ECC (default: `"ECC"`)

Dense, intensity-based registration using OpenCV's
`cv::findTransformECC` with a Euclidean (translation + rotation) motion
model. It directly optimizes the Enhanced Correlation Coefficient between
reference and warped inspected image, which is robust to small, mostly
linear illumination differences — a good fit for PCB images captured under
fairly controlled lighting.

- `alignment.ecc_max_iterations` / `alignment.ecc_epsilon`: convergence
  criteria for the iterative optimizer.
- The resulting correlation coefficient (already in `[0,1]` for well-posed
  problems) is used directly as `AlignmentResult::confidence`.
- If ECC fails to converge (`cv::findTransformECC` throws), alignment is
  reported as `success = false` with `confidence = 0.0` rather than
  silently falling back to an identity transform.

### ORB (feature-based)

Sparse registration: ORB keypoints/descriptors are detected in both images
(`alignment.orb_max_features` caps the number of keypoints), matched with a
brute-force Hamming matcher, filtered with Lowe's ratio test
(`alignment.orb_good_match_ratio`), and a homography is estimated from the
surviving matches via `cv::findHomography` with RANSAC. 

- `alignment.orb_min_good_matches`: minimum number of ratio-test survivors
  required even to attempt a homography fit; below this, alignment fails
  explicitly.
- `AlignmentResult::confidence` is the fraction of matches RANSAC classifies
  as inliers — a direct, interpretable measure of how well the estimated
  transform explains the observed correspondences.

### Sanity bound

Regardless of method, `alignment.max_translation_px` bounds how large a
translation component is accepted as plausible; a result implying a larger
shift than this is treated as a failed/implausible alignment (guards
against pathological fits on near-featureless boards).

## 3. Differencing (`ImageComparator`)

Once aligned, both images are (re-)converted to grayscale/blurred using the
same preprocessing parameters, and an absolute difference image is computed
(`cv::absdiff`). This difference image is what gets thresholded, and is
also saved directly as `<id>_difference.png` in the output directory for
visual inspection.

## 4. Thresholding (`ThresholdProcessor`)

Converts the grayscale difference image into a binary (0/255) candidate
mask. Method selected via `threshold.method`:

| Method | Behavior |
|---|---|
| `FIXED` | `cv::threshold` with a single global cutoff, `threshold.fixed_threshold`. Simple and fast, but sensitive to overall difference-image brightness. |
| `OTSU` (default) | `cv::threshold` with `THRESH_OTSU`, automatically choosing a global cutoff that best separates the difference image's bimodal (background vs. defect) histogram. Adapts per-image without manual tuning, which is why it is the default. |
| `ADAPTIVE_MEAN` | `cv::adaptiveThreshold` with `ADAPTIVE_THRESH_MEAN_C`: local mean-based threshold over `threshold.adaptive_block_size`-sized neighborhoods, offset by `threshold.adaptive_c`. More robust to uneven illumination/vignetting across a large board. |
| `ADAPTIVE_GAUSSIAN` | As above but with a Gaussian-weighted local mean (`ADAPTIVE_THRESH_GAUSSIAN_C`), generally smoother than the plain mean variant. |

## 5. Morphological cleanup (`MorphologicalProcessor`)

Thresholded masks from real images typically contain small speckle noise
and fragmented defect regions. A square structuring element of
`morphology.kernel_size` is used to apply, in order:

1. **Opening** (`morphology.open_iterations`, erosion then dilation) —
   removes small, isolated noise specks.
2. **Closing** (`morphology.close_iterations`, dilation then erosion) —
   fills small gaps/holes within a defect region so it is detected as one
   connected contour rather than several fragments.

## 6. Contour detection and measurement

`ContourDetector` runs `cv::findContours` (external contours only) on the
cleaned mask. Each contour is passed to `computeMeasurement`
(`DefectMeasurement`), which derives:

- `bounding_box`, `width`, `height` — via `cv::boundingRect` (note: this is
  inclusive of the contour's maximum coordinate, so a contour spanning
  x∈[10,30] yields `width = 21`, not 20 — relevant when writing tolerance
  checks against known synthetic shapes).
- `area_px` — via `cv::contourArea` (the shoelace-formula polygon area,
  which slightly *underestimates* the pixel count of a small filled
  rectangle compared to naive counting — a few percent for small regions).
- `centroid`, `perimeter_px` — via image moments and `cv::arcLength`.
- `aspect_ratio` — `width / max(height, 1)`.
- `confidence` — a **rule-based**, area-driven severity score in `[0,1]`
  (larger/more solid regions score higher); this is *not* a semantic
  defect-type classification. See the note in `DefectMeasurement.hpp`:
  traditional contour analysis cannot reliably distinguish between the six
  DeepPCB defect categories (open, short, mousebite, spur, copper,
  pin-hole) without additional domain rules or a trained model, so Phase 1
  makes no attempt to do so — `IDefectDetector` exists so a future
  ML-based implementation could be substituted without touching the rest
  of the pipeline.

`DefectDetector` filters raw contours by `detection.min_defect_area_px` /
`detection.max_defect_area_px` (discarding noise-scale and
implausibly-huge regions) and `detection.min_confidence`, producing the
final `DetectionResult`.

## 7. Decision logic (`InspectionDecider`)

Pure, OpenCV-independent rule evaluation over `AlignmentResult` +
the accepted defect list:

1. If `alignment.success == false` or
   `alignment.confidence < decision.min_alignment_confidence`, the result is
   `REVIEW` — the comparison is not trustworthy enough to assert PASS or
   FAIL.
2. **Area rule:** otherwise, if any single accepted region has
   `area_px >= decision.fail_area_px`, the result is `FAIL`.
3. **Count rule:** otherwise, if the number of accepted regions is
   `>= decision.fail_defect_count_threshold`, the result is `FAIL`.
4. Otherwise, `PASS`.

The rules are checked in this order and the first match decides, so the
reason in the report names only one rule. With the default config
(`fail_area_px = 400`, `fail_defect_count_threshold = 1`) the count rule
means *any* accepted region fails the board; the area rule only decides
which reason is reported for large regions.

This separation means decision thresholds (e.g. "fail on any defect over
400px²") can be tuned per product/line without touching alignment,
thresholding, or morphology code — and can be unit-tested purely against
synthetic `AlignmentResult`/`DefectMeasurement` values (see
`tests/unit/test_inspection_decider.cpp`).

## Known algorithmic limitations

- No semantic defect classification (see above) — only geometric/severity
  description of candidate regions.
- Alignment assumes a (near-)rigid transform between reference and
  inspected image; it is not designed to correct for large perspective
  distortion, lens distortion, or significant scale differences.
- Thresholding and morphology parameters are global per-run; there is no
  automatic per-region or per-ROI parameter adaptation within a single
  image.
- Difference-based detection is inherently sensitive to residual
  misalignment; this is why alignment confidence directly gates PASS/FAIL
  vs. REVIEW rather than being treated as an independent, ignorable
  diagnostic.
