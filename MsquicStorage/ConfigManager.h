#pragma once

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <mutex>
#include <string>
#include <optional>
#include <fstream>

class ConfigManager {
public:

    enum class Format {
        Ini,
        Json,
        Xml
    };


    static ConfigManager& Instance() {
        static ConfigManager instance;
        return instance;
    }


    bool Load(const std::string& fileName, Format format = Format::Ini) {
        std::lock_guard<std::mutex> lock(mutex);
        try {
            std::ifstream file(fileName);
            if (!file.is_open()) {
                return false;
            }

            switch (format) {
            case Format::Ini:
                boost::property_tree::read_ini(file, tree);
                break;
            case Format::Json:
                boost::property_tree::read_json(file, tree);
                break;
            case Format::Xml:
                boost::property_tree::read_xml(file, tree);
                break;
            }
            this->fileName = fileName;
            currentFormat = format;
            return true;
        }
        catch (const std::exception&) {
            return false;
        }
    }


    bool Reload() {
        if (fileName.empty()) return false;
        return Load(fileName, currentFormat);
    }


    bool Save(const std::string& targetFile = "") {
        std::lock_guard<std::mutex> lock(mutex);
        try {
            std::string savePath = targetFile.empty() ? fileName : targetFile;
            if (savePath.empty()) return false;

            std::ofstream file(savePath);
            if (!file.is_open()) return false;

            switch (currentFormat) {
            case Format::Ini:
                boost::property_tree::write_ini(file, tree);
                break;
            case Format::Json:
                boost::property_tree::write_json(file, tree, true);
                break;
            case Format::Xml:
                boost::property_tree::write_xml(file, tree);
                break;
            }
            return true;
        }
        catch (...) {
            return false;
        }
    }

 
    template<typename T>
    std::optional<T> Get(const std::string& key, const T& defaultValue = T{}) const {
        std::lock_guard<std::mutex> lock(mutex);
        try {
            auto value = tree.get_optional<T>(key);  // ���� boost::optional<T>
            if (value) {
                return std::optional<T>(*value);     // תΪ std::optional
            }
            else {
                return std::optional<T>(defaultValue);
            }
        }
        catch (...) {
            return std::optional<T>(defaultValue);
        }
    }

    
    std::string GetString(const std::string& key, const std::string& defaultValue = "") const {
        return Get<std::string>(key, defaultValue).value_or(defaultValue);
    }

    int GetInt(const std::string& key, int defaultValue = 0) const {
        return Get<int>(key, defaultValue).value_or(defaultValue);
    }

    double GetDouble(const std::string& key, double defaultValue = 0.0) const {
        return Get<double>(key, defaultValue).value_or(defaultValue);
    }

    bool GetBool(const std::string& key, bool defaultValue = false) const {
        return Get<bool>(key, defaultValue).value_or(defaultValue);
    }

 
    template<typename T>
    void Set(const std::string& key, const T& value) {
        std::lock_guard<std::mutex> lock(mutex);
        tree.put(key, value);
    }


    bool Contains(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex);
        return tree.find(key) != tree.not_found();
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex);
        tree.clear();
    }

private:
    ConfigManager() = default;
    ~ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    mutable std::mutex mutex;
    boost::property_tree::ptree tree;
    std::string fileName;
    Format currentFormat = Format::Ini;
};