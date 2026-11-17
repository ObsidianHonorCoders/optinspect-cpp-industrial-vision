# visualization/ (Phase 3 - OpenGL rendering components)

Reusable OpenGL 3.3 core rendering components for the desktop GUI. This module owns
**all GPU resources**; it knows nothing about the inspection pipeline, the database or
Dear ImGui. Headers are in [`include/visualization/`](../../include/visualization), GLSL in
[`resources/shaders/`](../../resources/shaders).

| Component | Responsibility |
|---|---|
| `GLContext` | RAII GLFW init + window + OpenGL 3.3 core context + GLAD loading, driver info |
| `Shader` | Compile/link GLSL from files, uniform setters, errors with the driver log |
| `Texture2D` | RAII texture from `cv::Mat` (gray / BGR / BGRA), no CPU colour conversion |
| `ImageFormat` | Pure logic: validate a `cv::Mat`, plan the upload (stride / row length) |
| `TextureCache` | Pure logic: bounded LRU cache of shared resources (header only) |
| `ViewportTransform` | Pure logic: image/screen mapping, zoom, pan, fit, ROI conversion |
| `ImageViewport` | Pure logic: input handling (wheel zoom, drag pan, click, ROI drag) |
| `ImageRenderer` | One textured quad (VAO + VBO), image and blend drawing into a sub-viewport |
| `DefectOverlayRenderer` | Overlay geometry (pure) + batched renderer for boxes, selection, ROI |
| `GpuTimer` | GL timer queries for GPU frame time |

The pure-logic parts need no OpenGL context and are covered by `tests/visualization`.
The GUI that uses them lives in `src/application/gui/`.
See [docs/visualization-design.md](../../docs/visualization-design.md).
