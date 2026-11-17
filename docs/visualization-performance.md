# Visualization performance (Phase 3)

All numbers in this document were measured on the machine below with the tools that ship with the
repository; nothing is estimated. Raw JSON files are produced by the commands in
[How to reproduce](#how-to-reproduce).

## Environment

| Item | Value |
|---|---|
| Machine | AWS virtual machine, AMD EPYC 9R14, 4 vCPU, 31 GB RAM |
| OS | Windows Server 2022 Datacenter (10.0.20348) |
| GPU | **none** - the only display adapters are "Microsoft Basic Display Adapter" and "AWS Indirect Display Device" |
| OpenGL | **4.6 (Core Profile) Mesa 26.2.0, renderer `llvmpipe (LLVM 22.1.8, 256 bits)`** - a CPU software rasterizer, vendor "Mesa"; `GL_MAX_TEXTURE_SIZE` 16384, GLSL 4.60 |
| Compiler | MSVC 19.16 (Visual Studio 2017 15.9), `Release` |
| Libraries | OpenCV 4.14.0, SQLite 3.54.1, GLFW 3.5.1, GLAD 0.1.36, Dear ImGui 1.93.0 (docking) |
| Window | 1500 x 820 requested; framebuffer 1500 x 675 (limited by the screen) |
| Test images | DeepPCB sample `00041000_temp.jpg` / `_test.jpg`, 640 x 640 px, grey JPEG (decoded as 1 channel for the GUI; the Phase 1 annotated image is BGR, the difference image grey) and a synthetic 4000 x 3000 BGR image for the micro-benchmark |
| Texture formats | `GL_R8` (reference, inspected, difference), `GL_RGB8` (annotated), all with mip chains |

**Consequences of the software renderer.** Because the OpenGL implementation runs on the CPU, the
"GPU" figures are really CPU rasterization time, the GPU timer-query values are tiny and not a
measure of hardware GPU load, and there is no meaningful "GPU memory" (textures live in system
RAM). The absolute numbers are therefore **not** representative of a machine with a real GPU;
the relative comparisons (what work is avoided) are the useful part. CPU usage and GPU utilization
were not sampled.

## Methodology

* `optinspect_gui --benchmark out.json` (end to end): vsync off, `glFinish` after every texture
  upload (so the upload time includes completion), 300 frames per scenario after warm-up, three
  complete runs; the tables show the median of the three runs (the three values are listed where
  they differ noticeably).
* `optinspect_visualization_benchmark` (strategy comparison, no UI): offscreen 1200 x 800 target,
  `glFinish` after every iteration, one run (100 iterations for the 640 x 640 image, 40 for the
  4000 x 3000 image). Run-to-run noise was not measured for this tool.
* CPU time, upload time, render time and UI time are separate metrics:
  *frame total* = loop iteration incl. swap, *CPU frame* = same without the swap, *UI build* = ImGui
  widget construction, *GL submit* = CPU time to issue image/overlay GL calls, *ImGui render* =
  CPU time to render ImGui draw data, *GPU* = timer query of the image + overlay draws.
* Important: the implementation was designed with texture reuse, batching and background
  workers from the start. There is no earlier, slower GUI build. The "baseline" column below is
  a **naive alternative measured in the same program**, not a previous release.

## Results

### Startup and loading (median of 3 runs)

| Measurement | Result | Notes |
|---|---|---|
| Application startup (process start to first presented frame) | 318 ms (412 / 318 / 318) | includes the GL context below |
| GLFW window + GL context + GLAD | 226 ms (313 / 226 / 226) | software GL driver start-up dominates |
| Shaders, VAO/VBO, ImGui backend init | 8 ms | |
| Decode reference image (640 x 640 JPEG, calling thread) | 0.69 ms avg | 5 repetitions |
| Decode inspected image | 0.62 ms avg | |
| Texture upload, 1st image (grey, first use of a format) | 64 ms (64 - 68) | `glFinish`-synchronised, incl. mip generation |
| Texture upload, 2nd image (same format) | 0.8 ms | |
| Texture upload, 3rd image (BGR, first use of that format) | 50 ms (50 - 52) | |
| Texture upload, 4th image (grey) | 0.7 ms | |
| Texture memory (4 textures of 640 x 640, with mips) | 3.28 MB | `Texture2D::gpuBytes()` estimate |

The 50 - 64 ms uploads happen only for the *first use of a pixel layout*; the following uploads of
the same layout take < 1 ms. This looks like one-time work inside the software driver but was not
investigated further, and it was **not optimized**.

### Inspection and UI responsiveness (median of 3 runs)

| Measurement | Result |
|---|---|
| Inspection job on the worker thread (existing pipeline + outputs + decode of 4 display images) | 258 ms (356 / 258 / 209) |
| UI frame time while the worker ran | avg 4.0 ms, p95 5.2 ms, max 12.7 ms (52 - 96 frames rendered during the job) |
| UI frame time with an idle UI (no image) | avg 3.6 ms |
| Same inputs run a second time: worker time | 191 ms (191 / 258 / 183), 2 of 4 images not decoded and not uploaded again (reference + inspected hit the texture cache) |
| History query (12 rows, SQLite, background job) | 2.7 ms avg |
| Load a stored inspection (record + defects + image references + decode of its images) | 4.1 ms avg |

Had the same job run on the render thread, the window would have been unresponsive for the whole job
duration (about 200 - 350 ms here; derived from the measured worker time, not measured separately).
With the worker thread the longest frame during the job was 12.7 ms.

### Steady-state frame times (median of 3 runs, vsync off, ms)

| Scenario | Frame total avg | p95 | max | CPU frame | UI build | GL submit | ImGui render | GPU (timer query) |
|---|---|---|---|---|---|---|---|---|
| UI only, no image | 3.62 | 4.3 | 4.9 | 0.87 | 0.40 | 0 | 0.33 | - |
| Annotated, static | 4.40 | 5.05 | 6.5 | 1.22 | 0.48 | 0.07 | 0.50 | 0.03 |
| Annotated, continuous zoom + pan | 4.72 | 5.26 | **42** | 1.43 | 0.48 | 0.28 | 0.49 | 0.03 |
| Side-by-side, static | 4.89 | 5.51 | 6.8 | 1.27 | 0.48 | 0.10 | 0.51 | 0.04 |
| Blend, continuous zoom + pan | 5.27 | 6.26 | **47** | 1.47 | 0.48 | 0.31 | 0.50 | 0.06 |

The 42 / 47 ms maxima occur once per animation run in all three runs, in the `gl_submit` part of the
frame at the moment the zoom crosses a filter threshold. Probable cause: the software driver compiles
a new shader/sampler variant when the texture filter state changes. This is a hypothesis; it was not
verified and it is not expected to appear in the same form on a hardware GPU.

### Strategy comparison (`optinspect_visualization_benchmark`, one run)

Texture upload, wall time including GPU completion:

| Strategy | 640 x 640 BGR | 4000 x 3000 BGR |
|---|---|---|
| New texture + glTexImage2D every time, with mips | 2.21 ms | 46.4 ms |
| New texture + glTexImage2D every time, no mips | 0.97 ms | 26.3 ms |
| Replace contents of an existing texture (glTexSubImage2D), with mips | 1.56 ms | 31.5 ms |
| Replace contents, no mips | 0.69 ms | 20.5 ms |
| **Unchanged image: texture reused from the cache (implemented)** | **0 ms (no GL call)** | **0 ms** |
| Sub-matrix uploaded in place (`GL_UNPACK_ROW_LENGTH`, implemented) | 0.39 ms | 11.8 ms |
| Sub-matrix with a `clone()` first | 0.42 ms | 15.3 ms |

Drawing one frame of the fitted image (1200 x 800 target):

| Strategy | 640 x 640 | 4000 x 3000 |
|---|---|---|
| Cached texture, mipmapped trilinear (implemented) | 1.07 ms | 2.90 ms |
| Cached texture, no mipmaps, bilinear | 1.09 ms | 2.01 ms |
| **Naive: re-upload the image every frame, then draw** | 2.44 ms | **25.6 ms** |

Overlay rendering (geometry building is included in each figure):

| Boxes | 1 batched draw call (implemented) 640 / 4000x3000 image | 1 draw call per box 640 / 4000x3000 image |
|---|---|---|
| 10 | 0.23 / 0.20 ms | 0.26 / 0.21 ms |
| 100 | 0.34 / 0.26 ms | 0.62 / 0.51 ms |
| 500 | 1.54 / 0.77 ms | 2.22 / 1.78 ms |

Minification quality (4000 x 3000 image shown at 26 %; mean absolute error per channel against an
area-averaged reference resize, lower is better): mipmapped 16.7, plain bilinear 19.7.

## What changed, what improved, what did not

* **Texture reuse / no per-frame upload** - the image is uploaded once per content change and then only drawn.
  Measured: at 4000 x 3000, drawing from the cached texture takes about 2 - 3 ms versus 25.6 ms when
  the image is re-uploaded every frame (about 9 x less); at 640 x 640 about 2 x less. A second inspection with
  the same inputs skipped 2 of 4 decodes and 2 of 4 uploads.
* **Decoding and the inspection on worker threads** - the render loop kept 4 ms frames (max 12.7 ms)
  while a 200 - 350 ms job ran.
* **In-place sub-matrix upload** (`GL_UNPACK_ROW_LENGTH`) - 11.8 ms vs 15.3 ms at 4000 x 3000 (about 23 % less),
  no measurable difference at 640 x 640.
* **Batched overlay** - one draw call for all boxes: 1.4x - 2.3x less time than a draw call per box at 100 - 500 boxes
  with this driver; for 10 boxes no relevant difference. With few defects (typical here: about 10) the overlay is not a bottleneck.
* **Mipmaps (kept, with a cost)** - mipmaps cost +50 - 130 % upload time and +44 % draw time on the large
  image (2.90 vs 2.01 ms) on this software renderer and reduced the minification error by 15 % on a synthetic
  line image. They were kept because thin PCB traces alias badly when minified, but the benefit was only measured on
  one synthetic image and the cost on one machine.
* **glTexSubImage2D instead of reallocation** - 20.5 vs 26.3 ms (no mips) and 31.5 vs 46.4 ms (mips) at
  4000 x 3000; used when a texture object is reused with the same size and layout.
* **Not improved / not done**: first-use upload cost of a pixel layout (50 - 64 ms here); the one-off 42 - 47 ms spike when
  the filter state changes; no pixel buffer objects or persistent mapping (no evidence they are needed at
  these image sizes); no GPU-side timing on real hardware.

## Remaining bottlenecks and limits

* On this machine each frame costs 3.6 - 5.3 ms mostly in the software driver (swap/rasterization) and ImGui
  rendering (0.3 - 0.5 ms CPU); building the UI is 0.4 - 0.5 ms.
* The inspection itself (about 200 ms for a 640 x 640 pair, mostly ECC alignment) is Phase 1 work and unchanged;
  the GUI only keeps it off the render thread.
* A real-GPU measurement, CPU usage, GPU utilization and VRAM figures are missing and must be taken on a workstation.
* Large images (4000 x 3000 and up) were only measured in the strategy benchmark, not through the full GUI.

## How to reproduce

```powershell
# end-to-end (writes the JSON; uses the database path for the history timings)
.\build\bin\Release\optinspect.exe db init --database results\bench.db
.\build\bin\Release\optinspect.exe inspect --reference data\samples\deeppcb\images --input data\samples\deeppcb\images --output results\bench_seed --database results\bench.db
.\build\bin\Release\optinspect_gui.exe --reference data\samples\deeppcb\images\00041000_temp.jpg `
    --input data\samples\deeppcb\images\00041000_test.jpg --output results\bench `
    --database results\bench.db --benchmark results\bench_gui.json --frames 300

# strategy comparison
.\build\bin\Release\optinspect_visualization_benchmark.exe --image data\samples\deeppcb\images\00041000_temp.jpg --iterations 100 --output results\bench_viz_640.json
.\build\bin\Release\optinspect_visualization_benchmark.exe --iterations 40 --output results\bench_viz_4000x3000.json
```

`--benchmark` persists the two benchmark inspections into the given database, so use a throw-away file.
The Performance page of the GUI shows the same metrics live.
