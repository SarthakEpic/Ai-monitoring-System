#include "CooperativeIntegrations.h"

#include <bits.h>
#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <sstream>

#include "sqlite3.h"

using namespace std;

namespace {

long long NowMs() {
    return chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
}

string Lower(string value) {
    transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return value;
}

bool Contains(const string& value, const string& token) { return Lower(value).find(Lower(token)) != string::npos; }

bool IsHttpUrl(const string& url) {
    const string lowered = Lower(url);
    return lowered.starts_with("http://") || lowered.starts_with("https://");
}

string Narrow(const wchar_t* value) {
    if (!value || !*value) return "";
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    string result(max(0, bytes), '\0');
    if (bytes > 1) WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), bytes, nullptr, nullptr);
    return result.empty() ? "" : string(result.c_str());
}

string GuidString(const GUID& value) {
    wchar_t buffer[64]{};
    StringFromGUID2(value, buffer, static_cast<int>(size(buffer)));
    return Narrow(buffer);
}

bool ParseGuid(const string& value, GUID& result) {
    const int chars = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (chars <= 1) return false;
    wstring wide(chars, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), chars);
    return CLSIDFromString(wide.c_str(), &result) == S_OK;
}

string NewTransactionId(const string& prefix) {
    static atomic<unsigned long long> sequence{0};
    return prefix + '-' + to_string(NowMs()) + '-' + to_string(GetCurrentProcessId()) + '-' + to_string(++sequence);
}

const char* BitsStateName(BG_JOB_STATE state) {
    switch (state) {
    case BG_JOB_STATE_QUEUED: return "QUEUED";
    case BG_JOB_STATE_CONNECTING: return "CONNECTING";
    case BG_JOB_STATE_TRANSFERRING: return "TRANSFERRING";
    case BG_JOB_STATE_SUSPENDED: return "SUSPENDED";
    case BG_JOB_STATE_ERROR: return "ERROR";
    case BG_JOB_STATE_TRANSIENT_ERROR: return "TRANSIENT_ERROR";
    case BG_JOB_STATE_TRANSFERRED: return "TRANSFERRED";
    case BG_JOB_STATE_ACKNOWLEDGED: return "ACKNOWLEDGED";
    case BG_JOB_STATE_CANCELLED: return "CANCELLED";
    default: return "UNKNOWN";
    }
}

void AddUnique(vector<string>& values, const string& value) {
    if (find_if(values.begin(), values.end(), [&](const string& current) { return Lower(current) == Lower(value); }) == values.end()) {
        values.push_back(value);
    }
}

void AddUnique(vector<DWORD>& values, DWORD value) {
    if (value != 0 && find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
}

void BindText(sqlite3_stmt* statement, int index, const string& value) {
    sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

}  // namespace

CooperativeSafetyDecision BrowserTabSafetyValidator::CanDiscard(const BrowserTabState& tab, bool userApproved) const {
    CooperativeSafetyDecision result;
    if (!userApproved) result.reason = "browser-tab action requires explicit user approval";
    else if (tab.tabId <= 0) result.reason = "browser tab ID is invalid";
    else if (!IsHttpUrl(tab.url)) result.reason = "internal, extension, file, and privileged pages are protected";
    else if (tab.active) result.reason = "active tab is protected";
    else if (tab.pinned) result.reason = "pinned tab is protected";
    else if (tab.audible || tab.sharingMedia) result.reason = "audible or media-sharing tab is protected";
    else if (tab.discarded) result.reason = "tab is already discarded";
    else if (!tab.autoDiscardable) result.reason = "browser marked tab as non-discardable";
    else { result.eligible = true; result.reason = "inactive cooperative browser tab passed all safety gates"; }
    return result;
}

WorkloadProtectionResult WorkloadProtectionEngine::Build(
    const SystemSnapshot& snapshot,
    const PerformanceCriticalityGraph& graph,
    WorkloadPhase workload
) const {
    WorkloadProtectionResult result;
    result.workload = workload;
    for (const CriticalityNode& node : graph.nodes) {
        if (node.protectedFromIntervention) {
            AddUnique(result.protectedPids, node.pid);
            AddUnique(result.protectedNames, node.processName);
        }
    }
    AddUnique(result.protectedPids, snapshot.intent.foregroundPid);
    AddUnique(result.protectedNames, snapshot.intent.foregroundProcess);

    for (const ProcessSnapshot& process : snapshot.processGenome) {
        const string name = Lower(process.name);
        bool protect = false;
        string reason;
        if (workload == WorkloadPhase::Compilation &&
            (Contains(name, "msbuild") || name == "cl.exe" || Contains(name, "clang") || Contains(name, "ninja") || Contains(name, "language-server"))) {
            protect = true; reason = "compilation and IDE dependency protected";
        } else if (workload == WorkloadPhase::Gaming &&
                   (process.isForeground || name == "audiodg.exe" || Contains(name, "gamebar") || Contains(name, "steam"))) {
            protect = true; reason = "game, audio, or launcher dependency protected";
        } else if (workload == WorkloadPhase::VideoMeeting &&
                   (name == "audiodg.exe" || Contains(name, "teams") || Contains(name, "zoom") || Contains(name, "meet") || Contains(name, "camera"))) {
            protect = true; reason = "meeting audio/video dependency protected";
        } else if (workload == WorkloadPhase::PassivePlayback &&
                   (name == "audiodg.exe" || Contains(name, "vlc") || Contains(name, "spotify") || process.matchesUserIntent)) {
            protect = true; reason = "media playback dependency protected";
        }
        if (protect) {
            AddUnique(result.protectedPids, process.pid);
            AddUnique(result.protectedNames, process.name);
            result.reasons.push_back(process.name + ": " + reason);
        }
    }
    return result;
}

struct BitsTransferAdapter::Impl {
    IBackgroundCopyManager* manager = nullptr;
    bool shouldUninitializeCom = false;
    mutable mutex lock;
    unordered_map<string, GUID> activeTransactions;
};

BitsTransferAdapter::BitsTransferAdapter() : impl_(make_unique<Impl>()) {}

BitsTransferAdapter::~BitsTransferAdapter() {
    if (impl_->manager) impl_->manager->Release();
    if (impl_->shouldUninitializeCom) CoUninitialize();
}

bool BitsTransferAdapter::Initialize(string& error) {
    lock_guard lock(impl_->lock);
    if (impl_->manager) return true;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(initialized)) impl_->shouldUninitializeCom = true;
    else if (initialized != RPC_E_CHANGED_MODE) { error = "COM initialization failed: " + to_string(initialized); return false; }
    const HRESULT created = CoCreateInstance(__uuidof(BackgroundCopyManager), nullptr, CLSCTX_LOCAL_SERVER,
                                             __uuidof(IBackgroundCopyManager), reinterpret_cast<void**>(&impl_->manager));
    if (FAILED(created)) { error = "BITS manager unavailable: " + to_string(created); return false; }
    return true;
}

vector<BitsJobInfo> BitsTransferAdapter::ListCurrentUserJobs(string& error) const {
    lock_guard lock(impl_->lock);
    vector<BitsJobInfo> result;
    if (!impl_->manager) { error = "BITS adapter is not initialized"; return result; }
    IEnumBackgroundCopyJobs* jobs = nullptr;
    if (FAILED(impl_->manager->EnumJobs(0, &jobs))) { error = "cannot enumerate current-user BITS jobs"; return result; }
    IBackgroundCopyJob* job = nullptr;
    ULONG fetched = 0;
    while (jobs->Next(1, &job, &fetched) == S_OK && fetched == 1) {
        BitsJobInfo info;
        GUID id{}; job->GetId(&id); info.jobId = GuidString(id);
        LPWSTR display = nullptr; if (SUCCEEDED(job->GetDisplayName(&display)) && display) { info.displayName = Narrow(display); CoTaskMemFree(display); }
        BG_JOB_STATE state{}; if (SUCCEEDED(job->GetState(&state))) info.state = BitsStateName(state);
        info.currentUserOwned = true;
        result.push_back(move(info));
        job->Release(); job = nullptr;
    }
    jobs->Release();
    return result;
}

CooperativeActionTransaction BitsTransferAdapter::PauseApprovedJob(
    const string& jobId,
    bool userApproved
) {
    CooperativeActionTransaction transaction;
    transaction.transactionId = NewTransactionId("BITS");
    transaction.action = "PAUSE_USER_BITS_JOB";
    transaction.target = jobId;
    transaction.startedAtMs = NowMs();
    transaction.updatedAtMs = transaction.startedAtMs;
    transaction.reversible = true;

    if (!userApproved) {
        transaction.reason = "BITS pause requires explicit user approval";
        return transaction;
    }

    string listError;
    const vector<BitsJobInfo> jobs = ListCurrentUserJobs(listError);
    if (!listError.empty()) {
        transaction.reason = listError;
        return transaction;
    }

    const auto approvedJob = find_if(
        jobs.begin(),
        jobs.end(),
        [&](const BitsJobInfo& job) {
            return Lower(job.jobId) == Lower(jobId) && job.currentUserOwned;
        }
    );
    if (approvedJob == jobs.end()) {
        transaction.reason = "job is not present in the current-user BITS enumeration";
        return transaction;
    }

    GUID id{};
    if (!ParseGuid(jobId, id)) {
        transaction.reason = "invalid BITS job ID";
        return transaction;
    }

    lock_guard lock(impl_->lock);
    IBackgroundCopyJob* job = nullptr;
    if (FAILED(impl_->manager->GetJob(id, &job)) || !job) {
        transaction.reason = "approved BITS job was not found";
        return transaction;
    }

    const HRESULT status = job->Suspend();
    job->Release();
    if (FAILED(status)) {
        transaction.reason = "BITS suspend failed: " + to_string(status);
        return transaction;
    }

    impl_->activeTransactions[transaction.transactionId] = id;
    transaction.status = "EXECUTED";
    transaction.reason = "current-user BITS job paused cooperatively";
    return transaction;
}

bool BitsTransferAdapter::Resume(const string& transactionId, string& error) {
    lock_guard lock(impl_->lock);
    const auto found = impl_->activeTransactions.find(transactionId);
    if (found == impl_->activeTransactions.end()) {
        error = "active BITS transaction not found";
        return false;
    }

    IBackgroundCopyJob* job = nullptr;
    if (FAILED(impl_->manager->GetJob(found->second, &job)) || !job) {
        error = "BITS job no longer exists";
        impl_->activeTransactions.erase(found);
        return false;
    }

    const HRESULT status = job->Resume();
    job->Release();
    if (FAILED(status)) {
        error = "BITS resume failed: " + to_string(status);
        return false;
    }

    impl_->activeTransactions.erase(found);
    return true;
}

PrefetchLease::~PrefetchLease() {
    Release();
}

PrefetchLease::PrefetchLease(PrefetchLease&& other) noexcept {
    *this = move(other);
}

PrefetchLease& PrefetchLease::operator=(PrefetchLease&& other) noexcept {
    if (this != &other) {
        Release();
        file_ = other.file_;
        mapping_ = other.mapping_;
        view_ = other.view_;
        bytes_ = other.bytes_;
        other.file_ = INVALID_HANDLE_VALUE;
        other.mapping_ = nullptr;
        other.view_ = nullptr;
        other.bytes_ = 0;
    }
    return *this;
}

bool PrefetchLease::Active() const {
    return view_ != nullptr;
}

size_t PrefetchLease::Bytes() const {
    return bytes_;
}

void PrefetchLease::Release() {
    if (view_) {
        UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (mapping_) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
    if (file_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
    bytes_ = 0;
}

PredictivePrefetcher::Result PredictivePrefetcher::PrefetchFile(
    const wstring& path,
    double predictionConfidence,
    double historicalBenefit,
    size_t maximumBytes,
    bool userApproved
) const {
    Result result;
    result.transaction.transactionId = NewTransactionId("PREFETCH");
    result.transaction.action = "PREFETCH_PROVEN_FILE";
    result.transaction.target = filesystem::path(path).filename().string();
    result.transaction.startedAtMs = NowMs();
    result.transaction.updatedAtMs = result.transaction.startedAtMs;
    result.transaction.reversible = true;

    if (!userApproved) {
        result.transaction.reason = "prefetch requires explicit approval";
        return result;
    }
    if (predictionConfidence < 0.90 || historicalBenefit <= 0.05) {
        result.transaction.reason =
            "prediction confidence or measured historical benefit is too low";
        return result;
    }
    if (maximumBytes == 0 || maximumBytes > 256ULL * 1024ULL * 1024ULL) {
        result.transaction.reason = "prefetch byte budget is invalid";
        return result;
    }

    auto lease = make_unique<PrefetchLease>();
    lease->file_ = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (lease->file_ == INVALID_HANDLE_VALUE) {
        result.transaction.reason = "prefetch file cannot be opened";
        return result;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(lease->file_, &fileSize) || fileSize.QuadPart <= 0) {
        result.transaction.reason = "prefetch file has no readable content";
        return result;
    }

    lease->bytes_ = static_cast<size_t>(min<unsigned long long>(
        static_cast<unsigned long long>(fileSize.QuadPart),
        maximumBytes
    ));
    lease->mapping_ = CreateFileMappingW(
        lease->file_,
        nullptr,
        PAGE_READONLY,
        0,
        0,
        nullptr
    );
    if (!lease->mapping_) {
        result.transaction.reason = "prefetch file mapping failed";
        return result;
    }

    lease->view_ = MapViewOfFile(lease->mapping_, FILE_MAP_READ, 0, 0, lease->bytes_);
    if (!lease->view_) {
        result.transaction.reason = "prefetch view mapping failed";
        return result;
    }

    WIN32_MEMORY_RANGE_ENTRY range{lease->view_, lease->bytes_};
    if (!PrefetchVirtualMemory(GetCurrentProcess(), 1, &range, 0)) {
        result.transaction.reason = "PrefetchVirtualMemory failed";
        return result;
    }

    result.transaction.status = "EXECUTED";
    result.transaction.reason = "approved high-confidence file range prefetched";
    result.lease = move(lease);
    return result;
}

CooperativeIntegrationJournal::~CooperativeIntegrationJournal() {
    Close();
}

bool CooperativeIntegrationJournal::Open(const string& path, string& error) {
    lock_guard lock(mutex_);
    if (db_) {
        return true;
    }

    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(path.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
        error = db_ ? sqlite3_errmsg(db_) : "sqlite open failed";
        if (db_) {
            sqlite3_close(db_);
        }
        db_ = nullptr;
        return false;
    }

    sqlite3_busy_timeout(db_, 5000);
    if (!EnsureSchema(error)) {
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    return true;
}

void CooperativeIntegrationJournal::Close() {
    lock_guard lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool CooperativeIntegrationJournal::EnsureSchema(string& error) {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS cooperative_action_audits("
        "transaction_id TEXT PRIMARY KEY, action TEXT NOT NULL, target TEXT NOT NULL, "
        "status TEXT NOT NULL, started_at_ms INTEGER NOT NULL, updated_at_ms INTEGER NOT NULL, "
        "reversible INTEGER NOT NULL, restored INTEGER NOT NULL, reason TEXT NOT NULL);";

    char* message = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        error = message ? message : sqlite3_errmsg(db_);
        if (message) {
            sqlite3_free(message);
        }
        return false;
    }
    return true;
}

bool CooperativeIntegrationJournal::Save(
    const CooperativeActionTransaction& transaction,
    string& error
) {
    lock_guard lock(mutex_);
    const char* sql =
        "INSERT OR REPLACE INTO cooperative_action_audits VALUES(?,?,?,?,?,?,?,?,?);";
    sqlite3_stmt* statement = nullptr;
    if (!db_ || sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = db_ ? sqlite3_errmsg(db_) : "journal not open";
        return false;
    }

    BindText(statement, 1, transaction.transactionId);
    BindText(statement, 2, transaction.action);
    BindText(statement, 3, transaction.target);
    BindText(statement, 4, transaction.status);
    sqlite3_bind_int64(statement, 5, transaction.startedAtMs);
    sqlite3_bind_int64(statement, 6, transaction.updatedAtMs);
    sqlite3_bind_int(statement, 7, transaction.reversible ? 1 : 0);
    sqlite3_bind_int(statement, 8, transaction.restored ? 1 : 0);
    BindText(statement, 9, transaction.reason);

    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    if (!ok) {
        error = sqlite3_errmsg(db_);
    }
    sqlite3_finalize(statement);
    return ok;
}

IntegrationCapabilities IntegrationCapabilityDetector::Detect(
    const wstring& executableDirectory
) const {
    IntegrationCapabilities result;
    const filesystem::path base(executableDirectory);
    const filesystem::path browserDirectory = base / L"integrations" / L"browser";

    result.browserExtensionInstalled = filesystem::exists(browserDirectory / L"manifest.json");
    result.browserNativeHostInstalled =
        filesystem::exists(browserDirectory / L"native-host-manifest.json");
    result.browserReason = result.browserExtensionInstalled
        ? "browser extension files detected"
        : "browser extension is optional and not installed";

    BitsTransferAdapter bits;
    string bitsError;
    result.bitsAvailable = bits.Initialize(bitsError);
    result.bitsReason = result.bitsAvailable
        ? "current-user BITS adapter available"
        : bitsError;

    result.prefetchAvailable =
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "PrefetchVirtualMemory") != nullptr;
    result.prefetchReason = result.prefetchAvailable
        ? "PrefetchVirtualMemory available"
        : "PrefetchVirtualMemory unavailable on this Windows version";
    return result;
}