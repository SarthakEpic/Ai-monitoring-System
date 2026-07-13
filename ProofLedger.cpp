#include "ProofLedger.h"
#include "sqlite3.h"

#include <string_view>

namespace {
void Bind(sqlite3_stmt* statement, int index, const std::string& value) {
    sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

bool Execute(sqlite3* database, const char* sql, std::string& error, bool ignoreDuplicateColumn = false) {
    char* message = nullptr;
    if (sqlite3_exec(database, sql, nullptr, nullptr, &message) == SQLITE_OK) return true;
    const std::string detail = message ? message : sqlite3_errmsg(database);
    if (message) sqlite3_free(message);
    if (ignoreDuplicateColumn && detail.find("duplicate column name") != std::string::npos) return true;
    error = detail;
    return false;
}
}

ProofLedger::~ProofLedger() { Close(); }

bool ProofLedger::Open(const std::string& path, std::string& error) {
    std::lock_guard lock(mutex_);
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        error = db_ ? sqlite3_errmsg(db_) : "proof ledger open failed";
        return false;
    }
    return EnsureSchema(error);
}

void ProofLedger::Close() {
    std::lock_guard lock(mutex_);
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
}

bool ProofLedger::EnsureSchema(std::string& error) {
    const char* create =
        "CREATE TABLE IF NOT EXISTS proof_actions("
        "request_id TEXT PRIMARY KEY, model_id TEXT NOT NULL, certificate_id TEXT NOT NULL, "
        "target_pid INTEGER NOT NULL, foreground_pid INTEGER NOT NULL, action INTEGER NOT NULL, "
        "benefit_lower REAL NOT NULL, harm_upper REAL NOT NULL, lease_ms INTEGER NOT NULL, canary_ms INTEGER NOT NULL, "
        "rollback_complete INTEGER NOT NULL, user_approved INTEGER NOT NULL, need_accepted INTEGER NOT NULL DEFAULT 0, "
        "mechanism_consistent INTEGER NOT NULL DEFAULT 0, guardian_passed INTEGER NOT NULL DEFAULT 0, "
        "deterministic_safety INTEGER NOT NULL DEFAULT 0, lease_state TEXT NOT NULL);";
    if (!Execute(db_, create, error)) return false;
    const char* migrations[] = {
        "ALTER TABLE proof_actions ADD COLUMN need_accepted INTEGER NOT NULL DEFAULT 0;",
        "ALTER TABLE proof_actions ADD COLUMN mechanism_consistent INTEGER NOT NULL DEFAULT 0;",
        "ALTER TABLE proof_actions ADD COLUMN guardian_passed INTEGER NOT NULL DEFAULT 0;",
        "ALTER TABLE proof_actions ADD COLUMN deterministic_safety INTEGER NOT NULL DEFAULT 0;",
    };
    for (const char* migration : migrations) if (!Execute(db_, migration, error, true)) return false;
    return true;
}

bool ProofLedger::Save(const ProofCarryingAction& proof, std::string& error) {
    std::lock_guard lock(mutex_);
    const char* sql =
        "INSERT INTO proof_actions(request_id,model_id,certificate_id,target_pid,foreground_pid,action,benefit_lower,harm_upper,"
        "lease_ms,canary_ms,rollback_complete,user_approved,need_accepted,mechanism_consistent,guardian_passed,deterministic_safety,lease_state) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(request_id) DO NOTHING;";
    sqlite3_stmt* statement = nullptr;
    if (!db_ || sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = db_ ? sqlite3_errmsg(db_) : "ledger closed";
        return false;
    }
    Bind(statement, 1, proof.requestId); Bind(statement, 2, proof.modelId); Bind(statement, 3, proof.certificateId);
    sqlite3_bind_int(statement, 4, static_cast<int>(proof.context.targetPid));
    sqlite3_bind_int(statement, 5, static_cast<int>(proof.context.foregroundPid));
    sqlite3_bind_int(statement, 6, static_cast<int>(proof.evidence.action));
    sqlite3_bind_double(statement, 7, proof.evidence.benefitLowerBound); sqlite3_bind_double(statement, 8, proof.evidence.harmUpperBound);
    sqlite3_bind_int(statement, 9, proof.maximumLeaseMs); sqlite3_bind_int(statement, 10, proof.canaryDurationMs);
    sqlite3_bind_int(statement, 11, proof.rollbackSnapshotComplete); sqlite3_bind_int(statement, 12, proof.context.userApproved);
    sqlite3_bind_int(statement, 13, proof.need.accepted); sqlite3_bind_int(statement, 14, proof.evidence.mechanismConsistent);
    sqlite3_bind_int(statement, 15, proof.evidence.guardianPassed); sqlite3_bind_int(statement, 16, proof.deterministicSafetyPassed);
    Bind(statement, 17, "PENDING");
    const bool saved = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(db_) == 1;
    if (!saved) error = sqlite3_errmsg(db_);
    sqlite3_finalize(statement);
    return saved;
}

bool ProofLedger::Load(const std::string& id, ProofCarryingAction& proof, std::string& error) {
    std::lock_guard lock(mutex_);
    const char* sql =
        "SELECT model_id,certificate_id,target_pid,foreground_pid,action,benefit_lower,harm_upper,lease_ms,canary_ms,"
        "rollback_complete,user_approved,need_accepted,mechanism_consistent,guardian_passed,deterministic_safety "
        "FROM proof_actions WHERE request_id=?;";
    sqlite3_stmt* statement = nullptr;
    if (!db_ || sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) { error = "ledger closed"; return false; }
    Bind(statement, 1, id);
    if (sqlite3_step(statement) != SQLITE_ROW) { sqlite3_finalize(statement); error = "proof not found"; return false; }
    proof = {};
    proof.requestId = id;
    proof.modelId = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
    proof.certificateId = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
    proof.context.targetPid = sqlite3_column_int(statement, 2); proof.context.foregroundPid = sqlite3_column_int(statement, 3);
    proof.target.pid = proof.context.targetPid; proof.evidence.action = static_cast<ResourceActionType>(sqlite3_column_int(statement, 4));
    proof.evidence.benefitLowerBound = sqlite3_column_double(statement, 5); proof.evidence.harmUpperBound = sqlite3_column_double(statement, 6);
    proof.maximumLeaseMs = sqlite3_column_int(statement, 7); proof.canaryDurationMs = sqlite3_column_int(statement, 8);
    proof.rollbackSnapshotComplete = sqlite3_column_int(statement, 9) != 0; proof.context.userApproved = sqlite3_column_int(statement, 10) != 0;
    proof.need.accepted = sqlite3_column_int(statement, 11) != 0; proof.evidence.mechanismConsistent = sqlite3_column_int(statement, 12) != 0;
    proof.evidence.guardianPassed = sqlite3_column_int(statement, 13) != 0; proof.deterministicSafetyPassed = sqlite3_column_int(statement, 14) != 0;
    sqlite3_finalize(statement);
    return true;
}

bool ProofLedger::MarkLeaseState(const std::string& id, const std::string& state, std::string& error) {
    std::lock_guard lock(mutex_);
    sqlite3_stmt* statement = nullptr;
    if (!db_ || sqlite3_prepare_v2(db_, "UPDATE proof_actions SET lease_state=? WHERE request_id=?;", -1, &statement, nullptr) != SQLITE_OK) { error = "ledger closed"; return false; }
    Bind(statement, 1, state); Bind(statement, 2, id);
    const bool updated = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(db_) == 1;
    if (!updated) error = sqlite3_errmsg(db_);
    sqlite3_finalize(statement);
    return updated;
}

bool ProofLedger::ClaimForCanary(const std::string& id, std::string& error) {
    std::lock_guard lock(mutex_);
    sqlite3_stmt* statement = nullptr;
    const char* sql = "UPDATE proof_actions SET lease_state='REVALIDATED' WHERE request_id=? AND lease_state='PENDING';";
    if (!db_ || sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) { error = "ledger closed"; return false; }
    Bind(statement, 1, id);
    const bool claimed = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(db_) == 1;
    if (!claimed) error = "proof_not_pending_or_not_found";
    sqlite3_finalize(statement);
    return claimed;
}