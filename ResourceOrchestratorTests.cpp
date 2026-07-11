#include "ResourceOrchestrator.h"
#include "ActionVerification.h"

#include <windows.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace {

void Require(bool condition, const string& message) {
    if (!condition) throw runtime_error(message);
}

string CurrentExecutablePath() {
    wchar_t buffer[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(size(buffer)));
    Require(length > 0 && length < size(buffer), "cannot read test executable path");
    return filesystem::path(wstring(buffer, length)).string();
}

struct ChildProcess {
    PROCESS_INFORMATION info{};

    ChildProcess() = default;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&& other) noexcept : info(other.info) { other.info = {}; }

    ~ChildProcess() {
        if (info.hProcess) {
            if (WaitForSingleObject(info.hProcess, 0) == WAIT_TIMEOUT) TerminateProcess(info.hProcess, 0);
            WaitForSingleObject(info.hProcess, 3000);
            CloseHandle(info.hProcess);
        }
        if (info.hThread) CloseHandle(info.hThread);
    }
};

ChildProcess StartChild() {
    const filesystem::path exe(CurrentExecutablePath());
    wstring command = L"\"" + exe.wstring() + L"\" --action-test-child";
    vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    ChildProcess child;
    Require(CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, nullptr, &startup, &child.info) != FALSE,
            "cannot start isolated action test process");
    Sleep(100);
    return child;
}

ActionContext MakeContext(const ChildProcess& child, ResourceActionType action) {
    const filesystem::path exe(CurrentExecutablePath());
    ActionContext context;
    context.targetPid = child.info.dwProcessId;
    context.action = action;
    context.expectedProcessName = exe.filename().string();
    context.expectedExecutablePath = exe.string();
    context.targetCategory = "TEST_PROCESS";
    context.targetSafety = "SAFE";
    context.allowlist = {exe.filename().string()};
    context.userApproved = true;
    context.maxDuration = chrono::milliseconds(250);
    return context;
}

OrchestratorConfig EnabledConfig() {
    OrchestratorConfig config;
    config.executionEnabled = true;
    config.globalDisable = false;
    config.maximumActionSeconds = 2;
    return config;
}

DWORD ReadPriority(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    Require(process != nullptr, "cannot open child to query priority");
    const DWORD value = GetPriorityClass(process);
    CloseHandle(process);
    Require(value != 0, "cannot read child priority");
    return value;
}

ULONG ReadMemoryPriority(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    Require(process != nullptr, "cannot open child to query memory priority");
    MEMORY_PRIORITY_INFORMATION info{};
    const BOOL ok = GetProcessInformation(process, ProcessMemoryPriority, &info, sizeof(info));
    CloseHandle(process);
    Require(ok != FALSE, "cannot read child memory priority");
    return info.MemoryPriority;
}

PROCESS_POWER_THROTTLING_STATE ReadPowerState(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    Require(process != nullptr, "cannot open child to query power throttling");
    PROCESS_POWER_THROTTLING_STATE info{};
    info.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    const BOOL ok = GetProcessInformation(process, ProcessPowerThrottling, &info, sizeof(info));
    CloseHandle(process);
    Require(ok != FALSE, "cannot read child power throttling");
    return info;
}

unique_ptr<ActionCoordinator> MakeCoordinator(const filesystem::path& database) {
    auto coordinator = make_unique<ActionCoordinator>(make_shared<WindowsProcessActionExecutor>());
    coordinator->Configure(EnabledConfig());
    string error;
    Require(coordinator->OpenJournal(database.string(), error), "cannot open action journal: " + error);
    return coordinator;
}

void VerifySafetyBlocks(const filesystem::path& database) {
    ChildProcess child = StartChild();
    auto coordinator = MakeCoordinator(database);
    OrchestratorConfig disabled = EnabledConfig();
    disabled.globalDisable = true;
    coordinator->Configure(disabled);
    Require(coordinator->Begin(MakeContext(child, ResourceActionType::LowerPriority)).status == ActionTransactionStatus::Blocked,
            "global kill switch must block execution");

    coordinator->Configure(EnabledConfig());
    ActionContext foreground = MakeContext(child, ResourceActionType::LowerPriority);
    foreground.foregroundPid = child.info.dwProcessId;
    Require(coordinator->Begin(foreground).status == ActionTransactionStatus::Blocked,
            "foreground target must be blocked");

    ActionContext noApproval = MakeContext(child, ResourceActionType::LowerPriority);
    noApproval.userApproved = false;
    Require(coordinator->Begin(noApproval).status == ActionTransactionStatus::Blocked,
            "missing approval must be blocked");

    ActionContext noAllowlist = MakeContext(child, ResourceActionType::LowerPriority);
    noAllowlist.allowlist.clear();
    Require(coordinator->Begin(noAllowlist).status == ActionTransactionStatus::Blocked,
            "missing allowlist must be blocked");
}

void VerifyPriorityRoundTrip(const filesystem::path& database) {
    ChildProcess child = StartChild();
    auto coordinator = MakeCoordinator(database);
    const DWORD before = ReadPriority(child.info.dwProcessId);
    ActionTransaction tx = coordinator->Begin(MakeContext(child, ResourceActionType::LowerPriority));
    Require(tx.status == ActionTransactionStatus::Executed, "priority action failed: " + tx.error);
    Require(ReadPriority(child.info.dwProcessId) == BELOW_NORMAL_PRIORITY_CLASS, "priority was not lowered");
    string error;
    Require(coordinator->Rollback(tx.transactionId, "integration test", error), "priority rollback failed: " + error);
    Require(ReadPriority(child.info.dwProcessId) == before, "priority was not restored");
}

void VerifyMemoryPriorityRoundTrip(const filesystem::path& database) {
    ChildProcess child = StartChild();
    auto coordinator = MakeCoordinator(database);
    const ULONG before = ReadMemoryPriority(child.info.dwProcessId);
    ActionTransaction tx = coordinator->Begin(MakeContext(child, ResourceActionType::LowerMemoryPriority));
    Require(tx.status == ActionTransactionStatus::Executed, "memory action failed: " + tx.error);
    Require(ReadMemoryPriority(child.info.dwProcessId) == 2, "memory priority was not lowered");
    string error;
    Require(coordinator->Rollback(tx.transactionId, "integration test", error), "memory rollback failed: " + error);
    Require(ReadMemoryPriority(child.info.dwProcessId) == before, "memory priority was not restored");
}

void VerifyEcoQosRoundTrip(const filesystem::path& database) {
    ChildProcess child = StartChild();
    auto coordinator = MakeCoordinator(database);
    const PROCESS_POWER_THROTTLING_STATE before = ReadPowerState(child.info.dwProcessId);
    ActionTransaction tx = coordinator->Begin(MakeContext(child, ResourceActionType::EnableEcoQos));
    Require(tx.status == ActionTransactionStatus::Executed, "EcoQoS action failed: " + tx.error);
    const PROCESS_POWER_THROTTLING_STATE during = ReadPowerState(child.info.dwProcessId);
    Require((during.ControlMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) != 0 &&
            (during.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) != 0,
            "EcoQoS was not enabled");
    string error;
    Require(coordinator->Rollback(tx.transactionId, "integration test", error), "EcoQoS rollback failed: " + error);
    const PROCESS_POWER_THROTTLING_STATE after = ReadPowerState(child.info.dwProcessId);
    Require((after.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) ==
                (before.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED),
            "EcoQoS effective execution-speed state was not restored");
}

void VerifyMeasuredHarmRollback(const filesystem::path& database) {
    ChildProcess child = StartChild();
    auto coordinator = MakeCoordinator(database);
    MeasuredActionSession session(*coordinator);
    string journalError;
    Require(session.OpenJournal(database.string(), journalError), "cannot open verification journal: " + journalError);
    const DWORD beforePriority = ReadPriority(child.info.dwProcessId);
    ActionContext context = MakeContext(child, ResourceActionType::LowerPriority);
    SystemSnapshot system;
    system.availableMemoryMB = 500.0;
    ActionTransaction tx = session.Begin(context, system);
    Require(tx.status == ActionTransactionStatus::Executed, "measured rollback action did not execute");

    ActionMetricSnapshot before;
    before.timestampMs = 1000;
    before.systemCpuPercent = 40.0;
    before.availableMemoryMB = 500.0;
    before.foregroundAlive = true;
    before.targetAlive = true;
    before.measured = true;
    ActionMetricSnapshot after = before;
    after.timestampMs = 11000;
    after.systemCpuPercent = 75.0;
    after.availableMemoryMB = 250.0;
    after.foregroundHung = true;
    after.targetPageFaults = 2000;

    ActionImpactResult outcome = session.VerifyWithSnapshots(tx.transactionId, before, after, OutcomePolicy{});
    Require(outcome.status == ImpactStatus::VerifiedHarmful, "harmful measurement was not detected");
    Require(outcome.rollbackRequired && outcome.rollbackSucceeded, "harmful measurement did not auto-rollback");
    Require(coordinator->ActiveTransactions().empty(), "harmful transaction remained active");
    Require(ReadPriority(child.info.dwProcessId) == beforePriority, "automatic rollback did not restore priority");
}
void VerifyStartupRecovery(const filesystem::path& database) {
    ChildProcess child = StartChild();
    auto firstCoordinator = MakeCoordinator(database);
    const DWORD before = ReadPriority(child.info.dwProcessId);
    ActionTransaction tx = firstCoordinator->Begin(MakeContext(child, ResourceActionType::LowerPriority));
    Require(tx.status == ActionTransactionStatus::Executed, "startup recovery action did not execute");
    Require(ReadPriority(child.info.dwProcessId) == BELOW_NORMAL_PRIORITY_CLASS, "startup recovery setup did not change priority");

    auto recoveryCoordinator = MakeCoordinator(database);
    vector<string> errors;
    Require(recoveryCoordinator->RecoverUnfinished(errors) >= 1, "startup recovery found no unfinished action");
    Require(errors.empty(), "startup recovery reported an error");
    Require(ReadPriority(child.info.dwProcessId) == before, "startup recovery did not restore original priority");
}
void VerifyExpiryRollback(const filesystem::path& database) {
    ChildProcess child = StartChild();
    auto coordinator = MakeCoordinator(database);
    ActionContext context = MakeContext(child, ResourceActionType::LowerPriority);
    context.maxDuration = chrono::milliseconds(20);
    const DWORD before = ReadPriority(child.info.dwProcessId);
    ActionTransaction tx = coordinator->Begin(context);
    Require(tx.status == ActionTransactionStatus::Executed, "expiry test action failed");
    Sleep(35);
    coordinator->Tick();
    Require(coordinator->ActiveTransactions().empty(), "expired action remained active");
    Require(ReadPriority(child.info.dwProcessId) == before, "expiry did not restore priority");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && string(argv[1]) == "--action-test-child") {
        Sleep(30000);
        return 0;
    }

    const filesystem::path database = filesystem::temp_directory_path() /
        ("predictive_autoheal_action_tests_" + to_string(GetCurrentProcessId()) + ".db");
    filesystem::remove(database);
    filesystem::remove(database.string() + "-wal");
    filesystem::remove(database.string() + "-shm");
    try {
        VerifySafetyBlocks(database);
        VerifyPriorityRoundTrip(database);
        VerifyMemoryPriorityRoundTrip(database);
        VerifyEcoQosRoundTrip(database);
        VerifyMeasuredHarmRollback(database);
        VerifyExpiryRollback(database);
        cout << "ResourceOrchestratorTests passed\n";
        return 0;
    } catch (const exception& ex) {
        cerr << "ResourceOrchestratorTests failed: " << ex.what() << '\n';
        return 1;
    }
}
