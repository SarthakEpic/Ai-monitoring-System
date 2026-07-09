#pragma once

#include <string>
#include <vector>

#include "SystemMetrics.h"

struct sqlite3;

class MetricsStorage {
public:
    MetricsStorage() = default;
    ~MetricsStorage();

    bool Open(const std::string& path);
    void Close();
    bool IsReady() const;
    void LogSnapshot(const SystemSnapshot& snapshot);
    void LogBatch(const std::vector<SystemSnapshot>& snapshots);

private:
    bool EnsureSchema();
    bool EnsureColumn(const std::string& columnName, const std::string& columnDefinition);
    bool EnsureTableColumn(const std::string& tableName, const std::string& columnName, const std::string& columnDefinition);
    bool ExecuteInsertBatch(const std::vector<SystemSnapshot>& snapshots);

    sqlite3* db_ = nullptr;
};
