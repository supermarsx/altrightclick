/**
 * @file persistence.h
 * @brief Simple persistence/monitoring helpers.
 */

#pragma once

#include <string>

namespace arc { namespace persistence {

/**
 * @brief Spawns a detached monitor process that watches the current process
 *        and relaunches the app if it exits abnormally.
 *
 * The monitor is the same executable invoked with --monitor and the parent PID.
 * This is a no-op on failure; errors are logged by the caller.
 *
 * @param exe_path Full path to current executable.
 * @param config_path UTF-8 path to config to pass to relaunched process.
 * @return true if the monitor was started.
 */
bool spawn_monitor(const std::wstring &exe_path, const std::string &config_path);

/**
 * @brief Runs the monitor loop.
 *
 * Waits for the parent process to exit, then relaunches the main app and
 * monitors it. If the relaunched app exits with code 0, the monitor stops.
 * Otherwise it applies a backoff and retries, capped to a reasonable rate.
 *
 * @param parent_pid PID to watch.
 * @param exe_path Full path to the app executable to relaunch.
 * @param config_path Optional config path to pass through.
 * @return 0 on normal completion.
 */
int run_monitor(unsigned long parent_pid, const std::wstring &exe_path, const std::wstring &config_path);

/**
 * @brief Returns the path to the intentional-exit marker file.
 *        Located under %APPDATA%\\altrightclick for write access.
 */
std::wstring intent_marker_path();

/**
 * @brief Writes the intentional-exit marker so the monitor will not restart
 *        the app, regardless of exit code.
 */
void write_intent_marker();

/** Returns true if a monitor process is known to be running. */
bool is_monitor_running();

/** Attempts to stop the monitor process if running. */
bool stop_monitor();

}  // namespace persistence

}  // namespace arc
