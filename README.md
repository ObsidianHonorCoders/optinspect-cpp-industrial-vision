# OptiInspect

**OptiInspect** is a C++17 industrial optical inspection engine for printed
circuit boards (PCBs), built on traditional (non-machine-learning) OpenCV
computer vision techniques. Given a defect-free reference image and an
inspected image of the same board, it aligns the two, computes a pixel
difference, segments candidate defect regions, measures their geometry, and
produces a PASS / FAIL / REVIEW decision with a structured JSON report and
annotated output images.

This is the **Phase 1** implementation: a standalone command-line tool with
no machine-learning defect classification, no database integration, and no
external communication protocol — those are explicitly reserved for future
phases (see [Scope](#scope-and-known-limitations) below).

## Features

- **Image alignment**: dense (ECC, intensity-based) or sparse (ORB
  feature-based + homography) registration of the inspected image to the
  reference, with explicit failure/low-confidence detection rather than
  silently proceeding on bad alignment.
- **Difference-based defect candidate detection**: grayscale conversion,
  Gaussian blur, absolute difference, configurable thresholding (fixed /
  Otsu / adaptive mean / adaptive Gaussian), and morphological cleanup
  (open/close) to segment candidate defect regions via contour analysis.
- **Geometric defect measurement**: bounding box, area, centroid, perimeter,
  aspect ratio, and a rule-based (non-semantic) confidence score per region.
- **Configurable decision logic**: PASS / FAIL / REVIEW based on defect
  count, defect area, and alignment confidence thresholds, fully separated
  from image-processing code.
- **Structured JSON reporting** and **annotated image output** for every
  inspection run.
- **Extensible detector interface** (`IDefectDetector`) so a future
  ML-based detector could be substituted without changing the pipeline.

## Repository layout

```
include/            Public headers, grouped by module (acquisition,
                     processing, detection, common, database*, communication*)
src/                 Implementation, plus the application layer (CLI,
                     pipeline orchestration, reporting, annotation)
tests/               GoogleTest unit and integration tests + synthetic
                     image test support
config/              Default JSON configuration files
data/                Sample images (small curated DeepPCB subset) and
                     reference-image storage location
scripts/             Python helper scripts (dataset prep, report summary)
docs/                Architecture, algorithm, dataset, and performance docs
vcpkg/               vcpkg (cloned locally; not committed) used to obtain
                     OpenCV and GoogleTest
.github/workflows/   CI configuration

* database/ and communication/ are reserved, empty-of-logic modules for
  future phases; see their README.md files for details.
```

## Building

### Prerequisites

- CMake >= 3.15
- A C++17 compiler (developed against MSVC from Visual Studio 2017; GCC/Clang
  should also work but are untested in this environment)
- [vcpkg](https://github.com/microsoft/vcpkg) to obtain OpenCV and GoogleTest

### 1. Set up vcpkg and dependencies

```powershell
git clone https://github.com/microsoft/vcpkg
.\vcpkg\bootstrap-vcpkg.bat

# OpenCV's default vcpkg features pull in dnn/gapi -> abseil, which requires
# a newer compiler toolset than VS2017 and is irrelevant for this
# traditional-CV-only project, so install a reduced feature set instead:
.\vcpkg\vcpkg.exe install "opencv4[core,highgui,calib3d,png,jpeg]:x64-windows"
.\vcpkg\vcpkg.exe install "gtest:x64-windows"
```

(If you vendor vcpkg inside this repository at `vcpkg/`, as was done during
development, adjust the paths above accordingly — `vcpkg/` is gitignored.)

### 2. Configure and build

```powershell
cmake -S . -B build `
    -G "Visual Studio 15 2017" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake `
    -DVCPKG_TARGET_TRIPLET=x64-windows

cmake --build build --config Release
```

### 3. Run the tests

```powershell
cd build
ctest -C Release --output-on-failure
```

Run `ctest` from inside `build/`. This works with every CMake version. The
`ctest --test-dir build ...` form needs **CMake >= 3.20**; older `ctest`
silently ignores `--test-dir` and reports "No tests were found!!!" because it
looks for tests in the current directory instead of `build/`. Check with
`ctest --version`. `-C Release` is required with the Visual Studio generator
because it is multi-config (the tests are registered per configuration).

To run a subset: `ctest -C Release -R BatchDiscovery` (name filter), or run
`build\bin\Release\optinspect_unit_tests.exe` directly.

As of this writing, all 53 unit and integration tests pass on this
environment (VS2017 / OpenCV 4.14.0 built via vcpkg with
`core,highgui,calib3d,png,jpeg` features).

## Usage

```powershell
.\build\bin\Release\optinspect.exe `
    --reference <path-to-golden-image> `
    --input <path-to-inspected-image> `
    --config config\inspection_config.json `
    --output results\
```

Exit codes: `0` = PASS, `1` = FAIL, `2` = REVIEW (alignment or confidence too
low to trust the result), `64`/`70` = usage/runtime errors.

### Decision rules (PASS / FAIL / REVIEW)

The decision is made by `InspectionDecider` from the `decision` section of the
config. Rules are checked **in this order; the first one that applies
decides**:

| # | Rule | Default | Result |
|---|---|---|---|
| 1 | Alignment failed, or alignment confidence `< min_alignment_confidence` | `0.5` | **REVIEW** |
| 2 | **Area rule:** any detected region has `area_px >= fail_area_px` | `400` px | **FAIL** |
| 3 | **Count rule:** number of detected regions `>= fail_defect_count_threshold` | `1` | **FAIL** |
| 4 | None of the above | | **PASS** |

`area_px` is the region's contour area, the `A=<number>` printed next to each
box in the annotated image. Only regions between `detection.min_defect_area_px`
(20) and `detection.max_defect_area_px` (50000) are counted at all. With the
defaults, the count rule makes *any* surviving region a FAIL; the area rule only
determines which reason is reported when a region is large. Raise
`fail_defect_count_threshold` to tolerate small regions, in which case only
regions meeting the area rule (or enough small ones) fail the board. See
[docs/algorithms.md](docs/algorithms.md#7-decision-logic-inspectiondecider).

Each run produces, in the output directory:
- `<id>_report.json` — structured inspection report (see
  [docs/algorithms.md](docs/algorithms.md) for schema details)
- `<id>_annotated.png` — inspected image with defect bounding boxes drawn
- `<id>_difference.png` — the aligned difference image used for detection
- `<id>_performance_report.md` — Markdown report: decision rules applied,
  per-stage timings, and how many detected regions meet / do not meet the area
  rule (batch runs add per-pair rows and min/median/mean/max)

### Batch mode (folders)

Pass directories to `--reference` and `--input` to inspect every
`<id>_temp.*` / `<id>_test.*` pair (DeepPCB naming) in one run:

```powershell
.\build\bin\Release\optinspect.exe `
    --reference data\samples\deeppcb\images --input data\samples\deeppcb\images `
    --config config\inspection_config.json --output results\batch
```

A summary table is printed; exit code is `70` if any pair errored, else `1` if
any FAILed, else `2` if any needs REVIEW, else `0`. Details in
[docs/dataset.md](docs/dataset.md).

### Trying it against real PCB images

A small curated sample of the public DeepPCB dataset is included under
`data/samples/deeppcb/` — see [docs/dataset.md](docs/dataset.md) for
provenance, license notes, and example commands.

```powershell
.\build\bin\Release\optinspect.exe `
    --reference data\samples\deeppcb\images\00041000_temp.jpg `
    --input data\samples\deeppcb\images\00041000_test.jpg `
    --config config\inspection_config.json --output results\
```

### Helper scripts

- `scripts/dataset_preparation.py` — validates and summarizes DeepPCB-style
  image/annotation pairs; optionally copies a pair into the conventional
  `data/references/` / `data/samples/` locations.
- `scripts/generate_report.py` — scans a directory of `*_report.json` files
  (e.g. after a batch of inspection runs) and produces a console table, CSV,
  and/or Markdown summary.

## Documentation

- [docs/architecture.md](docs/architecture.md) — module responsibilities and
  data flow through the pipeline.
- [docs/algorithms.md](docs/algorithms.md) — detailed description of each
  processing/detection algorithm and its configuration parameters.
- [docs/dataset.md](docs/dataset.md) — provenance and usage of the bundled
  DeepPCB sample.
- [docs/performance-report.md](docs/performance-report.md) — measured
  timings on this development environment.
- [docs/database-design.md](docs/database-design.md) and
  [docs/communication-protocol.md](docs/communication-protocol.md) —
  forward-looking design notes for reserved future-phase modules.
- [CHANGELOG.md](CHANGELOG.md) — Phase 1 release notes.

## Scope and known limitations

- **No machine learning**: all detection is traditional CV (difference +
  threshold + morphology + contour analysis). The six DeepPCB defect
  categories (open, short, mousebite, spur, copper, pin-hole) are **not**
  classified — only generic "candidate defect region" geometry is reported.
  `IDefectDetector` exists specifically so an ML-based detector could later
  be substituted without pipeline changes.
- **No database integration** (`database/` module is a reserved stub).
- **No external communication protocol** (`communication/` module is a
  reserved stub); results are communicated only via JSON files, images, and
  process exit codes.
- **No GUI/visualization tool** beyond static annotated-image output
  (`visualization/` module is a reserved stub).
- Built and tested only against **VS2017 + OpenCV 4.14.0 (vcpkg)** on
  Windows in this environment; other toolchains/platforms are untested.

## License

MIT — see [LICENSE](LICENSE). The bundled DeepPCB sample images in
`data/samples/deeppcb/` are subject to their own, more restrictive license
(research use only) — see [docs/dataset.md](docs/dataset.md).
