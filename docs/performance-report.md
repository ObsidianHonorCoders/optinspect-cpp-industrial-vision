# Performance Report

> This page is a hand-curated snapshot of measurements taken during
> development. Every CLI run now generates a report of the same kind
> automatically: `<timestamp>_performance_report.md` in the `--output` folder.

> **Phase 3 (GUI / visualization):** the measurements of the desktop application (startup, texture upload,
> frame times, UI responsiveness, history queries) are in the separate report
> [visualization-performance.md](visualization-performance.md). The Phase 1 and Phase 2 results below are unchanged.

All figures in this document are **real measurements** taken on the
development machine during this session — nothing here is estimated or
fabricated. They characterize the current Phase 1 implementation and
environment only; no claims are made about behavior on other hardware,
OpenCV builds, or image sets.

## Test environment

- OS: Windows (build/test machine used for this project)
- Compiler: MSVC (Visual Studio 2017 toolset), Release configuration
- OpenCV: 4.14.0, built via vcpkg with features `core,highgui,calib3d,png,jpeg`
- Images: real PCB photographs from the public DeepPCB dataset, group
  `00041`, images `00041000` through `00041009` (10 reference/inspected
  pairs; see [dataset.md](dataset.md)). Each image is a single-channel-ish
  (grayscale-content) PCB photo saved as JPEG; see `docs/dataset.md` for
  exact provenance.
- Config: default `config/inspection_config.json` (ECC alignment, OTSU
  thresholding, 3x3 morphology kernel, decision thresholds
  `fail_defect_count_threshold=1`, `fail_area_px=400`,
  `min_alignment_confidence=0.5`).
- Measurement method: the CLI's own built-in stage timing (`common/Timing.hpp`),
  printed in each run's console summary and JSON report
  (`stage_timings_ms`), covering `alignment`, `detection`, and `decision`
  stages plus a `Total time` figure reported by the CLI. Each of the 10
  pairs was run once, sequentially, with no other significant load on the
  machine.

## Results

| Image pair | Alignment confidence | Defects found | Decision | Total time, run 1 (ms) | Total time, batch run (ms) |
|---|---|---|---|---|---|
| 00041000 | 0.986 | 10 | FAIL | 103.9 | 101.7 |
| 00041001 | 0.995 | 39 | FAIL | 145.1 | 137.6 |
| 00041002 | 0.990 | 13 | FAIL | 191.4 | 185.8 |
| 00041003 | 0.985 | 18 | FAIL | 149.3 | 140.6 |
| 00041004 | 0.958 | 27 | FAIL | 1317.7 | 1190.7 |
| 00041005 | 0.976 | 30 | FAIL | 113.2 | 162.2 |
| 00041006 | 0.984 | 63 | FAIL | 83.0 | 91.4 |
| 00041007 | 0.980 | 74 | FAIL | 84.4 | 105.3 |
| 00041008 | 0.976 | 10 | FAIL | 163.3 | 159.2 |
| 00041009 | 0.987 | 71 | FAIL | 105.1 | 104.7 |

"Run 1" is ten separate CLI invocations; "batch run" is a single
`optinspect --reference <dir> --input <dir>` invocation (see
[dataset.md](dataset.md)), whose alignment confidence and defect counts are
shown. Both runs used the default config.

### Regions vs. the FAIL rules (batch run, default config)

Decision rules, in order: REVIEW if alignment confidence < 0.5; FAIL if any
region has area >= 400 px (area rule); FAIL if the region count >= 1 (count
rule); otherwise PASS. Regions "meeting" the rule are those with
area >= 400 px. The remaining regions are still detected and still count toward
the count rule.

| Image pair | Regions | Meet area rule | Below area rule | Decided by |
|---|---|---|---|---|
| 00041000 | 10 | 1 | 9 | area |
| 00041001 | 39 | 1 | 38 | area |
| 00041002 | 13 | 2 | 11 | area |
| 00041003 | 18 | 6 | 12 | area |
| 00041004 | 27 | 6 | 21 | area |
| 00041005 | 30 | 4 | 26 | area |
| 00041006 | 63 | 3 | 60 | area |
| 00041007 | 74 | 3 | 71 | area |
| 00041008 | 10 | 1 | 9 | area |
| 00041009 | 71 | 8 | 63 | area |
| **Total** | **355** | **35** | **320** | 10 of 10 by area rule |

Every pair fails on the area rule, but that says nothing about whether the 35
regions are real defects. The sample annotations list 74 real defects across
these 10 pairs, and how many detected regions correspond to them has not been
measured.

### Stage breakdown (pair `00041000`)

| Stage | Time (ms) |
|---|---|
| alignment | 78.0 |
| detection | 8.6 |
| decision | 0.009 |
| *(total incl. I/O, preprocessing, comparison, annotation, report writing)* | 103.9 |

### Summary statistics (10 pairs)

- **Median total time**: ~124 ms
- **Min / Max**: 83.0 ms / 1317.7 ms
- **Mean (excluding the one outlier)**: ~137 ms across the other 9 pairs
- **Outlier**: pair `00041004` took 1317.7 ms in run 1 and 1190.7 ms in the
  batch run, roughly 10x the others, so it is **reproducible** and not
  one-off OS noise. It also has the lowest alignment confidence of the set
  (0.958), which is consistent with ECC needing more iterations to converge
  on that pair; the generated performance report confirms the time is spent in
  the alignment stage (about 1090 ms vs. 8 ms for detection), but ECC iteration
  counts were not logged, so the root cause is undiagnosed.

## Interpretation

- For this dataset (PCB photos in the low-hundreds-of-KB JPEG range),
  end-to-end single-pair inspection is comfortably **sub-200ms** in the
  typical case, dominated by the alignment stage (ECC iterative
  optimization), with detection and decision stages contributing only a
  few milliseconds each.
- No throughput/concurrency testing was performed — the CLI processes one
  pair per invocation and these figures are single-run wall-clock times,
  not steady-state throughput under load.
- These numbers should **not** be treated as representative of a
  production line's real camera resolution/frame rate requirements; the
  DeepPCB sample images are modestly sized. Only what was actually measured
  is reported here.

## Known limitation observed during testing

When the CLI is invoked many times in separate processes within the same
wall-clock second (e.g. a shell loop) with the same output directory, the
single-pair inspection ID (`%Y%m%d-%H%M%S`) is identical and files overwrite
each other. The built-in batch mode (`--reference <dir> --input <dir>`)
avoids this by appending the pair id to the ID, so it is the recommended way
to inspect many pairs.

---

# Database benchmarks (Phase 2)

These measurements cover the SQLite persistence and analytics layer only. They
are **separate from, and not comparable with,** the Phase 1 image-processing
timings above: the Phase 1 section was measured on real DeepPCB images, whereas
the database benchmark uses synthetic records, and the two were not necessarily
measured on the same machine (the Phase 1 machine's hardware was not recorded).

## Test environment

- Machine: AMD EPYC 9R14 (4 cores), 31 GB RAM, Windows Server 2022 Datacenter
- Compiler: MSVC 19.16 (Visual Studio 2017 toolset), Release configuration
- SQLite: 3.54.1 (vcpkg `sqlite3`, default features), linked as a DLL
- Database: file in the system temp folder on the machine's local disk,
  `journal_mode=WAL`, `synchronous=FULL` (SQLite's default, i.e. every commit is
  flushed to disk), foreign keys on
- Program: `build\bin\Release\optinspect_database_benchmark.exe`
  (`tests/database/benchmark_database.cpp`), run twice in a row

## Dataset and methodology

- **Synthetic, deterministic data:** records come from a fixed-seed linear
  congruential generator, so row counts and values are identical on every
  run. They are invented test data and say nothing about real inspection
  results or real defect statistics.
- **Individual inserts:** 500 inspections with 0 defects and 500 with 20
  defects (each with 4 image references), every one persisted through
  `InspectionPersistenceService::persist` in its own transaction, in a fresh
  database with 5 products. Reported value = total elapsed time / 500.
- **Batch insert:** 10,000 inspections with 5 defects and 4 images each
  (10,000 inspections, 50,000 defects, 40,000 image references, 20 products,
  timestamps spread over 90 days) written through the same service inside one
  outer transaction. Request construction is excluded from the timing.
- **Queries:** run against the 10,000-inspection database after the batch
  insert, each repeated 100 (history) or 20 (analytics) times; the mean is
  reported. All timings are wall-clock (`std::chrono::steady_clock`) and
  include building the C++ result objects, not just SQLite's execution.
- **File size:** size of the database plus WAL after a `wal_checkpoint(TRUNCATE)`.

## Results

| Operation | Dataset / method | Run 1 | Run 2 | Unit |
|---|---|---:|---:|---|
| Insert inspection (0 defects, 4 images) | 500 inserts, one transaction each | 2.653 | 2.629 | ms / inspection |
| Insert inspection (20 defects, 4 images) | 500 inserts, one transaction each | 8.080 | 7.957 | ms / inspection |
| Batch insert (5 defects, 4 images each) | 10,000 in one transaction | 6665.753 | 6575.643 | ms total |
| Batch insert, per inspection | 10,000 in one transaction | 0.667 | 0.658 | ms / inspection |
| History query | product + date range, `LIMIT 50` | 0.111 | 0.119 | ms |
| History query | status = REVIEW, `LIMIT 50` | 0.095 | 0.103 | ms |
| History query | single serial number | 0.043 | 0.042 | ms |
| Analytics: inspection summary | 10,000 inspections | 1.084 | 1.088 | ms |
| Analytics: product quality | 10,000 inspections, 20 products | 3.009 | 2.963 | ms |
| Analytics: defect statistics | 50,000 defects | 29.972 | 29.895 | ms |
| Analytics: daily time series (CTE + window functions) | 10,000 inspections | 4.262 | 4.265 | ms |
| Analytics: processing time by product | 10,000 inspections | 2.836 | 2.755 | ms |
| Database file size | 10,000 inspections / 50,000 defects | 12.691 | 12.691 | MiB |
| Database file size per inspection | same | 1.300 | 1.300 | KiB |

## Interpretation and limits

- Persisting one inspection costs roughly 3-8 ms with a commit (and flush) per
  inspection, versus about 0.67 ms per inspection when 10,000 are committed
  together. The gap suggests the per-commit flush dominates single inserts, but that was not measured separately. Compared with
  the 80-200 ms typical Phase 1 inspection time (see above) this is small, but
  the two figures come from different datasets and machines and were not
  measured together as an end-to-end overhead.
- History lookups and the analytics queries finished in about 0.04-30 ms on
  10,000 inspections. The defect statistics are the slowest because they scan
  all 50,000 defect rows.
- These numbers come from a single machine, two runs, one synthetic data
  distribution (file cache state was not controlled). No concurrency, multi-process contention,
  much larger datasets or slower storage were tested, so nothing here supports
  claims beyond this setup.
- No index was added or removed on the basis of these measurements; the indexes
  documented in [database-design.md](database-design.md) were chosen for the
  query patterns and confirmed with `EXPLAIN QUERY PLAN`. No before/after
  comparison without indexes was measured.