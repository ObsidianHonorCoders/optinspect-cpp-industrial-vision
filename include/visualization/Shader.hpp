#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace optinspect::viz {

class ShaderError : public std::runtime_error {
public:
    explicit ShaderError(const std::string& message) : std::runtime_error(message) {}
};

/// RAII linked GLSL program built from a vertex and a fragment shader file.
/// Compile and link failures throw ShaderError containing the file name and
/// the driver's info log; nothing is silently ignored. The individual shader
/// objects are deleted right after linking.
///
/// Requires a current OpenGL context for construction, use and destruction.
class Shader {
public:
    static Shader fromFiles(const std::filesystem::path& vertex_path,
                            const std::filesystem::path& fragment_path);

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;
    ~Shader();

    void use() const;
    unsigned int id() const { return program_; }

    /// Uniform setters. The program must be in use (use()). Unknown uniform
    /// names throw ShaderError (typos must not fail silently); a uniform the
    /// compiler optimized away is reported the same way.
    void setInt(const std::string& name, int value);
    void setFloat(const std::string& name, float value);
    void setVec2(const std::string& name, float x, float y);
    void setVec4(const std::string& name, float x, float y, float z, float w);

private:
    explicit Shader(unsigned int program) : program_(program) {}
    int location(const std::string& name);

    unsigned int program_ = 0;
    std::unordered_map<std::string, int> locations_;
};

/// Reads a whole text file; throws ShaderError when it cannot be read.
std::string readTextFile(const std::filesystem::path& path);

}  // namespace optinspect::viz
