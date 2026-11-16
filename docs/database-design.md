# Database Design (Future Scope — Not Implemented in Phase 1)

The `include/database/` and `src/database/` directories are reserved,
intentionally empty placeholders for a future phase. **No database
integration exists in Phase 1.** Inspection results are currently only
written as JSON files and PNG images directly to the filesystem (see
`ReportWriter` / `ResultAnnotator` in `src/application/`).

## Anticipated future scope

A future phase is expected to add persistent storage of inspection history,
likely covering:

- An `InspectionRecord` table/collection capturing: inspection ID,
  timestamp, reference/inspected image paths (or references to stored
  images), alignment confidence, decision (PASS/FAIL/REVIEW), and the full
  list of detected defect measurements.
- A query/reporting interface so historical inspection results can be
  browsed or aggregated without re-parsing JSON report files from disk
  (the current approach, as implemented ad hoc by
  `scripts/generate_report.py`).
- Likely candidates: an embedded database (e.g. SQLite) for a
  single-station deployment, or a client/server database for a multi-line
  deployment — the choice depends on deployment requirements not yet
  defined for this project.

## Why this is out of scope for Phase 1

The Phase 1 specification is explicitly focused on the traditional-CV
image-processing/detection pipeline and CLI tool. Persistence introduces
schema design, migration, and deployment-topology questions that are
independent of (and should not block) validating the core
alignment/detection/decision algorithms. Keeping `database/` as a
documented empty stub makes this scope boundary explicit rather than
leaving it undocumented or omitting the directory the spec mandates.
