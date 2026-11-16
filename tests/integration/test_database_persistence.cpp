#include <gtest/gtest.h>

#include "InspectionPipeline.hpp"
#include "InspectionRecorder.hpp"
#include "ReportWriter.hpp"
#include "ResultAnnotator.hpp"
#include "SyntheticImages.hpp"
#include "TempDatabase.hpp"
#include "acquisition/FileImageSource.hpp"
#include "common/Errors.hpp"
#include "database/DatabaseConnection.hpp"
#include "database/DatabaseError.hpp"
#include "database/DatabaseInitializer.hpp"
#include "database/DefectRepository.hpp"
#include "database/InspectionImageRepository.hpp"
#include "database/InspectionPersistenceService.hpp"
#include "database/InspectionRepository.hpp"
#include "database/ProductRepository.hpp"
#include "database/QualityAnalyticsService.hpp"
#include "database/Statement.hpp"
#include "database/TimeUtil.hpp"
#include "third_party/nlohmann/json.hpp"

#include <opencv2/imgcodecs.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace optinspect;
using namespace optinspect::database;
using namespace optinspect::testing;

namespace {

InspectionConfig fastTestConfig() {
    InspectionConfig config;
    config.alignment.method = AlignmentMethod::NONE;  // synthetic images are already aligned
    config.threshold.method = ThresholdMethod::FIXED;
    config.threshold.fixed_threshold = 15.0;
    config.detection.min_defect_area_px = 10.0;
    config.detection.max_defect_area_px = 50000.0;
    config.decision.fail_area_px = 50.0;
    config.decision.fail_defect_count_threshold = 1;
    return config;
}

/// Everything a CLI run produces for one inspection, written to a temp directory.
struct RunResult {
    InspectionResult result;
    InspectionArtifacts artifacts;
    std::string report_path;
};

class PersistenceIntegrationTest : public ::testing::Test {
protected:
    PersistenceIntegrationTest()
        : db_file("persist"), out("persist_out"), db(DatabaseConnection::open(db_file.path())) {
        DatabaseInitializer::initialize(db);
    }

    /// Runs the real Phase 1 pipeline on synthetic images, writes the same
    /// output files the CLI writes, and returns what is needed to persist it.
    RunResult runPhase1(const cv::Mat& reference, const cv::Mat& inspected, const std::string& id,
                        const InspectionConfig& config) {
        const fs::path dir = out.path();
        const std::string ref_path = (dir / (id + "_temp.png")).string();
        const std::string insp_path = (dir / (id + "_test.png")).string();
        cv::imwrite(ref_path, reference);
        cv::imwrite(insp_path, inspected);

        const cv::Mat ref = FileImageSource(ref_path).load();
        const cv::Mat insp = FileImageSource(insp_path).load();
        const PipelineOutput output = InspectionPipeline(config).run(ref, insp);

        RunResult run;
        run.result = output.result;
        run.artifacts.external_id = id;
        run.artifacts.reference_path = ref_path;
        run.artifacts.inspected_path = insp_path;
        run.artifacts.reference_size = ref.size();
        run.artifacts.inspected_size = insp.size();

        const cv::Mat annotated = ResultAnnotator::annotate(insp, output.result);
        run.artifacts.annotated_path = (dir / (id + "_annotated.png")).string();
        cv::imwrite(run.artifacts.annotated_path, annotated);
        run.artifacts.annotated_size = annotated.size();

        if (!output.difference_image.empty()) {
            run.artifacts.difference_path = (dir / (id + "_difference.png")).string();
            cv::imwrite(run.artifacts.difference_path, output.difference_image);
            run.artifacts.difference_size = output.difference_image.size();
        }

        InspectionMetadata metadata;
        metadata.inspection_id = id;
        metadata.reference_path = ref_path;
        metadata.inspected_path = insp_path;
        metadata.reference_size = ref.size();
        metadata.inspected_size = insp.size();
        run.report_path = (dir / (id + "_report.json")).string();
        ReportWriter::writeJson(run.report_path, metadata, output.result, output.timer, config);
        run.artifacts.report_path = run.report_path;
        run.artifacts.processing_time_ms = output.timer.totalMilliseconds();
        return run;
    }

    std::int64_t persist(const RunResult& run, const InspectionConfig& config,
                         const PersistenceOptions& options = {}) {
        InspectionPersistenceService service(db);
        return service.persist(buildPersistenceRequest(run.result, config, run.artifacts, options,
                                                       nowUtcTimestamp()));
    }

    int count(const std::string& table) {
        Statement stmt(db, "SELECT COUNT(*) FROM " + table);
        stmt.step();
        return stmt.columnInt(0);
    }

    TempDatabaseFile db_file;
    TempDirectory out;
    DatabaseConnection db;
};

cv::Mat referenceImage() { return makeSyntheticReferenceImage(); }

cv::Mat twoDefectImage(const cv::Mat& reference) {
    return withSyntheticDefect(withSyntheticDefect(reference, cv::Rect(60, 60, 25, 25)),
                               cv::Rect(130, 120, 20, 30));
}

}  // namespace

TEST_F(PersistenceIntegrationTest, PassInspectionWithZeroDefectsIsPersisted) {
    const InspectionConfig config = fastTestConfig();
    const cv::Mat ref = referenceImage();
    const RunResult run = runPhase1(ref, ref.clone(), "pass-001", config);
    ASSERT_EQ(run.result.decision, InspectionDecision::PASS);
    ASSERT_TRUE(run.result.defects.empty());

    const std::int64_t id = persist(run, config);
    const auto stored = InspectionRepository(db).getById(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->status, InspectionStatus::PASS);
    EXPECT_EQ(stored->detected_defect_count, 0);
    EXPECT_TRUE(DefectRepository(db).listByInspection(id).empty());
    EXPECT_EQ(stored->decision_reason, std::optional<std::string>(run.result.decision_reason));
}

TEST_F(PersistenceIntegrationTest, FailInspectionWithMultipleDefectsStoresEveryMeasurement) {
    const InspectionConfig config = fastTestConfig();
    const cv::Mat ref = referenceImage();
    const RunResult run = runPhase1(ref, twoDefectImage(ref), "fail-001", config);
    ASSERT_EQ(run.result.decision, InspectionDecision::FAIL);
    ASSERT_GE(run.result.defects.size(), 2u);

    const std::int64_t id = persist(run, config);
    const auto stored = InspectionRepository(db).getById(id);
    EXPECT_EQ(stored->status, InspectionStatus::FAIL);
    EXPECT_EQ(static_cast<std::size_t>(stored->detected_defect_count), run.result.defects.size());

    const auto defects = DefectRepository(db).listByInspection(id);
    ASSERT_EQ(defects.size(), run.result.defects.size());
    for (std::size_t i = 0; i < defects.size(); ++i) {
        const DefectMeasurement& m = run.result.defects[i];
        EXPECT_EQ(defects[i].x, m.bounding_box.x);
        EXPECT_EQ(defects[i].y, m.bounding_box.y);
        EXPECT_EQ(defects[i].width, m.bounding_box.width);
        EXPECT_EQ(defects[i].height, m.bounding_box.height);
        EXPECT_DOUBLE_EQ(defects[i].area, m.area_px);
        EXPECT_DOUBLE_EQ(*defects[i].perimeter, m.perimeter_px);
        EXPECT_DOUBLE_EQ(*defects[i].centroid_x, m.centroid.x);
        EXPECT_DOUBLE_EQ(*defects[i].centroid_y, m.centroid.y);
        EXPECT_DOUBLE_EQ(*defects[i].aspect_ratio, m.aspect_ratio);
        EXPECT_DOUBLE_EQ(*defects[i].confidence, m.confidence);
    }
}

TEST_F(PersistenceIntegrationTest, ReviewInspectionAfterAlignmentFailureIsPersisted) {
    InspectionConfig config = fastTestConfig();
    config.alignment.method = AlignmentMethod::ECC;
    config.alignment.ecc_max_iterations = 1;
    config.alignment.ecc_epsilon = 1e-12;
    config.alignment.max_translation_px = 0.0001;

    const cv::Mat ref = referenceImage();
    cv::Mat noisy(ref.size(), ref.type());
    cv::randu(noisy, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));
    const RunResult run = runPhase1(ref, noisy, "review-001", config);
    ASSERT_EQ(run.result.decision, InspectionDecision::REVIEW);

    const std::int64_t id = persist(run, config);
    const auto stored = InspectionRepository(db).getById(id);
    EXPECT_EQ(stored->status, InspectionStatus::REVIEW);
    EXPECT_EQ(stored->alignment_status, std::optional<AlignmentStatus>(AlignmentStatus::FAILED));
}

TEST_F(PersistenceIntegrationTest, CompleteInspectionRoundTripsAllStoredData) {
    ProductRepository products(db);
    Product product;
    product.code = "PCB-SYN";
    product.name = "Synthetic board";
    const std::int64_t product_id = products.create(product);

    const InspectionConfig config = fastTestConfig();
    const cv::Mat ref = referenceImage();
    const RunResult run = runPhase1(ref, twoDefectImage(ref), "full-001", config);

    PersistenceOptions options;
    options.product_id = product_id;
    options.serial_number = "SN-777";
    options.notes = "integration test";
    const std::int64_t id = persist(run, config, options);

    const auto stored = InspectionRepository(db).getById(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->external_id, std::optional<std::string>("full-001"));
    EXPECT_EQ(stored->product_id, std::optional<std::int64_t>(product_id));
    EXPECT_EQ(stored->serial_number, std::optional<std::string>("SN-777"));
    EXPECT_EQ(stored->notes, std::optional<std::string>("integration test"));
    EXPECT_EQ(stored->alignment_status, std::optional<AlignmentStatus>(AlignmentStatus::SUCCEEDED));
    EXPECT_DOUBLE_EQ(*stored->alignment_confidence, run.result.alignment_confidence);
    EXPECT_DOUBLE_EQ(*stored->processing_time_ms, run.artifacts.processing_time_ms);
    EXPECT_EQ(stored->report_path, std::optional<std::string>(run.report_path));
    EXPECT_EQ(stored->inspection_timestamp.size(), 20u);

    // The snapshot is the configuration block of the Phase 1 report.
    ASSERT_TRUE(stored->configuration_snapshot.has_value());
    const auto snapshot = nlohmann::json::parse(*stored->configuration_snapshot);
    EXPECT_EQ(snapshot.at("threshold").at("method"), "FIXED");
    EXPECT_EQ(snapshot.at("decision").at("fail_area_px"), 50.0);
    std::ifstream report_file(run.report_path);
    nlohmann::json report;
    report_file >> report;
    EXPECT_EQ(snapshot, report.at("configuration"));

    // Image references: right type, exact path, correct dimensions, files really exist.
    InspectionImageRepository images(db);
    const auto stored_images = images.listByInspection(id);
    ASSERT_EQ(stored_images.size(), 4u);
    auto expectImage = [&](ImageType type, const std::string& path, const cv::Size& size) {
        const auto image = images.getByInspectionAndType(id, type);
        ASSERT_TRUE(image.has_value()) << toString(type);
        EXPECT_EQ(image->file_path, path);
        EXPECT_EQ(image->width, std::optional<int>(size.width));
        EXPECT_EQ(image->height, std::optional<int>(size.height));
        EXPECT_TRUE(fs::exists(image->file_path)) << image->file_path;
    };
    expectImage(ImageType::REFERENCE, run.artifacts.reference_path, run.artifacts.reference_size);
    expectImage(ImageType::INSPECTED, run.artifacts.inspected_path, run.artifacts.inspected_size);
    expectImage(ImageType::ANNOTATED, run.artifacts.annotated_path, run.artifacts.annotated_size);
    expectImage(ImageType::DIFFERENCE, run.artifacts.difference_path, run.artifacts.difference_size);
    EXPECT_TRUE(fs::exists(*stored->report_path));

    // The inspection can be found again through the history queries.
    InspectionFilter filter;
    filter.product_id = product_id;
    filter.serial_number = "SN-777";
    filter.status = InspectionStatus::FAIL;
    ASSERT_EQ(InspectionRepository(db).list(filter).size(), 1u);
    EXPECT_EQ(InspectionRepository(db).list(filter)[0].id, id);

    // Deleting the database record never deletes the generated files.
    EXPECT_TRUE(InspectionPersistenceService(db).deleteInspection(id));
    EXPECT_TRUE(fs::exists(run.artifacts.annotated_path));
    EXPECT_TRUE(fs::exists(run.report_path));
}

TEST_F(PersistenceIntegrationTest, RollbackLeavesNoPartialInspectionRecords) {
    const InspectionConfig config = fastTestConfig();
    const cv::Mat ref = referenceImage();
    const RunResult run = runPhase1(ref, twoDefectImage(ref), "rollback-001", config);

    InspectionPersistenceRequest request = buildPersistenceRequest(
        run.result, config, run.artifacts, {}, nowUtcTimestamp());
    ASSERT_GE(request.defects.size(), 2u);
    request.defects.back().area = -1.0;  // violates CHECK (area >= 0) after earlier rows were written

    InspectionPersistenceService service(db);
    EXPECT_THROW(service.persist(request), ConstraintViolationError);

    EXPECT_EQ(count("inspections"), 0);
    EXPECT_EQ(count("defects"), 0);
    EXPECT_EQ(count("inspection_images"), 0);
    EXPECT_EQ(count("configurations"), 0);
    EXPECT_FALSE(db.inTransaction());

    // The same request succeeds once the bad value is fixed: nothing was left half-done.
    request.defects.back().area = 12.0;
    EXPECT_NO_THROW(service.persist(request));
    EXPECT_EQ(count("inspections"), 1);
}

TEST_F(PersistenceIntegrationTest, ReportImportRebuildsTheSameRecordForRetry) {
    const InspectionConfig config = fastTestConfig();
    const cv::Mat ref = referenceImage();
    const RunResult run = runPhase1(ref, twoDefectImage(ref), "20261114-213626-retry", config);

    PersistenceOptions options;
    options.serial_number = "SN-RETRY";
    const InspectionPersistenceRequest request = buildRequestFromReport(run.report_path, options);

    EXPECT_EQ(request.inspection.external_id, std::optional<std::string>("20261114-213626-retry"));
    EXPECT_EQ(request.inspection.status, InspectionStatus::FAIL);
    EXPECT_EQ(request.defects.size(), run.result.defects.size());
    EXPECT_EQ(request.images.size(), 4u);  // reference, inspected + annotated/difference next to the report
    EXPECT_EQ(request.inspection.inspection_timestamp.size(), 20u);

    const std::int64_t id = InspectionPersistenceService(db).persist(request);
    const auto defects = DefectRepository(db).listByInspection(id);
    ASSERT_EQ(defects.size(), run.result.defects.size());
    EXPECT_NEAR(defects[0].area, run.result.defects[0].area_px, 1e-9);
    EXPECT_EQ(InspectionRepository(db).getById(id)->serial_number, std::optional<std::string>("SN-RETRY"));

    // Importing again would collide on the unique inspection id, which the CLI reports as "already stored".
    EXPECT_THROW(InspectionPersistenceService(db).persist(request), ConstraintViolationError);
    EXPECT_EQ(count("inspections"), 1);
}

TEST_F(PersistenceIntegrationTest, ReportImportRejectsMissingOrMalformedReports) {
    EXPECT_THROW(buildRequestFromReport((out.path() / "nope.json").string(), {}), OptInspectError);

    const fs::path bad = out.path() / "bad_report.json";
    { std::ofstream(bad) << "{ not json"; }
    EXPECT_THROW(buildRequestFromReport(bad.string(), {}), OptInspectError);

    const fs::path incomplete = out.path() / "incomplete_report.json";
    { std::ofstream(incomplete) << R"({"inspection_id": "x"})"; }
    EXPECT_THROW(buildRequestFromReport(incomplete.string(), {}), OptInspectError);
}

TEST_F(PersistenceIntegrationTest, AnalyticsOverPersistedPhase1ResultsMatchStoredRows) {
    const InspectionConfig config = fastTestConfig();
    const cv::Mat ref = referenceImage();
    persist(runPhase1(ref, ref.clone(), "a-pass", config), config);
    persist(runPhase1(ref, twoDefectImage(ref), "a-fail-1", config), config);
    const RunResult second_fail = runPhase1(ref, withSyntheticDefect(ref, cv::Rect(40, 40, 25, 25)),
                                            "a-fail-2", config);
    persist(second_fail, config);

    QualityAnalyticsService analytics(db);
    const InspectionSummary summary = analytics.summary();
    EXPECT_EQ(summary.total, 3);
    EXPECT_EQ(summary.pass, 1);
    EXPECT_EQ(summary.fail, 2);

    const DefectStatistics defects = analytics.defectStatistics();
    EXPECT_EQ(defects.total_defects, count("defects"));
    EXPECT_GE(defects.max_defects_in_inspection, 2);
    EXPECT_EQ(analytics.productQuality().size(), 1u);  // all three are unassigned
}

TEST(InspectionRecorderTest, AutoInitializeCreatesDirectoryAndSchema) {
    TempDirectory dir("recorder");
    const std::string path = (dir.path() / "nested" / "data.db").string();
    {
        InspectionRecorder recorder(path, /*auto_initialize=*/true);
        EXPECT_FALSE(recorder.productExists(1));
    }
    EXPECT_TRUE(fs::exists(path));
    DatabaseConnection db = DatabaseConnection::open(path, OpenMode::ReadWrite);
    EXPECT_NO_THROW(DatabaseInitializer::requireCurrentSchema(db));
}

TEST(InspectionRecorderTest, WithoutAutoInitializeMissingDatabaseIsReported) {
    TempDirectory dir("recorder_missing");
    const std::string path = (dir.path() / "absent.db").string();
    EXPECT_THROW(InspectionRecorder(path, /*auto_initialize=*/false), DatabaseError);
    EXPECT_FALSE(fs::exists(path));
}

TEST(InspectionRecorderTest, InvalidDatabasePathIsReportedNotSwallowed) {
    TempDirectory dir("recorder_bad");
    // A directory cannot be opened as a database file.
    EXPECT_THROW(InspectionRecorder(dir.path().string(), /*auto_initialize=*/true), DatabaseError);
}

TEST(TimeUtilTest, ParsesPhase1InspectionIdStampsAndValidatesDates) {
    EXPECT_EQ(utcTimestampFromLocalStamp("not-a-stamp"), "");
    EXPECT_EQ(utcTimestampFromLocalStamp("20261114-213626-00041000").size(), 20u);
    EXPECT_TRUE(isValidDateOrTimestamp("2026-11-14"));
    EXPECT_TRUE(isValidDateOrTimestamp("2026-11-14T21:36:26Z"));
    EXPECT_FALSE(isValidDateOrTimestamp("2026-13-14"));
    EXPECT_FALSE(isValidDateOrTimestamp("2026-11-14T25:00:00Z"));
    EXPECT_FALSE(isValidDateOrTimestamp("14/11/2026"));
}
