#include "resort/persistence/SqliteConnection.hpp"

#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace pr::resort {

namespace {

std::string sqliteError(sqlite3* db, const std::string& prefix) {
    return prefix + ": " + (db ? sqlite3_errmsg(db) : "no database");
}

void check(int rc, sqlite3* db, const std::string& prefix) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw std::runtime_error(sqliteError(db, prefix));
    }
}

} // namespace

SqliteConnection::SqliteConnection(const fs::path& path) {
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path());
    }
    if (sqlite3_open(path.string().c_str(), &db_) != SQLITE_OK) {
        std::string message = sqliteError(db_, "Could not open Resort profile database");
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error(message);
    }

    exec("PRAGMA foreign_keys = ON");
    exec("PRAGMA journal_mode = WAL");
}

SqliteConnection::~SqliteConnection() {
    if (db_) {
        sqlite3_close(db_);
    }
}

SqliteConnection::SqliteConnection(SqliteConnection&& other) noexcept
    : db_(other.db_) {
    other.db_ = nullptr;
}

SqliteConnection& SqliteConnection::operator=(SqliteConnection&& other) noexcept {
    if (this != &other) {
        if (db_) {
            sqlite3_close(db_);
        }
        db_ = other.db_;
        other.db_ = nullptr;
    }
    return *this;
}

void SqliteConnection::exec(const std::string& sql) const {
    char* error = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
        std::string message = error ? error : sqlite3_errmsg(db_);
        sqlite3_free(error);
        throw std::runtime_error("SQLite exec failed: " + message);
    }
}

SqliteStatement SqliteConnection::prepare(const std::string& sql) const {
    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    check(rc, db_, "SQLite prepare failed");
    return SqliteStatement(db_, stmt);
}

long long SqliteConnection::lastInsertRowId() const {
    return sqlite3_last_insert_rowid(db_);
}

int SqliteConnection::changes() const {
    return sqlite3_changes(db_);
}

SqliteStatement::SqliteStatement(sqlite3* db, sqlite3_stmt* stmt)
    : db_(db), stmt_(stmt) {}

SqliteStatement::~SqliteStatement() {
    if (stmt_) {
        sqlite3_finalize(stmt_);
    }
}

SqliteStatement::SqliteStatement(SqliteStatement&& other) noexcept
    : db_(other.db_), stmt_(other.stmt_) {
    other.db_ = nullptr;
    other.stmt_ = nullptr;
}

SqliteStatement& SqliteStatement::operator=(SqliteStatement&& other) noexcept {
    if (this != &other) {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
        db_ = other.db_;
        stmt_ = other.stmt_;
        other.db_ = nullptr;
        other.stmt_ = nullptr;
    }
    return *this;
}

void SqliteStatement::bindText(int index, const std::string& value) {
    check(sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT), db_, "SQLite bind text failed");
}

void SqliteStatement::bindInt(int index, int value) {
    check(sqlite3_bind_int(stmt_, index, value), db_, "SQLite bind int failed");
}

void SqliteStatement::bindInt64(int index, long long value) {
    check(sqlite3_bind_int64(stmt_, index, value), db_, "SQLite bind int64 failed");
}

void SqliteStatement::bindNull(int index) {
    check(sqlite3_bind_null(stmt_, index), db_, "SQLite bind null failed");
}

void SqliteStatement::bindBlob(int index, const void* data, int size) {
    check(sqlite3_bind_blob(stmt_, index, data, size, SQLITE_TRANSIENT), db_, "SQLite bind blob failed");
}

bool SqliteStatement::stepRow() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    check(rc, db_, "SQLite step failed");
    return false;
}

void SqliteStatement::stepDone() {
    const int rc = sqlite3_step(stmt_);
    if (rc != SQLITE_DONE) {
        check(rc, db_, "SQLite step expected done");
        throw std::runtime_error("SQLite statement returned rows where none were expected");
    }
}

void SqliteStatement::reset() {
    check(sqlite3_reset(stmt_), db_, "SQLite reset failed");
    check(sqlite3_clear_bindings(stmt_), db_, "SQLite clear bindings failed");
}

bool SqliteStatement::columnIsNull(int index) const {
    return sqlite3_column_type(stmt_, index) == SQLITE_NULL;
}

int SqliteStatement::columnInt(int index) const {
    return sqlite3_column_int(stmt_, index);
}

long long SqliteStatement::columnInt64(int index) const {
    return sqlite3_column_int64(stmt_, index);
}

std::string SqliteStatement::columnText(int index) const {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, index));
    return text ? std::string(text) : std::string();
}

std::string SqliteStatement::columnBlobAsString(int index) const {
    const auto* data = reinterpret_cast<const char*>(sqlite3_column_blob(stmt_, index));
    const int size = sqlite3_column_bytes(stmt_, index);
    return data && size > 0 ? std::string(data, data + size) : std::string();
}

SqliteTransaction::SqliteTransaction(SqliteConnection& connection)
    : connection_(&connection) {
    connection_->exec("BEGIN IMMEDIATE");
    connection_->exec("PRAGMA defer_foreign_keys = ON");
}

SqliteTransaction::~SqliteTransaction() {
    if (connection_ && !committed_) {
        try {
            connection_->exec("ROLLBACK");
        } catch (...) {
        }
    }
}

void SqliteTransaction::commit() {
    if (!connection_ || committed_) {
        return;
    }
    connection_->exec("COMMIT");
    committed_ = true;
}

} // namespace pr::resort
