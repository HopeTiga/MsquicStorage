#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>

enum class LogLevels {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERRORS = 3,
};

class Logger {
private:
    static Logger* instance;
    static std::mutex mutex;
    std::ofstream logFile;
    LogLevels minLevel;
    std::string logFilePath;

    Logger();
    ~Logger();

    std::string getCurrentTime();
    std::string levelToString(LogLevels level);

public:
    static Logger* getInstance();

    void setLogLevels(LogLevels level);
    void setLogFilePath(const std::string& path);

    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
};
