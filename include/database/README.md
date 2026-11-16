# database/ (reserved — not implemented in Phase 1)

This module is reserved for a future phase that persists inspection
results, defect history, and traceability records to a relational or
document database (see [docs/database-design.md](/docs/database-design.md)
for the forward-looking design note).

Phase 1 writes inspection results as standalone JSON report files via
`ReportWriter` (see [src/application/ReportWriter.cpp](/src/application/ReportWriter.cpp));
no database integration exists yet.
