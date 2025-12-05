#pragma once
#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <chrono>
#include <boost/json.hpp>

constexpr std::chrono::seconds PING_INTERVAL = std::chrono::seconds(30);


typedef enum {
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_DEBUG
} LogLevel;


#define LOG_INFO(fmt, ...)    log_message(LOG_INFO, fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) log_message(LOG_WARNING, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)   log_message(LOG_ERROR, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)   log_message(LOG_DEBUG, fmt, ##__VA_ARGS__)


#define LOG_INFO_PLAIN(fmt, ...)    log_message_plain(LOG_INFO, fmt, ##__VA_ARGS__)
#define LOG_WARNING_PLAIN(fmt, ...) log_message_plain(LOG_WARNING, fmt, ##__VA_ARGS__)
#define LOG_ERROR_PLAIN(fmt, ...)   log_message_plain(LOG_ERROR, fmt, ##__VA_ARGS__)
#define LOG_DEBUG_PLAIN(fmt, ...)   log_message_plain(LOG_DEBUG, fmt, ##__VA_ARGS__)


#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[91m"
#define COLOR_GREEN   "\033[92m"
#define COLOR_YELLOW  "\033[93m"
#define COLOR_BLUE    "\033[94m"


void get_timestamp(char* buffer, size_t size);
void get_level_info(LogLevel level, const char** level_str, const char** color);
void log_message(LogLevel level, const char* format, ...);
void log_message_plain(LogLevel level, const char* format, ...);


// 粗爆转义 JSON 字符串值（只处理 string 类型）
static void brutalEscapeJson(boost::json::object& obj) {
    for (auto& [k, v] : obj) {
        if (v.is_string()) {
            std::string s = v.as_string().c_str();
            // 只干两件事：去 \0 和 '
            std::replace(s.begin(), s.end(), '\0', ' ');
            std::replace(s.begin(), s.end(), '\'', ' ');
            obj[k] = std::move(s);
        }
        else if (v.is_object()) {
            brutalEscapeJson(v.as_object());   // 递归
        }
        else if (v.is_array()) {
            for (auto& elem : v.as_array()) {
                if (elem.is_object()) brutalEscapeJson(elem.as_object());
                if (elem.is_string()) {
                    std::string s = elem.as_string().c_str();
                    std::replace(s.begin(), s.end(), '\0', ' ');
                    std::replace(s.begin(), s.end(), '\'', ' ');
                    elem = std::move(s);
                }
            }
        }
    }
}

// 深度拷贝一份 JSON 并转义
static boost::json::object makeCleanCopy(const boost::json::object& src) {
    boost::json::object dst = src;   // 深度拷贝
    brutalEscapeJson(dst);
    return dst;
}

namespace {
    // 辅助函数：构建消息头 + 消息体（只有length作为消息头）
    std::pair<unsigned char *, size_t> buildMessage(const std::string& body) {
        int64_t bodyLength = static_cast<int64_t>(body.size());
        size_t totalSize = sizeof(int64_t) + bodyLength;

        unsigned char* buffer = new unsigned char[totalSize];

        // 写入 length (int64_t)
        *reinterpret_cast<int64_t*>(buffer) = bodyLength;

        // 写入 body
        memcpy(buffer + sizeof(int64_t), body.data(), bodyLength);

        return { std::move(buffer), totalSize };
    }

    // 重载版本，直接使用 json
    std::pair<unsigned char * , size_t> buildMessage(const boost::json::object& jsonObj) {
        std::string body = boost::json::serialize(jsonObj);
        return buildMessage(body);
    }
}

#endif // UTILS_H