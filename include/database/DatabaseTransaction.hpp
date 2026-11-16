#pragma once

#include "database/DatabaseConnection.hpp"

#include <string>

namespace optinspect::database {

/// Scoped transaction. Rolls back on destruction unless commit() was called,
/// so an exception thrown part-way through a multi-statement write can never
/// leave partial data behind.
///
/// When no transaction is open it issues BEGIN IMMEDIATE (taking the write
/// lock up-front, so lock conflicts surface at begin rather than mid-way).
/// When one is already open it creates a SAVEPOINT instead, so Transaction
/// objects can be nested safely, e.g. a repository call made from within a
/// service-level transaction.
class Transaction {
public:
    explicit Transaction(DatabaseConnection& connection);
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    ~Transaction();

    void commit();
    void rollback();
    bool active() const { return active_; }

private:
    DatabaseConnection& connection_;
    std::string savepoint_;  ///< Empty for an outermost transaction.
    bool active_ = false;
};

}  // namespace optinspect::database
