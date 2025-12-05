#include "Utils.h"

void get_timestamp(char* buffer, size_t size) {
    time_t rawtime;
    struct tm timeinfo;

    time(&rawtime);


#ifdef _WIN32
    
    if (localtime_s(&timeinfo, &rawtime) != 0) {

        strncpy_s(buffer, size, "0000-00-00 00::00::00", _TRUNCATE);
        return;
    }
#else

    if (localtime_r(&rawtime, &timeinfo) == NULL) {

        strncpy(buffer, "0000-00-00 00::00::00", size - 1);
        buffer[size - 1] = '\0';
        return;
    }
#endif

    strftime(buffer, size, "%Y-%m-%d %H::%M::%S", &timeinfo);
}


void get_level_info(LogLevel level, const char** level_str, const char** color) {
    switch (level) {
    case LOG_INFO:
        *level_str = "INFO";
        *color = COLOR_GREEN;
        break;
    case LOG_WARNING:
        *level_str = "WARNING";
        *color = COLOR_YELLOW;
        break;
    case LOG_ERROR:
        *level_str = "ERROR";
        *color = COLOR_RED;
        break;
    case LOG_DEBUG:
        *level_str = "DEBUG";
        *color = COLOR_BLUE;
        break;
    default:
        *level_str = "UNKNOWN";
        *color = COLOR_RESET;
        break;
    }
}


void log_message(LogLevel level, const char* format, ...) {
    char timestamp[32];
    const char* level_str;
    const char* color;
    va_list args;

    get_timestamp(timestamp, sizeof(timestamp));


    get_level_info(level, &level_str, &color);

 
    printf("%s[%s][%s]", color, level_str, timestamp);


    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("%s\n", COLOR_RESET);
}


void log_message_plain(LogLevel level, const char* format, ...) {
    char timestamp[32];
    const char* level_str;
    const char* color;
    va_list args;

    get_timestamp(timestamp, sizeof(timestamp));
    get_level_info(level, &level_str, &color);

    printf("[%s][%s]", level_str, timestamp);

    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("\n");
}