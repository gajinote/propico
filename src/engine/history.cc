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

sqlite3_stmt* prepare_or_throw(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("SQLite prepare failed: ") + sqlite3_errmsg(db));
    }
    return stmt;
}

void step_done_or_throw(sqlite3* db, sqlite3_stmt* stmt) {
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(std::string("SQLite step failed: ") + sqlite3_errmsg(db));
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
    std::lock_guard<std::mutex> lock(mu_);
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));

    // 1. learn_log に生ログを追記
    {
        const char* sql =
            "INSERT INTO learn_log (candidate_id, prefix, selected_at) VALUES (?, ?, ?);";
        sqlite3_stmt* stmt = prepare_or_throw(db_, sql);
        sqlite3_bind_text(stmt, 1, candidate_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, prefix.c_str(),       -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, now);
        step_done_or_throw(db_, stmt);
    }

    // 2. score_override のカウントを +1（存在しなければ新規挿入）
    {
        const char* sql = R"sql(
            INSERT INTO score_override (candidate_id, count, updated_at)
                VALUES (?, 1, ?)
            ON CONFLICT(candidate_id) DO UPDATE SET
                count      = count + 1,
                updated_at = excluded.updated_at;
        )sql";
        sqlite3_stmt* stmt = prepare_or_throw(db_, sql);
        sqlite3_bind_text(stmt, 1, candidate_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, now);
        step_done_or_throw(db_, stmt);
    }
}

std::unordered_map<std::string, int> History::load_boost_map() const {
    std::lock_guard<std::mutex> lock(mu_);
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

std::vector<SyncRow> History::load_updated_since(std::int64_t since) const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<SyncRow> result;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = (since == 0)
        ? "SELECT candidate_id, count, updated_at FROM score_override;"
        : "SELECT candidate_id, count, updated_at FROM score_override WHERE updated_at > ?;";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }

    if (since != 0) {
        sqlite3_bind_int64(stmt, 1, since);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (id) {
            SyncRow row;
            row.candidate_id = id;
            row.count        = sqlite3_column_int(stmt, 1);
            row.updated_at   = sqlite3_column_int64(stmt, 2);
            result.push_back(std::move(row));
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

void History::merge_entries(const std::vector<SyncRow>& entries) {
    std::lock_guard<std::mutex> lock(mu_);
    const char* sql = R"sql(
        INSERT INTO score_override (candidate_id, count, updated_at)
            VALUES (?, ?, ?)
        ON CONFLICT(candidate_id) DO UPDATE SET
            count      = excluded.count,
            updated_at = excluded.updated_at
        WHERE excluded.updated_at > score_override.updated_at;
    )sql";
    for (const auto& e : entries) {
        sqlite3_stmt* stmt = prepare_or_throw(db_, sql);
        sqlite3_bind_text(stmt, 1, e.candidate_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, e.count);
        sqlite3_bind_int64(stmt, 3, e.updated_at);
        step_done_or_throw(db_, stmt);
    }
}

} // namespace pobox::engine
