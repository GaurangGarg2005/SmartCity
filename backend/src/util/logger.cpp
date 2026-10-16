// logger.cpp — Simple structured logger (Phase 0/2)
#include "logger.h"
#include <ctime>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace smartcity {

static std::mutex log_mutex;

static const char* level_str(LogLevel l) {
    switch (l) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
    }
    return "?????";
}

void log(LogLevel level, const char* component, const char* msg) {
    std::time_t now = std::time(nullptr);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));

    std::lock_guard<std::mutex> lock(log_mutex);
    std::fprintf(stderr, "[%s] %s [%s] %s\n", ts, level_str(level), component, msg);
    std::fflush(stderr);
}

} // namespace smartcity
