#include "visualization/Shader.hpp"

#include <glad/glad.h>

#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace optinspect::viz {

namespace {

unsigned int compile(GLenum type, const std::string& source, const std::string& label) {
    const GLuint shader = glCreateShader(type);
    if (shader == 0) throw ShaderError("glCreateShader failed for " + label);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<std::size_t>(length > 1 ? length : 1), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        glDeleteShader(shader);
        throw ShaderError("Shader compilation failed (" + label + "):\n" + log.data());
    }
    return shader;
}

}  // namespace

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) throw ShaderError("Cannot open shader file: " + path.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (buffer.str().empty()) throw ShaderError("Shader file is empty: " + path.string());
    return buffer.str();
}

Shader Shader::fromFiles(const std::filesystem::path& vertex_path,
                         const std::filesystem::path& fragment_path) {
    const std::string vs_source = readTextFile(vertex_path);
    const std::string fs_source = readTextFile(fragment_path);

    const GLuint vs = compile(GL_VERTEX_SHADER, vs_source, vertex_path.string());
    GLuint fs = 0;
    try {
        fs = compile(GL_FRAGMENT_SHADER, fs_source, fragment_path.string());
    } catch (...) {
        glDeleteShader(vs);
        throw;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDetachShader(program, vs);
    glDetachShader(program, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<std::size_t>(length > 1 ? length : 1), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        glDeleteProgram(program);
        throw ShaderError("Shader program link failed (" + vertex_path.string() + " + " +
                          fragment_path.string() + "):\n" + log.data());
    }
    return Shader(program);
}

Shader::Shader(Shader&& other) noexcept
    : program_(std::exchange(other.program_, 0)), locations_(std::move(other.locations_)) {}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (program_) glDeleteProgram(program_);
        program_ = std::exchange(other.program_, 0);
        locations_ = std::move(other.locations_);
    }
    return *this;
}

Shader::~Shader() {
    if (program_) glDeleteProgram(program_);
}

void Shader::use() const { glUseProgram(program_); }

int Shader::location(const std::string& name) {
    const auto it = locations_.find(name);
    if (it != locations_.end()) return it->second;
    const GLint loc = glGetUniformLocation(program_, name.c_str());
    if (loc < 0) throw ShaderError("Shader uniform not found (or optimized out): " + name);
    locations_.emplace(name, loc);
    return loc;
}

void Shader::setInt(const std::string& name, int value) { glUniform1i(location(name), value); }
void Shader::setFloat(const std::string& name, float value) { glUniform1f(location(name), value); }
void Shader::setVec2(const std::string& name, float x, float y) { glUniform2f(location(name), x, y); }
void Shader::setVec4(const std::string& name, float x, float y, float z, float w) {
    glUniform4f(location(name), x, y, z, w);
}

}  // namespace optinspect::viz
