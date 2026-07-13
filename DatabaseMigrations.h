#pragma once

#include <string>

struct sqlite3;

// Applies additive, transactional Aegis schema migrations. Existing monitoring
// tables are never dropped or rewritten by this Phase 1 migration path.
bool ApplyAegisMigrations(sqlite3* database, std::string& error);

int CurrentAegisSchemaVersion(sqlite3* database);
