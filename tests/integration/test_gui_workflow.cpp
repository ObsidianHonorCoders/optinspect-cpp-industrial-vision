// Headless tests of the GUI application logic (no window, no OpenGL, no ImGui):
// inspection submission, worker result delivery, error propagation, persistence through the
// Phase 2 services, history selection, historical image loading and analytics retrieval.
#include "AsyncTask.hpp"
#include "GuiInspectionController.hpp"
#include "GuiOptions.hpp"
#include "HistoryService.hpp"
#include "ImageLoader.hpp"
#include "InspectionWorker.hpp"

#include "SyntheticImages.hpp"
#include "TempDatabase.hpp"

#include "database/DatabaseConnection.hpp"
#include "database/DatabaseInitializer.hpp"
#include "database/InspectionRepository.hpp"

#include <opencv2/imgcodecs.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;
using namespace optinspect;
using namespace optinspect::gui;
using namespace optinspect::testing;

namespace {

/// Temp directory with a synthetic image pair, a test configuration and an output folder.
class Workspace {
public:
    Workspace() {
        static int counter = 0;
        root_ = fs::temp_directory_path() / ("optinspect_gui_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" + std::to_string(counter++));
        fs::create_directories(root_);
        const cv::Mat reference = makeSyntheticReferenceImage();
        cv::imwrite(path("reference.png"), reference);
        cv::imwrite(path("inspected.png"), withSyntheticDefect(reference, cv::Rect(60, 60, 25, 25)));
        std::ofstream(path("config.json")) << R"({
            "alignment": {"method": "NONE"},
            "threshold": {"method": "FIXED", "fixed_threshold": 15.0},
            "detection": {"min_defect_area_px": 10.0, "max_defect_area_px": 50000.0},
            "decision": {"fail_defect_count_threshold": 1, "fail_area_px": 50.0, "min_alignment_confidence": 0.0}
        })";
    }
    ~Workspace() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    std::string path(const std::string& name) const { return (root_ / name).string(); }
    std::string out() const { return (root_ / "out").string(); }

private:
    fs::path root_;
};

void configure(GuiInspectionController& c, const Workspace& w) {
    c.settings().reference_path = w.path("reference.png");
    c.settings().inspected_path = w.path("inspected.png");
    c.settings().config_path = w.path("config.json");
    c.settings().output_dir = w.out();
}

template <typename Predicate>
bool waitFor(GuiInspectionController& c, Predicate done, int timeout_ms = 20000) {
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeout_ms)) {
        c.update();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    c.update();
    return done();
}

bool waitIdle(GuiInspectionController& c) {
    return waitFor(c, [&] { return !c.anyBusy(); });
}

}  // namespace

// --- command line ------------------------------------------------------------------

TEST(GuiOptions, ParsesInspectionAndScriptOptions) {
    const char* argv[] = {"gui", "--reference", "a.png", "--input", "b.png", "--database", "x.db", "--product-id", "3",
                          "--run", "--view", "side-by-side", "--select-defect", "2", "--roi", "10,20,30,40",
                          "--screenshot", "s.png"};
    const GuiOptions o = parseGuiOptions(static_cast<int>(sizeof(argv) / sizeof(argv[0])), argv);
    EXPECT_EQ(o.reference_path, "a.png");
    EXPECT_EQ(o.inspected_path, "b.png");
    EXPECT_EQ(o.database_path, "x.db");
    ASSERT_TRUE(o.product_id.has_value());
    EXPECT_EQ(*o.product_id, 3);
    EXPECT_TRUE(o.run);
    EXPECT_EQ(o.view_mode, "side-by-side");
    EXPECT_EQ(o.select_defect, std::optional<int>(2));
    EXPECT_EQ(o.screenshot_path, "s.png");
}

TEST(GuiOptions, RejectsBadInput) {
    const auto parse = [](std::vector<const char*> args) {
        args.insert(args.begin(), "gui");
        return parseGuiOptions(static_cast<int>(args.size()), args.data());
    };
    EXPECT_THROW(parse({"--bogus"}), std::invalid_argument);
    EXPECT_THROW(parse({"--reference"}), std::invalid_argument);
    EXPECT_THROW(parse({"--product-id", "abc"}), std::invalid_argument);
    EXPECT_THROW(parse({"--roi", "1,2,0,4"}), std::invalid_argument);
    EXPECT_THROW(parse({"--load-inspection", "5"}), std::invalid_argument);  // needs --database
    EXPECT_THROW(parse({"--width", "100"}), std::invalid_argument);
    EXPECT_TRUE(parse({"--help"}).show_help);
}

TEST(GuiOptions, RoiSpecParsing) {
    ASSERT_TRUE(parseRoiSpec("1,2,3,4").has_value());
    EXPECT_EQ(parseRoiSpec("1,2,3,4")->h, 4);
    EXPECT_FALSE(parseRoiSpec("1,2,3").has_value());
    EXPECT_FALSE(parseRoiSpec("1,2,3,4,5").has_value());
    EXPECT_FALSE(parseRoiSpec("a,b,c,d").has_value());
    EXPECT_FALSE(parseRoiSpec("-1,2,3,4").has_value());
}

// --- image loading -------------------------------------------------------------------

TEST(ImageLoader, MissingFileReportsErrorAndNeverSubstitutes) {
    const ImageSlot slot = loadImageSlot("definitely/not/here.png", {});
    EXPECT_FALSE(slot.available());
    EXPECT_NE(slot.error.find("File not found"), std::string::npos);
    EXPECT_EQ(slot.pixels, nullptr);
    EXPECT_TRUE(slot.cache_key.empty());
}

TEST(ImageLoader, LoadsAndSkipsWhenTextureIsAlreadyCached) {
    Workspace w;
    const ImageSlot first = loadImageSlot(w.path("reference.png"), {});
    ASSERT_TRUE(first.available());
    ASSERT_NE(first.pixels, nullptr);
    EXPECT_FALSE(first.pixels->empty());

    const ImageSlot second = loadImageSlot(w.path("reference.png"), {first.cache_key});
    EXPECT_TRUE(second.available());
    EXPECT_TRUE(second.already_cached);
    EXPECT_EQ(second.pixels, nullptr);  // no decode
    EXPECT_EQ(second.cache_key, first.cache_key);
}

TEST(ImageLoader, CacheKeyChangesWhenTheFileChanges) {
    Workspace w;
    const std::string key1 = makeImageCacheKey(w.path("reference.png"));
    ASSERT_FALSE(key1.empty());
    EXPECT_EQ(makeImageCacheKey(w.path("reference.png")), key1);
    cv::imwrite(w.path("reference.png"), cv::Mat(50, 60, CV_8UC3, cv::Scalar(1, 2, 3)));  // different size/content
    EXPECT_NE(makeImageCacheKey(w.path("reference.png")), key1);
}

TEST(ImageLoader, CorruptFileIsReportedNotThrown) {
    Workspace w;
    std::ofstream(w.path("bad.png")) << "this is not a png";
    const ImageSlot slot = loadImageSlot(w.path("bad.png"), {});
    EXPECT_FALSE(slot.available());
    EXPECT_FALSE(slot.error.empty());
}

TEST(ImageLoader, SixteenBitImagesAreReducedToEightBit) {
    Workspace w;
    cv::imwrite(w.path("deep.png"), cv::Mat(10, 10, CV_16UC1, cv::Scalar(65535)));
    const ImageSlot slot = loadImageSlot(w.path("deep.png"), {});
    ASSERT_TRUE(slot.available());
    EXPECT_EQ(slot.pixels->depth(), CV_8U);
    EXPECT_EQ(slot.pixels->at<uchar>(0, 0), 255);
}

// --- async helper ---------------------------------------------------------------------

TEST(AsyncTask, DeliversValueAndErrorsAndJoinsOnDestruction) {
    {
        AsyncTask<int> task;
        ASSERT_TRUE(task.start([] { return 42; }));
        std::optional<TaskResult<int>> r;
        for (int i = 0; i < 2000 && !r; ++i) {
            r = task.poll();
            if (!r) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(r->value, std::optional<int>(42));

        ASSERT_TRUE(task.start([]() -> int { throw std::runtime_error("boom"); }));
        r.reset();
        for (int i = 0; i < 2000 && !r; ++i) {
            r = task.poll();
            if (!r) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_TRUE(r.has_value());
        EXPECT_FALSE(r->ok());
        EXPECT_EQ(r->error, "boom");
    }
    // Destruction while a job is running must wait for it (no detached thread, no crash).
    std::atomic<bool> finished{false};
    {
        AsyncTask<int> slow;
        slow.start([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            finished = true;
            return 1;
        });
    }
    EXPECT_TRUE(finished.load());
}

TEST(AsyncTask, RejectsSecondJobWhileRunning) {
    AsyncTask<int> task;
    std::atomic<bool> release{false};
    ASSERT_TRUE(task.start([&] {
        while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return 1;
    }));
    EXPECT_TRUE(task.busy());
    EXPECT_FALSE(task.start([] { return 2; }));
    release = true;
}

// --- inspection workflow ------------------------------------------------------------------

TEST(GuiController, ValidationReportsMissingInputs) {
    GuiInspectionController c;
    EXPECT_NE(c.validateSettings().find("reference"), std::string::npos);
    c.settings().reference_path = "missing_ref.png";
    EXPECT_NE(c.validateSettings().find("not found"), std::string::npos);
    EXPECT_FALSE(c.runInspection());
    EXPECT_FALSE(c.lastInspectionError().empty());
    EXPECT_FALSE(c.inspectionRunning());
}

TEST(GuiController, ValidationRejectsBadProductId) {
    Workspace w;
    GuiInspectionController c;
    configure(c, w);
    EXPECT_TRUE(c.validateSettings().empty());
    c.settings().product_id_text = "12x";
    EXPECT_NE(c.validateSettings().find("Product ID"), std::string::npos);
    c.settings().product_id_text = "0";
    EXPECT_FALSE(c.validateSettings().empty());
}

TEST(GuiController, RunsTheRealPipelineAndDeliversTheResult) {
    Workspace w;
    PerformanceMetrics metrics;
    GuiInspectionController c({}, &metrics);
    configure(c, w);

    ASSERT_TRUE(c.runInspection());
    ASSERT_TRUE(waitFor(c, [&] { return c.view().hasResult() || !c.lastInspectionError().empty(); }));
    ASSERT_TRUE(c.lastInspectionError().empty()) << c.lastInspectionError();

    const InspectionView& v = c.view();
    EXPECT_EQ(v.source, ResultSource::LiveInspection);
    EXPECT_EQ(v.decision, "FAIL");  // same semantics as the CLI: the synthetic defect fails the board
    EXPECT_FALSE(v.defects.empty());
    EXPECT_EQ(v.defects.front().index, 1);
    EXPECT_GT(v.defects.front().area_px, 0.0);
    EXPECT_TRUE(v.defects.front().perimeter_px.has_value());
    EXPECT_FALSE(v.stages.empty());
    EXPECT_TRUE(v.processing_time_ms.has_value());
    EXPECT_FALSE(v.db_id.has_value());  // persistence was off
    EXPECT_GT(v.generation, 0u);

    // All four display images were loaded on the worker thread, plus the outputs exist on disk.
    EXPECT_TRUE(v.images.reference.available());
    EXPECT_TRUE(v.images.inspected.available());
    EXPECT_TRUE(v.images.annotated.available());
    EXPECT_TRUE(v.images.difference.available());
    ASSERT_NE(v.images.annotated.pixels, nullptr);
    EXPECT_EQ(v.images.annotated.pixels->channels(), 3);
    ASSERT_NE(v.images.difference.pixels, nullptr);
    EXPECT_EQ(v.images.difference.pixels->channels(), 1);  // grayscale difference image, as produced by Phase 1
    EXPECT_TRUE(fs::exists(v.report_path));
    EXPECT_EQ(metrics.inspection_worker_ms.totalCount(), 1u);
    EXPECT_FALSE(c.log().empty());
}

TEST(GuiController, SecondSubmissionWhileRunningIsRejected) {
    Workspace w;
    GuiInspectionController c;
    configure(c, w);
    ASSERT_TRUE(c.runInspection());
    EXPECT_FALSE(c.runInspection());  // worker busy (or already finished: then the call above would not fail)
    ASSERT_TRUE(waitIdle(c));
}

TEST(GuiController, InspectionErrorsArePropagatedAndDoNotKillTheWorker) {
    Workspace w;
    std::ofstream(w.path("corrupt.png")) << "not an image";
    GuiInspectionController c;
    configure(c, w);
    c.settings().inspected_path = w.path("corrupt.png");

    ASSERT_TRUE(c.runInspection());  // file exists, so validation passes; decoding fails in the job
    ASSERT_TRUE(waitFor(c, [&] { return !c.lastInspectionError().empty(); }));
    EXPECT_FALSE(c.view().hasResult());
    EXPECT_NE(c.lastInspectionError().find("corrupt.png"), std::string::npos);

    // The worker is reusable afterwards.
    c.settings().inspected_path = w.path("inspected.png");
    ASSERT_TRUE(waitIdle(c));
    ASSERT_TRUE(c.runInspection());
    ASSERT_TRUE(waitFor(c, [&] { return c.view().hasResult(); }));
    EXPECT_EQ(c.view().decision, "FAIL");
}

TEST(GuiController, ConfigurationErrorIsReported) {
    Workspace w;
    std::ofstream(w.path("broken.json")) << "{ not json";
    GuiInspectionController c;
    configure(c, w);
    c.settings().config_path = w.path("broken.json");
    ASSERT_TRUE(c.runInspection());
    ASSERT_TRUE(waitFor(c, [&] { return !c.lastInspectionError().empty(); }));
    EXPECT_FALSE(c.view().hasResult());
}

TEST(GuiController, ShutdownWhileAnInspectionIsRunningDoesNotHangOrCrash) {
    Workspace w;
    {
        GuiInspectionController c;
        configure(c, w);
        ASSERT_TRUE(c.runInspection());
        // Destroy immediately without polling: the destructor cancels and joins.
    }
    SUCCEED();
}

TEST(GuiController, PhaseNamesAreReal) {
    EXPECT_STREQ(toString(WorkerPhase::Aligning), "Aligning images");
    EXPECT_STREQ(toString(WorkerPhase::Persisting), "Persisting results");
}

TEST(GuiController, ReusedTexturesAreNotDecodedAgain) {
    Workspace w;
    GuiInspectionController first;
    configure(first, w);
    ASSERT_TRUE(first.runInspection());
    ASSERT_TRUE(waitFor(first, [&] { return first.view().hasResult(); }));
    const std::string ref_key = first.view().images.reference.cache_key;
    const std::string insp_key = first.view().images.inspected.cache_key;

    PerformanceMetrics metrics;
    GuiInspectionController second([&] { return std::unordered_set<std::string>{ref_key, insp_key}; }, &metrics);
    configure(second, w);
    ASSERT_TRUE(second.runInspection());
    ASSERT_TRUE(waitFor(second, [&] { return second.view().hasResult(); }));
    EXPECT_TRUE(second.view().images.reference.already_cached);
    EXPECT_EQ(second.view().images.reference.pixels, nullptr);
    EXPECT_TRUE(second.view().images.inspected.already_cached);
    EXPECT_FALSE(second.view().images.annotated.already_cached);  // new output file: decoded
    EXPECT_NE(second.view().images.annotated.pixels, nullptr);
    EXPECT_EQ(metrics.images_skipped_cached, 2u);
}

// --- persistence, history and analytics ----------------------------------------------------------

TEST(GuiController, PersistsThroughThePhase2ServicesAndReportsTheDatabaseId) {
    Workspace w;
    TempDatabaseFile db("gui_persist");
    GuiInspectionController c;
    configure(c, w);
    c.settings().persist = true;
    c.settings().database_path = db.path();
    c.settings().serial_number = "SN-GUI-1";

    ASSERT_TRUE(c.runInspection());
    ASSERT_TRUE(waitFor(c, [&] { return c.view().hasResult(); }));
    ASSERT_TRUE(c.lastPersistenceError().empty()) << c.lastPersistenceError();
    ASSERT_TRUE(c.view().db_id.has_value());

    // The row is really in the database, written by the existing recorder.
    database::DatabaseConnection conn = database::DatabaseConnection::open(db.path(), database::OpenMode::ReadOnly);
    database::InspectionRepository repo(conn);
    const auto stored = repo.getById(*c.view().db_id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->status, database::InspectionStatus::FAIL);
    EXPECT_EQ(stored->serial_number, std::optional<std::string>("SN-GUI-1"));
    EXPECT_EQ(stored->external_id, std::optional<std::string>(c.view().external_id));
    EXPECT_EQ(static_cast<std::size_t>(stored->detected_defect_count), c.view().defects.size());
}

TEST(GuiController, DatabaseFailureKeepsTheInspectionResult) {
    Workspace w;
    std::ofstream(w.path("garbage.db")) << "this is not an SQLite database, just text";
    GuiInspectionController c;
    configure(c, w);
    c.settings().persist = true;
    c.settings().database_path = w.path("garbage.db");

    ASSERT_TRUE(c.runInspection());
    ASSERT_TRUE(waitFor(c, [&] { return c.view().hasResult(); }));
    EXPECT_EQ(c.view().decision, "FAIL");  // the decision is unaffected
    EXPECT_FALSE(c.view().db_id.has_value());
    EXPECT_FALSE(c.lastPersistenceError().empty());
    EXPECT_TRUE(fs::exists(c.view().report_path));  // output files were kept
}

TEST(GuiController, UnknownProductIsReportedAsPersistenceError) {
    Workspace w;
    TempDatabaseFile db("gui_product");
    GuiInspectionController c;
    configure(c, w);
    c.settings().persist = true;
    c.settings().database_path = db.path();
    c.settings().product_id_text = "99";
    ASSERT_TRUE(c.runInspection());
    ASSERT_TRUE(waitFor(c, [&] { return c.view().hasResult(); }));
    EXPECT_NE(c.lastPersistenceError().find("Product 99"), std::string::npos);
    EXPECT_FALSE(c.view().db_id.has_value());
}

class GuiHistoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_ = std::make_unique<TempDatabaseFile>("gui_history");
        for (int i = 0; i < 2; ++i) {
            GuiInspectionController c;
            configure(c, w_);
            c.settings().persist = true;
            c.settings().database_path = db_->path();
            c.settings().serial_number = i == 0 ? "SN-A" : "SN-B";
            // Distinct ids: the id has second resolution, the worker de-duplicates within the output folder.
            ASSERT_TRUE(c.runInspection());
            ASSERT_TRUE(waitFor(c, [&] { return c.view().hasResult(); }));
            ASSERT_TRUE(c.view().db_id.has_value()) << c.lastPersistenceError();
            ids_.push_back(*c.view().db_id);
        }
    }
    Workspace w_;
    std::unique_ptr<TempDatabaseFile> db_;
    std::vector<std::int64_t> ids_;
};

TEST_F(GuiHistoryTest, HistoryListsAndFiltersStoredInspections) {
    GuiInspectionController c;
    c.settings().database_path = db_->path();
    ASSERT_TRUE(c.refreshHistory());
    ASSERT_TRUE(waitFor(c, [&] { return c.history() != nullptr; }));
    EXPECT_EQ(c.history()->inspections.size(), 2u);
    EXPECT_EQ(c.history()->matching_count, 2);
    EXPECT_GE(c.history()->query_ms, 0.0);

    c.historyFilter().serial_number = "SN-B";
    ASSERT_TRUE(waitIdle(c));
    ASSERT_TRUE(c.refreshHistory());
    ASSERT_TRUE(waitIdle(c));
    ASSERT_EQ(c.history()->inspections.size(), 1u);
    EXPECT_EQ(c.history()->inspections[0].serial_number, std::optional<std::string>("SN-B"));

    c.historyFilter().serial_number.clear();
    c.historyFilter().status = 1;  // PASS: none of the stored inspections
    ASSERT_TRUE(c.refreshHistory());
    ASSERT_TRUE(waitIdle(c));
    EXPECT_TRUE(c.history()->inspections.empty());

    c.historyFilter().status = 0;
    c.historyFilter().id_search = "no-such-id";
    ASSERT_TRUE(c.refreshHistory());
    ASSERT_TRUE(waitIdle(c));
    EXPECT_TRUE(c.history()->inspections.empty());
}

TEST_F(GuiHistoryTest, SelectingAnInspectionLoadsRecordDefectsAndImages) {
    GuiInspectionController c;
    c.settings().database_path = db_->path();
    ASSERT_TRUE(c.selectInspection(ids_[0]));
    ASSERT_TRUE(waitFor(c, [&] { return c.view().hasResult(); }));

    const InspectionView& v = c.view();
    EXPECT_EQ(v.source, ResultSource::History);
    EXPECT_EQ(v.db_id, std::optional<std::int64_t>(ids_[0]));
    EXPECT_EQ(v.decision, "FAIL");
    EXPECT_EQ(v.serial_number, std::optional<std::string>("SN-A"));
    EXPECT_FALSE(v.defects.empty());
    EXPECT_EQ(v.defects.front().index, 1);
    EXPECT_TRUE(v.images.reference.available());
    EXPECT_TRUE(v.images.annotated.available());
    EXPECT_TRUE(v.images.difference.available());
    EXPECT_NE(v.images.annotated.pixels, nullptr);
    EXPECT_FALSE(v.stages.empty());  // restored from the JSON report next to the images
    EXPECT_TRUE(v.warnings.empty());
}

TEST_F(GuiHistoryTest, MissingHistoricalFileIsReportedWithoutSubstitutionOrRecordChange) {
    // Delete the annotated image of the first inspection.
    std::string annotated;
    {
        GuiInspectionController probe;
        probe.settings().database_path = db_->path();
        ASSERT_TRUE(probe.selectInspection(ids_[0]));
        ASSERT_TRUE(waitFor(probe, [&] { return probe.view().hasResult(); }));
        annotated = probe.view().images.annotated.path;
    }
    ASSERT_TRUE(fs::remove(annotated));

    GuiInspectionController c;
    c.settings().database_path = db_->path();
    ASSERT_TRUE(c.selectInspection(ids_[0]));
    ASSERT_TRUE(waitFor(c, [&] { return c.view().hasResult(); }));
    const InspectionView& v = c.view();
    EXPECT_FALSE(v.images.annotated.available());
    EXPECT_NE(v.images.annotated.error.find("File not found"), std::string::npos);
    EXPECT_EQ(v.images.annotated.pixels, nullptr);        // nothing was substituted
    EXPECT_TRUE(v.images.reference.available());           // the other images still load
    ASSERT_FALSE(v.warnings.empty());
    EXPECT_NE(v.warnings.front().find("Annotated"), std::string::npos);

    // The stored record is unchanged: still two inspections, image reference intact.
    database::DatabaseConnection conn = database::DatabaseConnection::open(db_->path(), database::OpenMode::ReadOnly);
    EXPECT_EQ(database::InspectionRepository(conn).count(), 2);
}

TEST_F(GuiHistoryTest, UnknownInspectionIdProducesALoggedError) {
    GuiInspectionController c;
    c.settings().database_path = db_->path();
    ASSERT_TRUE(c.selectInspection(987654));
    ASSERT_TRUE(waitIdle(c));
    EXPECT_FALSE(c.view().hasResult());
    bool logged = false;
    for (const LogEntry& e : c.log()) logged = logged || (e.level == LogLevel::Error && e.text.find("987654") != std::string::npos);
    EXPECT_TRUE(logged);
}

TEST_F(GuiHistoryTest, AnalyticsSnapshotComesFromTheAnalyticsService) {
    GuiInspectionController c;
    c.settings().database_path = db_->path();
    ASSERT_TRUE(c.refreshAnalytics());
    ASSERT_TRUE(waitFor(c, [&] { return c.analytics() != nullptr; }));
    const AnalyticsSnapshot& a = *c.analytics();
    EXPECT_EQ(a.summary.total, 2);
    EXPECT_EQ(a.summary.fail, 2);
    EXPECT_EQ(a.summary.pass, 0);
    EXPECT_DOUBLE_EQ(a.summary.fail_rate, 1.0);
    EXPECT_GT(a.defects.total_defects, 0);
    EXPECT_FALSE(a.trend.empty());
    ASSERT_TRUE(a.processing.avg_ms.has_value());
    EXPECT_GT(*a.processing.avg_ms, 0.0);
    EXPECT_FALSE(a.products.empty());  // the "(unassigned)" bucket
}

TEST(GuiHistory, EmptyDatabaseYieldsZeroTotalsWithoutDividingByZero) {
    TempDatabaseFile db("gui_empty");
    {
        database::DatabaseConnection conn = database::DatabaseConnection::open(db.path());
        database::DatabaseInitializer::initialize(conn);
    }
    GuiInspectionController c;
    c.settings().database_path = db.path();
    ASSERT_TRUE(c.refreshAnalytics());
    ASSERT_TRUE(waitFor(c, [&] { return c.analytics() != nullptr; }));
    EXPECT_EQ(c.analytics()->summary.total, 0);
    EXPECT_DOUBLE_EQ(c.analytics()->summary.fail_rate, 0.0);
    EXPECT_TRUE(c.analytics()->trend.empty());
    EXPECT_FALSE(c.analytics()->processing.avg_ms.has_value());
}

TEST(GuiHistory, MissingOrForeignDatabaseIsAnErrorNotACrash) {
    GuiInspectionController c;
    c.settings().database_path = "no/such/database.db";
    ASSERT_TRUE(c.refreshHistory());
    ASSERT_TRUE(waitIdle(c));
    EXPECT_EQ(c.history(), nullptr);
    EXPECT_FALSE(c.historyError().empty());

    c.settings().database_path = "";
    ASSERT_TRUE(c.refreshAnalytics());
    ASSERT_TRUE(waitIdle(c));
    EXPECT_FALSE(c.analyticsError().empty());
}

TEST(GuiHistory, BadProductFilterIsRejectedBeforeQuerying) {
    GuiInspectionController c;
    c.historyFilter().product_id_text = "abc";
    EXPECT_FALSE(c.refreshHistory());
    EXPECT_FALSE(c.historyError().empty());
}
