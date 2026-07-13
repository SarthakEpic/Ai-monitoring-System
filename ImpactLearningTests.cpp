#include "ImpactLearning.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace std;

namespace {

void Require(bool condition, const string& message) {
    if (!condition) {
        throw runtime_error(message);
    }
}

WorkloadContextFeatures Context() {
    WorkloadContextFeatures context;
    context.values.assign(WorkloadContextEncoder::Dimension(), 0.0);
    context.values[0] = 1.0;
    context.values[1] = 0.8;
    context.values[2] = 0.9;
    context.values[4] = 0.5;
    context.workload = "ACTIVE_INTERACTION";
    context.targetPid = 20;
    context.targetCriticality = 10;
    context.targetSafety = 95;
    return context;
}

void VerifyLearningAndUncertainty() {
    ContextualImpactModel model;
    const auto before = model.Predict(ResourceActionType::EnableEcoQos, Context());
    for (int i = 0; i < 80; ++i) {
        Require(
            model.Update(ResourceActionType::EnableEcoQos, Context(), 0.8),
            "model update rejected measured reward"
        );
    }

    const auto after = model.Predict(ResourceActionType::EnableEcoQos, Context());
    Require(after.expectedReward > 0.5, "positive action value was not learned");
    Require(after.uncertainty < before.uncertainty, "uncertainty did not fall with evidence");
    Require(after.observations == 80, "observation count is wrong");
}

void VerifyShadowSafety() {
    ContextualImpactModel model;
    for (int i = 0; i < 80; ++i) {
        model.Update(ResourceActionType::EnableEcoQos, Context(), 0.8);
    }

    ShadowContextualPolicy policy;
    ImpactCandidate candidate;
    candidate.action = ResourceActionType::EnableEcoQos;
    candidate.reversible = true;
    candidate.deterministicSafetyPassed = false;

    const auto blocked = policy.Select(Context(), {candidate}, model);
    Require(!blocked.wouldAct, "unsafe shadow candidate was selected");

    candidate.deterministicSafetyPassed = true;
    candidate.targetPid = 20;
    candidate.targetName = "test.exe";
    const auto selected = policy.Select(Context(), {candidate}, model, -0.1, 20);
    Require(selected.wouldAct, "well-supported positive action was not selected in shadow mode");
    Require(selected.mode == "SHADOW_ONLY", "shadow policy changed execution mode");
}

void VerifyOfflineEvaluation() {
    vector<LoggedPolicyOutcome> records;
    for (int i = 0; i < 60; ++i) {
        records.push_back({
            ResourceActionType::EnableEcoQos,
            ResourceActionType::EnableEcoQos,
            0.5,
            0.0,
            1.0,
            true,
            true
        });
    }

    OfflinePolicyEvaluator evaluator;
    auto result = evaluator.Evaluate(records);
    Require(result.sufficientEvidence && result.promotionEligible, "positive offline policy did not pass");

    records.resize(5);
    result = evaluator.Evaluate(records);
    Require(
        !result.sufficientEvidence && !result.promotionEligible,
        "small offline sample was promoted"
    );
}

void VerifyStateRoundTripAndConservativeRejection() {
    ContextualImpactModel original;
    for (int i = 0; i < 30; ++i) original.Update(ResourceActionType::EnableEcoQos, Context(), 0.6);
    const auto before = original.Predict(ResourceActionType::EnableEcoQos, Context());
    ContextualImpactModel restored;
    Require(restored.ImportState(original.ExportState()), "valid model state did not restore");
    const auto after = restored.Predict(ResourceActionType::EnableEcoQos, Context());
    Require(abs(before.expectedReward - after.expectedReward) < 1e-9, "restored model changed prediction");
    Require(before.observations == after.observations, "restored observation count changed");

    vector<LoggedPolicyOutcome> poorOverlap;
    for (int i = 0; i < 30; ++i) poorOverlap.push_back({ResourceActionType::EnableEcoQos, ResourceActionType::EnableEcoQos, 0.8, 0.0, i == 0 ? 0.01 : 1.0, true, true});
    const auto rejected = OfflinePolicyEvaluator().Evaluate(poorOverlap);
    Require(!rejected.promotionEligible, "poor propensity overlap was promoted");
    for (auto& entry : poorOverlap) { entry.loggingPropensity = 0.5; entry.causalSupport = false; }
    const auto causalRejected = OfflinePolicyEvaluator().Evaluate(poorOverlap);
    Require(!causalRejected.promotionEligible, "missing causal evidence was promoted");
}
void VerifyPromotionJournal() {
    const filesystem::path database = filesystem::temp_directory_path() /
        ("impact_promotion_" + to_string(GetCurrentProcessId()) + ".db");
    filesystem::remove(database);

    LearningJournal journal;
    string error;
    Require(journal.Open(database.string(), error), "promotion journal failed to open: " + error);
    Require(
        journal.RegisterPolicyVersion("test-policy", "test-contract", false, error),
        "policy registration failed"
    );

    bool promoted = true;
    Require(
        journal.IsPolicyPromoted("test-policy", promoted, error) && !promoted,
        "new policy must not start promoted"
    );

    OfflineEvaluationResult evaluation;
    evaluation.totalSamples = 100;
    evaluation.matchedSamples = 80;
    evaluation.estimatedPolicyReward = 0.4;
    evaluation.baselineReward = 0.0;
    evaluation.lowerConfidenceBenefit = 0.2;
    evaluation.sufficientEvidence = true;
    evaluation.promotionEligible = true;
    evaluation.reason = "test evidence passed";
    Require(
        journal.RecordOfflineEvaluation("test-policy", evaluation, error),
        "offline evidence was not recorded"
    );
    Require(
        journal.IsPolicyPromoted("test-policy", promoted, error) && promoted,
        "eligible evidence did not promote policy"
    );
    Require(
        journal.RegisterPolicyVersion("test-policy", "test-contract", false, error),
        "policy refresh failed"
    );
    Require(
        journal.IsPolicyPromoted("test-policy", promoted, error) && promoted,
        "startup registration demoted policy"
    );

    ContextualImpactModel original;
    for (int i = 0; i < 25; ++i) original.Update(ResourceActionType::EnableEcoQos, Context(), 0.5);
    Require(journal.SaveImpactModelState("test-impact-model", original.ExportState(), error), "impact state did not save: " + error);
    ImpactModelState restoredState;
    Require(journal.LoadImpactModelState("test-impact-model", restoredState, error), "impact state did not load: " + error);
    ContextualImpactModel restored;
    Require(restored.ImportState(restoredState), "loaded impact state was invalid");
    Require(restored.Predict(ResourceActionType::EnableEcoQos, Context()).observations == 25, "journal state lost observations");
    journal.Close();
    filesystem::remove(database);
}

void VerifyPrivateUpdate() {
    PrivacyPreservingUpdateBuilder builder;
    const auto update = builder.Build(
        {10.0, 10.0, 10.0},
        1.0,
        0.0,
        "chrome.exe",
        "device-local-salt"
    );
    Require(update.clippedNorm <= 1.000001, "federated delta exceeded clip limit");
    Require(!update.containsRawProcessIdentity, "federated update exposed raw identity");
    Require(
        update.categoryToken != "chrome.exe" && !update.categoryToken.empty(),
        "category was not tokenized"
    );
}

}  // namespace

int main() {
    try {
        VerifyLearningAndUncertainty();
        VerifyShadowSafety();
        VerifyOfflineEvaluation();
        VerifyStateRoundTripAndConservativeRejection();
        VerifyPromotionJournal();
        VerifyPrivateUpdate();
        cout << "ImpactLearningTests passed\n";
        return 0;
    } catch (const exception& ex) {
        cerr << "ImpactLearningTests failed: " << ex.what() << '\n';
        return 1;
    }
}
