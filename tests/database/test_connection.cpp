#include <gtest/gtest.h>

#include "TempDatabase.hpp"
#include "database/DatabaseConnection.hpp"
#include "database/DatabaseError.hpp"
#include "database/DatabaseInitializer.hpp"
#include "database/DatabaseTransaction.hpp"
#include "database/Statement.hpp"

#include <filesystem>
#include <fstream>

using namespace optinspect::database;
using optinspect::testing::TempDatabaseFile;

namespace {

int countRows(DatabaseConnection& db, const std::string& table) {
    Statement stmt(db, "SELECT COUNT(*) FROM " + table);
    stmt.step();
    return stmt.columnInt(0);
}

bool tableExists(DatabaseConnection& db, const std::string& name) {
    Statement stmt(db, "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = :name");
    stmt.bind(":name", name);
    return stmt.step();
}

void createScratchTable(DatabaseConnection& db) {
    db.execute("CREATE TABLE scratch (id INTEGER PRIMARY KEY, label TEXT, score REAL, qty INTEGER)");
}

}  // namespace

// --- Connection ---------------------------------------------------------

TEST(DatabaseConnectionTest, OpensAndCreatesDatabaseFile) {
    TempDatabaseFile file("conn");
    {
        DatabaseConnection db = DatabaseConnection::open(file.path());
        EXPECT_EQ(db.path(), file.path());
        db.execute("CREATE TABLE t (id INTEGER)");
    }
    EXPECT_TRUE(std::filesystem::exists(file.path()));
}

TEST(DatabaseConnectionTest, OpensInMemoryDatabase) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    db.execute("CREATE TABLE t (id INTEGER)");
    EXPECT_TRUE(tableExists(db, "t"));
}

TEST(DatabaseConnectionTest, InvalidPathThrowsDatabaseError) {
    const auto bad = std::filesystem::temp_directory_path() / "optinspect_no_such_dir_xyz" / "db.sqlite";
    EXPECT_THROW(DatabaseConnection::open(bad.string()), DatabaseError);
}

TEST(DatabaseConnectionTest, EmptyPathThrowsDatabaseError) {
    EXPECT_THROW(DatabaseConnection::open(""), DatabaseError);
}

TEST(DatabaseConnectionTest, MissingFileWithoutCreateThrows) {
    TempDatabaseFile file("missing");
    EXPECT_THROW(DatabaseConnection::open(file.path(), OpenMode::ReadWrite), DatabaseError);
    EXPECT_FALSE(std::filesystem::exists(file.path()));
}

TEST(DatabaseConnectionTest, ReadOnlyConnectionRejectsWrites) {
    TempDatabaseFile file("readonly");
    {
        DatabaseConnection db = DatabaseConnection::open(file.path());
        db.execute("CREATE TABLE t (id INTEGER)");
    }
    DatabaseConnection ro = DatabaseConnection::open(file.path(), OpenMode::ReadOnly);
    EXPECT_THROW(ro.execute("INSERT INTO t VALUES (1)"), DatabaseError);
}

TEST(DatabaseConnectionTest, ForeignKeysAreEnabledOnEveryConnection) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    EXPECT_EQ(db.pragmaValue("foreign_keys"), "1");
}

TEST(DatabaseConnectionTest, InvalidSqlThrowsDatabaseError) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    EXPECT_THROW(db.execute("THIS IS NOT SQL"), DatabaseError);
    EXPECT_THROW(Statement(db, "SELECT * FROM no_such_table"), DatabaseError);
}

TEST(DatabaseConnectionTest, MoveTransfersOwnership) {
    DatabaseConnection a = DatabaseConnection::openInMemory();
    a.execute("CREATE TABLE t (id INTEGER)");
    DatabaseConnection b = std::move(a);
    EXPECT_TRUE(tableExists(b, "t"));
}

// --- Prepared statements ------------------------------------------------

TEST(StatementTest, BindsAndReadsAllValueKinds) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    createScratchTable(db);

    Statement insert(db, "INSERT INTO scratch (label, score, qty) VALUES (:label, :score, :qty)");
    insert.bind(":label", "widget").bind(":score", 2.5).bind(":qty", 7);
    insert.execute();
    EXPECT_EQ(db.changes(), 1);
    EXPECT_EQ(db.lastInsertRowId(), 1);

    Statement select(db, "SELECT label, score, qty FROM scratch WHERE id = :id");
    select.bind(":id", 1);
    ASSERT_TRUE(select.step());
    EXPECT_EQ(select.columnText(0), "widget");
    EXPECT_DOUBLE_EQ(select.columnDouble(1), 2.5);
    EXPECT_EQ(select.columnInt(2), 7);
    EXPECT_FALSE(select.step());
}

TEST(StatementTest, BindsNullAndOptionalValues) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    createScratchTable(db);

    Statement insert(db, "INSERT INTO scratch (label, score, qty) VALUES (:label, :score, :qty)");
    insert.bind(":label", std::optional<std::string>{})
        .bind(":score", std::optional<double>{1.5})
        .bind(":qty", std::optional<int>{});
    insert.execute();

    Statement select(db, "SELECT label, score, qty FROM scratch");
    ASSERT_TRUE(select.step());
    EXPECT_TRUE(select.isNull(0));
    EXPECT_FALSE(select.columnOptionalText(0).has_value());
    EXPECT_EQ(select.columnOptionalDouble(1), std::optional<double>(1.5));
    EXPECT_FALSE(select.columnOptionalInt64(2).has_value());
}

TEST(StatementTest, HostileTextIsStoredLiterallyNotExecuted) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    createScratchTable(db);
    const std::string hostile = "x'); DROP TABLE scratch; --";

    Statement insert(db, "INSERT INTO scratch (label) VALUES (:label)");
    insert.bind(":label", hostile);
    insert.execute();

    EXPECT_TRUE(tableExists(db, "scratch"));
    Statement select(db, "SELECT label FROM scratch");
    ASSERT_TRUE(select.step());
    EXPECT_EQ(select.columnText(0), hostile);
}

TEST(StatementTest, UnknownParameterNameThrows) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    createScratchTable(db);
    Statement stmt(db, "INSERT INTO scratch (label) VALUES (:label)");
    EXPECT_THROW(stmt.bind(":nope", "x"), DatabaseError);
}

TEST(StatementTest, ResetAllowsReuseWithNewBindings) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    createScratchTable(db);
    Statement insert(db, "INSERT INTO scratch (label) VALUES (:label)");
    for (const char* label : {"a", "b", "c"}) {
        insert.reset();
        insert.bind(":label", label);
        insert.execute();
    }
    EXPECT_EQ(countRows(db, "scratch"), 3);
}

TEST(StatementTest, ConstraintFailureThrowsConstraintViolationWithKind) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    db.execute("CREATE TABLE u (code TEXT UNIQUE NOT NULL)");
    Statement insert(db, "INSERT INTO u (code) VALUES (:code)");
    insert.bind(":code", "A");
    insert.execute();

    Statement dup(db, "INSERT INTO u (code) VALUES (:code)");
    dup.bind(":code", "A");
    try {
        dup.execute();
        FAIL() << "expected ConstraintViolationError";
    } catch (const ConstraintViolationError& e) {
        EXPECT_EQ(e.kind(), ConstraintKind::Unique);
    }

    Statement null_insert(db, "INSERT INTO u (code) VALUES (:code)");
    null_insert.bind(":code", nullptr);
    try {
        null_insert.execute();
        FAIL() << "expected ConstraintViolationError";
    } catch (const ConstraintViolationError& e) {
        EXPECT_EQ(e.kind(), ConstraintKind::NotNull);
    }
}

// --- Transactions -------------------------------------------------------

TEST(TransactionTest, CommitPersistsChanges) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    createScratchTable(db);
    {
        Transaction tx(db);
        EXPECT_TRUE(db.inTransaction());
        db.execute("INSERT INTO scratch (label) VALUES ('kept')");
        tx.commit();
    }
    EXPECT_FALSE(db.inTransaction());
    EXPECT_EQ(countRows(db, "scratch"), 1);
}

TEST(TransactionTest, DestructionWithoutCommitRollsBack) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    createScratchTable(db);
    {
        Transaction tx(db);
        db.execute("INSERT INTO scratch (label) VALUES ('discarded')");
    }
    EXPECT_FALSE(db.inTransaction());
    EXPECT_EQ(countRows(db, "scratch"), 0);
}

TEST(TransactionTest, ExplicitRollbackDiscardsChanges) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    createScratchTable(db);
    Transaction tx(db);
    db.execute("INSERT INTO scratch (label) VALUES ('discarded')");
    tx.rollback();
    EXPECT_FALSE(tx.active());
    EXPECT_EQ(countRows(db, "scratch"), 0);
}

TEST(TransactionTest, ExceptionDuringTransactionRollsBackEverything) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    db.execute("CREATE TABLE u (code TEXT UNIQUE)");
    try {
        Transaction tx(db);
        db.execute("INSERT INTO u VALUES ('a')");
        db.execute("INSERT INTO u VALUES ('a')");  // duplicate -> throws
        tx.commit();
    } catch (const ConstraintViolationError&) {
    }
    EXPECT_EQ(countRows(db, "u"), 0);
}

TEST(TransactionTest, NestedTransactionRollsBackOnlyInnerWork) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    createScratchTable(db);
    Transaction outer(db);
    db.execute("INSERT INTO scratch (label) VALUES ('outer')");
    {
        Transaction inner(db);
        db.execute("INSERT INTO scratch (label) VALUES ('inner')");
        inner.rollback();
    }
    EXPECT_TRUE(db.inTransaction());
    outer.commit();
    EXPECT_EQ(countRows(db, "scratch"), 1);
}

// --- Initialization and schema ------------------------------------------

TEST(DatabaseInitializerTest, CreatesSchemaAndRecordsVersion) {
    TempDatabaseFile file("init");
    DatabaseConnection db = DatabaseConnection::open(file.path());
    EXPECT_EQ(DatabaseInitializer::schemaVersion(db), 0);

    const InitializationResult result = DatabaseInitializer::initialize(db);
    EXPECT_TRUE(result.created);
    EXPECT_EQ(result.previous_version, 0);
    EXPECT_EQ(result.current_version, DatabaseInitializer::latestVersion());
    EXPECT_EQ(DatabaseInitializer::schemaVersion(db), DatabaseInitializer::latestVersion());

    for (const char* table : {"products", "configurations", "inspections", "defects", "inspection_images"}) {
        EXPECT_TRUE(tableExists(db, table)) << table;
    }
}

TEST(DatabaseInitializerTest, CreatesExpectedIndexes) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    DatabaseInitializer::initialize(db);
    Statement stmt(db, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name LIKE 'idx_%'");
    stmt.step();
    EXPECT_GE(stmt.columnInt(0), 6);
}

TEST(DatabaseInitializerTest, UsesWriteAheadLoggingForFileDatabases) {
    TempDatabaseFile file("wal");
    DatabaseConnection db = DatabaseConnection::open(file.path());
    DatabaseInitializer::initialize(db);
    EXPECT_EQ(db.pragmaValue("journal_mode"), "wal");
}

TEST(DatabaseInitializerTest, IsIdempotentAndPreservesExistingData) {
    TempDatabaseFile file("idem");
    DatabaseConnection db = DatabaseConnection::open(file.path());
    DatabaseInitializer::initialize(db);
    db.execute("INSERT INTO products (product_code, product_name) VALUES ('P1', 'Board')");

    const InitializationResult again = DatabaseInitializer::initialize(db);
    EXPECT_FALSE(again.created);
    EXPECT_TRUE(again.applied_versions.empty());
    EXPECT_EQ(countRows(db, "products"), 1);
}

TEST(DatabaseInitializerTest, ReopeningKeepsDataAndVersion) {
    TempDatabaseFile file("reopen");
    {
        DatabaseConnection db = DatabaseConnection::open(file.path());
        DatabaseInitializer::initialize(db);
        db.execute("INSERT INTO products (product_code, product_name) VALUES ('P1', 'Board')");
    }
    DatabaseConnection db = DatabaseConnection::open(file.path());
    DatabaseInitializer::initialize(db);
    EXPECT_EQ(countRows(db, "products"), 1);
}

TEST(DatabaseInitializerTest, RefusesToTouchForeignDatabase) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    db.execute("CREATE TABLE somebody_elses_table (id INTEGER)");
    EXPECT_THROW(DatabaseInitializer::initialize(db), DatabaseError);
    EXPECT_FALSE(tableExists(db, "products"));
    EXPECT_TRUE(tableExists(db, "somebody_elses_table"));
}

TEST(DatabaseInitializerTest, RefusesDatabaseNewerThanThisBuild) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    DatabaseInitializer::initialize(db);
    db.execute("PRAGMA user_version = " + std::to_string(DatabaseInitializer::latestVersion() + 1));
    EXPECT_THROW(DatabaseInitializer::initialize(db), DatabaseError);
    EXPECT_THROW(DatabaseInitializer::requireCurrentSchema(db), DatabaseError);
}

TEST(DatabaseInitializerTest, RequireCurrentSchemaRejectsUninitializedDatabase) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    EXPECT_THROW(DatabaseInitializer::requireCurrentSchema(db), DatabaseError);
    DatabaseInitializer::initialize(db);
    EXPECT_NO_THROW(DatabaseInitializer::requireCurrentSchema(db));
}

TEST(DatabaseSchemaTest, ForeignKeyEnforcementRejectsOrphanInspection) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    DatabaseInitializer::initialize(db);
    try {
        db.execute(
            "INSERT INTO inspections (product_id, inspection_timestamp, status) "
            "VALUES (999, '2026-01-01T00:00:00Z', 'PASS')");
        FAIL() << "expected a foreign key violation";
    } catch (const ConstraintViolationError& e) {
        EXPECT_EQ(e.kind(), ConstraintKind::ForeignKey);
    }
}

TEST(DatabaseSchemaTest, CheckConstraintRejectsInvalidStatus) {
    DatabaseConnection db = DatabaseConnection::openInMemory();
    DatabaseInitializer::initialize(db);
    try {
        db.execute(
            "INSERT INTO inspections (inspection_timestamp, status) "
            "VALUES ('2026-01-01T00:00:00Z', 'MAYBE')");
        FAIL() << "expected a CHECK violation";
    } catch (const ConstraintViolationError& e) {
        EXPECT_EQ(e.kind(), ConstraintKind::Check);
    }
}
