# Dataset Notes

## DeepPCB sample

`data/samples/deeppcb/` contains a small, curated subset (10 image pairs +
annotations, ~630 KB total) of the public **DeepPCB** dataset, used to
validate OptiInspect against real PCB photographs rather than only the
synthetic images used in the automated test suite.

- **Source**: [tangsanli5201/DeepPCB](https://github.com/tangsanli5201/DeepPCB)
  (`PCBData/group00041/00041/` and `PCBData/group00041/00041_not/`).
- **Contents copied**: images `00041000` through `00041009` — each is a
  `<id>_temp.jpg` (defect-free template/reference) and `<id>_test.jpg`
  (inspected image with real and synthetically-augmented defects), plus the
  matching `<id>.txt` annotation file.
- **License**: per the upstream repository, this dataset "can only be used
  for research purposes." It is included here solely to support manual
  validation and documentation of OptiInspect during development; it is
  **not** used for training any model (OptiInspect Phase 1 is a traditional,
  non-ML computer vision pipeline).
- **Full dataset**: the complete DeepPCB dataset (1,500 image pairs) is
  **not** vendored into this repository due to its size. If you want to
  validate against more samples, clone the upstream repository and point
  `scripts/dataset_preparation.py --source <path>` at the `PCBData/`
  directory.

## Annotation format

Each `<id>.txt` file contains one defect per line:

```
x1,y1,x2,y2,type
```

where `(x1,y1)`/`(x2,y2)` are the top-left/bottom-right corners of the
defect's bounding box (in pixels, 640x640 image space) and `type` is an
integer 1-6 mapping to:

| type | defect      |
|------|-------------|
| 1    | open        |
| 2    | short       |
| 3    | mousebite   |
| 4    | spur        |
| 5    | copper      |
| 6    | pin-hole    |

OptiInspect Phase 1 does **not** parse or use these annotation files at
runtime — it only consumes the reference/inspected image pair. The
annotation files are retained here purely as ground-truth reference for
anyone manually comparing OptiInspect's detected regions against the known
real defects, and are read by `scripts/dataset_preparation.py` for
reporting/validation purposes only.

## Using the sample with the CLI

### Single pair

```powershell
.\build\bin\Release\optinspect.exe `
    --reference data\samples\deeppcb\images\00041000_temp.jpg `
    --input data\samples\deeppcb\images\00041000_test.jpg `
    --config config\inspection_config.json `
    --output results\
```

### Batch: all pairs in a folder

If `--reference` and `--input` are both **directories**, OptiInspect pairs
every `<id>_temp.<ext>` in the reference folder with the `<id>_test.<ext>`
that has the same `<id>` in the input folder, and inspects each pair. In
DeepPCB both file kinds live in the same folder, so pass it twice:

```powershell
.\build\bin\Release\optinspect.exe `
    --reference data\samples\deeppcb\images `
    --input data\samples\deeppcb\images `
    --config config\inspection_config.json `
    --output results\batch
```

If the references and inspected images are in separate folders, pass each
one (file names still need to be `<id>_temp.*` / `<id>_test.*`):

```powershell
.\build\bin\Release\optinspect.exe --reference data\references --input data\inspected --output results\batch
```

Behaviour:

- Output files for each pair are named `<timestamp>-<id>_annotated.png`,
  `..._difference.png` and `..._report.json`, so pairs never overwrite each
  other.
- A summary table (decision, defect count, alignment confidence, time, report
  path) and PASS/FAIL/REVIEW/ERROR totals are printed at the end.
- A Markdown performance report, `<timestamp>_performance_report.md`, is also
  written to the output folder: the decision rules applied, per-pair and
  per-stage timings, min/median/mean/max, how many detected regions meet and
  do not meet the area rule, and the configuration used. Single-pair runs
  write `<id>_performance_report.md`.
- Files without a partner (a `_temp` with no `_test`, or vice versa) are
  reported as warnings on stderr and skipped. Only `.jpg/.jpeg/.png/.bmp/.tif/.tiff`
  are considered; the two files of a pair may have different extensions.
- A pair that fails to load or process is reported as `ERROR` and the batch
  continues with the remaining pairs.
- Exit code: `70` if any pair errored (or no pairs were found), otherwise `1`
  if any pair FAILed, otherwise `2` if any pair needs REVIEW, otherwise `0`.
- Mixing a file and a directory is a usage error (exit code `64`).

Use `scripts\generate_report.py` on the output folder to export the batch
results to CSV/Markdown.

### How a pair is judged PASS / FAIL / REVIEW

Rules are checked in this order; the first that applies decides (defaults from
`config/inspection_config.json`):

1. **REVIEW** if alignment failed or its confidence is `< 0.5`
   (`min_alignment_confidence`).
2. **FAIL (area rule)** if any detected region has area `>= 400` px
   (`fail_area_px`). The area is the `A=<number>` shown beside each box in
   `*_annotated.png`.
3. **FAIL (count rule)** if the number of detected regions is `>= 1`
   (`fail_defect_count_threshold`).
4. **PASS** otherwise.

Only regions of 20 to 50000 px (`detection.min/max_defect_area_px`) are
counted. Because the default count threshold is 1, any surviving region fails a
pair, so all 10 bundled pairs FAIL. In the latest batch run, 35 of the 355
detected regions met the area rule and 320 did not (per-pair figures in
[performance-report.md](performance-report.md)). The regions are *detected
differences*, not classified defects, and they have not been checked against
the annotation files above.

### Helper script

Validate/copy a pair into the conventional
`data/references/reference.jpg` / `data/samples/inspected.jpg` locations:

```powershell
python scripts\dataset_preparation.py --source data\samples\deeppcb --copy
```
