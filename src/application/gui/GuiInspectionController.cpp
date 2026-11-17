#include "GuiInspectionController.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace optinspect::gui {

namespace fs = std::filesystem;

namespace {

std::optional<std::int64_t> parseId(const std::string& text, const char* what) {
    std::string trimmed = text;
    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char c) { return !std::isspace(c); }));
    trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char c) { return !std::isspace(c); }).base(),
                  trimmed.end());
    if (trimmed.empty()) return std::nullopt;
    try {
        std::size_t used = 0;
        const long long value = std::stoll(trimmed, &used);
        if (used != trimmed.size() || value <= 0) throw std::invalid_argument("");
        return static_cast<std::int64_t>(value);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string(what) + " must be a positive whole number.");
    }
}

std::optional<std::string> nonEmpty(const std::string& s) {
    if (s.empty()) return std::nullopt;
    return s;
}

}  // namespace

GuiInspectionController::GuiInspectionController(CachedKeysProvider cached_keys, PerformanceMetrics* metrics)
    : cached_keys_provider_(std::move(cached_keys)), metrics_(metrics) {}

std::unordered_set<std::string> GuiInspectionController::cachedKeys() const {
    return cached_keys_provider_ ? cached_keys_provider_() : std::unordered_set<std::string>{};
}

void GuiInspectionController::addLog(LogLevel level, const std::string& text) {
    log_.push_back({level, text});
    if (log_.size() > 500) log_.erase(log_.begin(), log_.begin() + 100);
    if (level != LogLevel::Info) std::cerr << (level == LogLevel::Error ? "[error] " : "[warning] ") << text << "\n";
}

std::string GuiInspectionController::validateSettings() const {
    std::error_code ec;
    if (settings_.reference_path.empty()) return "Select a reference image.";
    if (!fs::is_regular_file(settings_.reference_path, ec)) return "Reference image not found: " + settings_.reference_path;
    if (settings_.inspected_path.empty()) return "Select an inspected image.";
    if (!fs::is_regular_file(settings_.inspected_path, ec)) return "Inspected image not found: " + settings_.inspected_path;
    if (settings_.config_path.empty() || !fs::is_regular_file(settings_.config_path, ec)) {
        return "Configuration file not found: " + settings_.config_path;
    }
    if (settings_.output_dir.empty()) return "Select an output directory.";
    if (fs::exists(settings_.output_dir, ec) && !fs::is_directory(settings_.output_dir, ec)) {
        return "Output path is not a directory: " + settings_.output_dir;
    }
    try {
        parseId(settings_.product_id_text, "Product ID");
    } catch (const std::invalid_argument& e) {
        return e.what();
    }
    if (settings_.persist && settings_.database_path.empty()) return "Select a database path or disable persistence.";
    return {};
}

bool GuiInspectionController::runInspection() {
    if (worker_.busy()) {
        addLog(LogLevel::Warning, "An inspection is already running.");
        return false;
    }
    const std::string problem = validateSettings();
    if (!problem.empty()) {
        last_inspection_error_ = problem;
        addLog(LogLevel::Error, problem);
        return false;
    }

    InspectionRequest request;
    request.reference_path = settings_.reference_path;
    request.inspected_path = settings_.inspected_path;
    request.config_path = settings_.config_path;
    request.output_dir = settings_.output_dir;
    request.persist = settings_.persist;
    request.database_path = settings_.database_path;
    request.product_id = parseId(settings_.product_id_text, "Product ID");
    request.serial_number = nonEmpty(settings_.serial_number);
    request.notes = nonEmpty(settings_.notes);
    request.cached_keys = cachedKeys();

    last_inspection_error_.clear();
    last_persistence_error_.clear();
    if (!worker_.submit(std::move(request))) return false;
    addLog(LogLevel::Info, "Inspection started: " + settings_.reference_path + " vs " + settings_.inspected_path);
    return true;
}

database::InspectionFilter GuiInspectionController::toDatabaseFilter(const FilterSettings& s, bool apply_limit) {
    database::InspectionFilter filter;
    switch (s.status) {
        case 1: filter.status = database::InspectionStatus::PASS; break;
        case 2: filter.status = database::InspectionStatus::FAIL; break;
        case 3: filter.status = database::InspectionStatus::REVIEW; break;
        default: break;
    }
    filter.product_id = parseId(s.product_id_text, "Product ID");
    filter.serial_number = nonEmpty(s.serial_number);
    filter.from = nonEmpty(s.date_from);
    filter.to = nonEmpty(s.date_to);
    if (apply_limit) filter.limit = std::max(1, s.limit);
    return filter;
}

bool GuiInspectionController::refreshHistory() {
    if (history_task_.busy()) return false;
    HistoryQuery query;
    try {
        query.filter = toDatabaseFilter(history_filter_, true);
    } catch (const std::invalid_argument& e) {
        history_error_ = e.what();
        return false;
    }
    query.database_path = settings_.database_path;
    query.id_search = history_filter_.id_search;
    history_error_.clear();
    return history_task_.start([query]() { return queryHistory(query); });
}

bool GuiInspectionController::refreshAnalytics() {
    if (analytics_task_.busy()) return false;
    AnalyticsQuery query;
    try {
        query.filter = toDatabaseFilter(analytics_filter_, false);
    } catch (const std::invalid_argument& e) {
        analytics_error_ = e.what();
        return false;
    }
    query.database_path = settings_.database_path;
    query.granularity = analytics_filter_.granularity == 1   ? database::TimeGranularity::Week
                        : analytics_filter_.granularity == 2 ? database::TimeGranularity::Month
                                                             : database::TimeGranularity::Day;
    analytics_error_.clear();
    return analytics_task_.start([query]() { return loadAnalytics(query); });
}

bool GuiInspectionController::selectInspection(std::int64_t inspection_id) {
    if (details_task_.busy()) return false;
    selected_history_id_ = inspection_id;
    const std::string db = settings_.database_path;
    const std::unordered_set<std::string> keys = cachedKeys();
    return details_task_.start([db, inspection_id, keys]() { return loadInspectionView(db, inspection_id, keys); });
}

bool GuiInspectionController::anyBusy() const {
    return worker_.busy() || history_task_.busy() || analytics_task_.busy() || details_task_.busy();
}

void GuiInspectionController::installView(InspectionView view) {
    view.generation = next_generation_++;
    view_ = std::move(view);
    if (metrics_) {
        for (const ImageSlot* slot : {&view_.images.reference, &view_.images.inspected, &view_.images.annotated,
                                      &view_.images.difference}) {
            if (slot->already_cached) ++metrics_->images_skipped_cached;
            if (slot->pixels) {
                metrics_->image_load_ms.add(slot->load_ms);
                metrics_->last_image_load = {slot->path, slot->load_ms, 0};
            }
        }
    }
}

void GuiInspectionController::update() {
    if (auto finished = worker_.poll()) {
        if (metrics_) metrics_->inspection_worker_ms.add(finished->worker_ms);
        if (finished->success) {
            last_persistence_error_ = finished->persistence_error;
            std::string text = "Inspection " + finished->view.external_id + ": " + finished->view.decision + " (" +
                               std::to_string(finished->view.defects.size()) + " region(s))";
            if (finished->view.db_id) text += ", stored as database ID " + std::to_string(*finished->view.db_id);
            addLog(LogLevel::Info, text);
            if (!finished->persistence_error.empty()) {
                addLog(LogLevel::Error, "Database error: the inspection was not stored: " + finished->persistence_error);
            }
            for (const ImageSlot* slot : {&finished->view.images.reference, &finished->view.images.inspected,
                                          &finished->view.images.annotated}) {
                if (!slot->error.empty()) finished->view.warnings.push_back(slot->error);
            }
            installView(std::move(finished->view));
        } else {
            last_inspection_error_ = finished->error;
            addLog(LogLevel::Error, "Inspection failed: " + finished->error);
        }
    }

    if (auto r = history_task_.poll()) {
        if (r->ok()) {
            history_ = std::move(r->value);
            if (metrics_) metrics_->db_query_ms.add(history_->query_ms);
            history_error_.clear();
        } else {
            history_error_ = r->error;
            addLog(LogLevel::Error, "History query failed: " + r->error);
        }
    }

    if (auto r = analytics_task_.poll()) {
        if (r->ok()) {
            analytics_ = std::move(r->value);
            if (metrics_) metrics_->db_query_ms.add(analytics_->query_ms);
            analytics_error_.clear();
        } else {
            analytics_error_ = r->error;
            addLog(LogLevel::Error, "Analytics query failed: " + r->error);
        }
    }

    if (auto r = details_task_.poll()) {
        if (r->ok()) {
            for (const std::string& w : r->value->warnings) addLog(LogLevel::Warning, w);
            if (metrics_) metrics_->db_query_ms.add(r->elapsed_ms);
            addLog(LogLevel::Info, "Loaded stored inspection " + r->value->external_id);
            installView(std::move(*r->value));
        } else {
            addLog(LogLevel::Error, "Could not load inspection: " + r->error);
        }
    }
}

}  // namespace optinspect::gui
