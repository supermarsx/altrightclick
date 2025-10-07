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

// Severity levels (in increasing verbosity order).
enum class LogLevel {
    Error = 0,
    Warn = 1,
    Info = 2,
    Debug = 3
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
 * @brief Emits a log line at the given severity.
 *
 * In async mode, enqueues the line; otherwise writes synchronously.
 *
 * @param lvl Severity level for the message.
 * @param msg UTF-8 message content (no trailing newline required).
 */
void write(LogLevel lvl, const std::string &msg);

// Convenience wrappers.
inline void error(const std::string &msg) { write(LogLevel::Error, msg); }
inline void warn(const std::string &msg) { write(LogLevel::Warn, msg); }
inline void info(const std::string &msg) { write(LogLevel::Info, msg); }
inline void debug(const std::string &msg) { write(LogLevel::Debug, msg); }

}  // namespace log

}  // namespace arc
