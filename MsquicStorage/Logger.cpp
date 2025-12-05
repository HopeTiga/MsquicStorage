#include "Logger.h"
#include <iostream>
#include <sstream>
#include <filesystem>

Logger* Logger::instance = nullptr;
std::mutex Logger::mutex;

Logger::Logger() : minLevel(LogLevels::INFO) {

    std::filesystem::path exePath = std::filesystem::current_path();

    logFilePath = (exePath / "system.log").string();

    std::filesystem::create_directories(exePath);

    logFile.open(logFilePath, std::ios::out | std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Failed to open log file: " << logFilePath << std::endl;
    }
}

Logger::~Logger() {
    if (logFile.is_open()) {
        logFile.close();
    }
}

Logger* Logger::getInstance() {
    std::lock_guard<std::mutex> lock(mutex);
    if (instance == nullptr) {
        instance = new Logger();
    }
    return instance;
}

void Logger::setLogLevels(LogLevels level) {
    minLevel = level;
}

void Logger::setLogFilePath(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex);
    if (logFile.is_open()) {
        logFile.close();
    }
    logFilePath = path;
    logFile.open(logFilePath, std::ios::out | std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Failed to open log file: " << logFilePath << std::endl;
    }
}

std::string Logger::getCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
    localtime_s(&local_tm, &now_time); // ??e????localtime_s????localtime
    std::stringstream ss;
    ss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string Logger::levelToString(LogLevels level) {
    switch (level) {
    case LogLevels::DEBUG: return "DEBUG";
    case LogLevels::INFO: return "INFO";
    case LogLevels::WARNING: return "WARNING";
    case LogLevels::ERRORS: return "ERROR";
    default: return "UNKNOWN";
    }
}

void Logger::debug(const std::string& message) {
    if (minLevel > LogLevels::DEBUG) return;
    std::lock_guard<std::mutex> lock(mutex);
    if (logFile.is_open()) {
        logFile << "[" << getCurrentTime() << "] [" << levelToString(LogLevels::DEBUG) << "]: " << message << std::endl;
        logFile.flush();
    }
}

void Logger::info(const std::string& message) {
    if (minLevel > LogLevels::INFO) return;
    std::lock_guard<std::mutex> lock(mutex);
    if (logFile.is_open()) {
        logFile << "[" << getCurrentTime() << "] [" << levelToString(LogLevels::INFO) << "]: " << message << std::endl;
        logFile.flush();
    }
}

void Logger::warning(const std::string& message) {
    if (minLevel > LogLevels::WARNING) return;
    std::lock_guard<std::mutex> lock(mutex);
    if (logFile.is_open()) {
        logFile << "[" << getCurrentTime() << "] [" << levelToString(LogLevels::WARNING) << "]: " << message << std::endl;
        logFile.flush();
    }
}

void Logger::error(const std::string& message) {
    if (minLevel > LogLevels::ERRORS) return;
    std::lock_guard<std::mutex> lock(mutex);
    if (logFile.is_open()) {
        logFile << "[" << getCurrentTime() << "] [" << levelToString(LogLevels::ERRORS) << "]: " << message << std::endl;
        logFile.flush();
    }
}
