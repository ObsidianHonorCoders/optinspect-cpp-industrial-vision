#pragma once

#include "visualization/VisualizationTypes.hpp"

#include <cstddef>
#include <string>

namespace optinspect::viz {

/// Tunable parameters of the visualization module. Defaults are chosen for
/// PCB images of a few hundred to a few thousand pixels per side.
struct VisualizationConfig {
    // OpenGL context
    int gl_major = 3;
    int gl_minor = 3;
    bool vsync = true;

    // Zoom / pan
    double min_zoom = 0.02;           ///< Never zoom out further than this (or the fit zoom if smaller).
    double max_zoom = 64.0;
    double zoom_step = 1.15;          ///< Zoom factor per mouse-wheel notch.
    double nearest_filter_zoom = 3.0; ///< From this zoom on, individual pixels are shown (GL_NEAREST).
    int pan_margin_px = 48;           ///< Minimum part of the image that must stay inside the viewport.
    double fit_margin_px = 8.0;       ///< Empty border kept around the image by fit-to-window.

    // Textures
    std::size_t texture_cache_budget_bytes = 192u * 1024u * 1024u;
    int max_texture_dimension = 16384;

    // Rendering
    Color4 viewport_background{0.06f, 0.07f, 0.08f, 1.0f};
    OverlayStyle overlay;
};

}  // namespace optinspect::viz
