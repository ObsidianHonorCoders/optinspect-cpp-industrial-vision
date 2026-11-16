# visualization/ (reserved — not implemented in Phase 1)

This module is reserved for a future phase that provides an interactive
(e.g. OpenGL or GUI-based) viewer for inspection results.

Phase 1's only presentation logic is `ResultAnnotator`
(see [src/application/ResultAnnotator.cpp](/src/application/ResultAnnotator.cpp)),
which draws static bounding-box overlays onto a copy of the inspected image;
this is intentionally kept in `src/application/` rather than here, since it
is a small CLI-output helper rather than a standalone visualization module.
