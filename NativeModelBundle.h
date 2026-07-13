#pragma once

#include <filesystem>
#include <string>

struct NativeModelBundlePolicy {
    std::string expectedFeatureSchemaHash;
    int expectedFeatureCount = 0;
    std::uintmax_t maximumBytes = 32 * 1024 * 1024;
    bool requireAuthenticodeSignature = true;
};

struct NativeModelBundleResult {
    bool accepted = false;
    std::string modelId;
    std::string reason = "bundle_not_checked";
};

// Native production loading starts at this verifier. It never deserializes
// joblib/pickle and rejects unsigned or malformed bundles before inference.
class NativeModelBundleValidator {
public:
    NativeModelBundleResult Validate(const std::filesystem::path& manifestPath, const NativeModelBundlePolicy& policy) const;
    static std::string Sha256File(const std::filesystem::path& path);
    static bool VerifyAuthenticode(const std::filesystem::path& path);
};
