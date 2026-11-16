#include "CliArguments.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>

namespace optinspect {

std::string usageText() {
    return "Usage: optinspect [inspect] --reference <path> --input <path> "
           "[--config <path>] [--output <dir>] [--database <path>] [--product-id <n>]\n"
           "\n"
           "  --reference <path>  Golden/reference PCB image, or a directory of <id>_temp.* images (required)\n"
           "  --input <path>      Inspected PCB image, or a directory of <id>_test.* images (required)\n"
           "                      Batch mode: if both are directories, each <id>_temp.* is inspected\n"
           "                      against the <id>_test.* with the same <id> (directories may be identical).\n"
           "  --config <path>     Path to inspection_config.json (default: config/inspection_config.json)\n"
           "  --output <dir>      Output directory for results (default: results/)\n"
           "  --help               Show this message\n"
           "\n"
           "Persistence (SQLite, optional; see 'optinspect db init'):\n"
           "  --database <path>   Store the inspection in this database (enables persistence for this run)\n"
           "  --no-database       Do not persist, even if database.enabled is true in the config\n"
           "  --product-id <n>    Product (see 'optinspect product create') the board belongs to\n"
           "  --serial <text>     Board serial number (single-image mode only)\n"
           "  --notes <text>      Free-text note stored with the inspection\n"
           "\n"
           "Database commands (run 'optinspect db|product|inspection|defect|analytics --help'):\n"
           "  db init|info        Create/upgrade or describe a database\n"
           "  product ...         Manage PCB products (create, list, show, update, delete)\n"
           "  inspection ...      Inspection history (list, show, update, delete, import)\n"
           "  defect ...          Defects of an inspection (list, note, delete)\n"
           "  analytics ...       Quality analytics (summary, defects, products, performance, trend)\n"
           "\n"
           "Exit codes (inspection): 0 PASS, 1 FAIL, 2 REVIEW, 64 usage error, 70 runtime error,\n"
           "                         74 database/persistence error (inspection files are kept).\n"
           "\n"
           "Example:\n"
           "  optinspect.exe --reference data/references/reference.png "
           "--input data/samples/defective.png --config config/inspection_config.json --output results/\n"
           "  optinspect.exe --reference data/samples/deeppcb/images "
           "--input data/samples/deeppcb/images --output results/   (batch)\n"
           "  optinspect.exe inspect --reference ref.png --input board.png --database data/optinspect.db "
           "--product-id 1 --serial SN-0001\n";
}

CliArguments parseArguments(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    CliArguments result;

    auto requireValue = [&](size_t& i, const std::string& flag) -> std::string {
        if (i + 1 >= args.size()) {
            throw std::invalid_argument("Missing value for argument: " + flag);
        }
        return args[++i];
    };

    // The explicit "inspect" word is optional so Phase 1 invocations keep working.
    const size_t first = (!args.empty() && args[0] == "inspect") ? 1 : 0;
    for (size_t i = first; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--help" || arg == "-h") {
            result.show_help = true;
        } else if (arg == "--reference") {
            result.reference_path = requireValue(i, arg);
        } else if (arg == "--input") {
            result.inspected_path = requireValue(i, arg);
        } else if (arg == "--config") {
            result.config_path = requireValue(i, arg);
        } else if (arg == "--output") {
            result.output_dir = requireValue(i, arg);
        } else if (arg == "--database") {
            result.database_path = requireValue(i, arg);
        } else if (arg == "--no-database") {
            result.no_database = true;
        } else if (arg == "--product-id") {
            result.product_id = parseInt64Option(requireValue(i, arg), arg);
        } else if (arg == "--serial") {
            result.serial_number = requireValue(i, arg);
        } else if (arg == "--notes") {
            result.notes = requireValue(i, arg);
        } else {
            throw std::invalid_argument("Unrecognized argument: " + arg);
        }
    }

    if (result.show_help) {
        return result;
    }

    if (result.reference_path.empty()) {
        throw std::invalid_argument("Missing required argument: --reference");
    }
    if (result.inspected_path.empty()) {
        throw std::invalid_argument("Missing required argument: --input");
    }
    if (result.no_database && result.database_path) {
        throw std::invalid_argument("--database and --no-database cannot be combined.");
    }

    return result;
}

OptionMap parseOptionMap(const std::vector<std::string>& args, std::size_t start,
                         const std::set<std::string>& allowed) {
    OptionMap options;
    for (std::size_t i = start; i < args.size(); ++i) {
        const std::string& flag = args[i];
        if (allowed.count(flag) == 0) {
            throw std::invalid_argument("Unrecognized argument: " + flag);
        }
        if (i + 1 >= args.size()) {
            throw std::invalid_argument("Missing value for argument: " + flag);
        }
        if (!options.emplace(flag, args[i + 1]).second) {
            throw std::invalid_argument("Argument given more than once: " + flag);
        }
        ++i;
    }
    return options;
}

std::int64_t parseInt64Option(const std::string& value, const std::string& flag) {
    char* end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(value.c_str(), &end, 10);
    if (value.empty() || end == value.c_str() || *end != '\0' || errno == ERANGE) {
        throw std::invalid_argument("Invalid integer for " + flag + ": '" + value + "'");
    }
    return static_cast<std::int64_t>(parsed);
}

int parseIntOption(const std::string& value, const std::string& flag) {
    const std::int64_t parsed = parseInt64Option(value, flag);
    if (parsed < INT32_MIN || parsed > INT32_MAX) {
        throw std::invalid_argument("Value out of range for " + flag + ": '" + value + "'");
    }
    return static_cast<int>(parsed);
}

double parseDoubleOption(const std::string& value, const std::string& flag) {
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(value.c_str(), &end);
    if (value.empty() || end == value.c_str() || *end != '\0' || errno == ERANGE) {
        throw std::invalid_argument("Invalid number for " + flag + ": '" + value + "'");
    }
    return parsed;
}

}  // namespace optinspect