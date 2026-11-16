# Changelog

All notable changes to OptiInspect are documented in this file.

## [0.2.0] — Phase 2: Database & Quality Analytics

Adds optional SQLite persistence and SQL analytics on top of the unchanged
Phase 1 engine. No detection algorithm or threshold was modified.

### Added

- Database layer (`optinspect_database`, `include/database/`, `src/database/`),
  independent of OpenCV and the engine:
  - RAII wrappers over the SQLite C API: `DatabaseConnection` (foreign keys on,
    busy timeout), `Statement` (prepared statements, named binding),
    `Transaction` (rolls back unless committed; savepoints when nested).
  - `DatabaseInitializer`: idempotent schema creation, `PRAGMA user_version`
    versioning with forward-only migrations, WAL journaling, refusal to touch
    foreign or newer databases.
  - Schema v1: `products`, `configurations`, `inspections`, `defects`,
    `inspection_images` with primary/foreign keys, UNIQUE/NOT NULL/CHECK
    constraints, seven indexes, trigger-maintained defect counts and triggers
    protecting recorded results from modification.
  - Repositories with full CRUD (`ProductRepository`, `InspectionRepository`,
    `DefectRepository`, `InspectionImageRepository`) and
    `InspectionPersistenceService` (atomic persist/delete).
  - `QualityAnalyticsService`: inspection summary, defect statistics, product
    quality, time series (day/week/month, CTE + window functions), processing
    time statistics, all filterable by product, serial number, status and date.
- Application layer: `InspectionRecorder` (maps `InspectionResult` to database
  records, retry-from-report import), `DatabaseCommands` and new CLI commands
  `db`, `product`, `inspection`, `defect`, `analytics`; inspection options
  `--database`, `--no-database`, `--product-id`, `--serial`, `--notes`; optional
  `inspect` keyword; `database` section in `config/inspection_config.json`
  (disabled by default).
- Exit code `74` for database/persistence errors; a failed persist keeps all
  generated files, never changes the printed decision, and prints a retry command.
- `ReportWriter::configurationJson` (the report's configuration block, reused as
  the stored configuration snapshot).
- Tests (158 in total, all passing locally): 75 database unit tests, 21
  persistence/CLI integration tests, 9 configuration/CLI-argument unit tests,
  plus the 53 existing Phase 1 tests, unchanged. All database tests use
  temporary databases.
- `optinspect_database_benchmark` and a database benchmark section in
  `docs/performance-report.md` (real measurements, synthetic data).
- Documentation: Phase 2 section in `README.md`, full `docs/database-design.md`
  (ER diagram, schema, indexes, deletion policy, migrations, transactions,
  analytics), updated `docs/architecture.md`.
- CI workflow: installs SQLite via vcpkg, runs the database tests through CTest
  and a short database CLI smoke test. **Not yet executed on GitHub Actions.**
- `.gitignore` entries for databases and SQLite journal/WAL/SHM files.

### Changed

- `src/application/main.cpp` and `CliArguments` extended for persistence and
  the new command groups; the Phase 1 command line and exit codes 0/1/2/64/70
  behave as before.
- Removed the placeholder `README.md` stubs in `include/database/` and
  `src/database/` now that the module exists.

### Known limitations

- No defect type or severity is stored: Phase 1 does not classify defects, so
  the schema stores measurements and the area-based score only.
- Analytics describe detector output, not detector accuracy; there are no
  true/false-positive metrics (no ground truth).
- SQLite only; no SQL Server. Only schema version 1 exists, so a real upgrade
  migration has not been exercised.
- Importing a report reconstructs the processing time from the stage timings
  stored in the report rather than the wall-clock total.
- Verified only on VS2017 + OpenCV 4.14.0 + SQLite 3.54.1 (vcpkg) on Windows.

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
