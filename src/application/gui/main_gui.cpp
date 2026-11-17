#include "GuiApplication.hpp"
#include "GuiOptions.hpp"

#include "visualization/GLContext.hpp"
#include "visualization/Shader.hpp"
#include "visualization/Texture2D.hpp"

#include <chrono>
#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    const auto process_start = std::chrono::steady_clock::now();
    optinspect::gui::GuiOptions options;
    try {
        options = optinspect::gui::parseGuiOptions(argc, argv);
    } catch (const std::invalid_argument& e) {
        std::cerr << "Error: " << e.what() << "\n\n" << optinspect::gui::guiUsageText();
        return 64;
    }
    if (options.show_help) {
        std::cout << optinspect::gui::guiUsageText();
        return 0;
    }

    try {
        optinspect::gui::GuiApplication app(std::move(options), process_start);
        return app.run();
    } catch (const optinspect::viz::GLContextError& e) {
        std::cerr << "OpenGL initialization failed: " << e.what() << "\n";
        return 70;
    } catch (const optinspect::viz::ShaderError& e) {
        std::cerr << "Shader error: " << e.what() << "\n";
        return 70;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 70;
    }
}
