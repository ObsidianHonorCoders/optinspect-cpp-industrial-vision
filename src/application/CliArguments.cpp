#include "CliArguments.hpp"

#include <stdexcept>
#include <vector>

namespace optinspect {

std::string usageText() {
    return "Usage: optinspect --reference <path> --input <path> "
           "[--config <path>] [--output <dir>]\n"
           "\n"
           "  --reference <path>  Golden/reference PCB image, or a directory of <id>_temp.* images (required)\n"
           "  --input <path>      Inspected PCB image, or a directory of <id>_test.* images (required)\n"
           "                      Batch mode: if both are directories, each <id>_temp.* is inspected\n"
           "                      against the <id>_test.* with the same <id> (directories may be identical).\n"
           "  --config <path>     Path to inspection_config.json (default: config/inspection_config.json)\n"
           "  --output <dir>      Output directory for results (default: results/)\n"
           "  --help               Show this message\n"
           "\n"
           "Example:\n"
           "  optinspect.exe --reference data/references/reference.png "
           "--input data/samples/defective.png --config config/inspection_config.json --output results/\n"
           "  optinspect.exe --reference data/samples/deeppcb/images "
           "--input data/samples/deeppcb/images --output results/   (batch)\n";
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

    for (size_t i = 0; i < args.size(); ++i) {
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

    return result;
}

}  // namespace optinspect
