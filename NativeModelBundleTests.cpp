#include "NativeModelBundle.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace { void Require(bool value, const char* text) { if (!value) throw std::runtime_error(text); } }

int main() {
    try {
        const auto root = std::filesystem::temp_directory_path() / "aegis_native_bundle_test";
        std::filesystem::create_directories(root);
        const auto model = root / "model.native";
        { std::ofstream stream(model); stream << "not a signed native model"; }
        const auto hash = NativeModelBundleValidator::Sha256File(model);
        const auto manifestPath = root / "model.manifest";
        {
            std::ofstream stream(manifestPath);
            stream << "model_id=test\nmodel_file=model.native\nmodel_sha256=" << hash
                   << "\nfeature_schema_hash=schema-v1\nfeature_count=22\nformat=native_linear_v1\n";
        }
        NativeModelBundlePolicy policy{"schema-v1", 22, 1024, true};
        const auto unsignedBundle = NativeModelBundleValidator().Validate(manifestPath, policy);
        Require(!unsignedBundle.accepted && unsignedBundle.reason == "authenticode_signature_rejected", "unsigned bundle accepted");
        policy.requireAuthenticodeSignature = false;
        Require(NativeModelBundleValidator().Validate(manifestPath, policy).accepted, "valid bounded test bundle rejected");
        { std::ofstream stream(model, std::ios::app); stream << "tamper"; }
        Require(!NativeModelBundleValidator().Validate(manifestPath, policy).accepted, "modified model accepted");
        std::filesystem::remove_all(root);
        std::cout << "NativeModelBundleTests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}