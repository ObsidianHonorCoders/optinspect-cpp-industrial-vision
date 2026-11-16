#pragma once

#include "database/DatabaseConnection.hpp"
#include "database/Models.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace optinspect::database {

/// CRUD access to the `products` table. Holds a non-owning reference to the
/// connection, which must outlive the repository.
class ProductRepository {
public:
    explicit ProductRepository(DatabaseConnection& connection) : db_(connection) {}

    /// Inserts a product and returns its new id. `id` and `created_at` of the
    /// argument are ignored. Throws ConstraintViolationError (kind Unique) if
    /// the product code already exists.
    std::int64_t create(const Product& product);

    std::optional<Product> getById(std::int64_t id);
    std::optional<Product> getByCode(const std::string& code);
    std::vector<Product> list(const ProductFilter& filter = {});

    /// Updates name, model, revision and description. The product code is the
    /// business key and cannot be changed. Returns false if the id does not exist.
    bool update(const Product& product);

    /// Deletes a product. Throws ConstraintViolationError (kind ForeignKey)
    /// while inspections still reference it: history is never deleted silently.
    /// Returns false if the id does not exist.
    bool remove(std::int64_t id);

    /// Number of inspections currently referencing the product.
    int countInspections(std::int64_t id);

private:
    DatabaseConnection& db_;
};

}  // namespace optinspect::database
