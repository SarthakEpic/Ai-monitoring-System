#pragma once

#include <windows.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "CooperativeIntegrations.h"

struct BrowserBridgeResult {
    std::string transactionId;
    std::string status;
    std::string reason;
};

class BrowserIntegrationBridge {
public:
    explicit BrowserIntegrationBridge(CooperativeIntegrationJournal* journal = nullptr);
    ~BrowserIntegrationBridge();

    bool Start(std::string& error);
    void Stop();
    bool IsRunning() const;

    CooperativeActionTransaction QueueDiscard(const BrowserTabState& tab, bool userApproved);
    CooperativeActionTransaction QueueRestore(const std::string& transactionId);
    int QueueRestoreAll();
    std::vector<BrowserBridgeResult> DrainResults();

    std::string ProcessMessageForTest(const std::string& message);

private:
    struct PendingCommand {
        std::string transactionId;
        std::string json;
    };

    static std::string ExtractJsonString(const std::string& json, const std::string& key);
    static long long NowMs();
    static std::string NewTransactionId();
    static std::string EscapeJson(const std::string& value);
    std::string ProcessMessage(const std::string& message);
    void ServerLoop();
    void Persist(CooperativeActionTransaction transaction);

    BrowserTabSafetyValidator validator_;
    CooperativeIntegrationJournal* journal_ = nullptr;
    std::atomic<bool> running_{false};
    std::thread serverThread_;
    mutable std::mutex mutex_;
    std::deque<PendingCommand> pending_;
    std::vector<BrowserBridgeResult> results_;
    std::unordered_map<std::string, long long> activeTabsByTransaction_;
};
