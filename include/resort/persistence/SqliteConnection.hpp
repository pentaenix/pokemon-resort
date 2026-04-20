#pragma once

#include <sqlite3.h>

#include <filesystem>
#include <string>

namespace pr::resort {

class SqliteStatement;

class SqliteConnection {
public:
    explicit SqliteConnection(const std::filesystem::path& path);
    ~SqliteConnection();

    SqliteConnection(const SqliteConnection&) = delete;
    SqliteConnection& operator=(const SqliteConnection&) = delete;

    SqliteConnection(SqliteConnection&& other) noexcept;
    SqliteConnection& operator=(SqliteConnection&& other) noexcept;

    sqlite3* get() const { return db_; }
    void exec(const std::string& sql) const;
    SqliteStatement prepare(const std::string& sql) const;
    long long lastInsertRowId() const;
    int changes() const;

private:
    sqlite3* db_ = nullptr;
};

class SqliteStatement {
public:
    SqliteStatement(sqlite3* db, sqlite3_stmt* stmt);
    ~SqliteStatement();

    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;

    SqliteStatement(SqliteStatement&& other) noexcept;
    SqliteStatement& operator=(SqliteStatement&& other) noexcept;

    void bindText(int index, const std::string& value);
    void bindInt(int index, int value);
    void bindInt64(int index, long long value);
    void bindNull(int index);
    void bindBlob(int index, const void* data, int size);

    bool stepRow();
    void stepDone();
    void reset();

    bool columnIsNull(int index) const;
    int columnInt(int index) const;
    long long columnInt64(int index) const;
    std::string columnText(int index) const;
    std::string columnBlobAsString(int index) const;

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

class SqliteTransaction {
public:
    explicit SqliteTransaction(SqliteConnection& connection);
    ~SqliteTransaction();

    SqliteTransaction(const SqliteTransaction&) = delete;
    SqliteTransaction& operator=(const SqliteTransaction&) = delete;

    void commit();

private:
    SqliteConnection* connection_ = nullptr;
    bool committed_ = false;
};

} // namespace pr::resort
