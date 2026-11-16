# Communication Protocol (Future Scope — Not Implemented in Phase 1)

The `include/communication/` and `src/communication/` directories are
reserved, intentionally empty placeholders for a future phase. **No
external communication protocol exists in Phase 1.** The only way to invoke
an inspection is via the `optinspect` command-line executable, and the
only outputs are local files (JSON report, annotated/difference PNGs) and a
process exit code (`0` = PASS, `1` = FAIL, `2` = REVIEW).

## Anticipated future scope

A future phase is expected to integrate OptiInspect into a wider line-
control system, likely covering:

- A request/response protocol (e.g. TCP socket, REST endpoint, or
  message-queue consumer) allowing an external PLC/line controller to
  trigger an inspection and receive the PASS/FAIL/REVIEW decision
  programmatically, instead of parsing CLI exit codes/output files.
- A machine-readable status/health endpoint distinct from a single
  inspection result (e.g. "is the inspection service ready/degraded").
- Possibly integration with the (also not-yet-implemented)
  [database](database-design.md) module, so a communication request could
  also trigger persistence of the result.

## Why this is out of scope for Phase 1

As with the database module, the Phase 1 specification centers on the
image-processing/detection pipeline validated as a standalone CLI tool.
Defining a communication protocol requires knowing the target line-control
system's actual integration requirements, which have not been specified.
Keeping `communication/` as a documented empty stub makes this boundary
explicit rather than silently omitting a mandated directory or
implementing a speculative protocol with no real consumer to validate it
against.
