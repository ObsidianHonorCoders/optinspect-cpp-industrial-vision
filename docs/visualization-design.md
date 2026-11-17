# Visualization design (Phase 3)

This document describes the OpenGL/ImGui desktop application `optinspect_gui` as implemented.
Measured behaviour is in [visualization-performance.md](visualization-performance.md); the
module map is in [architecture.md](architecture.md).

## 1. Stack and responsibilities

| Library (vcpkg) | Version used | Responsibility |
|---|---|---|
| GLFW (`glfw3`) | 3.5.1 | window, OpenGL context, input events, framebuffer size |
| GLAD (`glad[gl-api-33,loader]`) | 0.1.36 (GLAD 1 API: `gladLoadGLLoader`) | loads the OpenGL 3.3 function pointers after the context exists |
| Dear ImGui (`imgui[glfw-binding,opengl3-binding,docking-experimental]`) | 1.93.0, docking branch | panels, tables, widgets, text and simple charts (`ImDrawList`); backends `imgui_impl_glfw` and `imgui_impl_opengl3` come from the same port |
| OpenCV | 4.14.0 | decoding image files; `cv::Mat` is the CPU image type uploaded to textures |

OpenGL renders the images, textures and overlay primitives. ImGui is **not** used to display
images (`ImGui::Image` is never called for PCB images); it only draws the user interface around
and on top of the OpenGL viewport. OpenCV HighGUI is not used by the GUI.

## 2. OpenGL context initialization (`GLContext`)

`GLContext::create` fixes the order, so no GL call can precede the loader:

1. `glfwInit()` - failure throws `GLContextError` with the GLFW error text.
2. Window hints: version 3.3, `GLFW_OPENGL_CORE_PROFILE`, visibility, maximize.
3. `glfwCreateWindow` + `glfwMakeContextCurrent`.
4. `gladLoadGLLoader(glfwGetProcAddress)` - failure throws.
5. `GL_MAJOR_VERSION/GL_MINOR_VERSION` are read and compared with the request (3.3); too old throws.
6. `GL_VERSION`, `GL_RENDERER`, `GL_VENDOR`, GLSL version and `GL_MAX_TEXTURE_SIZE` are stored in `GLInfo`
   and shown on the Performance page; the creation time is measured.

The object is move-only RAII. Its destructor destroys the window and calls `glfwTerminate()`.
`GuiApplication` destroys the main window (textures, renderers), then the controller, then the
ImGui backends and context, and only then the `GLContext`, so GPU resources are always released
while the context is current. No OpenGL object is held in a static/global variable.

## 3. Shaders (`Shader`, `resources/shaders/`)

`Shader::fromFiles(vert, frag)` reads both files, compiles them, links, detaches and deletes the
shader objects. A missing/empty file, a compile error or a link error throws `ShaderError` with the
file names and the driver's info log. Uniform setters cache locations and throw for an unknown
uniform (a typo is an error, not a silent no-op). Programs are created once and reused.

* `image.vert/frag` - draws one textured quad. The vertex shader receives the unit square
  (`aPos`, (0,0) = image top-left) and one `vec4 uRect` that places it in the NDC of the current
  viewport; the fragment shader shows `uTex0` or `mix(uTex0, uTex1, uBlend)` (blend mode).
* `overlay.vert/frag` - coloured triangles in viewport-local pixels, converted to NDC with
  `uViewportSize`.

The files are copied next to the executable by a post-build step; the application also searches
the source tree location baked in at configure time and `--shader-dir`.

## 4. Textures (`Texture2D`, `ImageFormat`)

* Accepted: 8-bit, 2-D, 1/3/4 channels (`GL_R8`, `GL_RGB8`, `GL_RGBA8`). Anything else (16-bit,
  float, 2 channels, empty, larger than `GL_MAX_TEXTURE_SIZE`) throws `TextureError` before any GL
  call. 16-bit files are reduced to 8 bit by the image loader so every decodable image can be shown.
* **Channel order**: BGR/BGRA is uploaded as `GL_BGR`/`GL_BGRA` and reordered by the driver; grey
  uses a `(R,R,R,1)` texture swizzle. No per-frame or per-upload CPU colour conversion exists.
* **Orientation**: OpenCV row 0 is the top row; the unit quad maps t = 0 to the image top, so no
  flip is needed (verified by a GL smoke test with a four-colour image).
* **Row alignment / strides**: `GL_UNPACK_ALIGNMENT` is 1. `planUpload` uploads continuous
  matrices in place, uploads sub-matrices / padded rows in place via `GL_UNPACK_ROW_LENGTH` when the
  stride is a multiple of the pixel size, and otherwise makes one deliberate contiguous copy.
* **Lifecycle**: `upload()` allocates storage (`glTexImage2D`) only when width, height or layout
  change and otherwise overwrites (`glTexSubImage2D`); mipmaps are regenerated after an upload.
  The filter is selected from the zoom (`chooseFilter`): nearest from 3x (individual pixels),
  trilinear mipmapped below 1x, bilinear otherwise, and only touched when it changes.
  Wrapping is `GL_CLAMP_TO_EDGE`. `release()`/the destructor delete the texture; allocation
  failures (`GL_OUT_OF_MEMORY`) are turned into `TextureError`.
* **Cache** (`BoundedLruCache<Texture2D>`, header-only, no GL dependency): key
  `"<absolute path>|<mtime>|<size>"` (a changed file is a new key = implicit invalidation),
  budget 192 MB of estimated GPU bytes, least-recently-used eviction, the newest entry is never
  evicted, values are `shared_ptr` so a texture that is still being drawn survives its eviction.
  Used on the GL thread only.

## 5. Rendering pipeline (`ImageRenderer`, `DefectOverlayRenderer`)

Per frame, on the GL thread:

```
glfwPollEvents -> controller.update() -> syncTextures()      (decode results installed, uploads if new)
ImGui::NewFrame -> panels build the UI; the image panel reserves the viewport rectangle(s)
ImGui::Render
glClear(whole framebuffer)
for each pane:  glViewport/glScissor = pane rectangle
                ImageRenderer.drawImage / drawBlend      (1 draw call, uRect from ViewportTransform)
                DefectOverlayRenderer.render             (1 batched draw call for all boxes + ROI)
ImGui_ImplOpenGL3_RenderDrawData                          (UI on top; labels are ImGui text)
swap
```

* The image is one 4-vertex triangle strip in a static VAO/VBO (no EBO - four vertices need no index
  buffer). Zoom and pan only change one uniform; no geometry is rebuilt.
* Overlays are **separate primitives**, never written into a texture: outlines are four thin quads
  with a constant screen thickness (2 px, 3.5 px + translucent fill for the selected region),
  boxes smaller than 4 px are inflated to stay visible, shapes outside the viewport are culled,
  all shapes of a pane go to one dynamic VBO (grown geometrically, reused) and one draw call.
  Labels (`#index`, optional `A=area`) are drawn with ImGui at the anchor positions computed by
  the same geometry builder.
* Aspect ratio: a single uniform zoom is used, so images are never stretched.
* The viewport background is the clear colour (`VisualizationConfig::viewport_background`).

## 6. Coordinate transformations (`ViewportTransform`)

All coordinate logic is a pure class (no OpenGL) so it is unit-tested.

* Image space: pixels, origin at the top-left corner of pixel (0,0), y down. Screen space: ImGui
  logical coordinates. `screen = viewport_origin + offset + image * zoom`.
* `fit()` chooses `min(avail_w/image_w, avail_h/image_h)` (8 px margin), centres the image and keeps
  following the viewport size until the user zooms or pans. `actualSize()` = 1:1. `reset()` = `fit()`.
* `zoomAt(cursor, factor)` keeps the image point under the cursor fixed; zoom is clamped to
  `[min(0.02, fit), 64]`; non-positive / NaN factors are ignored.
* `panBy` clamps the offset so at least 48 px (or the whole image, if smaller) stay inside the
  viewport: the image can never be lost off-screen.
* `toGlViewportRect` converts the top-left screen rectangle to the bottom-left framebuffer rectangle
  expected by `glViewport`/`glScissor`, including the high-DPI scale.
* ROI conversion: `imageRectFromScreenDrag(a, b)` maps two screen points to image space, clamps to the
  image and rounds **outward** to whole pixels (floor of the minimum, ceil of the maximum).

## 7. Interaction and ROI (`ImageViewport`)

`ImageViewport` turns a per-frame `PointerState` (filled from ImGui, or synthetically in tests)
into: wheel zoom around the cursor (factor 1.15 per notch), left-drag or middle-drag pan, the image
coordinate under the cursor, click detection (press/release with <= 4 px travel) and ROI drag.

ROI: toggle the **ROI** tool, drag a rectangle (live preview), release to commit; a plain click
clears it. The ROI is stored in **image pixels**, so it stays attached to the image under zoom and
pan. The tool strip shows x, y, width, height and area (px^2). **Export ROI** writes
`<inspection id>_roi.json` next to the inspection images (coordinates, size, area, timestamp).
The ROI never changes the inspection, the thresholds, the stored measurements or the report, and no
re-inspection is triggered.

Defect picking: a click (Pan tool) hit-tests the measured boxes with a 4-screen-pixel tolerance
(smallest box wins on overlap); selecting a row in the region table or list highlights the box and,
if "center on select" is on (default), centres/zooms the view on it.

Modes: Reference, Inspected, Annotated (the Phase 1 image, which has its own baked-in boxes),
Difference (the Phase 1 difference image, unchanged), Side-by-side (two synchronized viewports:
zoom/pan are copied between them) and Blend (shader `mix`, opacity slider; needs equal image sizes).
Defect boxes are expressed in the reference/aligned frame; the raw "Inspected" image is not warped,
so boxes can be offset by the alignment transform in that mode.

## 8. Threading model

| Thread | Owns / does |
|---|---|
| GUI (main) thread | GLFW events, ImGui, **all** OpenGL calls, texture creation/destruction, the texture cache, the controller state |
| `InspectionWorker` thread | `runInspection` (existing pipeline + report/image output), optional persistence via `InspectionRecorder`, decoding of the four display images |
| `AsyncTask` threads | history query, analytics query, loading a stored inspection with its images |

* `AsyncTask<T>` runs one job on an owned `std::thread`, publishes `{value | error}` under a mutex and
  is polled by the GUI thread; the destructor joins. There are no detached threads.
* Jobs receive their input by value and never touch ImGui or OpenGL; results are immutable
  (`shared_ptr<const cv::Mat>`).
* Progress: `InspectionWorker` stores a `std::atomic<WorkerPhase>` set from a `StageObserver` that the
  real `InspectionPipeline` calls before its stages (`alignment`, `detection`, `decision`) plus
  loading / writing / persisting. No percentage is shown because the pipeline does not report one; the
  bar is indeterminate and labelled with the real phase.
* Shutdown: the destructor sets a cancel flag and joins; the job stops at its next stage boundary
  (the observer throws), so closing the window waits for at most one pipeline stage.
* Errors: every exception in a job is converted into a `TaskResult::error` / failed
  `InspectionJobResult` and shown in the panel and log.

## 9. GUI-to-engine integration

`runInspection(...)` (new `src/application/InspectionService.*`, moved out of `main.cpp` unchanged
and shared by the CLI and the GUI) loads both images, runs `InspectionPipeline`, writes the annotated
image, difference image and JSON report, and returns the outcome. The GUI therefore produces the same
output files as the CLI. The CLI is not invoked as a subprocess. The only change to the pipeline is
an optional `StageObserver` parameter with an empty default.

## 10. GUI-to-database integration

* Persist: the same `InspectionRecorder` + `buildPersistenceRequest` + `makeArtifacts` path as the CLI
  (one transaction per inspection). A database failure never changes the shown decision; the error is
  displayed and logged, the output files are kept.
* History / analytics: `HistoryService` calls `InspectionRepository`, `ProductRepository`,
  `DefectRepository`, `InspectionImageRepository` and `QualityAnalyticsService` through a **read-only**
  connection that is opened and closed inside each background job. No SQL is written in the GUI.
  The identifier search filters the rows returned by `InspectionRepository::list` (the repository has no
  text search).
* Selecting an inspection loads the record, its defects, image references and report-based stage
  timings, checks that each file exists and decodes the existing ones. A missing file is reported
  ("File not found: ...") in the details, the log and the result warnings; nothing is substituted and the
  database is not modified.

## 11. Error handling summary

GLFW/GLAD/context failures, shader compile/link errors, unsupported or oversized images and GPU
allocation failures throw typed exceptions that `main` reports with exit code 70; image decode failures,
missing history files, database errors and inspection exceptions are shown in the UI and the log
(`LogLevel::Error` entries are also written to `stderr`). There is no other logging mechanism in the
code base.

## 12. Resource ownership

```
GuiApplication
 +-- GLContext (destroyed last)
 +-- GuiInspectionController -- InspectionWorker, AsyncTask x3 (join on destruction)
 +-- MainWindow
      +-- ImageComparisonPanel
           +-- ImageRenderer (Shader, VAO, VBO)
           +-- DefectOverlayRenderer (Shader, VAO, dynamic VBO)
           +-- GpuTimer
           +-- BoundedLruCache<Texture2D>   (shared_ptr textures; the 4 current ones are also held in current_[])
```

## 13. Known limitations

* Windows only for the file dialogs (COM `IFileOpenDialog`); the rest is portable C++/OpenGL.
* Paths use the ANSI code page like the rest of the code base; characters outside it are not supported.
* The dock layout is rebuilt on each start (`imgui.ini` is disabled).
* No hit-testing/ROI on the raw (un-warped) inspected image beyond the displayed coordinates.
