#include "Schema.hpp"

namespace optinspect::database {

namespace {

// Version 1: initial Phase 2 schema.
//
// Foreign-key policy (see docs/database-design.md):
//   products -> inspections       RESTRICT  (history is never silently deleted)
//   configurations -> inspections RESTRICT
//   inspections -> defects        CASCADE   (children of a deleted inspection)
//   inspections -> images         CASCADE
//
// Image files are referenced by path and never stored in the database.
const char* const kSchemaV1 = R"SQL(
CREATE TABLE products (
    product_id   INTEGER PRIMARY KEY,
    product_code TEXT NOT NULL UNIQUE CHECK (length(trim(product_code)) > 0),
    product_name TEXT NOT NULL CHECK (length(trim(product_name)) > 0),
    model        TEXT,
    revision     TEXT,
    description  TEXT,
    created_at   TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
);

CREATE TABLE configurations (
    configuration_id INTEGER PRIMARY KEY,
    config_json      TEXT NOT NULL UNIQUE,
    created_at       TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
);

CREATE TABLE inspections (
    inspection_id         INTEGER PRIMARY KEY,
    external_id           TEXT UNIQUE,
    product_id            INTEGER REFERENCES products (product_id) ON DELETE RESTRICT,
    serial_number         TEXT,
    inspection_timestamp  TEXT NOT NULL,
    status                TEXT NOT NULL CHECK (status IN ('PASS', 'FAIL', 'REVIEW')),
    decision_reason       TEXT,
    alignment_status      TEXT CHECK (alignment_status IS NULL
                                      OR alignment_status IN ('SUCCEEDED', 'FAILED')),
    alignment_confidence  REAL,
    report_path           TEXT,
    detected_defect_count INTEGER NOT NULL DEFAULT 0 CHECK (detected_defect_count >= 0),
    processing_time_ms    REAL CHECK (processing_time_ms IS NULL OR processing_time_ms >= 0),
    configuration_id      INTEGER REFERENCES configurations (configuration_id) ON DELETE RESTRICT,
    notes                 TEXT,
    created_at            TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
);

CREATE TABLE defects (
    defect_id     INTEGER PRIMARY KEY,
    inspection_id INTEGER NOT NULL REFERENCES inspections (inspection_id) ON DELETE CASCADE,
    x             INTEGER NOT NULL,
    y             INTEGER NOT NULL,
    width         INTEGER NOT NULL CHECK (width >= 0),
    height        INTEGER NOT NULL CHECK (height >= 0),
    area          REAL NOT NULL CHECK (area >= 0),
    perimeter     REAL CHECK (perimeter IS NULL OR perimeter >= 0),
    centroid_x    REAL,
    centroid_y    REAL,
    aspect_ratio  REAL CHECK (aspect_ratio IS NULL OR aspect_ratio >= 0),
    confidence    REAL CHECK (confidence IS NULL OR (confidence >= 0 AND confidence <= 1)),
    review_note   TEXT,
    created_at    TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
);

CREATE TABLE inspection_images (
    image_id      INTEGER PRIMARY KEY,
    inspection_id INTEGER NOT NULL REFERENCES inspections (inspection_id) ON DELETE CASCADE,
    image_type    TEXT NOT NULL CHECK (image_type IN ('REFERENCE', 'INSPECTED', 'ANNOTATED', 'DIFFERENCE')),
    file_path     TEXT NOT NULL CHECK (length(file_path) > 0),
    width         INTEGER CHECK (width IS NULL OR width > 0),
    height        INTEGER CHECK (height IS NULL OR height > 0),
    created_at    TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')),
    UNIQUE (inspection_id, image_type)
);

-- History by product, newest first; also serves per-product analytics.
CREATE INDEX idx_inspections_product_time ON inspections (product_id, inspection_timestamp);
-- Date-range scans and time-bucketed analytics.
CREATE INDEX idx_inspections_time ON inspections (inspection_timestamp);
-- Status filtering within a date range (PASS/FAIL/REVIEW dashboards).
CREATE INDEX idx_inspections_status_time ON inspections (status, inspection_timestamp);
-- Traceability lookups by board serial number; NULL serials are not indexed.
CREATE INDEX idx_inspections_serial ON inspections (serial_number) WHERE serial_number IS NOT NULL;
-- Foreign-key lookup used by RESTRICT checks and joins.
CREATE INDEX idx_inspections_configuration ON inspections (configuration_id);
-- Child-row access: defects of an inspection (also used by cascade deletes).
CREATE INDEX idx_defects_inspection ON defects (inspection_id);
-- Area-range filters on defects.
CREATE INDEX idx_defects_area ON defects (area);

-- detected_defect_count is derived data: triggers keep it equal to the number
-- of defect rows so it can never drift from the rows it summarises.
CREATE TRIGGER trg_defects_count_insert AFTER INSERT ON defects
BEGIN
    UPDATE inspections SET detected_defect_count = detected_defect_count + 1
    WHERE inspection_id = NEW.inspection_id;
END;

CREATE TRIGGER trg_defects_count_delete AFTER DELETE ON defects
BEGIN
    UPDATE inspections SET detected_defect_count = detected_defect_count - 1
    WHERE inspection_id = OLD.inspection_id;
END;

-- Measurement geometry is historical data and must not be rewritten; only
-- review_note may change on an existing defect row.
CREATE TRIGGER trg_defects_immutable BEFORE UPDATE OF
    inspection_id, x, y, width, height, area, perimeter, centroid_x, centroid_y,
    aspect_ratio, confidence, created_at
ON defects
BEGIN
    SELECT RAISE(ABORT, 'defect measurements are immutable');
END;

-- Likewise the recorded decision and measurements of an inspection are
-- immutable; only product_id, serial_number, report_path and notes may be
-- corrected after the fact.
CREATE TRIGGER trg_inspections_immutable BEFORE UPDATE OF
    external_id, inspection_timestamp, status, decision_reason, alignment_status,
    alignment_confidence, processing_time_ms, configuration_id, created_at
ON inspections
BEGIN
    SELECT RAISE(ABORT, 'recorded inspection results are immutable');
END;
)SQL";

}  // namespace

const std::vector<Migration>& migrations() {
    static const std::vector<Migration> all = {
        {1, "Initial schema: products, configurations, inspections, defects, inspection_images",
         kSchemaV1},
    };
    return all;
}

int latestSchemaVersion() { return migrations().back().version; }

}  // namespace optinspect::database
