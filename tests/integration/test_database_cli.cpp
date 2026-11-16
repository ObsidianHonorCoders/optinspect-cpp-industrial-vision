#include <gtest/gtest.h>

#include "DatabaseCommands.hpp"
#include "InspectionRecorder.hpp"
#include "TempDatabase.hpp"
#include "database/DatabaseConnection.hpp"
#include "database/DatabaseInitializer.hpp"
#include "database/InspectionPersistenceService.hpp"
#include "third_party/nlohmann/json.hpp"

#include <sstream>
#include <filesystem>

using namespace optinspect;
using namespace optinspect::database;
using optinspect::testing::TempDatabaseFile;

namespace {

struct CommandResult {
    int exit_code;
    std::string out;
    std::string err;
};

CommandResult run(std::vector<std::string> args) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = runDatabaseCommand(args, out, err);
    return {code, out.str(), err.str()};
}

class DatabaseCliTest : public ::testing::Test {
protected:
    DatabaseCliTest() : file("cli") {}

    CommandResult cmd(std::vector<std::string> args) {
        args.push_back("--database");
        args.push_back(file.path());
        return run(std::move(args));
    }

    void initDatabase() { ASSERT_EQ(cmd({"db", "init"}).exit_code, 0); }

    /// Stores a synthetic inspection directly (no image processing needed).
    std::int64_t addInspection(InspectionStatus status, const std::string& timestamp,
                               std::optional<std::int64_t> product_id = std::nullopt,
                               int defects = 0) {
        DatabaseConnection db = DatabaseConnection::open(file.path(), OpenMode::ReadWrite);
        InspectionPersistenceRequest request;
        request.inspection.status = status;
        request.inspection.inspection_timestamp = timestamp;
        request.inspection.product_id = product_id;
        request.inspection.processing_time_ms = 100.0;
        for (int i = 0; i < defects; ++i) {
            DefectRecord d;
            d.width = 4;
            d.height = 4;
            d.area = 16.0 * (i + 1);
            request.defects.push_back(d);
        }
        return InspectionPersistenceService(db).persist(request);
    }

    TempDatabaseFile file;
};

}  // namespace

TEST_F(DatabaseCliTest, InitCreatesDatabaseAndIsSafeToRepeat) {
    const CommandResult first = cmd({"db", "init"});
    EXPECT_EQ(first.exit_code, 0);
    EXPECT_NE(first.out.find("created schema"), std::string::npos);

    const CommandResult second = cmd({"db", "init"});
    EXPECT_EQ(second.exit_code, 0);
    EXPECT_NE(second.out.find("already up to date"), std::string::npos);

    const CommandResult info = cmd({"db", "info"});
    EXPECT_EQ(info.exit_code, 0);
    EXPECT_NE(info.out.find("Schema version : 1"), std::string::npos);
}

TEST_F(DatabaseCliTest, CommandsRefuseMissingOrUninitializedDatabase) {
    const CommandResult missing = cmd({"product", "list"});
    EXPECT_EQ(missing.exit_code, 74);
    EXPECT_NE(missing.err.find("db init"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(file.path()));  // never created implicitly

    { DatabaseConnection::open(file.path()); }  // empty file, no schema
    const CommandResult uninitialized = cmd({"product", "list"});
    EXPECT_EQ(uninitialized.exit_code, 74);
    EXPECT_NE(uninitialized.err.find("not initialized"), std::string::npos);
}

TEST_F(DatabaseCliTest, ProductLifecycle) {
    initDatabase();
    EXPECT_EQ(cmd({"product", "create", "--code", "PCB-001", "--name", "Controller Board", "--revision",
                   "A"})
                  .exit_code,
              0);

    const CommandResult duplicate = cmd({"product", "create", "--code", "PCB-001", "--name", "Other"});
    EXPECT_EQ(duplicate.exit_code, 74);
    EXPECT_NE(duplicate.err.find("already exists"), std::string::npos);

    const CommandResult list = cmd({"product", "list"});
    EXPECT_NE(list.out.find("PCB-001"), std::string::npos);
    EXPECT_NE(list.out.find("Controller Board"), std::string::npos);

    EXPECT_EQ(cmd({"product", "update", "--id", "1", "--revision", "B"}).exit_code, 0);
    const CommandResult show = cmd({"product", "show", "--id", "1"});
    EXPECT_NE(show.out.find("Revision    : B"), std::string::npos);

    const auto json = nlohmann::json::parse(cmd({"product", "show", "--code", "PCB-001", "--format", "json"}).out);
    EXPECT_EQ(json.at("code"), "PCB-001");
    EXPECT_EQ(json.at("revision"), "B");

    EXPECT_EQ(cmd({"product", "show", "--id", "99"}).exit_code, 66);
    EXPECT_EQ(cmd({"product", "delete", "--id", "1"}).exit_code, 0);
    EXPECT_EQ(cmd({"product", "delete", "--id", "1"}).exit_code, 66);
}

TEST_F(DatabaseCliTest, ProductWithInspectionsCannotBeDeleted) {
    initDatabase();
    ASSERT_EQ(cmd({"product", "create", "--code", "P", "--name", "N"}).exit_code, 0);
    addInspection(InspectionStatus::PASS, "2026-04-01T10:00:00Z", 1);

    const CommandResult refused = cmd({"product", "delete", "--id", "1"});
    EXPECT_EQ(refused.exit_code, 74);
    EXPECT_NE(refused.err.find("inspections still reference"), std::string::npos);
    EXPECT_EQ(cmd({"product", "show", "--id", "1"}).exit_code, 0);
}

TEST_F(DatabaseCliTest, InspectionHistoryListShowUpdateDelete) {
    initDatabase();
    ASSERT_EQ(cmd({"product", "create", "--code", "P", "--name", "N"}).exit_code, 0);
    const auto first = addInspection(InspectionStatus::PASS, "2026-04-01T10:00:00Z", 1);
    const auto second = addInspection(InspectionStatus::FAIL, "2026-04-02T10:00:00Z", 1, 2);

    const auto all = nlohmann::json::parse(cmd({"inspection", "list", "--format", "json"}).out);
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].at("id"), second);  // newest first

    const auto failed =
        nlohmann::json::parse(cmd({"inspection", "list", "--status", "FAIL", "--format", "json"}).out);
    ASSERT_EQ(failed.size(), 1u);

    const auto by_date = nlohmann::json::parse(
        cmd({"inspection", "list", "--from", "2026-04-02", "--to", "2026-04-02", "--format", "json"}).out);
    ASSERT_EQ(by_date.size(), 1u);
    EXPECT_EQ(by_date[0].at("status"), "FAIL");

    const auto detail = nlohmann::json::parse(
        cmd({"inspection", "show", "--id", std::to_string(second), "--format", "json"}).out);
    EXPECT_EQ(detail.at("defect_count"), 2);
    EXPECT_EQ(detail.at("defects").size(), 2u);
    EXPECT_EQ(detail.at("product").at("code"), "P");

    const CommandResult defects = cmd({"defect", "list", "--inspection-id", std::to_string(second)});
    EXPECT_NE(defects.out.find("2 defect region(s)"), std::string::npos);

    EXPECT_EQ(cmd({"inspection", "update", "--id", std::to_string(first), "--serial", "SN-9", "--notes",
                   "checked"})
                  .exit_code,
              0);
    EXPECT_NE(cmd({"inspection", "show", "--id", std::to_string(first)}).out.find("SN-9"),
              std::string::npos);

    EXPECT_EQ(cmd({"inspection", "delete", "--id", std::to_string(first)}).exit_code, 0);
    EXPECT_EQ(cmd({"inspection", "delete", "--id", std::to_string(first)}).exit_code, 66);
    EXPECT_EQ(cmd({"inspection", "show", "--id", std::to_string(first)}).exit_code, 66);
}

TEST_F(DatabaseCliTest, DefectNoteAndDelete) {
    initDatabase();
    addInspection(InspectionStatus::FAIL, "2026-04-02T10:00:00Z", std::nullopt, 2);
    EXPECT_EQ(cmd({"defect", "note", "--id", "1", "--note", "dust"}).exit_code, 0);
    EXPECT_NE(cmd({"defect", "list", "--inspection-id", "1"}).out.find("dust"), std::string::npos);
    EXPECT_EQ(cmd({"defect", "delete", "--id", "1"}).exit_code, 0);
    EXPECT_EQ(cmd({"defect", "delete", "--id", "1"}).exit_code, 66);
}

TEST_F(DatabaseCliTest, AnalyticsCommandsReportStoredData) {
    initDatabase();
    ASSERT_EQ(cmd({"product", "create", "--code", "P", "--name", "N"}).exit_code, 0);
    addInspection(InspectionStatus::PASS, "2026-04-01T10:00:00Z", 1);
    addInspection(InspectionStatus::FAIL, "2026-04-01T11:00:00Z", 1, 3);
    addInspection(InspectionStatus::REVIEW, "2026-04-02T11:00:00Z");
    addInspection(InspectionStatus::PASS, "2026-04-03T11:00:00Z", 1);

    const auto summary = nlohmann::json::parse(cmd({"analytics", "summary", "--format", "json"}).out);
    EXPECT_EQ(summary.at("total"), 4);
    EXPECT_EQ(summary.at("pass"), 2);
    EXPECT_EQ(summary.at("fail"), 1);
    EXPECT_EQ(summary.at("review"), 1);
    EXPECT_DOUBLE_EQ(summary.at("fail_rate"), 0.25);

    const auto defects = nlohmann::json::parse(cmd({"analytics", "defects", "--format", "json"}).out);
    EXPECT_EQ(defects.at("total_defects"), 3);

    const auto products = nlohmann::json::parse(cmd({"analytics", "products", "--format", "json"}).out);
    ASSERT_EQ(products.size(), 2u);  // product P plus the unassigned inspection

    const auto perf = nlohmann::json::parse(cmd({"analytics", "performance", "--format", "json"}).out);
    EXPECT_EQ(perf.at("overall").at("samples"), 4);

    const auto trend =
        nlohmann::json::parse(cmd({"analytics", "trend", "--period", "day", "--format", "json"}).out);
    EXPECT_EQ(trend.size(), 3u);

    const CommandResult table = cmd({"analytics", "summary", "--product-id", "1"});
    EXPECT_EQ(table.exit_code, 0);
    EXPECT_NE(table.out.find("PASS"), std::string::npos);
    EXPECT_NE(table.out.find("product 1"), std::string::npos);
}

TEST_F(DatabaseCliTest, InvalidUsageReturnsUsageErrorWithoutTouchingData) {
    initDatabase();
    EXPECT_EQ(cmd({"product", "create", "--code", "X"}).exit_code, 64);                 // missing --name
    EXPECT_EQ(cmd({"product", "frobnicate"}).exit_code, 64);                             // unknown action
    EXPECT_EQ(cmd({"inspection", "list", "--status", "MAYBE"}).exit_code, 64);           // bad status
    EXPECT_EQ(cmd({"inspection", "list", "--from", "yesterday"}).exit_code, 64);         // bad date
    EXPECT_EQ(cmd({"analytics", "trend", "--period", "decade"}).exit_code, 64);          // bad period
    EXPECT_EQ(cmd({"analytics", "summary", "--format", "xml"}).exit_code, 64);           // bad format
    EXPECT_EQ(cmd({"product", "show"}).exit_code, 64);                                   // missing id/code
    EXPECT_EQ(cmd({"product", "show", "--id", "abc"}).exit_code, 64);                    // not a number
    EXPECT_EQ(run({"product"}).exit_code, 64);                                           // missing action
    EXPECT_EQ(cmd({"product", "list", "--bogus", "1"}).exit_code, 64);                   // unknown flag
    EXPECT_EQ(run({"product", "--help"}).exit_code, 0);
    EXPECT_TRUE(isDatabaseCommand("analytics"));
    EXPECT_FALSE(isDatabaseCommand("--reference"));
}

TEST_F(DatabaseCliTest, SqlInjectionInTextArgumentsIsStoredLiterally) {
    initDatabase();
    const std::string hostile = "x'); DROP TABLE products; --";
    EXPECT_EQ(cmd({"product", "create", "--code", hostile, "--name", "N"}).exit_code, 0);
    EXPECT_NE(cmd({"product", "list"}).out.find("DROP TABLE"), std::string::npos);
    EXPECT_EQ(cmd({"product", "list", "--search", "'; DROP"}).exit_code, 0);
    EXPECT_EQ(cmd({"inspection", "list", "--serial", "' OR 1=1 --"}).exit_code, 0);
}
