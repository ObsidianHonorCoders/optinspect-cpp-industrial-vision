#pragma once

#include <optional>
#include <string>

namespace optinspect {

/// Parsed command-line arguments for the optinspect executable.
struct CliArguments {
    std::string reference_path;
    std::string inspected_path;
    std::string config_path = "config/inspection_config.json";
    std::string output_dir = "results";
    bool show_help = false;
};

/// Parses argv into CliArguments. Throws std::invalid_argument with a
/// descriptive message if required arguments are missing or a flag is
/// unrecognized.
CliArguments parseArguments(int argc, char** argv);

/// Returns the usage/help text shown with --help or on argument errors.
std::string usageText();

}  // namespace optinspect
