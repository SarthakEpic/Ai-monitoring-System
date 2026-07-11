#include "SafeOnlinePolicy.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace std;

namespace {

void Require(bool condition, const string& message) {
    if (!condition) {
        throw runtime_error(message);
    }
}

string CurrentExecutablePath() {
    wchar_t buffer[32768]{};
    const DWORD length = GetModuleFileNameW(
        nullptr,
        buffer,
        static_cast<DWORD>(size(buffer))
    );
    Require(length > 0, "executable path unavailable");
    return filesystem::path(wstring(buffer, length)).string();
}

DWORD ReadPriorityClass(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    Require(process != nullptr, "test process could not be opened");
    const DWORD priority = GetPriorityClass(process);
    CloseHandle(process);
    Require(priority != 0, "test process priority could not be read");
    return priority;
}

struct Child {
    PROCESS_INFORMATION process{};

    Child() = default;
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;

    Child(Child&& other) noexcept : process(other.process) {
        other.process = {};
    }

    ~Child() {
        if (process.hProcess) {
            if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
                TerminateProcess(process.hProcess, 0);
            }
            WaitForSingleObject(process.hProcess, 2000);
            CloseHandle(process.hProcess);
        }
        if (process.hThread) {
            CloseHandle(process.hThread);
        }
    }
};

Child StartChild() {
    const filesystem::path executable(CurrentExecutablePath());
    const wstring command = L"\"" + executable.wstring() + L"\" --policy-test-child";
    vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    Child child;
    Require(
        CreateProcessW(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &child.process
        ) != FALSE,
        "child start failed"
    );
    Sleep(100);
    return child;
}

OnlinePolicyConfig EnabledConfig() {
    OnlinePolicyConfig config;
    config.onlineEnabled = true;
    config.policyPromoted = true;
    config.globalDisable = false;
    config.requireApproval = true;
    config.maximumActionsPerHour = 2;
    config.cooldownSeconds = 60;
    config.maximumActionSeconds = 5;
    config.observationSeconds = 1;
    config.minimumLowerConfidenceBenefit = 0.05;
    config.minimumTargetSafety = 90;
    config.maximumTargetCriticality = 30;
    return config;
}

OnlinePolicyInput EligibleInput(const Child& child) {
    const filesystem::path executable(CurrentExecutablePath());
    OnlinePolicyInput input;
    input.shadow.wouldAct = true;
    input.shadow.selectedAction = ResourceActionType::LowerPriority;
    input.shadow.targetPid = child.process.dwProcessId;
    input.shadow.targetName = executable.filename().string();
    input.shadow.lowerConfidenceBound = 0.4;
    input.deterministicPolicy.executionEligible = true;
    input.deterministicPolicy.hardBlock = false;
    input.context.values.assign(WorkloadContextEncoder::Dimension(), 0.0);
    input.context.values[0] = 1.0;
    input.context.targetPid = child.process.dwProcessId;
    input.context.targetSafety = 95;
    input.context.targetCriticality = 10;
    input.targetExecutablePath = executable.string();
    input.targetCategory = "TEST";
    input.targetSafety = "SAFE";
    input.allowlist = {executable.filename().string()};
    input.userApproved = true;
    return input;
}

void VerifyGatesAndEmergencyRestore(const filesystem::path& database) {
    const auto executor = make_shared<WindowsProcessActionExecutor>();
    ActionCoordinator coordinator(executor);
    ContextualImpactModel model;
    LearningJournal journal;
    string error;
    Require(journal.Open(database.string(), error), "learning journal open failed");

    SafeOnlinePolicyController controller(coordinator, model, journal);
    OnlinePolicyConfig config = EnabledConfig();
    controller.Configure(config);

    vector<string> recovery;
    Require(
        controller.Initialize(database.string(), recovery),
        "online controller initialization failed"
    );

    Child child = StartChild();
    const OnlinePolicyInput input = EligibleInput(child);

    config.policyPromoted = false;
    controller.Configure(config);
    Require(!controller.Evaluate(input).eligible, "unpromoted policy executed");

    config.policyPromoted = true;
    config.killSwitchFile = database.parent_path() /
        ("STOP_ACTIONS_" + to_string(GetCurrentProcessId()));
    controller.Configure(config);
    {
        ofstream stop(config.killSwitchFile);
        stop << "stop";
    }
    Require(!controller.Evaluate(input).eligible, "kill-switch file did not block execution");
    filesystem::remove(config.killSwitchFile);
    Require(controller.Evaluate(input).eligible, "fully safe policy was not eligible");

    const DWORD priorityBefore = ReadPriorityClass(child.process.dwProcessId);
    SystemSnapshot system;
    system.availableMemoryMB = 500;
    const ActionTransaction transaction = controller.Execute(input, system);
    Require(
        transaction.status == ActionTransactionStatus::Executed,
        "online action did not execute: " + transaction.reason + transaction.error
    );
    Require(
        ReadPriorityClass(child.process.dwProcessId) == BELOW_NORMAL_PRIORITY_CLASS,
        "online priority action not applied"
    );

    vector<string> errors;
    Require(
        controller.EmergencyStop("test emergency stop", errors) == 1,
        "emergency stop did not restore action"
    );
    Require(
        ReadPriorityClass(child.process.dwProcessId) == priorityBefore,
        "emergency stop did not restore priority"
    );
    Require(!controller.Evaluate(input).eligible, "cooldown did not block repeated action");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && string(argv[1]) == "--policy-test-child") {
        Sleep(30000);
        return 0;
    }

    const filesystem::path database = filesystem::temp_directory_path() /
        ("safe_online_policy_" + to_string(GetCurrentProcessId()) + ".db");
    filesystem::remove(database);

    int exitCode = 0;
    try {
        VerifyGatesAndEmergencyRestore(database);
        cout << "SafeOnlinePolicyTests passed\n";
    } catch (const exception& ex) {
        cerr << "SafeOnlinePolicyTests failed: " << ex.what() << '\n';
        exitCode = 1;
    }

    filesystem::remove(database);
    return exitCode;
}
