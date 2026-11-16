#!/usr/bin/env python3
"""Dataset preparation helper for OptiInspect.

This script organizes and validates DeepPCB-style image/annotation pairs so
they can be used as realistic reference/inspected image pairs for manual
testing and demonstration of the OptiInspect CLI (`optinspect.exe`).

It does NOT download the full DeepPCB dataset (1,500 image pairs). Instead it
operates on whatever subset has been placed under a source directory (for
example `data/samples/deeppcb/`, which already contains a small sample
fetched during development), and:

  1. Validates that every "<id>_temp.jpg" (reference/template) image has a
     matching "<id>_test.jpg" (inspected) image and a "<id>.txt" annotation
     file.
  2. Parses each annotation file (DeepPCB format: "x1,y1,x2,y2,type" per
     line, where type is 1-6 mapping to open/short/mousebite/spur/copper/
     pin-hole) and reports basic statistics.
  3. Optionally copies validated pairs into `data/samples/` and
     `data/references/` using OptiInspect's expected naming convention, so
     they can be passed directly to the `optinspect` executable.

Usage:
    python scripts/dataset_preparation.py --source data/samples/deeppcb --copy

This script has no third-party dependencies (standard library only), so it
runs with the system Python installation without any additional setup.
"""
from __future__ import annotations

import argparse
import shutil
import sys
from dataclasses import dataclass, field
from pathlib import Path

DEFECT_TYPES = {
    1: "open",
    2: "short",
    3: "mousebite",
    4: "spur",
    5: "copper",
    6: "pin-hole",
}


@dataclass
class AnnotationStats:
    total_defects: int = 0
    by_type: dict = field(default_factory=dict)

    def add(self, defect_type: int) -> None:
        self.total_defects += 1
        name = DEFECT_TYPES.get(defect_type, f"unknown({defect_type})")
        self.by_type[name] = self.by_type.get(name, 0) + 1


def parse_annotation(path: Path) -> AnnotationStats:
    stats = AnnotationStats()
    with path.open("r", encoding="utf-8") as f:
        for line_no, line in enumerate(f, start=1):
            line = line.strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) != 5:
                print(f"  warning: {path.name}:{line_no}: expected 5 fields, got {len(parts)}",
                      file=sys.stderr)
                continue
            try:
                defect_type = int(parts[4])
            except ValueError:
                print(f"  warning: {path.name}:{line_no}: non-integer defect type '{parts[4]}'",
                      file=sys.stderr)
                continue
            stats.add(defect_type)
    return stats


def find_pairs(source_dir: Path):
    """Finds (reference, inspected, annotation) triples under source_dir.

    Searches recursively, matching OptiInspect-relevant DeepPCB layouts such
    as PCBData/group*/<id>/<id>NNN_temp.jpg + _test.jpg, with the
    corresponding annotation in a sibling "<id>_not" directory, as well as
    the flat layout used for the small bundled sample
    (data/samples/deeppcb/images/*.jpg + data/samples/deeppcb/annotations/*.txt).
    """
    pairs = []
    temp_images = sorted(source_dir.rglob("*_temp.jpg"))
    for temp_path in temp_images:
        stem = temp_path.name[: -len("_temp.jpg")]
        test_path = temp_path.with_name(f"{stem}_test.jpg")
        if not test_path.exists():
            print(f"  warning: no matching _test.jpg for {temp_path}", file=sys.stderr)
            continue

        annotation_path = find_annotation(source_dir, stem)
        pairs.append((temp_path, test_path, annotation_path))
    return pairs


def find_annotation(source_dir: Path, stem: str) -> Path | None:
    candidates = list(source_dir.rglob(f"{stem}.txt"))
    return candidates[0] if candidates else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                      formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=Path, default=Path("data/samples/deeppcb"),
                         help="Directory to search for DeepPCB-style image/annotation pairs.")
    parser.add_argument("--copy", action="store_true",
                         help="Copy the first validated pair into data/references/ and "
                              "data/samples/ using OptiInspect's default naming convention.")
    parser.add_argument("--references-dir", type=Path, default=Path("data/references"))
    parser.add_argument("--samples-dir", type=Path, default=Path("data/samples"))
    args = parser.parse_args()

    if not args.source.exists():
        print(f"error: source directory does not exist: {args.source}", file=sys.stderr)
        return 1

    print(f"Scanning {args.source} for DeepPCB-style image pairs...")
    pairs = find_pairs(args.source)

    if not pairs:
        print("No image pairs found.", file=sys.stderr)
        return 1

    print(f"\nFound {len(pairs)} reference/inspected pair(s):\n")
    total_stats = AnnotationStats()
    missing_annotations = 0
    for temp_path, test_path, annotation_path in pairs:
        stem = temp_path.name[: -len("_temp.jpg")]
        if annotation_path is None:
            print(f"  {stem}: reference={temp_path.name} inspected={test_path.name} "
                  f"annotation=MISSING")
            missing_annotations += 1
            continue
        stats = parse_annotation(annotation_path)
        for name, count in stats.by_type.items():
            total_stats.by_type[name] = total_stats.by_type.get(name, 0) + count
        total_stats.total_defects += stats.total_defects
        print(f"  {stem}: reference={temp_path.name} inspected={test_path.name} "
              f"defects={stats.total_defects} ({', '.join(f'{k}={v}' for k, v in stats.by_type.items()) or 'none'})")

    print(f"\nTotal pairs: {len(pairs)}  |  Missing annotations: {missing_annotations}")
    print(f"Total annotated defects across dataset: {total_stats.total_defects}")
    if total_stats.by_type:
        print("Defect type breakdown:")
        for name, count in sorted(total_stats.by_type.items()):
            print(f"  {name:10s}: {count}")

    if args.copy:
        first_ref, first_insp, _ = pairs[0]
        args.references_dir.mkdir(parents=True, exist_ok=True)
        args.samples_dir.mkdir(parents=True, exist_ok=True)
        ref_dest = args.references_dir / "reference.jpg"
        insp_dest = args.samples_dir / "inspected.jpg"
        shutil.copy2(first_ref, ref_dest)
        shutil.copy2(first_insp, insp_dest)
        print(f"\nCopied sample pair for quick CLI testing:\n"
              f"  {ref_dest}\n  {insp_dest}\n"
              f"Run: optinspect --reference {ref_dest} --input {insp_dest} "
              f"--config config/inspection_config.json --output results/")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
