#include "BrowserIntegrationBridge.h"

#include <algorithm>
#include <chrono>
#include <sstream>

using namespace std;

namespace {

constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\PredictiveAutoHealBrowserBridge";
constexpr DWORD kMaximumMessageBytes = 1024 * 1024;

bool ReadExact(HANDLE handle, void* destination, DWORD bytes) {
    auto* cursor = static_cast<unsigned char*>(destination);
    while (bytes > 0) {
        DWORD read = 0;
        if (!ReadFile(handle, cursor, bytes, &read, nullptr) || read == 0) return false;
        cursor += read;
        bytes -= read;
    }
    return true;
}

bool WriteExact(HANDLE handle, const void* source, DWORD bytes) {
    const auto* cursor = static_cast<const unsigned char*>(source);
    while (bytes > 0) {
        DWORD written = 0;
        if (!WriteFile(handle, cursor, bytes, &written, nullptr) || written == 0) return false;
        cursor += written;
        bytes -= written;
    }
    return true;
}

}  // namespace

BrowserIntegrationBridge::BrowserIntegrationBridge(CooperativeIntegrationJournal* journal) : journal_(journal) {}
BrowserIntegrationBridge::~BrowserIntegrationBridge() { Stop(); }

long long BrowserIntegrationBridge::NowMs() {
    return chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
}

string BrowserIntegrationBridge::NewTransactionId() {
    static atomic<unsigned long long> sequence{0};
    return "TAB-" + to_string(NowMs()) + '-' + to_string(GetCurrentProcessId()) + '-' + to_string(++sequence);
}

string BrowserIntegrationBridge::EscapeJson(const string& value) {
    string result;
    result.reserve(value.size());
    for (char c : value) {
        if (c == '\\' || c == '"') result.push_back('\\');
        if (c == '\n') result += "\\n";
        else if (c != '\r') result.push_back(c);
    }
    return result;
}

string BrowserIntegrationBridge::ExtractJsonString(const string& json, const string& key) {
    const string marker = '"' + key + "\":";
    const size_t keyPosition = json.find(marker);
    if (keyPosition == string::npos) return "";
    size_t position = keyPosition + marker.size();
    while (position < json.size() && isspace(static_cast<unsigned char>(json[position]))) ++position;
    if (position >= json.size() || json[position] != '"') return "";
    ++position;
    string result;
    bool escaped = false;
    for (; position < json.size(); ++position) {
        const char current = json[position];
        if (escaped) { result.push_back(current); escaped = false; }
        else if (current == '\\') escaped = true;
        else if (current == '"') break;
        else result.push_back(current);
    }
    return result;
}

bool BrowserIntegrationBridge::Start(string& error) {
    if (running_.exchange(true)) return true;
    try {
        serverThread_ = thread(&BrowserIntegrationBridge::ServerLoop, this);
    } catch (const exception& ex) {
        running_.store(false);
        error = ex.what();
        return false;
    }
    return true;
}

void BrowserIntegrationBridge::Stop() {
    if (!running_.exchange(false)) return;
    HANDLE wake = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);
    if (serverThread_.joinable()) serverThread_.join();
}

bool BrowserIntegrationBridge::IsRunning() const { return running_.load(); }

void BrowserIntegrationBridge::Persist(CooperativeActionTransaction transaction) {
    if (!journal_) return;
    string error;
    journal_->Save(transaction, error);
}

CooperativeActionTransaction BrowserIntegrationBridge::QueueDiscard(const BrowserTabState& tab, bool userApproved) {
    CooperativeActionTransaction transaction;
    transaction.transactionId = NewTransactionId();
    transaction.action = "DISCARD_INACTIVE_BROWSER_TAB";
    transaction.target = "tab:" + to_string(tab.tabId);
    transaction.startedAtMs = transaction.updatedAtMs = NowMs();
    transaction.reversible = true;
    const CooperativeSafetyDecision safety = validator_.CanDiscard(tab, userApproved);
    if (!safety.eligible) {
        transaction.status = "BLOCKED";
        transaction.reason = safety.reason;
        Persist(transaction);
        return transaction;
    }
    ostringstream command;
    command << "{\"command\":\"discard_tab\",\"transactionId\":\"" << EscapeJson(transaction.transactionId)
            << "\",\"tabId\":" << tab.tabId << '}';
    {
        lock_guard lock(mutex_);
        pending_.push_back({transaction.transactionId, command.str()});
        activeTabsByTransaction_[transaction.transactionId] = tab.tabId;
    }
    transaction.status = "PLANNED";
    transaction.reason = "approved cooperative tab discard queued for extension-side revalidation";
    Persist(transaction);
    return transaction;
}

CooperativeActionTransaction BrowserIntegrationBridge::QueueRestore(const string& transactionId) {
    CooperativeActionTransaction transaction;
    transaction.transactionId = transactionId;
    transaction.action = "RESTORE_BROWSER_TAB";
    transaction.startedAtMs = transaction.updatedAtMs = NowMs();
    transaction.reversible = true;
    lock_guard lock(mutex_);
    if (!activeTabsByTransaction_.contains(transactionId)) {
        transaction.status = "BLOCKED";
        transaction.reason = "active browser transaction not found";
        return transaction;
    }
    pending_.push_back({transactionId, "{\"command\":\"restore_tab\",\"transactionId\":\"" + EscapeJson(transactionId) + "\"}"});
    transaction.status = "PLANNED";
    transaction.reason = "browser restore queued";
    return transaction;
}

int BrowserIntegrationBridge::QueueRestoreAll() {
    vector<string> transactions;
    {
        lock_guard lock(mutex_);
        for (const auto& [transactionId, _] : activeTabsByTransaction_) transactions.push_back(transactionId);
    }
    int queued = 0;
    for (const string& transactionId : transactions) if (QueueRestore(transactionId).status == "PLANNED") ++queued;
    return queued;
}

vector<BrowserBridgeResult> BrowserIntegrationBridge::DrainResults() {
    lock_guard lock(mutex_);
    vector<BrowserBridgeResult> result = move(results_);
    results_.clear();
    return result;
}

string BrowserIntegrationBridge::ProcessMessageForTest(const string& message) { return ProcessMessage(message); }

string BrowserIntegrationBridge::ProcessMessage(const string& message) {
    if (message.find("\"type\":\"poll\"") != string::npos) {
        lock_guard lock(mutex_);
        if (pending_.empty()) return R"({"command":"poll"})";
        string command = pending_.front().json;
        pending_.pop_front();
        return command;
    }
    if (message.find("\"type\":\"action_result\"") != string::npos) {
        BrowserBridgeResult result;
        result.transactionId = ExtractJsonString(message, "transactionId");
        result.status = ExtractJsonString(message, "status");
        result.reason = ExtractJsonString(message, "reason");
        CooperativeActionTransaction audit;
        audit.transactionId = result.transactionId;
        audit.action = "BROWSER_EXTENSION_RESULT";
        audit.target = "browser_tab";
        audit.status = result.status.empty() ? "ERROR" : result.status;
        audit.startedAtMs = audit.updatedAtMs = NowMs();
        audit.reversible = true;
        audit.restored = audit.status == "ROLLED_BACK";
        audit.reason = result.reason;
        {
            lock_guard lock(mutex_);
            results_.push_back(result);
            if (audit.restored || audit.status == "BLOCKED" || audit.status == "RESTORE_FAILED") {
                activeTabsByTransaction_.erase(result.transactionId);
            }
        }
        Persist(audit);
        return R"({"command":"poll"})";
    }
    if (message.find("\"type\":\"tab_snapshot\"") != string::npos) {
        return R"({"command":"poll","status":"SNAPSHOT_RECEIVED"})";
    }
    return R"({"command":"poll","status":"UNKNOWN_MESSAGE"})";
}

void BrowserIntegrationBridge::ServerLoop() {
    while (running_.load()) {
        HANDLE pipe = CreateNamedPipeW(kPipeName, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, kMaximumMessageBytes, kMaximumMessageBytes, 1000, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) { running_.store(false); return; }
        const BOOL connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (connected && running_.load()) {
            uint32_t requestLength = 0;
            if (ReadExact(pipe, &requestLength, sizeof(requestLength)) && requestLength > 0 && requestLength <= kMaximumMessageBytes) {
                string request(requestLength, '\0');
                if (ReadExact(pipe, request.data(), requestLength)) {
                    const string response = ProcessMessage(request);
                    const uint32_t responseLength = static_cast<uint32_t>(response.size());
                    WriteExact(pipe, &responseLength, sizeof(responseLength));
                    WriteExact(pipe, response.data(), responseLength);
                }
            }
        }
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}
