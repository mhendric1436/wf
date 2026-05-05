#pragma once

#include <mutex>
#include <sqlite3.h>
#include <string>

namespace workflow::backend::sqlite
{

class SQLiteDatabase
{
  public:
    explicit SQLiteDatabase(const std::string& path);
    ~SQLiteDatabase();

    SQLiteDatabase(const SQLiteDatabase&) = delete;
    SQLiteDatabase& operator=(const SQLiteDatabase&) = delete;

    sqlite3* handle() const;
    std::mutex& mutex() const;

  private:
    void createTables();

    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_;
};

} // namespace workflow::backend::sqlite
