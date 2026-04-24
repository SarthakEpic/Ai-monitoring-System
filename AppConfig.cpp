#include "AppConfig.h"

#include <fstream>

using namespace std;

string AppConfig::Trim(const string& value) {
    size_t start = value.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

bool AppConfig::LoadFromFile(const string& path) {
    values_.clear();

    ifstream file(path);
    if (!file) return false;

    string line;
    while (getline(file, line)) {
        size_t pos = line.find('=');
        if (pos == string::npos) continue;

        string key = Trim(line.substr(0, pos));
        string value = Trim(line.substr(pos + 1));
        if (!key.empty()) {
            values_[key] = value;
        }
    }

    return true;
}

int AppConfig::GetInt(const string& key, int defaultValue) const {
    try {
        auto it = values_.find(key);
        if (it != values_.end() && !it->second.empty()) {
            return stoi(it->second);
        }
    } catch (...) {
    }
    return defaultValue;
}

double AppConfig::GetDouble(const string& key, double defaultValue) const {
    try {
        auto it = values_.find(key);
        if (it != values_.end() && !it->second.empty()) {
            return stod(it->second);
        }
    } catch (...) {
    }
    return defaultValue;
}

string AppConfig::GetString(const string& key, const string& defaultValue) const {
    auto it = values_.find(key);
    if (it != values_.end() && !it->second.empty()) {
        return it->second;
    }
    return defaultValue;
}
