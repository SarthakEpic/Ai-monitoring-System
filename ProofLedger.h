#pragma once

#include <mutex>
#include <string>

#include "SecureActuation.h"

struct sqlite3;

class ProofLedger {
public:
    ~ProofLedger();
    bool Open(const std::string& databasePath, std::string& error);
    void Close();
    bool Save(const ProofCarryingAction& proof, std::string& error);
    bool Load(const std::string& requestId, ProofCarryingAction& proof, std::string& error);
    bool MarkLeaseState(const std::string& requestId, const std::string& state, std::string& error);
    bool ClaimForCanary(const std::string& requestId, std::string& error);

private:
    bool EnsureSchema(std::string& error);
    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};
