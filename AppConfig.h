#pragma once

#include <map>
#include <string>

class AppConfig {
public:
    bool LoadFromFile(const std::string& path);

    int GetInt(const std::string& key, int defaultValue) const;
    double GetDouble(const std::string& key, double defaultValue) const;
    std::string GetString(const std::string& key, const std::string& defaultValue = "") const;

private:
    static std::string Trim(const std::string& value);

    std::map<std::string, std::string> values_;
};
