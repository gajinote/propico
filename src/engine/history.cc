#include "history.h"

#include <ctime>
#include <stdexcept>
#include <string>

#include <sqlite3.h>

namespace pobox::engine {

namespace {

void exec_or_throw(sqlite3* db, const char* sql) {
    char* errmsg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        std::string msg = errmsg ? errmsg : "unknown sqlite3 error";
        sqlite3_free(errmsg);
        throw std::runtime_error("SQLite exec failed: " + msg);
    }
}

} // namespace

History::History(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        std::string msg = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("Failed to open SQLite database '" + db_path + "': " + msg);
    }
    init_schema();
}

History::~History() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void History::init_schema() {
    exec_or_throw(db_, R"sql(
        CREATE TABLE IF NOT EXISTS learn_log (
            candidate_id TEXT NOT NULL,
            prefix       TEXT NOT NULL,
            selected_at  INTEGER NOT NULL
        );
        CREATE TABLE IF NOT EXISTS score_override (
            candidate_id TEXT    PRIMARY KEY,
            count        INTEGER NOT NULL DEFAULT 1,
            updated_at   INTEGER NOT NULL
        );
    )sql");
}

void History::record(const std::string& candidate_id, const std::string& prefix) {
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));

    // 1. learn_log に生ログを追記
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "INSERT INTO learn_log (candidate_id, prefix, selected_at) VALUES (?, ?, ?);";
        sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, candidate_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, prefix.c_str(),       -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, now);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // 2. score_override のカウントを +1（存在しなければ新規挿入）
    {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = R"sql(
            INSERT INTO score_override (candidate_id, count, updated_at)
                VALUES (?, 1, ?)
            ON CONFLICT(candidate_id) DO UPDATE SET
                count      = count + 1,
                updated_at = excluded.updated_at;
        )sql";
        sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, candidate_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, now);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::unordered_map<std::string, int> History::load_boost_map() const {
    std::unordered_map<std::string, int> result;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT candidate_id, count FROM score_override;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const int   count = sqlite3_column_int(stmt, 1);
        if (id) {
            result[id] = count;
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

} // namespace pobox::engine
