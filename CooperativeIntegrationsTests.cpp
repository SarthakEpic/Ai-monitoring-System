#include "CooperativeIntegrations.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace std;

namespace {

void Require(bool condition, const string& message) {
    if (!condition) {
        throw runtime_error(message);
    }
}

void VerifyBrowserSafety() {
    BrowserTabSafetyValidator validator;
    BrowserTabState tab;
    tab.tabId = 1;
    tab.url = "https://example.com";
    Require(validator.CanDiscard(tab, true).eligible, "safe inactive tab was rejected");

    tab.active = true;
    Require(!validator.CanDiscard(tab, true).eligible, "active tab was allowed");
    tab.active = false;
    tab.pinned = true;
    Require(!validator.CanDiscard(tab, true).eligible, "pinned tab was allowed");
    tab.pinned = false;
    tab.audible = true;
    Require(!validator.CanDiscard(tab, true).eligible, "audible tab was allowed");
    tab.audible = false;
    tab.url = "chrome://settings";
    Require(!validator.CanDiscard(tab, true).eligible, "privileged tab was allowed");
}

void VerifyWorkloadProtection() {
    SystemSnapshot snapshot;
    snapshot.intent.foregroundPid = 10;
    snapshot.intent.foregroundProcess = "teams.exe";

    ProcessSnapshot meeting;
    meeting.pid = 10;
    meeting.name = "teams.exe";
    meeting.isForeground = true;

    ProcessSnapshot audio;
    audio.pid = 11;
    audio.name = "audiodg.exe";

    ProcessSnapshot updater;
    updater.pid = 12;
    updater.name = "updater.exe";
    snapshot.processGenome = {meeting, audio, updater};

    PerformanceCriticalityGraph graph;
    graph.foregroundPid = 10;
    graph.nodes = {
        {10, "teams.exe", 100, true, true, {"foreground"}},
        {11, "audiodg.exe", 20, false, false, {}},
        {12, "updater.exe", 10, false, false, {}}
    };

    WorkloadProtectionEngine engine;
    const auto result = engine.Build(snapshot, graph, WorkloadPhase::VideoMeeting);
    Require(
        find(result.protectedPids.begin(), result.protectedPids.end(), DWORD{10}) !=
            result.protectedPids.end(),
        "foreground meeting app not protected"
    );
    Require(
        find(result.protectedPids.begin(), result.protectedPids.end(), DWORD{11}) !=
            result.protectedPids.end(),
        "audio dependency not protected"
    );
    Require(
        find(result.protectedPids.begin(), result.protectedPids.end(), DWORD{12}) ==
            result.protectedPids.end(),
        "unrelated updater was protected"
    );
}

void VerifyPrefetchLease() {
    const filesystem::path file = filesystem::temp_directory_path() /
        ("prefetch_test_" + to_string(GetCurrentProcessId()) + ".bin");
    {
        ofstream stream(file, ios::binary);
        const string block(4096, 'x');
        stream.write(block.data(), static_cast<streamsize>(block.size()));
    }

    PredictivePrefetcher prefetcher;
    const auto blocked = prefetcher.PrefetchFile(file.wstring(), 0.5, 0.5, 4096, true);
    Require(
        blocked.transaction.status == "BLOCKED" && !blocked.lease,
        "low-confidence prefetch executed"
    );

    const auto result = prefetcher.PrefetchFile(file.wstring(), 0.95, 0.2, 4096, true);
    Require(
        result.transaction.status == "EXECUTED" && result.lease && result.lease->Active(),
        "approved prefetch failed"
    );
    Require(result.lease->Bytes() == 4096, "prefetch byte budget was not respected");
    result.lease->Release();
    Require(!result.lease->Active(), "prefetch lease did not release");
    filesystem::remove(file);
}

void VerifyBitsIsConservative() {
    BitsTransferAdapter bits;
    string error;
    if (bits.Initialize(error)) {
        error.clear();
        Require(!bits.Resume("unknown-transaction", error), "unknown BITS transaction resumed");
        Require(!error.empty(), "BITS resume failure lacked a reason");
    } else {
        Require(!error.empty(), "BITS capability failure lacked a reason");
    }
}

}  // namespace

int main() {
    try {
        VerifyBrowserSafety();
        VerifyWorkloadProtection();
        VerifyPrefetchLease();
        VerifyBitsIsConservative();
        cout << "CooperativeIntegrationsTests passed\n";
        return 0;
    } catch (const exception& ex) {
        cerr << "CooperativeIntegrationsTests failed: " << ex.what() << '\n';
        return 1;
    }
}
