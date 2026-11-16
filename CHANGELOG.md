# Changelog

All notable changes to OptiInspect are documented in this file.

## [0.1.0] — Phase 1: Traditional CV Inspection Engine

Initial implementation of the OptiInspect PCB optical inspection engine.

### Added

- Core engine library (`optinspect_engine`):
  - `common`: configuration loading (JSON via nlohmann::json), exception
    hierarchy, stage timing utilities.
  - `acquisition`: `ImageSource` interface with a `FileImageSource`
    implementation.
  - `processing`: image preprocessing, ECC/ORB-based alignment
    (`ImageAligner`), difference computation (`ImageComparator`),
    configurable thresholding (`ThresholdProcessor`: fixed/Otsu/adaptive
    mean/adaptive Gaussian), and morphological cleanup
    (`MorphologicalProcessor`).
  - `detection`: contour-based candidate region detection
    (`ContourDetector`), geometric defect measurement
    (`DefectMeasurement`), an extensible detector interface
    (`IDefectDetector`/`DefectDetector`), and configurable rule-based
    decision logic (`InspectionDecider`) producing PASS/FAIL/REVIEW.
- Application layer (`optinspect_application`): `InspectionPipeline`
  orchestration, `ResultAnnotator` (annotated image generation),
  `ReportWriter` (JSON report serialization), CLI argument parsing, and the
  `optinspect` executable entry point.
- Default configuration file (`config/inspection_config.json`).
- GoogleTest-based unit tests covering configuration, each processing and
  detection component, and decision logic, plus integration tests
  exercising the full alignment -> detection -> decision -> report
  pipeline end-to-end, including synthetic-image test fixtures
  (`optinspect_test_support`) so the suite has no external dataset
  dependency. **53/53 tests passing** on the development environment
  (VS2017 + OpenCV 4.14.0 via vcpkg).
- Per-run Markdown performance report (`PerformanceReport`) written to the output folder, including the decision rules applied and how many regions meet / do not meet the area rule.
- Batch mode: --reference <dir> --input <dir> inspects every `<id>_temp.*` / `<id>_test.*` pair and prints a summary table (BatchDiscovery).
- Per-run Markdown performance report (`PerformanceReport`) written to the output folder, including the decision rules applied and how many regions meet / do not meet the area rule.
- CMake build system with vcpkg integration for OpenCV and GoogleTest.
- Reserved, documented-empty module stubs for future phases:
  `include/database/`, `src/database/`, `include/communication/`,
  `src/communication/`, `src/visualization/`.
- A curated 10-pair sample from the public DeepPCB dataset under
  `data/samples/deeppcb/`, used for real-world (non-synthetic) manual
  validation of the CLI end-to-end.
- Python helper scripts: `scripts/dataset_preparation.py` (validate/
  summarize DeepPCB-style pairs) and `scripts/generate_report.py`
  (summarize a directory of JSON inspection reports).
- Documentation: `README.md`, `docs/architecture.md`,
  `docs/algorithms.md`, `docs/dataset.md`, `docs/performance-report.md`,
  `docs/database-design.md`, `docs/communication-protocol.md`.
- CI workflow (`.github/workflows/build.yml`) building and testing on a
  Windows GitHub-hosted runner via vcpkg.

### Known limitations

- No semantic defect classification (only geometric/severity
  measurement of candidate regions) — traditional CV cannot reliably
  distinguish the six DeepPCB defect categories without additional rules
  or a trained model; see [docs/algorithms.md](docs/algorithms.md).
- No database, external communication protocol, or visualization tool —
  deferred to future phases.
- Inspection ID generation has second-resolution granularity, so rapid
  (sub-second) successive CLI invocations writing to the same output
  directory can overwrite each other's files; see
  [docs/performance-report.md](docs/performance-report.md).
- Verified only on VS2017 + OpenCV 4.14.0 (vcpkg) on Windows; other
  toolchains/platforms untested.
