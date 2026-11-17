#pragma once

#include <stdexcept>
#include <string>

struct GLFWwindow;

namespace optinspect::viz {

class GLContextError : public std::runtime_error {
public:
    explicit GLContextError(const std::string& message) : std::runtime_error(message) {}
};

struct GLContextOptions {
    std::string title = "OptiInspect";
    int width = 1600;
    int height = 950;
    int gl_major = 3;
    int gl_minor = 3;
    bool visible = true;    ///< false creates a hidden window (smoke tests, benchmarks).
    bool vsync = true;
    bool maximized = false;
};

/// What the driver reported after context creation.
struct GLInfo {
    std::string version;   ///< GL_VERSION
    std::string renderer;  ///< GL_RENDERER
    std::string vendor;    ///< GL_VENDOR
    std::string glsl_version;
    int major = 0;
    int minor = 0;
    int max_texture_size = 0;
};

/// RAII owner of the GLFW library state, one GLFW window and its OpenGL 3.3
/// core context, with GLAD already loaded. Creation order (GLFW init -> window
/// -> make current -> GLAD) is fixed in the factory, so no GL call can happen
/// before the function pointers are loaded.
///
/// Destruction order matters: every GPU resource (Texture2D, Shader, renderers)
/// must be destroyed BEFORE the GLContext, while the context is still current.
/// Own the context first in your application object so it is destroyed last.
class GLContext {
public:
    /// Throws GLContextError (with the GLFW error text) on any failure.
    static GLContext create(const GLContextOptions& options);

    GLContext(const GLContext&) = delete;
    GLContext& operator=(const GLContext&) = delete;
    GLContext(GLContext&& other) noexcept;
    GLContext& operator=(GLContext&& other) noexcept;
    ~GLContext();

    GLFWwindow* window() const { return window_; }
    const GLInfo& info() const { return info_; }
    double creationTimeMs() const { return creation_ms_; }

    void makeCurrent() const;
    void swapBuffers() const;
    bool shouldClose() const;
    void requestClose() const;
    void framebufferSize(int& width, int& height) const;
    void windowSize(int& width, int& height) const;
    void setSwapInterval(int interval) const;

private:
    GLContext() = default;
    void destroy() noexcept;

    GLFWwindow* window_ = nullptr;
    bool glfw_initialized_ = false;
    GLInfo info_;
    double creation_ms_ = 0.0;
};

/// Polls window events (thin wrapper so callers need not include GLFW).
void pollEvents();

}  // namespace optinspect::viz
