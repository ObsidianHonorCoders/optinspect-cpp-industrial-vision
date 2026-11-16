#!/usr/bin/env python3
"""Summarizes OptiInspect JSON inspection reports.

Each run of the `optinspect` CLI writes a `<id>_report.json` file (see
`ReportWriter` in src/application/ReportWriter.cpp for the schema). This
script scans a directory of such reports (e.g. `results/`) and produces a
consolidated summary, useful after running the engine over a batch of PCB
image pairs (for example, every sample downloaded by
`dataset_preparation.py`).

Output formats:
  - Console table (default)
  - CSV file (--csv path)
  - Markdown file (--markdown path)

Usage:
    python scripts/generate_report.py --results-dir results --csv summary.csv
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path


def load_reports(results_dir: Path):
    reports = []
    for path in sorted(results_dir.rglob("*_report.json")):
        try:
            with path.open("r", encoding="utf-8") as f:
                data = json.load(f)
        except (json.JSONDecodeError, OSError) as exc:
            print(f"warning: failed to read {path}: {exc}", file=sys.stderr)
            continue
        data["_source_path"] = str(path)
        reports.append(data)
    return reports


def summarize(reports):
    rows = []
    for r in reports:
        rows.append({
            "inspection_id": r.get("inspection_id", ""),
            "reference_image": r.get("reference_image", ""),
            "inspected_image": r.get("inspected_image", ""),
            "decision": r.get("decision", ""),
            "defect_count": r.get("defect_count", 0),
            "alignment_confidence": r.get("alignment", {}).get("confidence", 0.0),
            "total_processing_time_ms": r.get("total_processing_time_ms", 0.0),
            "source_report": r.get("_source_path", ""),
        })
    return rows


def print_console_table(rows) -> None:
    if not rows:
        print("No reports found.")
        return

    headers = ["inspection_id", "decision", "defect_count", "alignment_confidence",
               "total_processing_time_ms"]
    widths = {h: max(len(h), *(len(str(row[h])) for row in rows)) for h in headers}

    def format_row(values):
        return "  ".join(str(v).ljust(widths[h]) for h, v in zip(headers, values))

    print(format_row(headers))
    print("  ".join("-" * widths[h] for h in headers))
    for row in rows:
        print(format_row([row[h] for h in headers]))

    total = len(rows)
    passed = sum(1 for r in rows if r["decision"] == "PASS")
    failed = sum(1 for r in rows if r["decision"] == "FAIL")
    review = sum(1 for r in rows if r["decision"] == "REVIEW")
    print(f"\nTotal: {total}  PASS: {passed}  FAIL: {failed}  REVIEW: {review}")


def write_csv(rows, path: Path) -> None:
    if not rows:
        print(f"No reports found; not writing {path}", file=sys.stderr)
        return
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    print(f"Wrote CSV summary to {path}")


def write_markdown(rows, path: Path) -> None:
    if not rows:
        print(f"No reports found; not writing {path}", file=sys.stderr)
        return
    headers = ["inspection_id", "decision", "defect_count", "alignment_confidence",
               "total_processing_time_ms"]
    lines = ["| " + " | ".join(headers) + " |",
             "| " + " | ".join("---" for _ in headers) + " |"]
    for row in rows:
        lines.append("| " + " | ".join(str(row[h]) for h in headers) + " |")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote Markdown summary to {path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                      formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--results-dir", type=Path, default=Path("results"),
                         help="Directory to recursively search for *_report.json files.")
    parser.add_argument("--csv", type=Path, default=None, help="Write a CSV summary to this path.")
    parser.add_argument("--markdown", type=Path, default=None,
                         help="Write a Markdown summary table to this path.")
    args = parser.parse_args()

    if not args.results_dir.exists():
        print(f"error: results directory does not exist: {args.results_dir}", file=sys.stderr)
        return 1

    reports = load_reports(args.results_dir)
    rows = summarize(reports)

    print_console_table(rows)
    if args.csv:
        write_csv(rows, args.csv)
    if args.markdown:
        write_markdown(rows, args.markdown)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
