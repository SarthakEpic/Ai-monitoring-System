#pragma once

#include "ProcessSnapshot.h"

class ProcessClassifier {
public:
    static void Classify(ProcessSnapshot& process);

private:
    static bool ContainsToken(const std::string& text, const std::string& token);
    static bool IsCriticalProcessName(const std::string& name);
    static bool IsSecurityProcessName(const std::string& name);
    static bool IsBrowserProcessName(const std::string& name);
    static bool IsUpdaterOrSyncName(const std::string& name);
    static bool IsLikelyWindowsService(const ProcessSnapshot& process);
    static std::string Lower(std::string value);
};
