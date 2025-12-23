#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "Utils.h"
#include <chrono>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

// 颜色定义
static const char* COLOR_RESET = "\033[0m";
static const char* COLOR_RED = "\033[91m";
static const char* COLOR_GREEN = "\033[92m";
static const char* COLOR_YELLOW = "\033[93m";
static const char* COLOR_BLUE = "\033[94m";

// 全局变量
static const char* logFileNames[4] = {
    "debug.log",
    "info.log",
    "warning.log",
    "error.log"
};
static std::string logDir = "logs";
static int logToFileEnabled = 1;
static std::mutex logMutex;
static int loggerInitialized = 0;

// 哪些级别需要输出到控制台（1=输出到控制台，0=只输出到文件）
static int consoleOutputLevels[4] = { 1, 1, 1, 1 }; // DEBUG, INFO, WARNING, ERROR 默认都输出到控制台

// 初始化日志系统
void initLogger() {
    std::lock_guard<std::mutex> lock(logMutex);

    if (loggerInitialized) {
        return;
    }

    // 创建日志目录
#ifdef _WIN32
    _mkdir(logDir.c_str());
#else
    mkdir(logDir.c_str(), 0755);
#endif

    loggerInitialized = 1;
}

void closeLogger() {
    std::lock_guard<std::mutex> lock(logMutex);
    // 现在没有需要关闭的全局文件句柄
    loggerInitialized = 0;
}

void enableFileLogging(int enable) {
    std::lock_guard<std::mutex> lock(logMutex);
    logToFileEnabled = enable;
}

void setLogDirectory(const char* dir) {
    std::lock_guard<std::mutex> lock(logMutex);
    logDir = dir;
}

// 设置哪些级别只输出到文件（不在控制台显示）
void setConsoleOutputLevels(int debug, int info, int warning, int error) {
    std::lock_guard<std::mutex> lock(logMutex);
    consoleOutputLevels[LOG_LEVEL_DEBUG] = debug;
    consoleOutputLevels[LOG_LEVEL_INFO] = info;
    consoleOutputLevels[LOG_LEVEL_WARNING] = warning;
    consoleOutputLevels[LOG_LEVEL_ERROR] = error;
}

void getTimestamp(char* buffer, size_t size) {
    time_t rawtime;
    struct tm timeinfo;

    time(&rawtime);

#ifdef _WIN32
    if (localtime_s(&timeinfo, &rawtime) != 0) {
        strncpy_s(buffer, size, "0000-00-00 00:00:00", _TRUNCATE);
        return;
    }
#else
    if (localtime_r(&rawtime, &timeinfo) == nullptr) {
        strncpy(buffer, "0000-00-00 00:00:00", size - 1);
        buffer[size - 1] = '\0';
        return;
    }
#endif

    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", &timeinfo);
}

void getLevelInfo(LogLevel level, const char** levelStr, const char** color) {
    switch (level) {
    case LOG_LEVEL_INFO:
        *levelStr = "INFO";
        *color = COLOR_GREEN;
        break;
    case LOG_LEVEL_WARNING:
        *levelStr = "WARNING";
        *color = COLOR_YELLOW;
        break;
    case LOG_LEVEL_ERROR:
        *levelStr = "ERROR";
        *color = COLOR_RED;
        break;
    case LOG_LEVEL_DEBUG:
        *levelStr = "DEBUG";
        *color = COLOR_BLUE;
        break;
    default:
        *levelStr = "UNKNOWN";
        *color = COLOR_RESET;
        break;
    }
}

// 辅助函数：打开文件并写入日志
static void writeLogToFile(LogLevel level, const char* timestamp,
    const char* levelStr, const char* format, va_list args) {
    if (!logToFileEnabled || level < 0 || level > 3) {
        return;
    }

    std::string filePath = logDir + "/" + logFileNames[level];

    // 确保目录存在
    if (!loggerInitialized) {
#ifdef _WIN32
        // 使用 CreateDirectoryA 创建目录
        if (!CreateDirectoryA(logDir.c_str(), NULL)) {
            DWORD err = GetLastError();
            if (err != ERROR_ALREADY_EXISTS) {
                // 目录创建失败，但不是因为已经存在
                printf("ERROR: Failed to create log directory: %s, error: %lu\n",
                    logDir.c_str(), err);
                fflush(stdout);
            }
        }
#else
        mkdir(logDir.c_str(), 0755);
#endif
        loggerInitialized = 1;
    }

    FILE* logFile = nullptr;

#ifdef _WIN32
    // 直接使用 fopen_s，不要用宽字符转换
    errno_t err = fopen_s(&logFile, filePath.c_str(), "a");
    if (err != 0) {
        printf("ERROR: Failed to open log file: %s, error: %d\n",
            filePath.c_str(), err);
        fflush(stdout);
        return;
    }
#else
    logFile = fopen(filePath.c_str(), "a");
    if (!logFile) {
        printf("ERROR: Failed to open log file: %s\n", filePath.c_str());
        fflush(stdout);
        return;
    }
#endif

    if (logFile) {
        // 不使用缓冲，立即写入
        setvbuf(logFile, nullptr, _IONBF, 0);

        // 先写入时间戳和等级
        int written1 = fprintf(logFile, "[%s][%s] ", timestamp, levelStr);
        if (written1 < 0) {
            printf("ERROR: fprintf failed for timestamp, written: %d\n", written1);
            fflush(stdout);
        }

        // 写入消息
        int written2 = vfprintf(logFile, format, args);
        if (written2 < 0) {
            printf("ERROR: vfprintf failed for message, written: %d\n", written2);
            fflush(stdout);
        }

        // 写入换行
        int written3 = fprintf(logFile, "\n");
        if (written3 < 0) {
            printf("ERROR: fprintf failed for newline, written: %d\n", written3);
            fflush(stdout);
        }

        fclose(logFile);  // 立即关闭文件

        // 调试信息
        // printf("DEBUG: Successfully wrote log to %s\n", filePath.c_str());
        // fflush(stdout);
    }
}

// 带颜色的日志输出 - 线程安全版本
void logMessage(LogLevel level, const char* format, ...) {
    char timestamp[32];
    const char* levelStr;
    const char* color;
    va_list args, argsCopy;

    // 获取时间戳
    getTimestamp(timestamp, sizeof(timestamp));

    // 获取等级信息
    getLevelInfo(level, &levelStr, &color);

    // 复制参数列表
    va_start(args, format);
    va_copy(argsCopy, args);

    // 构建消息
    char messageBuffer[4096];
    int len = vsnprintf(messageBuffer, sizeof(messageBuffer), format, args);
    if (len < 0 || len >= (int)sizeof(messageBuffer)) {
#ifdef _WIN32
        strcpy_s(messageBuffer, sizeof(messageBuffer), "[Message too long or formatting error]");
#else
        strncpy(messageBuffer, "[Message too long or formatting error]", sizeof(messageBuffer) - 1);
        messageBuffer[sizeof(messageBuffer) - 1] = '\0';
#endif
    }

    // 使用互斥锁确保整个输出过程是原子的
    std::lock_guard<std::mutex> lock(logMutex);

    // 输出到控制台（如果该级别允许）
    if (consoleOutputLevels[level]) {
        printf("%s[%s][%s] %s%s\n", color, levelStr, timestamp, messageBuffer, COLOR_RESET);
        fflush(stdout);
    }

    // 输出到文件
    writeLogToFile(level, timestamp, levelStr, format, argsCopy);

    va_end(args);
    va_end(argsCopy);
}

// 不带颜色的日志输出 - 线程安全版本
void logMessagePlain(LogLevel level, const char* format, ...) {
    char timestamp[32];
    const char* levelStr;
    const char* color;
    va_list args, argsCopy;

    // 获取时间戳
    getTimestamp(timestamp, sizeof(timestamp));

    // 获取等级信息
    getLevelInfo(level, &levelStr, &color);

    // 复制参数列表
    va_start(args, format);
    va_copy(argsCopy, args);

    // 构建消息
    char messageBuffer[4096];
    int len = vsnprintf(messageBuffer, sizeof(messageBuffer), format, args);
    if (len < 0 || len >= (int)sizeof(messageBuffer)) {
#ifdef _WIN32
        strcpy_s(messageBuffer, sizeof(messageBuffer), "[Message too long or formatting error]");
#else
        strncpy(messageBuffer, "[Message too long or formatting error]", sizeof(messageBuffer) - 1);
        messageBuffer[sizeof(messageBuffer) - 1] = '\0';
#endif
    }

    // 使用互斥锁确保整个输出过程是原子的
    std::lock_guard<std::mutex> lock(logMutex);

    // 输出到控制台（如果该级别允许）
    if (consoleOutputLevels[level]) {
        printf("[%s][%s] %s\n", levelStr, timestamp, messageBuffer);
        fflush(stdout);
    }

    // 输出到文件
    writeLogToFile(level, timestamp, levelStr, format, argsCopy);

    va_end(args);
    va_end(argsCopy);
}

// 只输出到文件
void logToFileOnly(LogLevel level, const char* format, ...) {
    if (!logToFileEnabled) {
        return;
    }

    char timestamp[32];
    const char* levelStr;
    const char* color;
    va_list args;

    // 获取时间戳
    getTimestamp(timestamp, sizeof(timestamp));

    // 获取等级信息
    getLevelInfo(level, &levelStr, &color);

    // 获取参数列表
    va_start(args, format);

    // 使用互斥锁
    std::lock_guard<std::mutex> lock(logMutex);

    // 只输出到文件
    writeLogToFile(level, timestamp, levelStr, format, args);

    va_end(args);
}