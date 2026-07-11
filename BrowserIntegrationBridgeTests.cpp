#include "BrowserIntegrationBridge.h"

#include <iostream>
#include <stdexcept>

using namespace std;

namespace {

void Require(bool condition, const string& message) {
    if (!condition) {
        throw runtime_error(message);
    }
}

}  // namespace

int main() {
    try {
        BrowserIntegrationBridge bridge;

        BrowserTabState unsafe;
        unsafe.tabId = 1;
        unsafe.url = "https://example.com";
        unsafe.active = true;
        Require(bridge.QueueDiscard(unsafe, true).status == "BLOCKED", "active tab was queued");

        BrowserTabState safe;
        safe.tabId = 7;
        safe.url = "https://example.com";
        const auto planned = bridge.QueueDiscard(safe, true);
        Require(planned.status == "PLANNED", "safe tab was not queued");

        const string command = bridge.ProcessMessageForTest(R"({"type":"poll"})");
        Require(
            command.find("discard_tab") != string::npos &&
                command.find("\"tabId\":7") != string::npos,
            "poll did not return discard command"
        );

        const string result =
            "{\"type\":\"action_result\",\"transactionId\":\"" + planned.transactionId +
            "\",\"status\":\"EXECUTED\"}";
        bridge.ProcessMessageForTest(result);
        Require(
            bridge.QueueRestore(planned.transactionId).status == "PLANNED",
            "executed tab could not be restored"
        );

        const string restore = bridge.ProcessMessageForTest(R"({"type":"poll"})");
        Require(restore.find("restore_tab") != string::npos, "poll did not return restore command");

        const string restored =
            "{\"type\":\"action_result\",\"transactionId\":\"" + planned.transactionId +
            "\",\"status\":\"ROLLED_BACK\"}";
        bridge.ProcessMessageForTest(restored);

        const auto results = bridge.DrainResults();
        Require(results.size() == 2, "browser results were not audited");
        Require(
            bridge.QueueRestore(planned.transactionId).status == "BLOCKED",
            "restored transaction remained active"
        );

        cout << "BrowserIntegrationBridgeTests passed\n";
        return 0;
    } catch (const exception& ex) {
        cerr << "BrowserIntegrationBridgeTests failed: " << ex.what() << '\n';
        return 1;
    }
}
