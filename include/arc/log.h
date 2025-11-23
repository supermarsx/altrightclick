/**
 * @file log.h
 * @brief Lightweight logging (console + optional file) with optional async mode.
 *
 * Provides severity-filtered logging that writes to stdout/stderr and,
 * optionally, to a file. When async mode is enabled, a background thread
 * handles IO to reduce contention with UI/hook threads. Public APIs are
 * thread-safe unless otherwise noted.
 */
#pragma once

#include <string>

namespace arc { namespace log {

/// @brief Severity levels (in increasing verbosity order).
enum class LogLevel {
    Error = 0,  ///< Error conditions that typically abort functionality.
    Warn = 1,   ///< Recoverable problems worth surfacing to the user.
    Info = 2,   ///< Informational diagnostics about normal operation.
    Debug = 3   ///< Verbose debugging information.
};

/**
 * @brief Sets the minimum severity to emit.
 *
 * @param lvl Minimum level (messages below are discarded).
 */
void set_level(LogLevel lvl);

/**
 * @brief Parses a level name and sets the minimum severity.
 *
 * Accepts: "error", "warn"/"warning", "info", or "debug" (case-insensitive).
 * Unknown values are ignored.
 *
 * @param name Desired level name.
 */
void set_level_by_name(const std::string &name);

/**
 * @brief Selects a log file to append output to (optional).
 *
 * Opens the file in append mode. Passing an empty string disables file output.
 * Thread-safe.
 *
 * @param path UTF-8 path to a writable file, or empty to disable.
 */
void set_file(const std::string &path);

/**
 * @brief Starts the background logging worker (idempotent).
 *
 * In async mode, log lines are queued and written on a dedicated thread.
 */
void start_async();

/**
 * @brief Stops the background logging worker and flushes pending lines.
 */
void stop_async();

/**
 * @brief Returns a UTF-8 message string for a Windows error code.
 *
 * Uses FormatMessageW and converts the result to UTF-8. Suitable for
 * GetLastError() values.
 *
 * @param err Windows error code (e.g., GetLastError()).
 * @return Human-readable message text.
 */
std::string last_error_message(uint32_t err);

/**
 * @brief Enables or disables inclusion of Windows thread ids in log lines.
 *
 * @param enabled True to append `[T:<thread-id>]` to each message.
 */
void set_include_thread_id(bool enabled);

/**
 * @brief RAII helper that logs scope entry/exit automatically.
 *
 * Constructing the scope emits "<name> begin" at the requested severity and
 * destroying it emits "<name> end". Useful for tracing critical sections.
 */
class LogScope {
 public:
    LogScope(const char *name, LogLevel lvl = LogLevel::Debug);
    ~LogScope();

 private:
    std::string name_;
    LogLevel level_;
    bool active_ = false;
};

#define ARC_LOG_CONCAT_INNER(a, b) a##b
#define ARC_LOG_CONCAT(a, b) ARC_LOG_CONCAT_INNER(a, b)
#define ARC_LOG_SCOPE(name) ::arc::log::LogScope ARC_LOG_CONCAT(_arc_scope_, __LINE__)(name)

/**
 * @brief Emits a log line at the given severity.
 *
 * In async mode, enqueues the line; otherwise writes synchronously.
 *
 * @param lvl Severity level for the message.
 * @param msg UTF-8 message content (no trailing newline required).
 */
void write(LogLevel lvl, const std::string &msg);

/// @brief Convenience wrapper that logs at LogLevel::Error.
inline void error(const std::string &msg) { write(LogLevel::Error, msg); }
/// @brief Convenience wrapper that logs at LogLevel::Warn.
inline void warn(const std::string &msg) { write(LogLevel::Warn, msg); }
/// @brief Convenience wrapper that logs at LogLevel::Info.
inline void info(const std::string &msg) { write(LogLevel::Info, msg); }
/// @brief Convenience wrapper that logs at LogLevel::Debug.
inline void debug(const std::string &msg) { write(LogLevel::Debug, msg); }

}  // namespace log

}  // namespace arc
