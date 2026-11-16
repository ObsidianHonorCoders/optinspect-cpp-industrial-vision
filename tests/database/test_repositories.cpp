#include <gtest/gtest.h>

#include "TempDatabase.hpp"
#include "database/DatabaseConnection.hpp"
#include "database/DatabaseError.hpp"
#include "database/DatabaseInitializer.hpp"
#include "database/DefectRepository.hpp"
#include "database/InspectionImageRepository.hpp"
#include "database/InspectionPersistenceService.hpp"
#include "database/InspectionRepository.hpp"
#include "database/ProductRepository.hpp"
#include "database/Statement.hpp"

#include <filesystem>
#include <fstream>

using namespace optinspect::database;
using optinspect::testing::TempDatabaseFile;

namespace {

class RepositoryTest : public ::testing::Test {
protected:
    RepositoryTest()
        : file("repo"),
          db(DatabaseConnection::open(file.path())),
          products(db),
          inspections(db),
          defects(db),
          images(db) {
        DatabaseInitializer::initialize(db);
    }

    int count(const std::string& table) {
        Statement stmt(db, "SELECT COUNT(*) FROM " + table);
        stmt.step();
        return stmt.columnInt(0);
    }

    std::int64_t makeProduct(const std::string& code = "PCB-001") {
        Product p;
        p.code = code;
        p.name = "Name of " + code;
        return products.create(p);
    }

    static InspectionRecord makeInspection(std::optional<std::int64_t> product_id = std::nullopt,
                                           InspectionStatus status = InspectionStatus::PASS,
                                           const std::string& timestamp = "2026-03-01T10:00:00Z") {
        InspectionRecord r;
        r.product_id = product_id;
        r.status = status;
        r.inspection_timestamp = timestamp;
        return r;
    }

    static DefectRecord makeDefect(double area = 100.0) {
        DefectRecord d;
        d.x = 10;
        d.y = 20;
        d.width = 5;
        d.height = 4;
        d.area = area;
        return d;
    }

    TempDatabaseFile file;
    DatabaseConnection db;
    ProductRepository products;
    InspectionRepository inspections;
    DefectRepository defects;
    InspectionImageRepository images;
};

}  // namespace

// --- Products -----------------------------------------------------------

TEST_F(RepositoryTest, ProductCreateAndReadById) {
    Product p;
    p.code = "PCB-001";
    p.name = "Controller Board";
    p.model = "CB-100";
    p.revision = "A";
    p.description = "Main controller";
    const std::int64_t id = products.create(p);
    EXPECT_GT(id, 0);

    const auto read = products.getById(id);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->id, id);
    EXPECT_EQ(read->code, "PCB-001");
    EXPECT_EQ(read->name, "Controller Board");
    EXPECT_EQ(read->model, std::optional<std::string>("CB-100"));
    EXPECT_EQ(read->revision, std::optional<std::string>("A"));
    EXPECT_EQ(read->description, std::optional<std::string>("Main controller"));
    EXPECT_EQ(read->created_at.size(), 20u);
}

TEST_F(RepositoryTest, ProductReadByCodeAndMissingIds) {
    const std::int64_t id = makeProduct("PCB-007");
    ASSERT_TRUE(products.getByCode("PCB-007").has_value());
    EXPECT_EQ(products.getByCode("PCB-007")->id, id);
    EXPECT_FALSE(products.getByCode("NOPE").has_value());
    EXPECT_FALSE(products.getById(9999).has_value());
}

TEST_F(RepositoryTest, DuplicateProductCodeIsRejected) {
    makeProduct("PCB-001");
    Product dup;
    dup.code = "PCB-001";
    dup.name = "Another";
    try {
        products.create(dup);
        FAIL() << "expected ConstraintViolationError";
    } catch (const ConstraintViolationError& e) {
        EXPECT_EQ(e.kind(), ConstraintKind::Unique);
    }
    EXPECT_EQ(count("products"), 1);
}

TEST_F(RepositoryTest, ProductWithBlankCodeOrNameIsRejected) {
    Product blank_code;
    blank_code.code = "   ";
    blank_code.name = "x";
    EXPECT_THROW(products.create(blank_code), ConstraintViolationError);

    Product blank_name;
    blank_name.code = "C1";
    blank_name.name = "";
    EXPECT_THROW(products.create(blank_name), ConstraintViolationError);
}

TEST_F(RepositoryTest, ProductListSupportsSearchAndLimit) {
    makeProduct("PCB-001");
    makeProduct("PCB-002");
    makeProduct("MOTOR-9");

    EXPECT_EQ(products.list().size(), 3u);
    EXPECT_EQ(products.list({std::string("pcb"), std::nullopt}).size(), 2u);
    EXPECT_EQ(products.list({std::string("motor"), std::nullopt}).size(), 1u);
    EXPECT_EQ(products.list({std::string("100%"), std::nullopt}).size(), 0u);  // '%' is literal
    ProductFilter limited;
    limited.limit = 1;
    EXPECT_EQ(products.list(limited).size(), 1u);
}

TEST_F(RepositoryTest, ProductUpdateChangesEditableFieldsOnly) {
    const std::int64_t id = makeProduct("PCB-001");
    auto p = products.getById(id);
    p->name = "Renamed";
    p->revision = "B";
    p->code = "SHOULD-NOT-CHANGE";
    EXPECT_TRUE(products.update(*p));

    const auto read = products.getById(id);
    EXPECT_EQ(read->name, "Renamed");
    EXPECT_EQ(read->revision, std::optional<std::string>("B"));
    EXPECT_EQ(read->code, "PCB-001");

    Product ghost;
    ghost.id = 4242;
    ghost.name = "x";
    EXPECT_FALSE(products.update(ghost));
}

TEST_F(RepositoryTest, ProductWithoutInspectionsCanBeDeleted) {
    const std::int64_t id = makeProduct();
    EXPECT_TRUE(products.remove(id));
    EXPECT_FALSE(products.getById(id).has_value());
    EXPECT_FALSE(products.remove(id));
}

TEST_F(RepositoryTest, ProductWithInspectionsCannotBeDeleted) {
    const std::int64_t product_id = makeProduct();
    const std::int64_t inspection_id = inspections.create(makeInspection(product_id));
    EXPECT_EQ(products.countInspections(product_id), 1);

    try {
        products.remove(product_id);
        FAIL() << "expected ConstraintViolationError";
    } catch (const ConstraintViolationError& e) {
        EXPECT_EQ(e.kind(), ConstraintKind::ForeignKey);
    }
    EXPECT_TRUE(products.getById(product_id).has_value());
    EXPECT_TRUE(inspections.getById(inspection_id).has_value());

    // After the dependent history is removed the product can go.
    ASSERT_TRUE(inspections.remove(inspection_id));
    EXPECT_TRUE(products.remove(product_id));
}

// --- Inspections --------------------------------------------------------

TEST_F(RepositoryTest, InspectionCreateAndReadBackAllFields) {
    const std::int64_t product_id = makeProduct();
    InspectionRecord r = makeInspection(product_id, InspectionStatus::FAIL, "2026-03-01T10:15:30Z");
    r.external_id = "20260301-111530";
    r.serial_number = "SN-0042";
    r.decision_reason = "area rule";
    r.alignment_status = AlignmentStatus::SUCCEEDED;
    r.alignment_confidence = 0.987;
    r.report_path = "C:/results/report.json";
    r.processing_time_ms = 123.5;
    r.configuration_snapshot = R"({"threshold":{"method":"OTSU"}})";
    r.notes = "first article";

    const std::int64_t id = inspections.create(r);
    const auto read = inspections.getById(id);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->id, id);
    EXPECT_EQ(read->external_id, r.external_id);
    EXPECT_EQ(read->product_id, std::optional<std::int64_t>(product_id));
    EXPECT_EQ(read->serial_number, r.serial_number);
    EXPECT_EQ(read->inspection_timestamp, "2026-03-01T10:15:30Z");
    EXPECT_EQ(read->status, InspectionStatus::FAIL);
    EXPECT_EQ(read->decision_reason, r.decision_reason);
    EXPECT_EQ(read->alignment_status, std::optional<AlignmentStatus>(AlignmentStatus::SUCCEEDED));
    EXPECT_DOUBLE_EQ(*read->alignment_confidence, 0.987);
    EXPECT_EQ(read->report_path, r.report_path);
    EXPECT_DOUBLE_EQ(*read->processing_time_ms, 123.5);
    EXPECT_EQ(read->configuration_snapshot, r.configuration_snapshot);
    EXPECT_TRUE(read->configuration_id.has_value());
    EXPECT_EQ(read->notes, r.notes);
    EXPECT_EQ(read->detected_defect_count, 0);

    EXPECT_EQ(inspections.getByExternalId("20260301-111530")->id, id);
    EXPECT_FALSE(inspections.getByExternalId("unknown").has_value());
    EXPECT_FALSE(inspections.getById(777).has_value());
}

TEST_F(RepositoryTest, InspectionWithoutProductOrOptionalDataIsAllowed) {
    const std::int64_t id = inspections.create(makeInspection());
    const auto read = inspections.getById(id);
    EXPECT_FALSE(read->product_id.has_value());
    EXPECT_FALSE(read->serial_number.has_value());
    EXPECT_FALSE(read->configuration_id.has_value());
}

TEST_F(RepositoryTest, InspectionRejectsInvalidStatusValue) {
    InspectionRecord r = makeInspection();
    r.status = static_cast<InspectionStatus>(99);
    try {
        inspections.create(r);
        FAIL() << "expected ConstraintViolationError";
    } catch (const ConstraintViolationError& e) {
        EXPECT_EQ(e.kind(), ConstraintKind::Check);
    }
    EXPECT_EQ(count("inspections"), 0);
}

TEST_F(RepositoryTest, InspectionRejectsMalformedTimestamp) {
    EXPECT_THROW(inspections.create(makeInspection(std::nullopt, InspectionStatus::PASS, "yesterday")),
                 std::invalid_argument);
    EXPECT_THROW(inspections.create(makeInspection(std::nullopt, InspectionStatus::PASS, "2026-03-01")),
                 std::invalid_argument);
}

TEST_F(RepositoryTest, InspectionReferencingMissingProductIsRejected) {
    try {
        inspections.create(makeInspection(424242));
        FAIL() << "expected ConstraintViolationError";
    } catch (const ConstraintViolationError& e) {
        EXPECT_EQ(e.kind(), ConstraintKind::ForeignKey);
    }
    EXPECT_EQ(count("inspections"), 0);
}

TEST_F(RepositoryTest, DuplicateExternalIdIsRejected) {
    InspectionRecord r = makeInspection();
    r.external_id = "run-1";
    inspections.create(r);
    EXPECT_THROW(inspections.create(r), ConstraintViolationError);
    EXPECT_EQ(count("inspections"), 1);
}

TEST_F(RepositoryTest, IdenticalConfigurationSnapshotsAreStoredOnce) {
    InspectionRecord a = makeInspection();
    a.configuration_snapshot = R"({"k":1})";
    InspectionRecord b = a;
    InspectionRecord c = a;
    c.configuration_snapshot = R"({"k":2})";

    const auto id_a = inspections.create(a);
    const auto id_b = inspections.create(b);
    const auto id_c = inspections.create(c);

    EXPECT_EQ(inspections.getById(id_a)->configuration_id, inspections.getById(id_b)->configuration_id);
    EXPECT_NE(inspections.getById(id_a)->configuration_id, inspections.getById(id_c)->configuration_id);
    EXPECT_EQ(count("configurations"), 2);
}

TEST_F(RepositoryTest, InspectionMetadataCanBeUpdated) {
    const std::int64_t product_id = makeProduct();
    const std::int64_t id = inspections.create(makeInspection());

    auto record = inspections.getById(id);
    record->product_id = product_id;
    record->serial_number = "SN-1";
    record->notes = "re-labelled";
    record->report_path = "D:/moved/report.json";
    EXPECT_TRUE(inspections.updateMetadata(*record));

    const auto read = inspections.getById(id);
    EXPECT_EQ(read->product_id, std::optional<std::int64_t>(product_id));
    EXPECT_EQ(read->serial_number, std::optional<std::string>("SN-1"));
    EXPECT_EQ(read->notes, std::optional<std::string>("re-labelled"));
    EXPECT_EQ(read->report_path, std::optional<std::string>("D:/moved/report.json"));

    record->id = 5555;
    EXPECT_FALSE(inspections.updateMetadata(*record));
}

TEST_F(RepositoryTest, RecordedDecisionAndTimestampAreImmutable) {
    const std::int64_t id = inspections.create(makeInspection(std::nullopt, InspectionStatus::FAIL));
    for (const char* sql : {"UPDATE inspections SET status = 'PASS' WHERE inspection_id = :id",
                            "UPDATE inspections SET inspection_timestamp = '2000-01-01T00:00:00Z' "
                            "WHERE inspection_id = :id",
                            "UPDATE inspections SET processing_time_ms = 1 WHERE inspection_id = :id"}) {
        Statement stmt(db, sql);
        stmt.bind(":id", id);
        EXPECT_THROW(stmt.execute(), ConstraintViolationError) << sql;
    }
    EXPECT_EQ(inspections.getById(id)->status, InspectionStatus::FAIL);
}

TEST_F(RepositoryTest, DeletingInspectionRemovesDependentRowsButNotFiles) {
    const auto file_path = std::filesystem::temp_directory_path() / "optinspect_test_keep_me.png";
    { std::ofstream(file_path) << "pixels"; }

    const std::int64_t id = inspections.create(makeInspection());
    defects.createMany(id, {makeDefect(), makeDefect(200)});
    InspectionImage img;
    img.inspection_id = id;
    img.type = ImageType::ANNOTATED;
    img.file_path = file_path.string();
    images.create(img);
    ASSERT_EQ(count("defects"), 2);
    ASSERT_EQ(count("inspection_images"), 1);

    EXPECT_TRUE(inspections.remove(id));
    EXPECT_EQ(count("inspections"), 0);
    EXPECT_EQ(count("defects"), 0);
    EXPECT_EQ(count("inspection_images"), 0);
    EXPECT_TRUE(std::filesystem::exists(file_path));
    EXPECT_FALSE(inspections.remove(id));

    std::filesystem::remove(file_path);
}

TEST_F(RepositoryTest, InspectionQueriesFilterByProductStatusSerialAndDate) {
    const auto p1 = makeProduct("P1");
    const auto p2 = makeProduct("P2");

    InspectionRecord a = makeInspection(p1, InspectionStatus::PASS, "2026-03-01T08:00:00Z");
    a.serial_number = "SN-A";
    InspectionRecord b = makeInspection(p1, InspectionStatus::FAIL, "2026-03-02T09:00:00Z");
    b.serial_number = "SN-B";
    InspectionRecord c = makeInspection(p2, InspectionStatus::FAIL, "2026-03-02T23:59:59Z");
    InspectionRecord d = makeInspection(p2, InspectionStatus::REVIEW, "2026-03-03T00:00:00Z");
    inspections.create(a);
    inspections.create(b);
    inspections.create(c);
    inspections.create(d);

    EXPECT_EQ(inspections.list().size(), 4u);

    InspectionFilter by_product;
    by_product.product_id = p1;
    EXPECT_EQ(inspections.list(by_product).size(), 2u);

    InspectionFilter by_status;
    by_status.status = InspectionStatus::FAIL;
    EXPECT_EQ(inspections.list(by_status).size(), 2u);

    InspectionFilter by_serial;
    by_serial.serial_number = "SN-B";
    ASSERT_EQ(inspections.list(by_serial).size(), 1u);
    EXPECT_EQ(inspections.list(by_serial)[0].status, InspectionStatus::FAIL);

    // A bare --to date includes the whole day; --from is inclusive.
    InspectionFilter day;
    day.from = "2026-03-02";
    day.to = "2026-03-02";
    EXPECT_EQ(inspections.list(day).size(), 2u);

    InspectionFilter exact_to;
    exact_to.from = "2026-03-02T00:00:00Z";
    exact_to.to = "2026-03-02T23:59:59Z";  // full timestamp: exclusive
    EXPECT_EQ(inspections.list(exact_to).size(), 1u);

    InspectionFilter combined;
    combined.product_id = p2;
    combined.status = InspectionStatus::FAIL;
    combined.from = "2026-03-01";
    EXPECT_EQ(inspections.list(combined).size(), 1u);
    EXPECT_EQ(inspections.count(combined), 1);

    InspectionFilter none;
    none.serial_number = "does-not-exist";
    EXPECT_TRUE(inspections.list(none).empty());
}

TEST_F(RepositoryTest, InspectionHistoryIsNewestFirstWithPaging) {
    inspections.create(makeInspection(std::nullopt, InspectionStatus::PASS, "2026-03-01T08:00:00Z"));
    inspections.create(makeInspection(std::nullopt, InspectionStatus::PASS, "2026-03-03T08:00:00Z"));
    inspections.create(makeInspection(std::nullopt, InspectionStatus::PASS, "2026-03-02T08:00:00Z"));

    InspectionFilter page;
    page.limit = 2;
    const auto first = inspections.list(page);
    ASSERT_EQ(first.size(), 2u);
    EXPECT_EQ(first[0].inspection_timestamp, "2026-03-03T08:00:00Z");
    EXPECT_EQ(first[1].inspection_timestamp, "2026-03-02T08:00:00Z");

    page.offset = 2;
    const auto second = inspections.list(page);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(second[0].inspection_timestamp, "2026-03-01T08:00:00Z");
}

TEST_F(RepositoryTest, MalformedDateFilterIsRejected) {
    InspectionFilter filter;
    filter.from = "last tuesday";
    EXPECT_THROW(inspections.list(filter), std::invalid_argument);
}

// --- Defects ------------------------------------------------------------

TEST_F(RepositoryTest, DefectCreateReadAndCountMaintenance) {
    const std::int64_t inspection_id = inspections.create(makeInspection());

    DefectRecord d = makeDefect(321.5);
    d.inspection_id = inspection_id;
    d.perimeter = 77.0;
    d.centroid_x = 12.5;
    d.centroid_y = 22.5;
    d.aspect_ratio = 1.25;
    d.confidence = 0.16;
    const std::int64_t id = defects.create(d);

    const auto read = defects.getById(id);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->inspection_id, inspection_id);
    EXPECT_EQ(read->x, 10);
    EXPECT_EQ(read->y, 20);
    EXPECT_EQ(read->width, 5);
    EXPECT_EQ(read->height, 4);
    EXPECT_DOUBLE_EQ(read->area, 321.5);
    EXPECT_DOUBLE_EQ(*read->perimeter, 77.0);
    EXPECT_DOUBLE_EQ(*read->centroid_x, 12.5);
    EXPECT_DOUBLE_EQ(*read->aspect_ratio, 1.25);
    EXPECT_DOUBLE_EQ(*read->confidence, 0.16);
    EXPECT_EQ(inspections.getById(inspection_id)->detected_defect_count, 1);

    EXPECT_FALSE(defects.getById(31337).has_value());
}

TEST_F(RepositoryTest, DefectForMissingInspectionIsRejected) {
    DefectRecord d = makeDefect();
    d.inspection_id = 12345;
    try {
        defects.create(d);
        FAIL() << "expected ConstraintViolationError";
    } catch (const ConstraintViolationError& e) {
        EXPECT_EQ(e.kind(), ConstraintKind::ForeignKey);
    }
}

TEST_F(RepositoryTest, DefectCheckConstraintsRejectInvalidValues) {
    const std::int64_t inspection_id = inspections.create(makeInspection());
    DefectRecord negative_area = makeDefect(-1.0);
    negative_area.inspection_id = inspection_id;
    EXPECT_THROW(defects.create(negative_area), ConstraintViolationError);

    DefectRecord bad_confidence = makeDefect();
    bad_confidence.inspection_id = inspection_id;
    bad_confidence.confidence = 1.5;
    EXPECT_THROW(defects.create(bad_confidence), ConstraintViolationError);
    EXPECT_EQ(count("defects"), 0);
}

TEST_F(RepositoryTest, CreateManyIsAtomic) {
    const std::int64_t inspection_id = inspections.create(makeInspection());
    std::vector<DefectRecord> batch = {makeDefect(10), makeDefect(20), makeDefect(-5), makeDefect(40)};
    EXPECT_THROW(defects.createMany(inspection_id, batch), ConstraintViolationError);
    EXPECT_EQ(count("defects"), 0);
    EXPECT_EQ(inspections.getById(inspection_id)->detected_defect_count, 0);

    batch.erase(batch.begin() + 2);
    EXPECT_EQ(defects.createMany(inspection_id, batch), 3);
    EXPECT_EQ(inspections.getById(inspection_id)->detected_defect_count, 3);
    EXPECT_EQ(defects.createMany(inspection_id, {}), 0);
}

TEST_F(RepositoryTest, DefectsCanBeFilteredByInspectionAreaAndConfidence) {
    const auto i1 = inspections.create(makeInspection());
    const auto i2 = inspections.create(makeInspection());
    std::vector<DefectRecord> first;
    for (double area : {50.0, 150.0, 500.0}) {
        DefectRecord d = makeDefect(area);
        d.confidence = area / 1000.0;
        first.push_back(d);
    }
    defects.createMany(i1, first);
    defects.createMany(i2, {makeDefect(900.0)});

    EXPECT_EQ(defects.listByInspection(i1).size(), 3u);
    EXPECT_EQ(defects.listByInspection(i2).size(), 1u);

    DefectFilter by_area;
    by_area.min_area = 100.0;
    by_area.max_area = 600.0;
    EXPECT_EQ(defects.list(by_area).size(), 2u);

    DefectFilter by_confidence;
    by_confidence.inspection_id = i1;
    by_confidence.min_confidence = 0.1;
    EXPECT_EQ(defects.list(by_confidence).size(), 2u);

    DefectFilter limited;
    limited.limit = 2;
    EXPECT_EQ(defects.list(limited).size(), 2u);
}

TEST_F(RepositoryTest, OnlyReviewNoteOfADefectIsEditable) {
    const auto inspection_id = inspections.create(makeInspection());
    DefectRecord d = makeDefect(80.0);
    d.inspection_id = inspection_id;
    const auto id = defects.create(d);

    EXPECT_TRUE(defects.updateReviewNote(id, std::string("scratch, acceptable")));
    EXPECT_EQ(defects.getById(id)->review_note, std::optional<std::string>("scratch, acceptable"));
    EXPECT_TRUE(defects.updateReviewNote(id, std::nullopt));
    EXPECT_FALSE(defects.getById(id)->review_note.has_value());
    EXPECT_FALSE(defects.updateReviewNote(999999, std::string("x")));

    Statement stmt(db, "UPDATE defects SET area = 1 WHERE defect_id = :id");
    stmt.bind(":id", id);
    EXPECT_THROW(stmt.execute(), ConstraintViolationError);
    EXPECT_DOUBLE_EQ(defects.getById(id)->area, 80.0);
}

TEST_F(RepositoryTest, DeletingDefectAdjustsInspectionCount) {
    const auto inspection_id = inspections.create(makeInspection());
    defects.createMany(inspection_id, {makeDefect(10), makeDefect(20)});
    const auto ids = defects.listByInspection(inspection_id);
    ASSERT_EQ(ids.size(), 2u);

    EXPECT_TRUE(defects.remove(ids[0].id));
    EXPECT_EQ(inspections.getById(inspection_id)->detected_defect_count, 1);
    EXPECT_FALSE(defects.remove(ids[0].id));
}

// --- Inspection images --------------------------------------------------

TEST_F(RepositoryTest, ImageCrudRoundTrip) {
    const auto inspection_id = inspections.create(makeInspection());
    InspectionImage img;
    img.inspection_id = inspection_id;
    img.type = ImageType::REFERENCE;
    img.file_path = "C:/data/ref.png";
    img.width = 640;
    img.height = 480;
    const auto id = images.create(img);

    const auto read = images.getById(id);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->type, ImageType::REFERENCE);
    EXPECT_EQ(read->file_path, "C:/data/ref.png");
    EXPECT_EQ(read->width, std::optional<int>(640));
    EXPECT_EQ(read->height, std::optional<int>(480));

    const auto by_type = images.getByInspectionAndType(inspection_id, ImageType::REFERENCE);
    ASSERT_TRUE(by_type.has_value());
    EXPECT_EQ(by_type->id, id);
    EXPECT_FALSE(images.getByInspectionAndType(inspection_id, ImageType::DIFFERENCE).has_value());

    InspectionImage changed = *read;
    changed.file_path = "D:/archive/ref.png";
    changed.width = 1280;
    changed.height = std::nullopt;
    EXPECT_TRUE(images.update(changed));
    EXPECT_EQ(images.getById(id)->file_path, "D:/archive/ref.png");
    EXPECT_EQ(images.getById(id)->width, std::optional<int>(1280));
    EXPECT_FALSE(images.getById(id)->height.has_value());

    EXPECT_TRUE(images.remove(id));
    EXPECT_FALSE(images.getById(id).has_value());
    EXPECT_FALSE(images.remove(id));
}

TEST_F(RepositoryTest, ImagesAreListedPerInspectionAndTypeIsUnique) {
    const auto i1 = inspections.create(makeInspection());
    const auto i2 = inspections.create(makeInspection());
    for (ImageType type : {ImageType::REFERENCE, ImageType::INSPECTED, ImageType::ANNOTATED}) {
        InspectionImage img;
        img.inspection_id = i1;
        img.type = type;
        img.file_path = "p/" + toString(type);
        images.create(img);
    }
    InspectionImage other;
    other.inspection_id = i2;
    other.type = ImageType::REFERENCE;
    other.file_path = "x";
    images.create(other);

    EXPECT_EQ(images.listByInspection(i1).size(), 3u);
    EXPECT_EQ(images.listByInspection(i2).size(), 1u);

    InspectionImage dup;
    dup.inspection_id = i1;
    dup.type = ImageType::REFERENCE;
    dup.file_path = "again";
    try {
        images.create(dup);
        FAIL() << "expected ConstraintViolationError";
    } catch (const ConstraintViolationError& e) {
        EXPECT_EQ(e.kind(), ConstraintKind::Unique);
    }
}

TEST_F(RepositoryTest, ImageValidationAndMissingInspection) {
    InspectionImage orphan;
    orphan.inspection_id = 8888;
    orphan.type = ImageType::REFERENCE;
    orphan.file_path = "x";
    EXPECT_THROW(images.create(orphan), ConstraintViolationError);

    const auto inspection_id = inspections.create(makeInspection());
    InspectionImage empty_path;
    empty_path.inspection_id = inspection_id;
    empty_path.file_path = "";
    EXPECT_THROW(images.create(empty_path), ConstraintViolationError);

    InspectionImage bad_type;
    bad_type.inspection_id = inspection_id;
    bad_type.type = static_cast<ImageType>(42);
    bad_type.file_path = "x";
    EXPECT_THROW(images.create(bad_type), ConstraintViolationError);

    EXPECT_THROW(parseImageType("THUMBNAIL"), std::invalid_argument);
    EXPECT_THROW(parseInspectionStatus("pass"), std::invalid_argument);
}

TEST_F(RepositoryTest, RemovingImageReferenceKeepsFileOnDisk) {
    const auto file_path = std::filesystem::temp_directory_path() / "optinspect_test_image_ref.png";
    { std::ofstream(file_path) << "pixels"; }

    const auto inspection_id = inspections.create(makeInspection());
    InspectionImage img;
    img.inspection_id = inspection_id;
    img.type = ImageType::ANNOTATED;
    img.file_path = file_path.string();
    const auto id = images.create(img);
    ASSERT_TRUE(images.remove(id));
    EXPECT_TRUE(std::filesystem::exists(file_path));
    std::filesystem::remove(file_path);
}

// --- Persistence service --------------------------------------------------

TEST_F(RepositoryTest, PersistenceServiceWritesAllPartsAtomically) {
    InspectionPersistenceService service(db);
    InspectionPersistenceRequest request;
    request.inspection = makeInspection(std::nullopt, InspectionStatus::FAIL);
    request.inspection.configuration_snapshot = "{}";
    request.defects = {makeDefect(10), makeDefect(20)};
    InspectionImage img;
    img.type = ImageType::REFERENCE;
    img.file_path = "ref.png";
    request.images = {img};

    const auto id = service.persist(request);
    EXPECT_EQ(inspections.getById(id)->detected_defect_count, 2);
    EXPECT_EQ(defects.listByInspection(id).size(), 2u);
    EXPECT_EQ(images.listByInspection(id).size(), 1u);

    EXPECT_TRUE(service.deleteInspection(id));
    EXPECT_EQ(count("inspections"), 0);
    EXPECT_EQ(count("defects"), 0);
    EXPECT_EQ(count("inspection_images"), 0);
    EXPECT_FALSE(service.deleteInspection(id));
}

TEST_F(RepositoryTest, PersistenceServiceRollsBackWhenALaterStepFails) {
    InspectionPersistenceService service(db);
    InspectionPersistenceRequest request;
    request.inspection = makeInspection();
    request.inspection.configuration_snapshot = R"({"only":"in rolled back tx"})";
    request.defects = {makeDefect(10)};
    InspectionImage img;
    img.type = ImageType::REFERENCE;
    img.file_path = "ref.png";
    request.images = {img, img};  // second insert violates UNIQUE(inspection_id, image_type)

    EXPECT_THROW(service.persist(request), ConstraintViolationError);
    EXPECT_EQ(count("inspections"), 0);
    EXPECT_EQ(count("defects"), 0);
    EXPECT_EQ(count("inspection_images"), 0);
    EXPECT_EQ(count("configurations"), 0);
    EXPECT_FALSE(db.inTransaction());
}
