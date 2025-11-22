/**
 * @file config.h
 * @brief Configuration model and helpers.
 *
 * Defines the persistent configuration used by the application, along with
 * load/save helpers. The config can be stored alongside the executable or in
 * %APPDATA%\\altrightclick\\config.ini. The controller can optionally watch
 * the file for live reload.
 */
#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace arc { namespace config {

/**
 * @brief Global runtime configuration for the app.
 */
struct Config {
    /// Enable/disable the hook functionality at runtime.
    bool enabled = true;

    /// Show a system tray icon with runtime controls.
    bool show_tray = true;

    /// Legacy single modifier virtual-key (default ALT / VK_MENU).
    /// Used when @ref modifier_combo_vks is empty.
    unsigned int modifier_vk = 0x12;  // VK_MENU (ALT)
    /// Optional combo of modifier keys (e.g., {VK_MENU, VK_CONTROL}).
    std::vector<unsigned int> modifier_combo_vks;

    /// Exit key to stop the interactive app (ignored for service mode).
    unsigned int exit_vk = 0x1B;  // VK_ESCAPE

    /// Ignore externally injected mouse events (LLMHF_INJECTED/LLMHF_LOWER_IL_INJECTED).
    bool ignore_injected = true;

    /// Max press duration (ms) to consider a click vs. hold/drag.
    unsigned int click_time_ms = 250;
    /// Max pointer movement radius (px) to consider a click.
    int move_radius_px = 6;

    /// Logging level name: error|warn|info|debug.
    std::string log_level = "info";
    /// Optional log file path; empty for console only.
    std::string log_file;
    /// Include thread id in log lines (for debugging concurrent threads).
    bool log_thread_id = false;

    /// Source button that triggers translation.
    enum class Trigger {
        Left,
        Middle,
        X1,
        X2
    };
    Trigger trigger = Trigger::Left;

    /// Live reload toggle for config file changes.
    bool watch_config = false;

    /// Enable background persistence monitor to restart the app if it crashes.
    /// Disabled by default. Only applies to interactive mode (not service).
    bool persistence_enabled = false;

    /// Max number of restarts permitted before forcing an extended backoff.
    int persistence_max_restarts = 5;
    /// Rolling window length, in seconds, used to count restarts.
    int persistence_window_sec = 60;
    /// Initial backoff delay in milliseconds between restart attempts.
    int persistence_backoff_ms = 1000;
    /// Maximum exponential backoff cap in milliseconds.
    int persistence_backoff_max_ms = 30000;

    /// Timeout in milliseconds for graceful monitor shutdown before force-kill.
    int persistence_stop_timeout_ms = 3000;
};

/**
 * @brief Loads configuration from a file.
 *
 * If the file is missing or invalid, returns defaults. Supports key=value
 * lines with case-insensitive keys. Unknown keys are ignored.
 *
 * @param path UTF-8 path to configuration file.
 * @return Parsed Config object (with defaults on failure).
 */
Config load(const std::filesystem::path &path);

/**
 * @brief Computes the default configuration file path.
 *
 * Prefers <exe_dir>\\config.ini if present; otherwise uses
 * %APPDATA%\\altrightclick\\config.ini.
 */
std::filesystem::path default_path();

/**
 * @brief Saves configuration to disk.
 *
 * Creates the parent directory as needed.
 *
 * @param path Destination UTF-8 path.
 * @param cfg  Configuration to write.
 * @return true on success.
 */
bool save(const std::filesystem::path &path, const Config &cfg);

}  // namespace config

}  // namespace arc
