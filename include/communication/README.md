# communication/ (reserved — not implemented in Phase 1)

This module is reserved for a future phase that integrates OptiInspect with
external line-control systems (e.g. PLCs, MES) over a defined protocol (see
[docs/communication-protocol.md](/docs/communication-protocol.md) for the
forward-looking design note).

Phase 1 is a standalone CLI tool with no network or industrial protocol
integration; results are communicated only via JSON report files, annotated
images, and process exit codes.
