// Reproducible synthetic benchmark of the database layer. Not a unit test:
// it prints measurements and is run manually (see docs/performance-report.md).
//
// All data is generated from a fixed-seed linear congruential generator, so the
// dataset (row counts, values) is identical on every run; only the timings vary
// with the machine. Everything uses temporary database files that are deleted
// when the program exits.

#include "TempDatabase.hpp"
#include "common/Timing.hpp"
#include "database/DatabaseConnection.hpp"
#include "database/DatabaseInitializer.hpp"
#include "database/DatabaseTransaction.hpp"
#include "database/InspectionPersistenceService.hpp"
#include "database/InspectionRepository.hpp"
#include "database/ProductRepository.hpp"
#include "database/QualityAnalyticsService.hpp"
#include "database/Statement.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace optinspect;
using namespace optinspect::database;

namespace {

class Lcg {
public:
    explicit Lcg(std::uint64_t seed) : state_(seed) {}
    std::uint32_t next() {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(state_ >> 33);
    }
    double uniform() { return next() / 2147483648.0; }  // [0, 1)
    int range(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<std::uint32_t>(hi - lo + 1)); }

private:
    std::uint64_t state_;
};

std::string timestampFor(int index) {
    // Spread inspections evenly over 90 synthetic 30-day months from 2026-01-01 (about 112 per day).
    const int day = (index / 112) % 90;
    const int hour = (index % 112) % 24;
    const int month = 1 + day / 30;
    const int dom = 1 + day % 30;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "2026-%02d-%02dT%02d:%02d:00Z", month, dom, hour, index % 60);
    return buffer;
}

InspectionPersistenceRequest makeRequest(Lcg& rng, int index, int defect_count,
                                         const std::vector<std::int64_t>& product_ids) {
    InspectionPersistenceRequest request;
    InspectionRecord& r = request.inspection;
    r.external_id = "bench-" + std::to_string(index);
    r.product_id = product_ids[static_cast<std::size_t>(index) % product_ids.size()];
    r.serial_number = "SN-" + std::to_string(index);
    r.inspection_timestamp = timestampFor(index);
    r.status = defect_count == 0 ? InspectionStatus::PASS
                                 : (rng.range(0, 9) == 0 ? InspectionStatus::REVIEW : InspectionStatus::FAIL);
    r.decision_reason = "synthetic";
    r.alignment_status = AlignmentStatus::SUCCEEDED;
    r.alignment_confidence = 0.9 + rng.uniform() * 0.1;
    r.report_path = "bench/report_" + std::to_string(index) + ".json";
    r.processing_time_ms = 80.0 + rng.uniform() * 120.0;
    r.configuration_snapshot = R"({"threshold":{"method":"OTSU"},"decision":{"fail_area_px":400.0}})";

    for (int i = 0; i < defect_count; ++i) {
        DefectRecord d;
        d.x = rng.range(0, 600);
        d.y = rng.range(0, 600);
        d.width = rng.range(3, 40);
        d.height = rng.range(3, 40);
        d.area = 20.0 + rng.uniform() * 800.0;
        d.perimeter = 4.0 * d.width;
        d.centroid_x = d.x + d.width / 2.0;
        d.centroid_y = d.y + d.height / 2.0;
        d.aspect_ratio = static_cast<double>(d.width) / d.height;
        d.confidence = std::min(1.0, d.area / 2000.0);
        request.defects.push_back(d);
    }
    for (ImageType type : {ImageType::REFERENCE, ImageType::INSPECTED, ImageType::ANNOTATED, ImageType::DIFFERENCE}) {
        InspectionImage image;
        image.type = type;
        image.file_path = "bench/" + std::to_string(index) + "_" + toString(type) + ".png";
        image.width = 640;
        image.height = 640;
        request.images.push_back(image);
    }
    return request;
}

std::vector<std::int64_t> makeProducts(DatabaseConnection& db, int count) {
    ProductRepository products(db);
    std::vector<std::int64_t> ids;
    for (int i = 0; i < count; ++i) {
        Product p;
        p.code = "BENCH-" + std::to_string(i);
        p.name = "Benchmark product " + std::to_string(i);
        ids.push_back(products.create(p));
    }
    return ids;
}

struct Timing {
    double mean_ms = 0.0;
    double min_ms = 0.0;
};

Timing measure(int repetitions, const std::function<void()>& work) {
    std::vector<double> samples;
    for (int i = 0; i < repetitions; ++i) {
        Stopwatch sw;
        work();
        samples.push_back(sw.elapsedMilliseconds());
    }
    double sum = 0;
    for (double s : samples) sum += s;
    return {sum / samples.size(), *std::min_element(samples.begin(), samples.end())};
}

void row(const std::string& name, const std::string& detail, double value, const std::string& unit) {
    std::cout << "| " << name << " | " << detail << " | " << std::fixed << std::setprecision(3) << value
              << " | " << unit << " |\n";
}
std::uintmax_t fileSizeWithWal(const std::string& path) {
    std::uintmax_t total = 0;
    for (const char* suffix : {"", "-wal"}) {
        std::error_code ec;
        const auto size = std::filesystem::file_size(path + suffix, ec);
        if (!ec) total += size;
    }
    return total;
}

}  // namespace

int main() {
    std::cout << "SQLite " << sqliteLibraryVersion() << "; synthetic data, fixed seed; file database (WAL, synchronous=FULL)\n\n";
    std::cout << "| Operation | Dataset / method | Result | Unit |\n|---|---|---:|---|\n";

    // ---- Individual inserts, each persisted in its own transaction -------------------------------
    {
        testing::TempDatabaseFile file("bench_single");
        DatabaseConnection db = DatabaseConnection::open(file.path());
        DatabaseInitializer::initialize(db);
        const auto products = makeProducts(db, 5);
        InspectionPersistenceService service(db);
        Lcg rng(1);

        constexpr int kCount = 500;
        std::vector<InspectionPersistenceRequest> no_defects, with_defects;
        for (int i = 0; i < kCount; ++i) no_defects.push_back(makeRequest(rng, i, 0, products));
        for (int i = 0; i < kCount; ++i) with_defects.push_back(makeRequest(rng, kCount + i, 20, products));

        Stopwatch sw;
        for (const auto& request : no_defects) service.persist(request);
        row("Insert inspection (0 defects, 4 images)", "500 x own transaction", sw.elapsedMilliseconds() / kCount, "ms / inspection");

        sw.reset();
        for (const auto& request : with_defects) service.persist(request);
        row("Insert inspection (20 defects, 4 images)", "500 x own transaction", sw.elapsedMilliseconds() / kCount, "ms / inspection");
    }

    // ---- Batch insert and query timings on a larger dataset ----------------------------------------
    testing::TempDatabaseFile file("bench_batch");
    DatabaseConnection db = DatabaseConnection::open(file.path());
    DatabaseInitializer::initialize(db);
    const auto products = makeProducts(db, 20);
    InspectionPersistenceService service(db);
    Lcg rng(2);

    constexpr int kInspections = 10000;
    constexpr int kDefectsEach = 5;
    {
        std::vector<InspectionPersistenceRequest> requests;
        requests.reserve(kInspections);
        for (int i = 0; i < kInspections; ++i) requests.push_back(makeRequest(rng, i, kDefectsEach, products));

        Stopwatch sw;
        Transaction outer(db);
        for (const auto& request : requests) service.persist(request);
        outer.commit();
        const double total = sw.elapsedMilliseconds();
        row("Batch insert (5 defects, 4 images each)", "10,000 in one transaction", total, "ms total");
        row("  per inspection", "", total / kInspections, "ms / inspection");
    }

    InspectionRepository inspections(db);
    QualityAnalyticsService analytics(db);

    InspectionFilter history;
    history.product_id = products[3];
    history.from = "2026-02-01";
    history.to = "2026-02-28";
    history.limit = 50;
    Timing t = measure(100, [&] { inspections.list(history); });
    row("History query", "product + date range, LIMIT 50", t.mean_ms, "ms (mean/100)");

    InspectionFilter by_status;
    by_status.status = InspectionStatus::REVIEW;
    by_status.limit = 50;
    t = measure(100, [&] { inspections.list(by_status); });
    row("History query", "status = REVIEW, LIMIT 50", t.mean_ms, "ms (mean/100)");

    InspectionFilter by_serial;
    by_serial.serial_number = "SN-7777";
    t = measure(100, [&] { inspections.list(by_serial); });
    row("History query", "single serial number", t.mean_ms, "ms (mean/100)");

    t = measure(20, [&] { analytics.summary(); });
    row("Analytics: inspection summary", "10,000 inspections", t.mean_ms, "ms (mean/20)");
    t = measure(20, [&] { analytics.productQuality(); });
    row("Analytics: product quality", "10,000 inspections, 20 products", t.mean_ms, "ms (mean/20)");
    t = measure(20, [&] { analytics.defectStatistics(); });
    row("Analytics: defect statistics", "50,000 defects", t.mean_ms, "ms (mean/20)");
    t = measure(20, [&] { analytics.timeSeries(TimeGranularity::Day); });
    row("Analytics: daily time series (window fn)", "10,000 inspections", t.mean_ms, "ms (mean/20)");
    t = measure(20, [&] { analytics.processingTimeByProduct(); });
    row("Analytics: processing time by product", "10,000 inspections", t.mean_ms, "ms (mean/20)");

    db.execute("PRAGMA wal_checkpoint(TRUNCATE)");
    const double size_mb = static_cast<double>(fileSizeWithWal(file.path())) / (1024.0 * 1024.0);
    row("Database file size", "10,000 insp. / 50,000 defects", size_mb, "MiB");
    row("Database file size per inspection", "", size_mb * 1024.0 / kInspections, "KiB");
    return 0;
}
