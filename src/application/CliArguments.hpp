#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace optinspect {

/// Parsed command-line arguments for the optinspect inspection command
/// (`optinspect [inspect] --reference ... --input ...`).
struct CliArguments {
    std::string reference_path;
    std::string inspected_path;
    std::string config_path = "config/inspection_config.json";
    std::string output_dir = "results";
    bool show_help = false;

    /// Phase 2 persistence options. `--database` enables persistence for this
    /// run, `--no-database` disables it even when the configuration enables it.
    std::optional<std::string> database_path;
    bool no_database = false;
    std::optional<std::int64_t> product_id;
    std::optional<std::string> serial_number;
    std::optional<std::string> notes;
};

/// Parses argv into CliArguments. Throws std::invalid_argument with a
/// descriptive message if required arguments are missing or a flag is
/// unrecognized.
CliArguments parseArguments(int argc, char** argv);

/// Returns the usage/help text shown with --help or on argument errors.
std::string usageText();

/// "--flag" -> value, for the `--flag value` options of the database commands.
using OptionMap = std::map<std::string, std::string>;

/// Parses `--flag value` pairs from args[start..]. Throws std::invalid_argument
/// for a flag that is not in `allowed`, a missing value, or a repeated flag.
OptionMap parseOptionMap(const std::vector<std::string>& args, std::size_t start,
                         const std::set<std::string>& allowed);

/// Strict numeric conversions; throw std::invalid_argument naming the flag.
std::int64_t parseInt64Option(const std::string& value, const std::string& flag);
int parseIntOption(const std::string& value, const std::string& flag);
double parseDoubleOption(const std::string& value, const std::string& flag);

}  // namespace optinspect