#pragma once

#include <string>

namespace arc {

enum class LogLevel { Error = 0, Warn = 1, Info = 2, Debug = 3 };

// Configure destination and level
void log_set_level(LogLevel lvl);
void log_set_level_by_name(const std::string& name);
void log_set_file(const std::string& path);  // empty to disable file output

// Utilities
std::string last_error_message(unsigned long err);

// Emit log lines (timestamp + level prefix)
void log_msg(LogLevel lvl, const std::string& msg);

// Convenience wrappers
inline void log_error(const std::string& msg) { log_msg(LogLevel::Error, msg); }
inline void log_warn(const std::string& msg) { log_msg(LogLevel::Warn, msg); }
inline void log_info(const std::string& msg) { log_msg(LogLevel::Info, msg); }
inline void log_debug(const std::string& msg) { log_msg(LogLevel::Debug, msg); }

}  // namespace arc

