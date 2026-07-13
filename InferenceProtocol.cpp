#include "InferenceProtocol.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <windows.h>

#include <windows.h>

namespace {
std::string ExtractJsonString(const std::string& text, const std::string& key, const std::string& fallback = "") {
    const std::string needle = "\"" + key + "\":";
    std::size_t position = text.find(needle);
    if (position == std::string::npos) return fallback;
    position += needle.size();
    while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position]))) ++position;
    if (position >= text.size() || text[position] != '"') return fallback;
    ++position;
    std::string value;
    bool escaped = false;
    for (; position < text.size(); ++position) {
        const char character = text[position];
        if (escaped) {
            value.push_back(character);
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == '"') {
            break;
        } else {
            value.push_back(character);
        }
    }
    return value.empty() ? fallback : value;
}

double ExtractJsonDouble(const std::string& text, const std::string& key, double fallback = 0.0) {
    const std::string needle = "\"" + key + "\":";
    std::size_t position = text.find(needle);
    if (position == std::string::npos) return fallback;
    position += needle.size();
    while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position]))) ++position;
    std::size_t end = position;
    while (end < text.size() && (std::isdigit(static_cast<unsigned char>(text[end])) || text[end] == '-' || text[end] == '+' || text[end] == '.')) ++end;
    try {
        return std::stod(text.substr(position, end - position));
    } catch (...) {
        return fallback;
    }
}

bool ExtractJsonBool(const std::string& text, const std::string& key, bool fallback = false) {
    const std::string needle = "\"" + key + "\":";
    std::size_t position = text.find(needle);
    if (position == std::string::npos) return fallback;
    position += needle.size();
    while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position]))) ++position;
    if (text.compare(position, 4, "true") == 0) return true;
    if (text.compare(position, 5, "false") == 0) return false;
    return fallback;
}

std::string ToJsonArray(const std::deque<double>& values) {
    std::ostringstream output;
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) output << ',';
        output << std::fixed << std::setprecision(4) << values[index];
    }
    output << ']';
    return output.str();
}
}  // namespace

ModelPrediction ParseModelPredictionText(const std::string& text) {
    ModelPrediction failed;
    try {
        ModelPrediction prediction;
        if (!text.empty() && text.front() == '{') {
            prediction.risk = ExtractJsonDouble(text, "risk", ExtractJsonDouble(text, "probability", -1.0));
            prediction.confidence = ExtractJsonDouble(text, "confidence", 0.0);
            prediction.predictedClass = ExtractJsonString(text, "class", "UNKNOWN");
            prediction.reason = ExtractJsonString(text, "reason", "N/A");
            prediction.rootCause = ExtractJsonString(text, "primary", "unknown");
            prediction.rootSeverity = ExtractJsonString(text, "severity", "normal");
            prediction.modelReadiness = ExtractJsonString(text, "model_readiness", "unknown");
            prediction.modelGeneratedAt = ExtractJsonString(text, "model_generated_at", "unknown");
            prediction.featureCount = static_cast<int>(ExtractJsonDouble(text, "feature_count", 0.0));
            prediction.recommendedAction = ExtractJsonString(text, "recommended_action", "monitor_only");
            prediction.safeToHeal = ExtractJsonBool(text, "safe_to_heal", false);
        } else {
            prediction.risk = std::stod(text);
            prediction.confidence = 55.0;
            prediction.predictedClass = prediction.risk >= 75.0 ? "CRITICAL" : (prediction.risk >= 55.0 ? "WARNING" : "NORMAL");
            prediction.reason = "legacy model probability";
            prediction.rootCause = "legacy_probability";
        }
        if (prediction.risk < 0.0 || prediction.risk > 100.0) return failed;
        prediction.confidence = std::clamp(prediction.confidence, 0.0, 100.0);
        return prediction;
    } catch (...) {
        return failed;
    }
}

bool WriteRuntimeFeaturePacket(const std::filesystem::path& outputPath, const RuntimeFeaturePacket& packet) {
    // The legacy research bridge must not block monitoring when a consumer holds
    // the packet. The native IPC replacement planned for Phase 3 removes this file.
    std::ostringstream output;
    output << "{\n";
    output << "  \"window\": " << packet.window << ",\n";
    output << "  \"cpu_threshold\": " << packet.cpuThreshold << ",\n";
    output << "  \"mem_threshold\": " << packet.memoryThreshold << ",\n";
    output << "  \"disk_threshold\": " << packet.diskThreshold << ",\n";
    output << "  \"cpu_history\": " << ToJsonArray(packet.cpuHistory) << ",\n";
    output << "  \"mem_history\": " << ToJsonArray(packet.memoryHistory) << ",\n";
    output << "  \"disk_history\": " << ToJsonArray(packet.diskHistory) << ",\n";
    output << "  \"net_history\": " << ToJsonArray(packet.networkHistory) << ",\n";
    output << "  \"process_history\": " << ToJsonArray(packet.processHistory) << ",\n";
    output << "  \"top_process_cpu_history\": " << ToJsonArray(packet.topProcessCpuHistory) << ",\n";
    output << "  \"top_process_mem_history\": " << ToJsonArray(packet.topProcessMemoryHistory) << "\n";
    output << "}\n";
    const std::string payload = output.str();
    if (payload.empty() || payload.size() > MAXDWORD) return false;

    const HANDLE file = CreateFileW(
        outputPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    const HANDLE completion = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (completion == nullptr) {
        CloseHandle(file);
        return false;
    }

    OVERLAPPED operation{};
    operation.hEvent = completion;
    DWORD bytesWritten = 0;
    bool completed = WriteFile(
        file, payload.data(), static_cast<DWORD>(payload.size()), &bytesWritten, &operation) != FALSE;
    if (completed) {
        completed = bytesWritten == payload.size();
    } else if (GetLastError() == ERROR_IO_PENDING &&
               WaitForSingleObject(completion, 100) == WAIT_OBJECT_0) {
        completed = GetOverlappedResult(file, &operation, &bytesWritten, FALSE) != FALSE &&
                    bytesWritten == payload.size();
    }

    if (!completed) {
        CancelIoEx(file, &operation);
        WaitForSingleObject(completion, 100);
    }
    CloseHandle(completion);
    CloseHandle(file);
    return completed;
}