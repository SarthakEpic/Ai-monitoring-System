#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include "InferenceProtocol.h"

int main() {
    const ModelPrediction json = ParseModelPredictionText("{\"risk\":72.5,\"confidence\":80,\"class\":\"WARNING\",\"safe_to_heal\":false}");
    assert(json.risk == 72.5);
    assert(json.confidence == 80.0);
    assert(json.predictedClass == "WARNING");
    assert(!json.safeToHeal);
    assert(ParseModelPredictionText("bad").risk < 0.0);
    assert(ParseModelPredictionText("101").risk < 0.0);

    const std::filesystem::path path = std::filesystem::current_path() / "aegis_runtime_feature_packet.json";
    RuntimeFeaturePacket packet;
    packet.cpuHistory = {1.0, 2.0};
    packet.memoryHistory = {3.0, 4.0};
    assert(WriteRuntimeFeaturePacket(path, packet));
    {
        std::ifstream file(path);
        const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        assert(content.find("\"cpu_history\": [1.0000,2.0000]") != std::string::npos);
    }
    std::filesystem::remove(path);
    return 0;
}