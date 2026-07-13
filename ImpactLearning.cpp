#include "ImpactLearning.h"

#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <climits>
#include <numeric>
#include <sstream>

#include "sqlite3.h"

using namespace std;

namespace {

long long NowMs() {
    return chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
}

double Clamp(double value, double low, double high) { return max(low, min(high, value)); }

int ActionKey(ResourceActionType action) { return static_cast<int>(action); }

void BindText(sqlite3_stmt* statement, int index, const string& value) {
    sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

string SerializeFeatures(const vector<double>& values) {
    ostringstream stream;
    stream << fixed << setprecision(6);
    for (size_t index = 0; index < values.size(); ++index) {
        if (index) stream << ',';
        stream << values[index];
    }
    return stream.str();
}


bool ParseFeatures(const string& text, vector<double>& values) {
    values.clear();
    istringstream stream(text);
    string token;
    while (getline(stream, token, ',')) {
        try { values.push_back(stod(token)); }
        catch (...) { return false; }
    }
    return !values.empty();
}
string SerializeMatrix(const vector<vector<double>>& matrix) {
    ostringstream stream;
    for (size_t row = 0; row < matrix.size(); ++row) {
        if (row) stream << '|';
        stream << SerializeFeatures(matrix[row]);
    }
    return stream.str();
}

bool ParseMatrix(const string& text, vector<vector<double>>& matrix) {
    matrix.clear();
    istringstream stream(text);
    string row;
    while (getline(stream, row, '|')) {
        vector<double> values;
        if (!ParseFeatures(row, values)) return false;
        matrix.push_back(move(values));
    }
    return !matrix.empty();
}
}  // namespace

WorkloadContextFeatures WorkloadContextEncoder::Encode(
    const SystemSnapshot& system,
    const QoeTelemetrySample& qoe,
    WorkloadPhase workload,
    DWORD targetPid,
    double targetCriticality,
    double targetSafety
) const {
    WorkloadContextFeatures result;
    result.workload = ToString(workload);
    result.targetPid = targetPid;
    result.targetCriticality = Clamp(targetCriticality, 0.0, 100.0);
    result.targetSafety = Clamp(targetSafety, 0.0, 100.0);
    result.values.assign(Dimension(), 0.0);
    result.values[0] = 1.0;
    result.values[1] = Clamp(system.cpuUsage / 100.0, 0.0, 1.0);
    result.values[2] = Clamp(system.memoryUsage / 100.0, 0.0, 1.0);
    result.values[3] = Clamp((10.0 - system.diskFree) / 10.0, 0.0, 1.0);
    result.values[4] = Clamp(qoe.inputResponseMs / 100.0, 0.0, 2.0);
    result.values[5] = Clamp(qoe.systemPageReadsPerSecond / 500.0, 0.0, 2.0);
    result.values[6] = Clamp(qoe.diskQueueLength / 5.0, 0.0, 2.0);
    result.values[7] = Clamp(qoe.droppedFramesPerSecond / 10.0, 0.0, 2.0);
    result.values[8] = Clamp(result.targetCriticality / 100.0, 0.0, 1.0);
    result.values[9] = Clamp(result.targetSafety / 100.0, 0.0, 1.0);
    result.values[10] = Clamp(system.totalMemoryMB / 8192.0, 0.0, 2.0);
    result.values[11] = Clamp((system.netDownKBps + system.netUpKBps) / 4096.0, 0.0, 2.0);
    const int workloadIndex = static_cast<int>(workload);
    if (workloadIndex >= 1 && workloadIndex <= 10) result.values[11 + workloadIndex] = 1.0;
    return result;
}

ContextualImpactModel::ContextualImpactModel(double ridge, double explorationScale)
    : ridge_(max(0.001, ridge)), explorationScale_(max(0.0, explorationScale)) {}

ContextualImpactModel::ActionModel& ContextualImpactModel::EnsureModel(ResourceActionType action) {
    const int dimension = WorkloadContextEncoder::Dimension();
    auto [found, inserted] = models_.try_emplace(ActionKey(action));
    if (inserted) {
        found->second.covariance.assign(dimension, vector<double>(dimension, 0.0));
        found->second.rewardVector.assign(dimension, 0.0);
        for (int index = 0; index < dimension; ++index) found->second.covariance[index][index] = ridge_;
    }
    return found->second;
}

double ContextualImpactModel::Dot(const vector<double>& left, const vector<double>& right) {
    double value = 0.0;
    for (size_t index = 0; index < min(left.size(), right.size()); ++index) value += left[index] * right[index];
    return value;
}

vector<double> ContextualImpactModel::Multiply(const vector<vector<double>>& matrix, const vector<double>& input) {
    vector<double> result(matrix.size(), 0.0);
    for (size_t row = 0; row < matrix.size(); ++row) result[row] = Dot(matrix[row], input);
    return result;
}

bool ContextualImpactModel::Invert(const vector<vector<double>>& matrix, vector<vector<double>>& inverse) {
    const size_t size = matrix.size();
    if (size == 0) return false;
    vector<vector<double>> augmented(size, vector<double>(size * 2, 0.0));
    for (size_t row = 0; row < size; ++row) {
        if (matrix[row].size() != size) return false;
        for (size_t column = 0; column < size; ++column) augmented[row][column] = matrix[row][column];
        augmented[row][size + row] = 1.0;
    }
    for (size_t pivot = 0; pivot < size; ++pivot) {
        size_t best = pivot;
        for (size_t row = pivot + 1; row < size; ++row) {
            if (abs(augmented[row][pivot]) > abs(augmented[best][pivot])) best = row;
        }
        if (abs(augmented[best][pivot]) < 1e-12) return false;
        swap(augmented[pivot], augmented[best]);
        const double divisor = augmented[pivot][pivot];
        for (double& value : augmented[pivot]) value /= divisor;
        for (size_t row = 0; row < size; ++row) {
            if (row == pivot) continue;
            const double factor = augmented[row][pivot];
            for (size_t column = 0; column < size * 2; ++column) {
                augmented[row][column] -= factor * augmented[pivot][column];
            }
        }
    }
    inverse.assign(size, vector<double>(size, 0.0));
    for (size_t row = 0; row < size; ++row) {
        for (size_t column = 0; column < size; ++column) inverse[row][column] = augmented[row][size + column];
    }
    return true;
}

ImpactPrediction ContextualImpactModel::Predict(ResourceActionType action, const WorkloadContextFeatures& context) const {
    ImpactPrediction prediction;
    prediction.action = action;
    if (context.values.size() != WorkloadContextEncoder::Dimension()) return prediction;
    lock_guard lock(mutex_);
    auto* self = const_cast<ContextualImpactModel*>(this);
    const ActionModel& model = self->EnsureModel(action);
    vector<vector<double>> inverse;
    if (!Invert(model.covariance, inverse)) return prediction;
    const vector<double> weights = Multiply(inverse, model.rewardVector);
    const vector<double> projected = Multiply(inverse, context.values);
    prediction.expectedReward = Dot(weights, context.values);
    prediction.uncertainty = sqrt(max(0.0, Dot(context.values, projected)));
    prediction.lowerConfidenceBound = prediction.expectedReward - explorationScale_ * prediction.uncertainty;
    prediction.observations = model.observations;
    prediction.sufficientlyKnown = model.observations >= 20 && prediction.uncertainty <= 0.75;
    return prediction;
}

bool ContextualImpactModel::Update(ResourceActionType action, const WorkloadContextFeatures& context, double measuredReward) {
    if (action == ResourceActionType::None || context.values.size() != WorkloadContextEncoder::Dimension() || !isfinite(measuredReward)) return false;
    const double reward = Clamp(measuredReward, -2.0, 2.0);
    lock_guard lock(mutex_);
    ActionModel& model = EnsureModel(action);
    for (size_t row = 0; row < context.values.size(); ++row) {
        model.rewardVector[row] += reward * context.values[row];
        for (size_t column = 0; column < context.values.size(); ++column) {
            model.covariance[row][column] += context.values[row] * context.values[column];
        }
    }
    ++model.observations;
    return true;
}

ImpactModelState ContextualImpactModel::ExportState() const {
    lock_guard lock(mutex_);
    ImpactModelState state;
    state.ridge = ridge_;
    state.explorationScale = explorationScale_;
    for (const auto& [key, model] : models_) {
        state.entries.push_back({static_cast<ResourceActionType>(key), model.covariance, model.rewardVector, model.observations});
    }
    return state;
}

bool ContextualImpactModel::ImportState(const ImpactModelState& state) {
    const int dimension = WorkloadContextEncoder::Dimension();
    if (!isfinite(state.ridge) || !isfinite(state.explorationScale) || state.ridge <= 0.0 || state.explorationScale < 0.0) return false;
    unordered_map<int, ActionModel> restored;
    for (const ImpactModelStateEntry& entry : state.entries) {
        if (entry.action == ResourceActionType::None || entry.observations < 0 || entry.rewardVector.size() != dimension || entry.covariance.size() != dimension) return false;
        for (int row = 0; row < dimension; ++row) {
            if (entry.covariance[row].size() != dimension || !isfinite(entry.rewardVector[row])) return false;
            for (int column = 0; column < dimension; ++column) {
                if (!isfinite(entry.covariance[row][column]) || abs(entry.covariance[row][column] - entry.covariance[column][row]) > 1e-8) return false;
            }
        }
        restored.emplace(ActionKey(entry.action), ActionModel{entry.covariance, entry.rewardVector, entry.observations});
    }
    lock_guard lock(mutex_);
    ridge_ = state.ridge;
    explorationScale_ = state.explorationScale;
    models_ = move(restored);
    return true;
}
ShadowPolicyDecision ShadowContextualPolicy::Select(
    const WorkloadContextFeatures& context,
    const vector<ImpactCandidate>& candidates,
    const ContextualImpactModel& model,
    double requiredLowerBound,
    int minimumObservations
) const {
    ShadowPolicyDecision decision;
    decision.timestampMs = NowMs();
    ImpactPrediction best;
    bool found = false;
    for (const ImpactCandidate& candidate : candidates) {
        if (candidate.action == ResourceActionType::None || !candidate.reversible || !candidate.deterministicSafetyPassed) continue;
        ImpactPrediction prediction = model.Predict(candidate.action, context);
        if (!found || prediction.lowerConfidenceBound > best.lowerConfidenceBound) {
            best = prediction;
            decision.selectedAction = candidate.action;
            decision.targetPid = candidate.targetPid;
            decision.targetName = candidate.targetName;
            found = true;
        }
    }
    if (!found) {
        decision.reason = "no reversible candidate passed deterministic safety";
        return decision;
    }
    decision.expectedReward = best.expectedReward;
    decision.uncertainty = best.uncertainty;
    decision.lowerConfidenceBound = best.lowerConfidenceBound;
    if (best.observations < minimumObservations) {
        decision.reason = "insufficient measured outcomes; remain on no-intervention baseline";
    } else if (best.lowerConfidenceBound <= requiredLowerBound) {
        decision.reason = "lower-confidence benefit does not beat no intervention";
    } else {
        decision.wouldAct = true;
        decision.reason = "shadow candidate beats baseline with required lower-confidence benefit";
    }
    return decision;
}

OfflineEvaluationResult OfflinePolicyEvaluator::Evaluate(
    const vector<LoggedPolicyOutcome>& outcomes,
    int minimumMatchedSamples,
    double confidenceZ
) const {
    OfflineEvaluationResult result;
    result.totalSamples = static_cast<int>(outcomes.size());
    vector<double> weightedRewards;
    vector<double> baselineRewards;
    double sumWeights = 0.0;
    double sumSquaredWeights = 0.0;
    int causalObserved = 0;
    bool invalidPropensity = false;
    for (const LoggedPolicyOutcome& outcome : outcomes) {
        if (!isfinite(outcome.loggingPropensity) || outcome.loggingPropensity <= 0.0 || outcome.loggingPropensity > 1.0) {
            invalidPropensity = true;
            continue;
        }
        baselineRewards.push_back(outcome.baselineReward);
        if (outcome.loggedAction != outcome.candidatePolicyAction || !outcome.observedOutcome) continue;
        const double weight = 1.0 / outcome.loggingPropensity;
        weightedRewards.push_back(Clamp(outcome.reward * weight, -10.0, 10.0));
        sumWeights += weight;
        sumSquaredWeights += weight * weight;
        if (outcome.causalSupport) ++causalObserved;
    }
    result.matchedSamples = static_cast<int>(weightedRewards.size());
    if (!baselineRewards.empty()) result.baselineReward = accumulate(baselineRewards.begin(), baselineRewards.end(), 0.0) / baselineRewards.size();
    if (weightedRewards.empty()) { result.reason = invalidPropensity ? "invalid_logging_propensity" : "no_observed_actions_matched_candidate"; return result; }
    result.estimatedPolicyReward = accumulate(weightedRewards.begin(), weightedRewards.end(), 0.0) / weightedRewards.size();
    double variance = 0.0;
    for (double reward : weightedRewards) variance += pow(reward - result.estimatedPolicyReward, 2.0);
    if (weightedRewards.size() > 1) variance /= static_cast<double>(weightedRewards.size() - 1);
    result.standardError = sqrt(variance / weightedRewards.size());
    result.lowerConfidenceBenefit = (result.estimatedPolicyReward - result.baselineReward) - confidenceZ * result.standardError;
    result.effectiveSampleSize = sumSquaredWeights > 0.0 ? (sumWeights * sumWeights) / sumSquaredWeights : 0.0;
    result.overlapAdequate = !invalidPropensity && result.effectiveSampleSize >= minimumMatchedSamples;
    result.causalEvidenceAdequate = causalObserved == result.matchedSamples;
    result.sufficientEvidence = result.matchedSamples >= minimumMatchedSamples && result.overlapAdequate && result.causalEvidenceAdequate;
    result.promotionEligible = result.sufficientEvidence && result.lowerConfidenceBenefit > 0.0;
    result.reason = invalidPropensity ? "invalid_logging_propensity" :
        !result.overlapAdequate ? "insufficient_effective_sample_size_or_overlap" :
        !result.causalEvidenceAdequate ? "causal_checksum_missing_for_observed_outcomes" :
        !result.sufficientEvidence ? "insufficient_matched_observed_samples" :
        result.promotionEligible ? "causally_supported_policy_beats_baseline_at_requested_confidence" :
        "candidate_policy_lower_bound_does_not_beat_baseline";
    return result;
}
string PrivacyPreservingUpdateBuilder::Sha256Token(const string& value) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectSize = 0, hashSize = 0, bytes = 0;
    vector<unsigned char> object;
    vector<unsigned char> digest;
    string result;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return "hash_unavailable";
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &bytes, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashSize), sizeof(hashSize), &bytes, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0); return "hash_unavailable";
    }
    object.resize(objectSize); digest.resize(hashSize);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) >= 0 &&
        BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())), static_cast<ULONG>(value.size()), 0) >= 0 &&
        BCryptFinishHash(hash, digest.data(), hashSize, 0) >= 0) {
        ostringstream stream; stream << hex << setfill('0');
        for (size_t index = 0; index < min<size_t>(16, digest.size()); ++index) stream << setw(2) << static_cast<int>(digest[index]);
        result = stream.str();
    }
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result.empty() ? "hash_unavailable" : result;
}

double PrivacyPreservingUpdateBuilder::SecureNormal() {
    unsigned long long randomValues[2]{};
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(randomValues), sizeof(randomValues), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return 0.0;
    const double u1 = max(1e-12, static_cast<double>(randomValues[0]) / static_cast<double>(ULLONG_MAX));
    const double u2 = static_cast<double>(randomValues[1]) / static_cast<double>(ULLONG_MAX);
    return sqrt(-2.0 * log(u1)) * cos(2.0 * 3.14159265358979323846 * u2);
}

PrivateFederatedUpdate PrivacyPreservingUpdateBuilder::Build(
    const vector<double>& localDelta,
    double clipLimit,
    double noiseStdDev,
    const string& localCategory,
    const string& deviceSalt
) const {
    PrivateFederatedUpdate result;
    result.clipLimit = max(0.001, clipLimit);
    result.noiseStdDev = max(0.0, noiseStdDev);
    for (double value : localDelta) result.originalNorm += value * value;
    result.originalNorm = sqrt(result.originalNorm);
    const double scale = result.originalNorm > result.clipLimit ? result.clipLimit / result.originalNorm : 1.0;
    result.protectedDelta.reserve(localDelta.size());
    for (double value : localDelta) result.protectedDelta.push_back(value * scale + result.noiseStdDev * SecureNormal());
    for (double value : result.protectedDelta) result.clippedNorm += value * value;
    result.clippedNorm = sqrt(result.clippedNorm);
    result.categoryToken = Sha256Token(deviceSalt + "|" + localCategory);
    result.containsRawProcessIdentity = false;
    return result;
}

LearningJournal::~LearningJournal() {
    Close();
}

bool LearningJournal::Open(const string& path, string& error) {
    lock_guard lock(mutex_);
    if (db_) {
        return true;
    }

    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(path.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
        error = db_ ? sqlite3_errmsg(db_) : "sqlite open failed";
        if (db_) {
            sqlite3_close(db_);
        }
        db_ = nullptr;
        return false;
    }

    sqlite3_busy_timeout(db_, 5000);
    if (!EnsureSchema(error)) {
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    return true;
}

void LearningJournal::Close() {
    lock_guard lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool LearningJournal::EnsureSchema(string& error) {
    const char* statements[] = {
        "CREATE TABLE IF NOT EXISTS policy_versions("
        "version TEXT PRIMARY KEY, feature_contract TEXT NOT NULL, promoted INTEGER NOT NULL, "
        "created_at_ms INTEGER NOT NULL);",

        "CREATE TABLE IF NOT EXISTS offline_policy_evaluations("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, version TEXT NOT NULL, "
        "evaluated_at_ms INTEGER NOT NULL, total_samples INTEGER NOT NULL, "
        "matched_samples INTEGER NOT NULL, estimated_reward REAL NOT NULL, "
        "baseline_reward REAL NOT NULL, standard_error REAL NOT NULL, "
        "lower_confidence_benefit REAL NOT NULL, sufficient_evidence INTEGER NOT NULL, "
        "promotion_eligible INTEGER NOT NULL, reason TEXT NOT NULL);",

        "CREATE TABLE IF NOT EXISTS shadow_policy_decisions("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp_ms INTEGER NOT NULL, "
        "policy_version TEXT NOT NULL, mode TEXT NOT NULL, action TEXT NOT NULL, "
        "target_pid INTEGER NOT NULL, target_name TEXT NOT NULL, expected_reward REAL NOT NULL, "
        "uncertainty REAL NOT NULL, lower_bound REAL NOT NULL, baseline_reward REAL NOT NULL, "
        "would_act INTEGER NOT NULL, reason TEXT NOT NULL, workload TEXT NOT NULL, "
        "features TEXT NOT NULL);",

        "CREATE TABLE IF NOT EXISTS action_rewards("
        "transaction_id TEXT PRIMARY KEY, action TEXT NOT NULL, status TEXT NOT NULL, "
        "reward REAL NOT NULL, confidence REAL NOT NULL, measured INTEGER NOT NULL, "
        "reason TEXT NOT NULL, measured_at_ms INTEGER NOT NULL);",

        "CREATE TABLE IF NOT EXISTS federated_update_audits("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, created_at_ms INTEGER NOT NULL, "
        "original_norm REAL NOT NULL, clipped_norm REAL NOT NULL, clip_limit REAL NOT NULL, "
        "noise_stddev REAL NOT NULL, category_token TEXT NOT NULL, "
        "contains_raw_identity INTEGER NOT NULL);",

        "CREATE TABLE IF NOT EXISTS impact_model_state_entries("
        "model_id TEXT NOT NULL, action INTEGER NOT NULL, ridge REAL NOT NULL, exploration_scale REAL NOT NULL, "
        "observations INTEGER NOT NULL, covariance TEXT NOT NULL, reward_vector TEXT NOT NULL, "
        "saved_at_ms INTEGER NOT NULL, PRIMARY KEY(model_id, action));"
    };

    for (const char* statement : statements) {
        char* message = nullptr;
        if (sqlite3_exec(db_, statement, nullptr, nullptr, &message) != SQLITE_OK) {
            error = message ? message : sqlite3_errmsg(db_);
            if (message) {
                sqlite3_free(message);
            }
            return false;
        }
    }
    return true;
}

bool LearningJournal::RegisterPolicyVersion(
    const string& version,
    const string& featureContract,
    bool promoted,
    string& error
) {
    lock_guard lock(mutex_);
    const char* sql =
        "INSERT INTO policy_versions VALUES(?,?,?,?) "
        "ON CONFLICT(version) DO UPDATE SET "
        "feature_contract=excluded.feature_contract, "
        "promoted=MAX(policy_versions.promoted,excluded.promoted);";
    sqlite3_stmt* statement = nullptr;
    if (!db_ || sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = db_ ? sqlite3_errmsg(db_) : "journal not open";
        return false;
    }

    BindText(statement, 1, version);
    BindText(statement, 2, featureContract);
    sqlite3_bind_int(statement, 3, promoted ? 1 : 0);
    sqlite3_bind_int64(statement, 4, NowMs());

    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    if (!ok) {
        error = sqlite3_errmsg(db_);
    }
    sqlite3_finalize(statement);
    return ok;
}

bool LearningJournal::RecordOfflineEvaluation(
    const string& version,
    const OfflineEvaluationResult& evaluation,
    string& error
) {
    lock_guard lock(mutex_);
    if (!db_) {
        error = "journal not open";
        return false;
    }
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db_);
        return false;
    }

    const char* sql =
        "INSERT INTO offline_policy_evaluations VALUES(NULL,?,?,?,?,?,?,?,?,?,?,?);";
    sqlite3_stmt* statement = nullptr;
    bool ok = sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) == SQLITE_OK;
    if (ok) {
        BindText(statement, 1, version);
        sqlite3_bind_int64(statement, 2, NowMs());
        sqlite3_bind_int(statement, 3, evaluation.totalSamples);
        sqlite3_bind_int(statement, 4, evaluation.matchedSamples);
        sqlite3_bind_double(statement, 5, evaluation.estimatedPolicyReward);
        sqlite3_bind_double(statement, 6, evaluation.baselineReward);
        sqlite3_bind_double(statement, 7, evaluation.standardError);
        sqlite3_bind_double(statement, 8, evaluation.lowerConfidenceBenefit);
        sqlite3_bind_int(statement, 9, evaluation.sufficientEvidence ? 1 : 0);
        sqlite3_bind_int(statement, 10, evaluation.promotionEligible ? 1 : 0);
        BindText(statement, 11, evaluation.reason);
        ok = sqlite3_step(statement) == SQLITE_DONE;
    }
    if (statement) {
        sqlite3_finalize(statement);
    }

    if (ok && evaluation.promotionEligible && evaluation.sufficientEvidence) {
        const char* updateSql =
            "UPDATE policy_versions SET promoted=1 WHERE version=?;";
        sqlite3_stmt* update = nullptr;
        ok = sqlite3_prepare_v2(db_, updateSql, -1, &update, nullptr) == SQLITE_OK;
        if (ok) {
            BindText(update, 1, version);
            ok = sqlite3_step(update) == SQLITE_DONE && sqlite3_changes(db_) == 1;
        }
        if (update) {
            sqlite3_finalize(update);
        }
    }

    if (ok) {
        ok = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
    }
    if (!ok) {
        error = sqlite3_errmsg(db_);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    return ok;
}

bool LearningJournal::IsPolicyPromoted(
    const string& version,
    bool& promoted,
    string& error
) {
    lock_guard lock(mutex_);
    promoted = false;
    if (!db_) {
        error = "journal not open";
        return false;
    }

    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT promoted FROM policy_versions WHERE version=?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db_);
        return false;
    }

    BindText(statement, 1, version);
    const int step = sqlite3_step(statement);
    const bool found = step == SQLITE_ROW;
    if (found) {
        promoted = sqlite3_column_int(statement, 0) != 0;
    } else if (step != SQLITE_DONE) {
        error = sqlite3_errmsg(db_);
    }
    sqlite3_finalize(statement);
    return found;
}

bool LearningJournal::SaveShadowDecision(
    const ShadowPolicyDecision& decision,
    const WorkloadContextFeatures& context,
    string& error
) {
    lock_guard lock(mutex_);
    const char* sql =
        "INSERT INTO shadow_policy_decisions VALUES(NULL,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
    sqlite3_stmt* statement = nullptr;
    if (!db_ || sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = db_ ? sqlite3_errmsg(db_) : "journal not open";
        return false;
    }

    sqlite3_bind_int64(statement, 1, decision.timestampMs);
    BindText(statement, 2, decision.policyVersion);
    BindText(statement, 3, decision.mode);
    BindText(statement, 4, ToString(decision.selectedAction));
    sqlite3_bind_int64(statement, 5, decision.targetPid);
    BindText(statement, 6, decision.targetName);
    sqlite3_bind_double(statement, 7, decision.expectedReward);
    sqlite3_bind_double(statement, 8, decision.uncertainty);
    sqlite3_bind_double(statement, 9, decision.lowerConfidenceBound);
    sqlite3_bind_double(statement, 10, decision.baselineReward);
    sqlite3_bind_int(statement, 11, decision.wouldAct ? 1 : 0);
    BindText(statement, 12, decision.reason);
    BindText(statement, 13, context.workload);
    BindText(statement, 14, SerializeFeatures(context.values));

    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    if (!ok) {
        error = sqlite3_errmsg(db_);
    }
    sqlite3_finalize(statement);
    return ok;
}

bool LearningJournal::SaveReward(
    const string& transactionId,
    ResourceActionType action,
    const ActionImpactResult& outcome,
    string& error
) {
    lock_guard lock(mutex_);
    const char* sql =
        "INSERT OR REPLACE INTO action_rewards VALUES(?,?,?,?,?,?,?,?);";
    sqlite3_stmt* statement = nullptr;
    if (!db_ || sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = db_ ? sqlite3_errmsg(db_) : "journal not open";
        return false;
    }

    BindText(statement, 1, transactionId);
    BindText(statement, 2, ToString(action));
    BindText(statement, 3, ToString(outcome.status));
    sqlite3_bind_double(statement, 4, outcome.reward);
    sqlite3_bind_double(statement, 5, outcome.confidence);
    sqlite3_bind_int(statement, 6, outcome.measured ? 1 : 0);
    BindText(statement, 7, outcome.reason);
    sqlite3_bind_int64(statement, 8, outcome.measuredAtMs);

    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    if (!ok) {
        error = sqlite3_errmsg(db_);
    }
    sqlite3_finalize(statement);
    return ok;
}

bool LearningJournal::SaveFederatedAudit(
    const PrivateFederatedUpdate& update,
    string& error
) {
    lock_guard lock(mutex_);
    const char* sql =
        "INSERT INTO federated_update_audits VALUES(NULL,?,?,?,?,?,?,?);";
    sqlite3_stmt* statement = nullptr;
    if (!db_ || sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = db_ ? sqlite3_errmsg(db_) : "journal not open";
        return false;
    }

    sqlite3_bind_int64(statement, 1, NowMs());
    sqlite3_bind_double(statement, 2, update.originalNorm);
    sqlite3_bind_double(statement, 3, update.clippedNorm);
    sqlite3_bind_double(statement, 4, update.clipLimit);
    sqlite3_bind_double(statement, 5, update.noiseStdDev);
    BindText(statement, 6, update.categoryToken);
    sqlite3_bind_int(statement, 7, update.containsRawProcessIdentity ? 1 : 0);

    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    if (!ok) {
        error = sqlite3_errmsg(db_);
    }
    sqlite3_finalize(statement);
    return ok;
}
bool LearningJournal::SaveImpactModelState(const string& modelId, const ImpactModelState& state, string& error) {
    if (modelId.empty()) { error = "model id is required"; return false; }
    ContextualImpactModel validator;
    if (!validator.ImportState(state)) { error = "invalid model state"; return false; }
    lock_guard lock(mutex_);
    if (!db_) { error = "journal not open"; return false; }
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) { error = sqlite3_errmsg(db_); return false; }
    bool ok = true;
    sqlite3_stmt* clear = nullptr;
    ok = sqlite3_prepare_v2(db_, "DELETE FROM impact_model_state_entries WHERE model_id=?;", -1, &clear, nullptr) == SQLITE_OK;
    if (ok) { BindText(clear, 1, modelId); ok = sqlite3_step(clear) == SQLITE_DONE; }
    if (clear) sqlite3_finalize(clear);
    const char* sql = "INSERT INTO impact_model_state_entries VALUES(?,?,?,?,?,?,?,?);";
    for (const ImpactModelStateEntry& entry : state.entries) {
        sqlite3_stmt* statement = nullptr;
        ok = ok && sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) == SQLITE_OK;
        if (ok) {
            BindText(statement, 1, modelId);
            sqlite3_bind_int(statement, 2, ActionKey(entry.action));
            sqlite3_bind_double(statement, 3, state.ridge);
            sqlite3_bind_double(statement, 4, state.explorationScale);
            sqlite3_bind_int(statement, 5, entry.observations);
            BindText(statement, 6, SerializeMatrix(entry.covariance));
            BindText(statement, 7, SerializeFeatures(entry.rewardVector));
            sqlite3_bind_int64(statement, 8, NowMs());
            ok = sqlite3_step(statement) == SQLITE_DONE;
        }
        if (statement) sqlite3_finalize(statement);
        if (!ok) break;
    }
    if (ok) ok = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
    if (!ok) { error = sqlite3_errmsg(db_); sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr); }
    return ok;
}

bool LearningJournal::LoadImpactModelState(const string& modelId, ImpactModelState& state, string& error) {
    state = {};
    lock_guard lock(mutex_);
    if (!db_) { error = "journal not open"; return false; }
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT action,ridge,exploration_scale,observations,covariance,reward_vector FROM impact_model_state_entries WHERE model_id=? ORDER BY action;";
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) { error = sqlite3_errmsg(db_); return false; }
    BindText(statement, 1, modelId);
    bool found = false;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        ImpactModelStateEntry entry;
        entry.action = static_cast<ResourceActionType>(sqlite3_column_int(statement, 0));
        state.ridge = sqlite3_column_double(statement, 1);
        state.explorationScale = sqlite3_column_double(statement, 2);
        entry.observations = sqlite3_column_int(statement, 3);
        const auto* covariance = reinterpret_cast<const char*>(sqlite3_column_text(statement, 4));
        const auto* reward = reinterpret_cast<const char*>(sqlite3_column_text(statement, 5));
        if (!covariance || !reward || !ParseMatrix(covariance, entry.covariance) || !ParseFeatures(reward, entry.rewardVector)) { sqlite3_finalize(statement); error = "stored model state is malformed"; return false; }
        state.entries.push_back(move(entry));
        found = true;
    }
    if (sqlite3_errcode(db_) != SQLITE_DONE) { error = sqlite3_errmsg(db_); sqlite3_finalize(statement); return false; }
    sqlite3_finalize(statement);
    if (!found) { error = "model state not found"; return false; }
    ContextualImpactModel validator;
    if (!validator.ImportState(state)) { error = "stored model state failed validation"; return false; }
    return true;
}