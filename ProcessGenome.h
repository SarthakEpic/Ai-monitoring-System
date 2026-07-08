#pragma once

#include <vector>

#include "ProcessSnapshot.h"
#include "SystemMetrics.h"

class ProcessGenomeEngine {
public:
    static void Annotate(std::vector<ProcessSnapshot>& processes);
    static ProcessSummary SelectTopPressureProcess(const std::vector<ProcessSnapshot>& processes);
    static std::vector<ProcessSnapshot> TopCandidates(const std::vector<ProcessSnapshot>& processes, size_t limit);

private:
    static double CandidateRank(const ProcessSnapshot& process);
    static ProcessSummary ToSummary(const ProcessSnapshot& process);
};
