#include "database/DatabaseTransaction.hpp"

#include <atomic>

namespace optinspect::database {

namespace {
std::atomic<unsigned> g_savepoint_counter{0};
}

Transaction::Transaction(DatabaseConnection& connection) : connection_(connection) {
    if (connection_.inTransaction()) {
        savepoint_ = "optinspect_sp_" + std::to_string(++g_savepoint_counter);
        connection_.execute("SAVEPOINT " + savepoint_);
    } else {
        connection_.execute("BEGIN IMMEDIATE");
    }
    active_ = true;
}

Transaction::~Transaction() {
    if (active_) {
        try {
            rollback();
        } catch (...) {
            // Destructors must not throw; the connection will roll back on close.
        }
    }
}

void Transaction::commit() {
    if (!active_) return;
    if (savepoint_.empty()) {
        connection_.execute("COMMIT");
    } else {
        connection_.execute("RELEASE " + savepoint_);
    }
    active_ = false;
}

void Transaction::rollback() {
    if (!active_) return;
    active_ = false;
    if (savepoint_.empty()) {
        connection_.execute("ROLLBACK");
    } else {
        // ROLLBACK TO keeps the savepoint on the stack; RELEASE removes it.
        connection_.execute("ROLLBACK TO " + savepoint_);
        connection_.execute("RELEASE " + savepoint_);
    }
}

}  // namespace optinspect::database
