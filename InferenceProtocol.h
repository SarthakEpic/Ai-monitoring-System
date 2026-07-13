#pragma once

#include <deque>
#include <filesystem>
#include <string>

struct ModelPrediction {
    double risk = -1.0;
    double confidence = 0.0;
    std::string predictedClass = "UNKNOWN";
    std::string reason = "N/A";
    std::string rootCause = "unknown";
    std::string rootSeverity = "normal";
    std::string modelReadiness = "unknown";
    std::string modelGeneratedAt = "unknown";
    std::string recommendedAction = "monitor_only";
    bool safeToHeal = false;
    int featureCount = 0;
};

struct RuntimeFeaturePacket {
    int window = 8;
    int cpuThreshold = 80;
    int memoryThreshold = 85;
    int diskThreshold = 10;
    std::deque<double> cpuHistory;
    std::deque<double> memoryHistory;
    std::deque<double> diskHistory;
    std::deque<double> networkHistory;
    std::deque<double> processHistory;
    std::deque<double> topProcessCpuHistory;
    std::deque<double> topProcessMemoryHistory;
};

ModelPrediction ParseModelPredictionText(const std::string& text);
bool WriteRuntimeFeaturePacket(const std::filesystem::path& outputPath, const RuntimeFeaturePacket& packet);
