#include "catalog_sqlite.h"

#if !defined(DUOSORT_DISABLE_SQLITE) && __has_include(<sqlite3.h>)
#include <sqlite3.h>
#define DUOSORT_HAVE_SQLITE 1
#else
#define DUOSORT_HAVE_SQLITE 0
#endif

using namespace std;

namespace {
// Writes a log message only when catalog export logging is enabled.
void LogLine(const function<void(const string&)>& logger, const string& line) {
    if (logger) logger(line);
}
} // namespace

// Persists the in-memory photo catalog to SQLite or reports why cataloging is unavailable.
void WritePhotoCatalogDb(const vector<PhotoRecord>& photos,
                         const function<void(const string&)>& logger) {
#if DUOSORT_HAVE_SQLITE
    sqlite3* db = nullptr;
    if (sqlite3_open("duosort.db", &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        LogLine(logger, "Unable to open duosort.db");
        return;
    }

    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db,
                 "CREATE TABLE IF NOT EXISTS photos ("
                 "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                 "source TEXT NOT NULL,"
                 "local_path TEXT,"
                 "file_name TEXT,"
                 "file_size INTEGER,"
                 "modified_time INTEGER,"
                 "google_url TEXT,"
                 "exact_hash TEXT,"
                 "sample_hash TEXT,"
                 "metadata_hash TEXT);",
                 nullptr, nullptr, nullptr);
    sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_photos_exact_hash ON photos(exact_hash);",
                 nullptr, nullptr, nullptr);
    sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_photos_sample_hash ON photos(sample_hash);",
                 nullptr, nullptr, nullptr);
    sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_photos_metadata_hash ON photos(metadata_hash);",
                 nullptr, nullptr, nullptr);
    sqlite3_exec(db, "DELETE FROM photos;", nullptr, nullptr, nullptr);

    // Batch inserts in one transaction to avoid per-row fsync overhead.
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    sqlite3_stmt* stmt = nullptr;
    const char* insertSql =
        "INSERT INTO photos(source, local_path, file_name, file_size, modified_time, google_url, "
        "exact_hash, sample_hash, metadata_hash) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?);";

    if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) == SQLITE_OK) {
        for (const auto& p : photos) {
            sqlite3_bind_text(stmt, 1, p.source.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, p.localPath.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, p.fileName.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(p.fileSize));
            sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(p.modifiedTime));
            sqlite3_bind_text(stmt, 6, p.googleUrl.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 7, p.hashes.exactHash.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 8, p.hashes.sampleHash.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 9, p.hashes.metadataHash.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    LogLine(logger, "Catalog written to duosort.db");
#else
    (void)photos;
    LogLine(logger, "sqlite3.h not found. Install SQLite dev headers to enable DB catalog.");
#endif
}
