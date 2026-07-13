#include "SecureActuation.h"
#include "ProofLedger.h"

#include <windows.h>
#include <sddl.h>

#include <atomic>
#include <filesystem>
#include <string>

namespace {
constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\Aegis99ActuatorV1";
constexpr DWORD kMaximumMessageBytes = 2048;
constexpr wchar_t kServiceName[] = L"Aegis99Actuator";
std::atomic_bool g_running{true};
SERVICE_STATUS_HANDLE g_serviceHandle = nullptr;

bool ReadExact(HANDLE pipe, void* buffer, DWORD length) { DWORD received = 0; return ReadFile(pipe, buffer, length, &received, nullptr) != FALSE && received == length; }
bool WriteExact(HANDLE pipe, const void* buffer, DWORD length) { DWORD written = 0; return WriteFile(pipe, buffer, length, &written, nullptr) != FALSE && written == length; }

void ReportStatus(DWORD state, DWORD exitCode = NO_ERROR) {
    if (!g_serviceHandle) return;
    SERVICE_STATUS status{};
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = state;
    status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    status.dwWin32ExitCode = exitCode;
    SetServiceStatus(g_serviceHandle, &status);
}

std::filesystem::path LedgerPath() {
    wchar_t programData[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"PROGRAMDATA", programData, MAX_PATH);
    const std::filesystem::path root = length > 0 && length < MAX_PATH ? std::filesystem::path(programData) : std::filesystem::current_path();
    std::error_code error;
    std::filesystem::create_directories(root / L"Aegis99", error);
    return root / L"Aegis99" / L"proof_ledger.db";
}

std::string HandleRequest(const std::string& payload, ReplayProtection& replay, ProofLedger& ledger) {
    SecureProtocolMessage message; std::string reason;
    if (!SecureMessageValidation::Parse(payload, message, reason)) return "REJECT:" + reason;
    if (!replay.Accept(message, static_cast<long long>(GetTickCount64()), reason)) return "REJECT:" + reason;
    if (message.command != ActuatorCommand::BeginCanary) return "REJECT:transaction_not_active";
    ProofCarryingAction proof;
    if (!ledger.Load(message.proofId, proof, reason)) return "REJECT:proof_reference_not_found";
    if (proof.requestId != message.proofId) return "REJECT:proof_identity_mismatch";
    if (!TrustedSafetyController().ValidateProof(proof, reason)) return "REJECT:" + reason;
    if (!ledger.ClaimForCanary(message.proofId, reason)) return "REJECT:proof_already_consumed_or_not_pending";
    // A valid proof must additionally carry a signature from the isolated native
    // inference producer. Until that producer and verification key are deployed,
    // the actuator refuses mutation after revalidation rather than trusting UI data.
    ledger.MarkLeaseState(message.proofId, "REJECTED_UNSIGNED_PROOF", reason);
    return "REJECT:trusted_proof_signature_required";
}

void RunPipeServer() {
    ReplayProtection replay;
    ProofLedger ledger;
    std::string ledgerError;
    if (!ledger.Open(LedgerPath().string(), ledgerError)) return;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    // SYSTEM and LocalService own the server; local authenticated users may only
    // submit the bounded protocol. Remote pipe clients are rejected by creation flags.
    ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(A;;GA;;;SY)(A;;GA;;;LS)(A;;GRGW;;;AU)", SDDL_REVISION_1, &descriptor, nullptr);
    security.lpSecurityDescriptor = descriptor;
    while (g_running.load()) {
        HANDLE pipe = CreateNamedPipeW(kPipeName, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, kMaximumMessageBytes, kMaximumMessageBytes, 1000, descriptor ? &security : nullptr);
        if (pipe == INVALID_HANDLE_VALUE) break;
        const BOOL connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (connected) {
            DWORD length = 0;
            if (ReadExact(pipe, &length, sizeof(length)) && length > 0 && length <= kMaximumMessageBytes) {
                std::string payload(length, '\0');
                if (ReadExact(pipe, payload.data(), length)) {
                    const std::string response = HandleRequest(payload, replay, ledger);
                    const DWORD responseLength = static_cast<DWORD>(response.size());
                    WriteExact(pipe, &responseLength, sizeof(responseLength));
                    WriteExact(pipe, response.data(), responseLength);
                }
            }
        }
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    if (descriptor) LocalFree(descriptor);
    ledger.Close();
}

DWORD WINAPI ServiceControlHandler(DWORD control, DWORD, void*, void*) {
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
        ReportStatus(SERVICE_STOP_PENDING);
        g_running.store(false);
    }
    return NO_ERROR;
}

void WINAPI ServiceMain(DWORD, LPWSTR*) {
    g_serviceHandle = RegisterServiceCtrlHandlerExW(kServiceName, ServiceControlHandler, nullptr);
    if (!g_serviceHandle) return;
    ReportStatus(SERVICE_START_PENDING);
    ReportStatus(SERVICE_RUNNING);
    RunPipeServer();
    ReportStatus(SERVICE_STOPPED);
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--console") { RunPipeServer(); return 0; }
    SERVICE_TABLE_ENTRYW dispatchTable[] = {{const_cast<wchar_t*>(kServiceName), ServiceMain}, {nullptr, nullptr}};
    return StartServiceCtrlDispatcherW(dispatchTable) ? 0 : static_cast<int>(GetLastError());
}
