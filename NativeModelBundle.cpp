#include "NativeModelBundle.h"

#include <windows.h>
#include <bcrypt.h>
#include <wintrust.h>
#include <softpub.h>

#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <vector>

namespace {
std::map<std::string, std::string> ReadManifest(const std::filesystem::path& path) {
    std::ifstream stream(path); std::map<std::string, std::string> fields; std::string line;
    while (std::getline(stream, line)) { const auto equals=line.find('='); if (equals != std::string::npos) fields.emplace(line.substr(0, equals), line.substr(equals + 1)); }
    return fields;
}
}

std::string NativeModelBundleValidator::Sha256File(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary); if (!stream) return {};
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr; DWORD objectLength=0, hashLength=0, bytes=0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 || BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &bytes, 0) < 0 || BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &bytes, 0) < 0) { if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); return {}; }
    std::vector<unsigned char> object(objectLength), digest(hashLength), chunk(65536);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) < 0) { BCryptCloseAlgorithmProvider(algorithm, 0); return {}; }
    while (stream) { stream.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size())); const auto count=stream.gcount(); if (count > 0 && BCryptHashData(hash, chunk.data(), static_cast<ULONG>(count), 0) < 0) { BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0); return {}; } }
    std::string result; if (BCryptFinishHash(hash, digest.data(), hashLength, 0) >= 0) { std::ostringstream output; output << std::hex << std::setfill('0'); for (unsigned char value : digest) output << std::setw(2) << static_cast<int>(value); result=output.str(); }
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0); return result;
}

bool NativeModelBundleValidator::VerifyAuthenticode(const std::filesystem::path& path) {
    WINTRUST_FILE_INFO file{}; file.cbStruct=sizeof(file); const auto wide=path.wstring(); file.pcwszFilePath=wide.c_str();
    WINTRUST_DATA data{}; data.cbStruct=sizeof(data); data.dwUIChoice=WTD_UI_NONE; data.fdwRevocationChecks=WTD_REVOKE_WHOLECHAIN; data.dwUnionChoice=WTD_CHOICE_FILE; data.pFile=&file; data.dwStateAction=WTD_STATEACTION_VERIFY; data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2; const LONG status=WinVerifyTrust(nullptr, &action, &data); data.dwStateAction=WTD_STATEACTION_CLOSE; WinVerifyTrust(nullptr, &action, &data); return status == ERROR_SUCCESS;
}

NativeModelBundleResult NativeModelBundleValidator::Validate(const std::filesystem::path& manifestPath, const NativeModelBundlePolicy& policy) const {
    NativeModelBundleResult result; const auto fields=ReadManifest(manifestPath);
    const auto required = {"model_id", "model_file", "model_sha256", "feature_schema_hash", "feature_count", "format"};
    for (const char* name : required) if (!fields.contains(name) || fields.at(name).empty()) { result.reason="manifest_missing_required_field"; return result; }
    if (fields.at("format") != "native_linear_v1") { result.reason="unsupported_native_format"; return result; }
    const auto modelPath=manifestPath.parent_path() / fields.at("model_file");
    std::error_code error; const auto size=std::filesystem::file_size(modelPath, error);
    if (error || size == 0 || size > policy.maximumBytes) { result.reason="model_missing_or_size_rejected"; return result; }
    if (fields.at("model_sha256") != Sha256File(modelPath)) { result.reason="model_hash_mismatch"; return result; }
    if (fields.at("feature_schema_hash") != policy.expectedFeatureSchemaHash) { result.reason="feature_schema_mismatch"; return result; }
    try { if (std::stoi(fields.at("feature_count")) != policy.expectedFeatureCount) { result.reason="feature_dimension_mismatch"; return result; } } catch (...) { result.reason="invalid_feature_dimension"; return result; }
    if (policy.requireAuthenticodeSignature && !VerifyAuthenticode(modelPath)) { result.reason="authenticode_signature_rejected"; return result; }
    result.accepted=true; result.modelId=fields.at("model_id"); result.reason="native_bundle_verified"; return result;
}
