#include "database/ProductRepository.hpp"

#include "database/DatabaseError.hpp"
#include "database/Statement.hpp"
#include "database/TimeUtil.hpp"

namespace optinspect::database {

namespace {

const char* const kColumns =
    "product_id, product_code, product_name, model, revision, description, created_at";

Product readProduct(const Statement& stmt) {
    Product p;
    p.id = stmt.columnInt64(0);
    p.code = stmt.columnText(1);
    p.name = stmt.columnText(2);
    p.model = stmt.columnOptionalText(3);
    p.revision = stmt.columnOptionalText(4);
    p.description = stmt.columnOptionalText(5);
    p.created_at = stmt.columnText(6);
    return p;
}

std::string escapeLike(const std::string& text) {
    std::string out = "%";
    for (char c : text) {
        if (c == '%' || c == '_' || c == '\\') out += '\\';
        out += c;
    }
    out += '%';
    return out;
}

}  // namespace

std::int64_t ProductRepository::create(const Product& product) {
    Statement stmt(db_,
                   "INSERT INTO products (product_code, product_name, model, revision, description, "
                   "created_at) VALUES (:code, :name, :model, :revision, :description, :created_at)");
    stmt.bind(":code", product.code)
        .bind(":name", product.name)
        .bind(":model", product.model)
        .bind(":revision", product.revision)
        .bind(":description", product.description)
        .bind(":created_at", nowUtcTimestamp());
    try {
        stmt.execute();
    } catch (const ConstraintViolationError& e) {
        if (e.kind() == ConstraintKind::Unique) {
            throw ConstraintViolationError("Product code '" + product.code + "' already exists.",
                                           e.kind(), e.code(), e.extendedCode());
        }
        throw;
    }
    return db_.lastInsertRowId();
}

std::optional<Product> ProductRepository::getById(std::int64_t id) {
    Statement stmt(db_, std::string("SELECT ") + kColumns + " FROM products WHERE product_id = :id");
    stmt.bind(":id", id);
    if (!stmt.step()) return std::nullopt;
    return readProduct(stmt);
}

std::optional<Product> ProductRepository::getByCode(const std::string& code) {
    Statement stmt(db_,
                   std::string("SELECT ") + kColumns + " FROM products WHERE product_code = :code");
    stmt.bind(":code", code);
    if (!stmt.step()) return std::nullopt;
    return readProduct(stmt);
}

std::vector<Product> ProductRepository::list(const ProductFilter& filter) {
    std::string sql = std::string("SELECT ") + kColumns + " FROM products";
    if (filter.search) {
        sql += " WHERE product_code LIKE :pattern ESCAPE '\\' OR product_name LIKE :pattern ESCAPE '\\'";
    }
    sql += " ORDER BY product_code LIMIT :limit";

    Statement stmt(db_, sql);
    if (filter.search) stmt.bind(":pattern", escapeLike(*filter.search));
    stmt.bind(":limit", filter.limit.value_or(-1));

    std::vector<Product> products;
    while (stmt.step()) products.push_back(readProduct(stmt));
    return products;
}

bool ProductRepository::update(const Product& product) {
    Statement stmt(db_,
                   "UPDATE products SET product_name = :name, model = :model, "
                   "revision = :revision, description = :description WHERE product_id = :id");
    stmt.bind(":name", product.name)
        .bind(":model", product.model)
        .bind(":revision", product.revision)
        .bind(":description", product.description)
        .bind(":id", product.id);
    stmt.execute();
    return db_.changes() > 0;
}

bool ProductRepository::remove(std::int64_t id) {
    Statement stmt(db_, "DELETE FROM products WHERE product_id = :id");
    stmt.bind(":id", id);
    try {
        stmt.execute();
    } catch (const ConstraintViolationError& e) {
        if (e.kind() == ConstraintKind::ForeignKey) {
            throw ConstraintViolationError(
                "Product " + std::to_string(id) +
                    " cannot be deleted: inspections still reference it. Delete or reassign "
                    "those inspections first.",
                e.kind(), e.code(), e.extendedCode());
        }
        throw;
    }
    return db_.changes() > 0;
}

int ProductRepository::countInspections(std::int64_t id) {
    Statement stmt(db_, "SELECT COUNT(*) FROM inspections WHERE product_id = :id");
    stmt.bind(":id", id);
    stmt.step();
    return stmt.columnInt(0);
}

}  // namespace optinspect::database
