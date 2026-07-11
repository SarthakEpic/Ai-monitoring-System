#include <windows.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

namespace {

constexpr uint32_t kMaximumMessageBytes = 1024 * 1024;
constexpr wchar_t kBridgePipe[] = L"\\\\.\\pipe\\PredictiveAutoHealBrowserBridge";

bool ReadExact(HANDLE handle, void* destination, DWORD bytes) {
    auto* cursor = static_cast<unsigned char*>(destination);
    DWORD remaining = bytes;
    while (remaining > 0) {
        DWORD read = 0;
        if (!ReadFile(handle, cursor, remaining, &read, nullptr) || read == 0) return false;
        cursor += read;
        remaining -= read;
    }
    return true;
}

bool WriteExact(HANDLE handle, const void* source, DWORD bytes) {
    const auto* cursor = static_cast<const unsigned char*>(source);
    DWORD remaining = bytes;
    while (remaining > 0) {
        DWORD written = 0;
        if (!WriteFile(handle, cursor, remaining, &written, nullptr) || written == 0) return false;
        cursor += written;
        remaining -= written;
    }
    return true;
}

bool ReadNativeMessage(HANDLE input, string& message) {
    uint32_t length = 0;
    if (!ReadExact(input, &length, sizeof(length))) return false;
    if (length == 0 || length > kMaximumMessageBytes) return false;
    message.assign(length, '\0');
    return ReadExact(input, message.data(), length);
}

bool WriteNativeMessage(HANDLE output, const string& message) {
    const uint32_t length = static_cast<uint32_t>(message.size());
    return WriteExact(output, &length, sizeof(length)) && WriteExact(output, message.data(), length);
}

string RelayToControlCenter(const string& message) {
    if (!WaitNamedPipeW(kBridgePipe, 250)) {
        return R"({"type":"bridge_status","status":"UNAVAILABLE","reason":"PredictiveAutoHeal browser bridge is not running"})";
    }
    HANDLE pipe = CreateFileW(kBridgePipe, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        return R"({"type":"bridge_status","status":"UNAVAILABLE","reason":"cannot connect to PredictiveAutoHeal browser bridge"})";
    }
    const uint32_t requestLength = static_cast<uint32_t>(message.size());
    uint32_t responseLength = 0;
    string response;
    if (WriteExact(pipe, &requestLength, sizeof(requestLength)) &&
        WriteExact(pipe, message.data(), requestLength) &&
        ReadExact(pipe, &responseLength, sizeof(responseLength)) &&
        responseLength > 0 && responseLength <= kMaximumMessageBytes) {
        response.assign(responseLength, '\0');
        if (!ReadExact(pipe, response.data(), responseLength)) response.clear();
    }
    CloseHandle(pipe);
    return response.empty()
        ? R"({"type":"bridge_status","status":"ERROR","reason":"control center returned no valid response"})"
        : response;
}

}  // namespace

int main() {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (input == INVALID_HANDLE_VALUE || output == INVALID_HANDLE_VALUE) return 2;
    string request;
    while (ReadNativeMessage(input, request)) {
        if (!WriteNativeMessage(output, RelayToControlCenter(request))) return 3;
    }
    return 0;
}
