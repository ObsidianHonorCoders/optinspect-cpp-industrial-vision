#include "DatabaseCommands.hpp"

#include "CliArguments.hpp"
#include "InspectionRecorder.hpp"
#include "common/Errors.hpp"
#include "database/DatabaseError.hpp"
#include "database/DatabaseInitializer.hpp"
#include "database/DefectRepository.hpp"
#include "database/InspectionImageRepository.hpp"
#include "database/InspectionPersistenceService.hpp"
#include "database/InspectionRepository.hpp"
#include "database/ProductRepository.hpp"
#include "database/QualityAnalyticsService.hpp"
#include "database/Statement.hpp"
#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace optinspect {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace database;

constexpr int kExitOk = 0;
constexpr int kExitUsage = 64;
constexpr int kExitNotFound = 66;
constexpr int kExitRuntime = 70;
constexpr int kExitDatabase = 74;

const char* const kDefaultDatabase = "data/optinspect.db";

struct Io {
    std::ostream& out;
    std::ostream& err;
};

// --------------------------------------------------------------------------
// Small helpers
// --------------------------------------------------------------------------

std::optional<std::string> option(const OptionMap& options, const std::string& flag) {
    const auto it = options.find(flag);
    if (it == options.end()) return std::nullopt;
    return it->second;
}

std::string requireOption(const OptionMap& options, const std::string& flag) {
    const auto value = option(options, flag);
    if (!value) throw std::invalid_argument("Missing required argument: " + flag);
    return *value;
}

std::string databasePath(const OptionMap& options) {
    return option(options, "--database").value_or(kDefaultDatabase);
}

std::string fmt(double value, int precision = 1) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

std::string percent(double fraction) { return fmt(fraction * 100.0, 1) + "%"; }

std::string orDash(const std::optional<std::string>& value) {
    return value && !value->empty() ? *value : "-";
}

std::string orDash(const std::optional<double>& value, int precision = 1) {
    return value ? fmt(*value, precision) : "-";
}

/// Empty text clears an optional field, anything else sets it.
std::optional<std::string> textOrNull(const std::string& value) {
    if (value.empty()) return std::nullopt;
    return value;
}

json jsonOrNull(const std::optional<std::string>& value) {
    return value ? json(*value) : json(nullptr);
}

json jsonOrNull(const std::optional<double>& value) { return value ? json(*value) : json(nullptr); }

json jsonOrNull(const std::optional<std::int64_t>& value) {
    return value ? json(*value) : json(nullptr);
}

void printTable(std::ostream& out, const std::vector<std::string>& headers,
                const std::vector<std::vector<std::string>>& rows) {
    std::vector<std::size_t> widths;
    for (const auto& h : headers) widths.push_back(h.size());
    for (const auto& row : rows) {
        for (std::size_t c = 0; c < row.size(); ++c) widths[c] = std::max(widths[c], row[c].size());
    }
    auto line = [&](const std::vector<std::string>& cells) {
        for (std::size_t c = 0; c < cells.size(); ++c) {
            out << std::left << std::setw(static_cast<int>(widths[c] + 2)) << cells[c];
        }
        out << "\n";
    };
    line(headers);
    std::vector<std::string> rule;
    for (std::size_t w : widths) rule.push_back(std::string(w, '-'));
    line(rule);
    for (const auto& row : rows) line(row);
}

bool wantsJson(const OptionMap& options) {
    const auto format = option(options, "--format");
    if (!format || *format == "table") return false;
    if (*format == "json") return true;
    throw std::invalid_argument("Invalid --format '" + *format + "' (expected table or json).");
}

void printJson(std::ostream& out, const json& value) { out << value.dump(2) << "\n"; }

/// Opens an existing, fully initialized database; never creates or upgrades one.
DatabaseConnection openExisting(const std::string& path) {
    if (!fs::exists(path)) {
        throw DatabaseError("Database file not found: " + path +
                            ". Create it with: optinspect db init --database " + path);
    }
    DatabaseConnection connection = DatabaseConnection::open(path, OpenMode::ReadWrite);
    DatabaseInitializer::requireCurrentSchema(connection);
    return connection;
}

InspectionFilter filterFromOptions(const OptionMap& options) {
    InspectionFilter filter;
    if (const auto v = option(options, "--product-id")) {
        filter.product_id = parseInt64Option(*v, "--product-id");
    }
    filter.serial_number = option(options, "--serial");
    if (const auto v = option(options, "--status")) filter.status = parseInspectionStatus(*v);
    filter.from = option(options, "--from");
    filter.to = option(options, "--to");
    if (const auto v = option(options, "--limit")) filter.limit = parseIntOption(*v, "--limit");
    return filter;
}

// --------------------------------------------------------------------------
// JSON conversion
// --------------------------------------------------------------------------

json toJson(const Product& p) {
    return {{"id", p.id},
            {"code", p.code},
            {"name", p.name},
            {"model", jsonOrNull(p.model)},
            {"revision", jsonOrNull(p.revision)},
            {"description", jsonOrNull(p.description)},
            {"created_at", p.created_at}};
}

json toJson(const InspectionRecord& r) {
    json j = {{"id", r.id},
              {"external_id", jsonOrNull(r.external_id)},
              {"product_id", jsonOrNull(r.product_id)},
              {"serial_number", jsonOrNull(r.serial_number)},
              {"timestamp", r.inspection_timestamp},
              {"status", toString(r.status)},
              {"decision_reason", jsonOrNull(r.decision_reason)},
              {"alignment_status",
               r.alignment_status ? json(toString(*r.alignment_status)) : json(nullptr)},
              {"alignment_confidence", jsonOrNull(r.alignment_confidence)},
              {"report_path", jsonOrNull(r.report_path)},
              {"defect_count", r.detected_defect_count},
              {"processing_time_ms", jsonOrNull(r.processing_time_ms)},
              {"configuration_id", jsonOrNull(r.configuration_id)},
              {"notes", jsonOrNull(r.notes)},
              {"created_at", r.created_at}};
    return j;
}

json toJson(const DefectRecord& d) {
    return {{"id", d.id},
            {"inspection_id", d.inspection_id},
            {"x", d.x},
            {"y", d.y},
            {"width", d.width},
            {"height", d.height},
            {"area", d.area},
            {"perimeter", jsonOrNull(d.perimeter)},
            {"centroid_x", jsonOrNull(d.centroid_x)},
            {"centroid_y", jsonOrNull(d.centroid_y)},
            {"aspect_ratio", jsonOrNull(d.aspect_ratio)},
            {"confidence", jsonOrNull(d.confidence)},
            {"review_note", jsonOrNull(d.review_note)}};
}

json toJson(const InspectionImage& img) {
    return {{"id", img.id},
            {"type", toString(img.type)},
            {"file_path", img.file_path},
            {"width", img.width ? json(*img.width) : json(nullptr)},
            {"height", img.height ? json(*img.height) : json(nullptr)}};
}

json toJson(const ProcessingTimeStats& s) {
    return {{"samples", s.samples},
            {"avg_ms", jsonOrNull(s.avg_ms)},
            {"min_ms", jsonOrNull(s.min_ms)},
            {"max_ms", jsonOrNull(s.max_ms)}};
}

// --------------------------------------------------------------------------
// db
// --------------------------------------------------------------------------

int handleDb(const std::vector<std::string>& args, Io io) {
    const std::string& action = args[1];
    const OptionMap options = parseOptionMap(args, 2, {"--database"});
    const std::string path = databasePath(options);

    if (action == "init") {
        ensureParentDirectory(path);
        DatabaseConnection connection = DatabaseConnection::open(path);
        const InitializationResult result = DatabaseInitializer::initialize(connection);

        io.out << "Database       : " << path << "\n";
        io.out << "SQLite version : " << sqliteLibraryVersion() << "\n";
        io.out << "Schema version : " << result.current_version << "\n";
        if (result.created) {
            io.out << "Result         : created schema\n";
        } else if (!result.applied_versions.empty()) {
            io.out << "Result         : upgraded from version " << result.previous_version << "\n";
        } else {
            io.out << "Result         : already up to date (no changes made)\n";
        }
        return kExitOk;
    }

    if (action == "info") {
        if (!fs::exists(path)) throw DatabaseError("Database file not found: " + path);
        DatabaseConnection connection = DatabaseConnection::open(path, OpenMode::ReadOnly);
        const int version = DatabaseInitializer::schemaVersion(connection);

        io.out << "Database       : " << fs::absolute(path).string() << "\n";
        io.out << "File size      : " << fs::file_size(path) << " bytes\n";
        io.out << "SQLite version : " << sqliteLibraryVersion() << "\n";
        io.out << "Schema version : " << version << " (this build expects "
               << DatabaseInitializer::latestVersion() << ")\n";
        io.out << "Journal mode   : " << connection.pragmaValue("journal_mode") << "\n";
        if (version == DatabaseInitializer::latestVersion()) {
            for (const char* table :
                 {"products", "configurations", "inspections", "defects", "inspection_images"}) {
                Statement stmt(connection, std::string("SELECT COUNT(*) FROM ") + table);
                stmt.step();
                io.out << "Rows in " << std::left << std::setw(18) << table << ": "
                       << stmt.columnInt64(0) << "\n";
            }
        }
        return kExitOk;
    }

    throw std::invalid_argument("Unknown db command: " + action);
}

// --------------------------------------------------------------------------
// product
// --------------------------------------------------------------------------

void printProduct(std::ostream& out, const Product& p, int inspections) {
    out << "Product " << p.id << "\n";
    out << "  Code        : " << p.code << "\n";
    out << "  Name        : " << p.name << "\n";
    out << "  Model       : " << orDash(p.model) << "\n";
    out << "  Revision    : " << orDash(p.revision) << "\n";
    out << "  Description : " << orDash(p.description) << "\n";
    out << "  Created     : " << p.created_at << "\n";
    out << "  Inspections : " << inspections << "\n";
}

int handleProduct(const std::vector<std::string>& args, Io io) {
    const std::string& action = args[1];

    if (action == "create") {
        const OptionMap o = parseOptionMap(
            args, 2, {"--database", "--code", "--name", "--model", "--revision", "--description"});
        DatabaseConnection connection = openExisting(databasePath(o));
        Product product;
        product.code = requireOption(o, "--code");
        product.name = requireOption(o, "--name");
        product.model = option(o, "--model");
        product.revision = option(o, "--revision");
        product.description = option(o, "--description");
        const std::int64_t id = ProductRepository(connection).create(product);
        io.out << "Product created: id=" << id << " code=" << product.code << "\n";
        return kExitOk;
    }

    if (action == "list") {
        const OptionMap o = parseOptionMap(args, 2, {"--database", "--search", "--limit", "--format"});
        DatabaseConnection connection = openExisting(databasePath(o));
        ProductFilter filter;
        filter.search = option(o, "--search");
        if (const auto v = option(o, "--limit")) filter.limit = parseIntOption(*v, "--limit");
        ProductRepository repo(connection);
        const auto products = repo.list(filter);

        if (wantsJson(o)) {
            json array = json::array();
            for (const auto& p : products) array.push_back(toJson(p));
            printJson(io.out, array);
            return kExitOk;
        }
        std::vector<std::vector<std::string>> rows;
        for (const auto& p : products) {
            rows.push_back({std::to_string(p.id), p.code, p.name, orDash(p.model), orDash(p.revision),
                            std::to_string(repo.countInspections(p.id))});
        }
        printTable(io.out, {"ID", "Code", "Name", "Model", "Revision", "Inspections"}, rows);
        io.out << products.size() << " product(s)\n";
        return kExitOk;
    }

    if (action == "show" || action == "update" || action == "delete") {
        const OptionMap o = parseOptionMap(
            args, 2,
            action == "show"
                ? std::set<std::string>{"--database", "--id", "--code", "--format"}
                : action == "update"
                      ? std::set<std::string>{"--database", "--id", "--name", "--model", "--revision",
                                              "--description"}
                      : std::set<std::string>{"--database", "--id"});
        DatabaseConnection connection = openExisting(databasePath(o));
        ProductRepository repo(connection);

        std::optional<Product> product;
        if (const auto id = option(o, "--id")) {
            product = repo.getById(parseInt64Option(*id, "--id"));
        } else if (const auto code = option(o, "--code")) {
            product = repo.getByCode(*code);
        } else {
            throw std::invalid_argument("Specify --id" +
                                        std::string(action == "show" ? " or --code" : ""));
        }
        if (!product) {
            io.err << "Error: product not found.\n";
            return kExitNotFound;
        }

        if (action == "show") {
            const int inspections = repo.countInspections(product->id);
            if (wantsJson(o)) {
                json j = toJson(*product);
                j["inspection_count"] = inspections;
                printJson(io.out, j);
            } else {
                printProduct(io.out, *product, inspections);
            }
            return kExitOk;
        }

        if (action == "update") {
            if (const auto v = option(o, "--name")) product->name = *v;
            if (const auto v = option(o, "--model")) product->model = textOrNull(*v);
            if (const auto v = option(o, "--revision")) product->revision = textOrNull(*v);
            if (const auto v = option(o, "--description")) product->description = textOrNull(*v);
            repo.update(*product);
            io.out << "Product " << product->id << " updated.\n";
            return kExitOk;
        }

        repo.remove(product->id);
        io.out << "Product " << product->id << " (" << product->code << ") deleted.\n";
        return kExitOk;
    }

    throw std::invalid_argument("Unknown product command: " + action);
}

// --------------------------------------------------------------------------
// inspection
// --------------------------------------------------------------------------

int handleInspection(const std::vector<std::string>& args, Io io) {
    const std::string& action = args[1];

    if (action == "list") {
        const OptionMap o = parseOptionMap(args, 2,
                                           {"--database", "--product-id", "--serial", "--status",
                                            "--from", "--to", "--limit", "--format"});
        DatabaseConnection connection = openExisting(databasePath(o));
        InspectionFilter filter = filterFromOptions(o);
        if (!filter.limit) filter.limit = 50;

        InspectionRepository repo(connection);
        const auto records = repo.list(filter);

        if (wantsJson(o)) {
            json array = json::array();
            for (const auto& r : records) array.push_back(toJson(r));
            printJson(io.out, array);
            return kExitOk;
        }
        std::vector<std::vector<std::string>> rows;
        for (const auto& r : records) {
            rows.push_back({std::to_string(r.id), r.inspection_timestamp, toString(r.status),
                            r.product_id ? std::to_string(*r.product_id) : "-", orDash(r.serial_number),
                            std::to_string(r.detected_defect_count), orDash(r.processing_time_ms),
                            orDash(r.external_id)});
        }
        printTable(io.out, {"ID", "Timestamp (UTC)", "Status", "Product", "Serial", "Defects", "Time (ms)",
                            "Inspection id"},
                   rows);
        InspectionFilter count_filter = filter;
        count_filter.limit.reset();
        const int total = repo.count(count_filter);
        io.out << records.size() << " of " << total << " matching inspection(s) shown\n";
        return kExitOk;
    }

    if (action == "show" || action == "delete") {
        const OptionMap o = parseOptionMap(
            args, 2,
            action == "show" ? std::set<std::string>{"--database", "--id", "--format"}
                             : std::set<std::string>{"--database", "--id"});
        DatabaseConnection connection = openExisting(databasePath(o));
        const std::int64_t id = parseInt64Option(requireOption(o, "--id"), "--id");
        InspectionRepository repo(connection);

        if (action == "delete") {
            if (!InspectionPersistenceService(connection).deleteInspection(id)) {
                io.err << "Error: inspection " << id << " not found.\n";
                return kExitNotFound;
            }
            io.out << "Inspection " << id
                   << " deleted with its defect and image records (files on disk were not touched).\n";
            return kExitOk;
        }

        const auto record = repo.getById(id);
        if (!record) {
            io.err << "Error: inspection " << id << " not found.\n";
            return kExitNotFound;
        }
        const auto images = InspectionImageRepository(connection).listByInspection(id);

        std::optional<Product> product;
        if (record->product_id) product = ProductRepository(connection).getById(*record->product_id);

        if (wantsJson(o)) {
            json j = toJson(*record);
            if (product) j["product"] = toJson(*product);
            j["images"] = json::array();
            for (const auto& img : images) j["images"].push_back(toJson(img));
            j["defects"] = json::array();
            for (const auto& d : DefectRepository(connection).listByInspection(id)) {
                j["defects"].push_back(toJson(d));
            }
            if (record->configuration_snapshot) {
                j["configuration"] = json::parse(*record->configuration_snapshot, nullptr, false);
            }
            printJson(io.out, j);
            return kExitOk;
        }

        io.out << "Inspection " << record->id << "\n";
        io.out << "  Inspection id : " << orDash(record->external_id) << "\n";
        io.out << "  Product       : "
               << (product ? product->code + " (" + product->name + ")" : std::string("-")) << "\n";
        io.out << "  Serial number : " << orDash(record->serial_number) << "\n";
        io.out << "  Timestamp     : " << record->inspection_timestamp << " (UTC)\n";
        io.out << "  Status        : " << toString(record->status) << "\n";
        io.out << "  Reason        : " << orDash(record->decision_reason) << "\n";
        io.out << "  Alignment     : "
               << (record->alignment_status ? toString(*record->alignment_status) : "-")
               << " (confidence " << orDash(record->alignment_confidence, 3) << ")\n";
        io.out << "  Defects       : " << record->detected_defect_count << "\n";
        io.out << "  Processing    : " << orDash(record->processing_time_ms) << " ms\n";
        io.out << "  Report        : " << orDash(record->report_path) << "\n";
        io.out << "  Notes         : " << orDash(record->notes) << "\n";
        io.out << "  Configuration : "
               << (record->configuration_id ? "snapshot #" + std::to_string(*record->configuration_id)
                                            : std::string("-"))
               << "\n";
        io.out << "  Images:\n";
        for (const auto& img : images) {
            io.out << "    " << std::left << std::setw(11) << toString(img.type) << img.file_path;
            if (img.width && img.height) io.out << " (" << *img.width << "x" << *img.height << ")";
            io.out << "\n";
        }
        if (images.empty()) io.out << "    -\n";
        io.out << "  (use 'optinspect defect list --inspection-id " << id << "' for the defects)\n";
        return kExitOk;
    }

    if (action == "update") {
        const OptionMap o = parseOptionMap(
            args, 2, {"--database", "--id", "--product-id", "--serial", "--notes", "--report-path"});
        DatabaseConnection connection = openExisting(databasePath(o));
        InspectionRepository repo(connection);
        auto record = repo.getById(parseInt64Option(requireOption(o, "--id"), "--id"));
        if (!record) {
            io.err << "Error: inspection not found.\n";
            return kExitNotFound;
        }
        if (const auto v = option(o, "--product-id")) {
            record->product_id = *v == "none" ? std::nullopt
                                              : std::optional<std::int64_t>(
                                                    parseInt64Option(*v, "--product-id"));
        }
        if (const auto v = option(o, "--serial")) record->serial_number = textOrNull(*v);
        if (const auto v = option(o, "--notes")) record->notes = textOrNull(*v);
        if (const auto v = option(o, "--report-path")) record->report_path = textOrNull(*v);
        repo.updateMetadata(*record);
        io.out << "Inspection " << record->id << " updated (decision and measurements are immutable).\n";
        return kExitOk;
    }

    if (action == "import") {
        const OptionMap o = parseOptionMap(
            args, 2, {"--database", "--report", "--product-id", "--serial", "--notes"});
        const std::string report = requireOption(o, "--report");
        PersistenceOptions options;
        if (const auto v = option(o, "--product-id")) {
            options.product_id = parseInt64Option(*v, "--product-id");
        }
        options.serial_number = option(o, "--serial");
        options.notes = option(o, "--notes");

        const auto request = buildRequestFromReport(report, options);

        InspectionRecorder recorder(databasePath(o), /*auto_initialize=*/true);
        if (options.product_id && !recorder.productExists(*options.product_id)) {
            io.err << "Error: product " << *options.product_id << " not found.\n";
            return kExitNotFound;
        }
        const std::string external_id = request.inspection.external_id.value_or("");
        if (const auto existing = recorder.findByExternalId(external_id)) {
            io.out << "Inspection '" << external_id << "' is already stored as inspection " << *existing
                   << "; nothing to do.\n";
            return kExitOk;
        }
        const std::int64_t id = recorder.record(request);
        io.out << "Imported inspection " << id << " from " << report << " ("
               << toString(request.inspection.status) << ", " << request.defects.size()
               << " defect(s))\n";
        return kExitOk;
    }

    throw std::invalid_argument("Unknown inspection command: " + action);
}

// --------------------------------------------------------------------------
// defect
// --------------------------------------------------------------------------

int handleDefect(const std::vector<std::string>& args, Io io) {
    const std::string& action = args[1];

    if (action == "list") {
        const OptionMap o = parseOptionMap(args, 2,
                                           {"--database", "--inspection-id", "--min-area", "--max-area",
                                            "--min-confidence", "--max-confidence", "--limit",
                                            "--format"});
        DatabaseConnection connection = openExisting(databasePath(o));
        DefectFilter filter;
        if (const auto v = option(o, "--inspection-id")) {
            filter.inspection_id = parseInt64Option(*v, "--inspection-id");
        }
        if (const auto v = option(o, "--min-area")) filter.min_area = parseDoubleOption(*v, "--min-area");
        if (const auto v = option(o, "--max-area")) filter.max_area = parseDoubleOption(*v, "--max-area");
        if (const auto v = option(o, "--min-confidence")) {
            filter.min_confidence = parseDoubleOption(*v, "--min-confidence");
        }
        if (const auto v = option(o, "--max-confidence")) {
            filter.max_confidence = parseDoubleOption(*v, "--max-confidence");
        }
        if (const auto v = option(o, "--limit")) filter.limit = parseIntOption(*v, "--limit");

        const auto defects = DefectRepository(connection).list(filter);
        if (wantsJson(o)) {
            json array = json::array();
            for (const auto& d : defects) array.push_back(toJson(d));
            printJson(io.out, array);
            return kExitOk;
        }
        std::vector<std::vector<std::string>> rows;
        for (const auto& d : defects) {
            rows.push_back({std::to_string(d.id), std::to_string(d.inspection_id), std::to_string(d.x),
                            std::to_string(d.y), std::to_string(d.width), std::to_string(d.height),
                            fmt(d.area, 1), orDash(d.aspect_ratio, 2), orDash(d.confidence, 3),
                            orDash(d.review_note)});
        }
        printTable(io.out, {"ID", "Insp.", "X", "Y", "W", "H", "Area px", "Aspect", "Score", "Note"}, rows);
        io.out << defects.size() << " defect region(s)\n";
        return kExitOk;
    }

    if (action == "note" || action == "delete") {
        const OptionMap o = parseOptionMap(args, 2,
                                           action == "note"
                                               ? std::set<std::string>{"--database", "--id", "--note"}
                                               : std::set<std::string>{"--database", "--id"});
        DatabaseConnection connection = openExisting(databasePath(o));
        const std::int64_t id = parseInt64Option(requireOption(o, "--id"), "--id");
        DefectRepository repo(connection);
        const bool found = action == "note"
                               ? repo.updateReviewNote(id, textOrNull(requireOption(o, "--note")))
                               : repo.remove(id);
        if (!found) {
            io.err << "Error: defect " << id << " not found.\n";
            return kExitNotFound;
        }
        io.out << "Defect " << id << (action == "note" ? " note updated.\n" : " deleted.\n");
        return kExitOk;
    }

    throw std::invalid_argument("Unknown defect command: " + action);
}

// --------------------------------------------------------------------------
// analytics
// --------------------------------------------------------------------------

std::string filterDescription(const InspectionFilter& f) {
    std::vector<std::string> parts;
    if (f.product_id) parts.push_back("product " + std::to_string(*f.product_id));
    if (f.serial_number) parts.push_back("serial " + *f.serial_number);
    if (f.status) parts.push_back("status " + toString(*f.status));
    if (f.from) parts.push_back("from " + *f.from);
    if (f.to) parts.push_back("to " + *f.to);
    if (parts.empty()) return "all inspections";
    std::string text;
    for (std::size_t i = 0; i < parts.size(); ++i) text += (i ? ", " : "") + parts[i];
    return text;
}

std::vector<std::string> timeRow(const std::string& label, const ProcessingTimeStats& s) {
    return {label, std::to_string(s.samples), orDash(s.avg_ms), orDash(s.min_ms), orDash(s.max_ms)};
}

int handleAnalytics(const std::vector<std::string>& args, Io io) {
    const std::string& action = args[1];
    std::set<std::string> allowed = {"--database", "--product-id", "--serial", "--status",
                                     "--from",     "--to",         "--format"};
    if (action == "products") allowed.insert("--min-inspections");
    if (action == "trend") allowed.insert("--period");
    if (action != "summary" && action != "defects" && action != "products" &&
        action != "performance" && action != "trend") {
        throw std::invalid_argument("Unknown analytics command: " + action);
    }

    const OptionMap o = parseOptionMap(args, 2, allowed);
    DatabaseConnection connection = openExisting(databasePath(o));
    const InspectionFilter filter = filterFromOptions(o);
    const bool as_json = wantsJson(o);
    QualityAnalyticsService analytics(connection);

    if (action == "summary") {
        const InspectionSummary s = analytics.summary(filter);
        if (as_json) {
            printJson(io.out, {{"total", s.total}, {"pass", s.pass}, {"fail", s.fail},
                               {"review", s.review}, {"pass_rate", s.pass_rate},
                               {"fail_rate", s.fail_rate}, {"review_rate", s.review_rate}});
            return kExitOk;
        }
        io.out << "Inspection summary (" << filterDescription(filter) << ")\n";
        printTable(io.out, {"Status", "Count", "Rate"},
                   {{"PASS", std::to_string(s.pass), percent(s.pass_rate)},
                    {"FAIL", std::to_string(s.fail), percent(s.fail_rate)},
                    {"REVIEW", std::to_string(s.review), percent(s.review_rate)},
                    {"TOTAL", std::to_string(s.total), s.total ? "100.0%" : "-"}});
        io.out << "Rates use all inspections in scope as the denominator.\n";
        return kExitOk;
    }

    if (action == "defects") {
        const DefectStatistics s = analytics.defectStatistics(filter);
        if (as_json) {
            json bands = json::array();
            for (const auto& b : s.by_confidence_band) {
                bands.push_back({{"band", b.band}, {"count", b.count}});
            }
            printJson(io.out, {{"inspections", s.inspections},
                               {"total_defects", s.total_defects},
                               {"avg_defects_per_inspection", s.avg_defects_per_inspection},
                               {"max_defects_in_inspection", s.max_defects_in_inspection},
                               {"avg_defect_area_px", jsonOrNull(s.avg_defect_area_px)},
                               {"by_confidence_band", bands}});
            return kExitOk;
        }
        io.out << "Defect statistics (" << filterDescription(filter) << ")\n";
        io.out << "  Inspections in scope        : " << s.inspections << "\n";
        io.out << "  Total detected regions      : " << s.total_defects << "\n";
        io.out << "  Average regions/inspection  : " << fmt(s.avg_defects_per_inspection, 2) << "\n";
        io.out << "  Max regions in one inspection: " << s.max_defects_in_inspection << "\n";
        io.out << "  Average region area         : " << orDash(s.avg_defect_area_px) << " px\n";
        std::vector<std::vector<std::string>> rows;
        for (const auto& b : s.by_confidence_band) rows.push_back({b.band, std::to_string(b.count)});
        io.out << "\nRegions by Phase 1 score band (LOW < 0.33, MEDIUM < 0.66, HIGH >= 0.66;\n"
                  "the score is area-based, not a defect class):\n";
        printTable(io.out, {"Band", "Regions"}, rows);
        return kExitOk;
    }

    if (action == "products") {
        int min_inspections = 1;
        if (const auto v = option(o, "--min-inspections")) {
            min_inspections = parseIntOption(*v, "--min-inspections");
        }
        const auto products = analytics.productQuality(filter, min_inspections);
        if (as_json) {
            json array = json::array();
            for (const auto& p : products) {
                array.push_back({{"product_id", jsonOrNull(p.product_id)},
                                 {"product_code", p.product_code},
                                 {"product_name", p.product_name},
                                 {"total", p.total},
                                 {"failed", p.failed},
                                 {"review", p.review},
                                 {"failure_rate", p.failure_rate},
                                 {"avg_processing_time_ms", jsonOrNull(p.avg_processing_time_ms)}});
            }
            printJson(io.out, array);
            return kExitOk;
        }
        io.out << "Product quality (" << filterDescription(filter) << ")\n";
        std::vector<std::vector<std::string>> rows;
        for (const auto& p : products) {
            rows.push_back({p.product_code, p.product_name, std::to_string(p.total),
                            std::to_string(p.failed), std::to_string(p.review), percent(p.failure_rate),
                            orDash(p.avg_processing_time_ms)});
        }
        printTable(io.out, {"Product", "Name", "Inspections", "Failed", "Review", "Fail rate", "Avg ms"},
                   rows);
        return kExitOk;
    }

    if (action == "performance") {
        const ProcessingTimeStats overall = analytics.processingTime(filter);
        const auto by_status = analytics.processingTimeByStatus(filter);
        const auto by_product = analytics.processingTimeByProduct(filter);
        if (as_json) {
            json status = json::array();
            for (const auto& g : by_status) {
                status.push_back({{"status", g.group}, {"stats", toJson(g.stats)}});
            }
            json product = json::array();
            for (const auto& g : by_product) {
                product.push_back({{"product", g.group}, {"stats", toJson(g.stats)}});
            }
            printJson(io.out, {{"overall", toJson(overall)}, {"by_status", status}, {"by_product", product}});
            return kExitOk;
        }
        io.out << "Processing time in ms (" << filterDescription(filter) << ")\n";
        const std::vector<std::string> headers = {"Group", "Samples", "Avg", "Min", "Max"};
        std::vector<std::vector<std::string>> rows = {timeRow("ALL", overall)};
        for (const auto& g : by_status) rows.push_back(timeRow("status " + g.group, g.stats));
        for (const auto& g : by_product) rows.push_back(timeRow("product " + g.group, g.stats));
        printTable(io.out, headers, rows);
        return kExitOk;
    }

    // trend
    const std::string period = option(o, "--period").value_or("day");
    TimeGranularity granularity = TimeGranularity::Day;
    if (period == "week") {
        granularity = TimeGranularity::Week;
    } else if (period == "month") {
        granularity = TimeGranularity::Month;
    } else if (period != "day") {
        throw std::invalid_argument("Invalid --period '" + period + "' (expected day, week or month).");
    }
    const auto series = analytics.timeSeries(granularity, filter);
    if (as_json) {
        json array = json::array();
        for (const auto& p : series) {
            array.push_back({{"period", p.period},
                             {"total", p.total},
                             {"pass", p.pass},
                             {"fail", p.fail},
                             {"review", p.review},
                             {"fail_rate", p.fail_rate},
                             {"total_defects", p.total_defects},
                             {"avg_defects_per_inspection", p.avg_defects_per_inspection},
                             {"cumulative_inspections", p.cumulative_inspections},
                             {"defects_per_inspection_change",
                              jsonOrNull(p.defects_per_inspection_change)}});
        }
        printJson(io.out, array);
        return kExitOk;
    }
    io.out << "Inspections per " << period << " (" << filterDescription(filter) << ")\n";
    std::vector<std::vector<std::string>> rows;
    for (const auto& p : series) {
        rows.push_back({p.period, std::to_string(p.total), std::to_string(p.pass),
                        std::to_string(p.fail), std::to_string(p.review), percent(p.fail_rate),
                        fmt(p.avg_defects_per_inspection, 2),
                        p.defects_per_inspection_change ? fmt(*p.defects_per_inspection_change, 2) : "-",
                        std::to_string(p.cumulative_inspections)});
    }
    printTable(io.out, {"Period", "Total", "Pass", "Fail", "Review", "Fail rate", "Defects/insp.",
                        "Change", "Cumulative"},
               rows);
    return kExitOk;
}

}  // namespace

bool isDatabaseCommand(const std::string& word) {
    return word == "db" || word == "product" || word == "inspection" || word == "defect" ||
           word == "analytics";
}

std::string databaseUsageText() {
    return "Database commands (all accept --database <path>, default data/optinspect.db):\n"
           "\n"
           "  optinspect db init\n"
           "  optinspect db info\n"
           "\n"
           "  optinspect product create --code <code> --name <name> [--model <m>] [--revision <r>] [--description <d>]\n"
           "  optinspect product list [--search <text>] [--limit <n>] [--format table|json]\n"
           "  optinspect product show (--id <n> | --code <code>) [--format table|json]\n"
           "  optinspect product update --id <n> [--name <n>] [--model <m>] [--revision <r>] [--description <d>]\n"
           "  optinspect product delete --id <n>          (refused while inspections reference it)\n"
           "\n"
           "  optinspect inspection list [--product-id <n>] [--serial <s>] [--status PASS|FAIL|REVIEW]\n"
           "                             [--from <date>] [--to <date>] [--limit <n>] [--format table|json]\n"
           "  optinspect inspection show --id <n> [--format table|json]\n"
           "  optinspect inspection update --id <n> [--product-id <n>|none] [--serial <s>] [--notes <t>] [--report-path <p>]\n"
           "  optinspect inspection delete --id <n>       (removes defect/image rows; files are kept)\n"
           "  optinspect inspection import --report <report.json> [--product-id <n>] [--serial <s>] [--notes <t>]\n"
           "                                              (retry persistence from a Phase 1 report)\n"
           "\n"
           "  optinspect defect list [--inspection-id <n>] [--min-area <px>] [--max-area <px>]\n"
           "                         [--min-confidence <x>] [--max-confidence <x>] [--limit <n>] [--format table|json]\n"
           "  optinspect defect note --id <n> --note <text>\n"
           "  optinspect defect delete --id <n>\n"
           "\n"
           "  optinspect analytics summary|defects|products|performance|trend\n"
           "                         [--product-id <n>] [--serial <s>] [--status <s>] [--from <date>] [--to <date>]\n"
           "                         [--format table|json]   (products: --min-inspections <n>; trend: --period day|week|month)\n"
           "\n"
           "Dates are YYYY-MM-DD (a --to date includes that whole day) or YYYY-MM-DDTHH:MM:SSZ (UTC).\n";
}

int runDatabaseCommand(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    const Io io{out, err};
    try {
        const bool help = std::any_of(args.begin(), args.end(), [](const std::string& a) {
            return a == "--help" || a == "-h";
        });
        if (help) {
            out << databaseUsageText();
            return kExitOk;
        }
        if (args.size() < 2) {
            throw std::invalid_argument("Missing command for '" + args.at(0) + "'.");
        }

        const std::string& group = args[0];
        if (group == "db") return handleDb(args, io);
        if (group == "product") return handleProduct(args, io);
        if (group == "inspection") return handleInspection(args, io);
        if (group == "defect") return handleDefect(args, io);
        if (group == "analytics") return handleAnalytics(args, io);
        throw std::invalid_argument("Unknown command: " + group);
    } catch (const std::invalid_argument& e) {
        err << "Error: " << e.what() << "\n\n" << databaseUsageText();
        return kExitUsage;
    } catch (const ConstraintViolationError& e) {
        err << "Error: " << e.what() << "\n";
        return kExitDatabase;
    } catch (const DatabaseError& e) {
        err << "Database error: " << e.what() << "\n";
        return kExitDatabase;
    } catch (const std::exception& e) {
        err << "Error: " << e.what() << "\n";
        return kExitRuntime;
    }
}

}  // namespace optinspect
