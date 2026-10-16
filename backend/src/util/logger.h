#pragma once
// logger.h — Simple structured logger.
#include <cstdarg>
#include <string>

namespace smartcity {

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

void log(LogLevel level, const char* component, const char* msg);

// Convenience macros
#define SC_LOG_DEBUG(comp, msg) ::smartcity::log(::smartcity::LogLevel::DEBUG, comp, msg)
#define SC_LOG_INFO(comp,  msg) ::smartcity::log(::smartcity::LogLevel::INFO,  comp, msg)
#define SC_LOG_WARN(comp,  msg) ::smartcity::log(::smartcity::LogLevel::WARN,  comp, msg)
#define SC_LOG_ERR(comp,   msg) ::smartcity::log(::smartcity::LogLevel::ERROR, comp, msg)

} // namespace smartcity
