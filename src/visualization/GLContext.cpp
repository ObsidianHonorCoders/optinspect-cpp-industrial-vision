#include "visualization/GLContext.hpp"

#include <glad/glad.h>

#include <GLFW/glfw3.h>

#include <chrono>
#include <utility>

namespace optinspect::viz {

namespace {

std::string lastGlfwError() {
    const char* description = nullptr;
    const int code = glfwGetError(&description);
    if (code == GLFW_NO_ERROR || description == nullptr) return "unknown error";
    return description;
}

std::string glString(GLenum name) {
    const GLubyte* s = glGetString(name);
    return s ? reinterpret_cast<const char*>(s) : "(unavailable)";
}

}  // namespace

GLContext GLContext::create(const GLContextOptions& options) {
    const auto start = std::chrono::steady_clock::now();

    GLContext ctx;
    if (!glfwInit()) {
        throw GLContextError("GLFW initialization failed: " + lastGlfwError());
    }
    ctx.glfw_initialized_ = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, options.gl_major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, options.gl_minor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, options.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, options.maximized ? GLFW_TRUE : GLFW_FALSE);

    ctx.window_ = glfwCreateWindow(options.width, options.height, options.title.c_str(), nullptr, nullptr);
    if (!ctx.window_) {
        const std::string reason = lastGlfwError();
        // ctx's destructor terminates GLFW.
        throw GLContextError("Could not create an OpenGL " + std::to_string(options.gl_major) + "." +
                             std::to_string(options.gl_minor) + " core window: " + reason);
    }
    glfwMakeContextCurrent(ctx.window_);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        throw GLContextError("GLAD could not load the OpenGL function pointers.");
    }

    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < options.gl_major || (major == options.gl_major && minor < options.gl_minor)) {
        throw GLContextError("OpenGL " + std::to_string(options.gl_major) + "." +
                             std::to_string(options.gl_minor) + " is required, driver provides " +
                             std::to_string(major) + "." + std::to_string(minor));
    }

    ctx.info_.major = major;
    ctx.info_.minor = minor;
    ctx.info_.version = glString(GL_VERSION);
    ctx.info_.renderer = glString(GL_RENDERER);
    ctx.info_.vendor = glString(GL_VENDOR);
    ctx.info_.glsl_version = glString(GL_SHADING_LANGUAGE_VERSION);
    GLint max_tex = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
    ctx.info_.max_texture_size = max_tex;

    glfwSwapInterval(options.vsync ? 1 : 0);

    ctx.creation_ms_ =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return ctx;
}

GLContext::GLContext(GLContext&& other) noexcept
    : window_(std::exchange(other.window_, nullptr)),
      glfw_initialized_(std::exchange(other.glfw_initialized_, false)),
      info_(std::move(other.info_)),
      creation_ms_(other.creation_ms_) {}

GLContext& GLContext::operator=(GLContext&& other) noexcept {
    if (this != &other) {
        destroy();
        window_ = std::exchange(other.window_, nullptr);
        glfw_initialized_ = std::exchange(other.glfw_initialized_, false);
        info_ = std::move(other.info_);
        creation_ms_ = other.creation_ms_;
    }
    return *this;
}

GLContext::~GLContext() { destroy(); }

void GLContext::destroy() noexcept {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    if (glfw_initialized_) {
        glfwTerminate();
        glfw_initialized_ = false;
    }
}

void GLContext::makeCurrent() const { glfwMakeContextCurrent(window_); }
void GLContext::swapBuffers() const { glfwSwapBuffers(window_); }
bool GLContext::shouldClose() const { return glfwWindowShouldClose(window_) != 0; }
void GLContext::requestClose() const { glfwSetWindowShouldClose(window_, GLFW_TRUE); }
void GLContext::framebufferSize(int& width, int& height) const {
    glfwGetFramebufferSize(window_, &width, &height);
}
void GLContext::windowSize(int& width, int& height) const { glfwGetWindowSize(window_, &width, &height); }
void GLContext::setSwapInterval(int interval) const { glfwSwapInterval(interval); }

void pollEvents() { glfwPollEvents(); }

}  // namespace optinspect::viz
