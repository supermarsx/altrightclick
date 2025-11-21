# altrightclick Specification

## 1. Purpose & Scope
- **Goal**: Provide a lightweight Windows utility that translates a modifier-assisted click (default Alt + Left Click) into a Right Click so users can perform context-click actions without repositioning their mouse hand, while optionally emulating Mac-style single-button mice.
- **Scope**: Desktop-class Windows (x64/ARM64) environments with user-level interactivity. The specification covers interactive mode, Mac-style single-button emulation (right-click suppression), configurable exit hotkeys (including multi-key combos and disable support), persistence monitoring, scheduled task/service automation, configuration, tray UX, logging, and operational constraints derived from the current implementation.

## 2. Target Platforms & Dependencies
- **OS**: Windows 10/11 desktop/server SKUs that allow WH_MOUSE_LL hooks.
- **Architectures**: x64 and ARM64 builds produced via CMake + Visual Studio 2022 (C++17).
- **Runtime dependencies**: Win32 APIs (SetWindowsHookEx, Shell_NotifyIcon), schtasks.exe, SCM APIs, `%APPDATA%` access, console subsystem.
- **Build artifacts**: `altrightclick.exe`, optional `icon_gen` helper (produces tray icon); resources located under `res/`.

## 3. Operating Modes
| Mode | Trigger | Description |
| --- | --- | --- |
| Interactive (default) | Run `altrightclick` directly | Starts hook worker, tray worker (if enabled), optional config watcher, and optional persistence monitor. Enforces per-session singleton via `Local\AltRightClick.Singleton`. |
| Service | `altrightclick --service` (spawned by SCM) | Installs hook worker only; runs under `AltRightClickService` entry registered with SCM. Uses machine-global singleton (`Global\AltRightClick.Service`). Limited by Session 0 isolation (no tray, no interactive hook delivery). |
| CLI management | `--install`, `--task-install`, etc. | Provides administrative commands for service install/uninstall/start/stop/status and scheduled task create/delete/update/status. |
| Persistence monitor | `altrightclick --monitor --parent <pid>` | Detached helper that relaunches the app if it crashes, honoring restart window/backoff limits and stopping on intentional exits. |
| Scheduled Task | Created via CLI | Runs the interactive mode at user logon with highest privileges (optional). |

## 4. High-Level Functional Behavior
1. **Input Translation**
   - Installs a process-level WH_MOUSE_LL hook on a dedicated worker thread.
   - Tracks configurable trigger button (Left/Middle/X1/X2) while modifiers are held.
   - Distinguishes clicks vs drags using configurable time (`click_time_ms`, default 250 ms) and movement radius (`move_radius_px`, default 6 px). On drag, it replays the original trigger down event to preserve drag semantics.
   - Injects synthesized right-clicks via `SendInput`, tagging events with `0xA17C1C00` to avoid reprocessing.
    - Honors `ignore_injected` to skip externally injected events (LLMHF flags).
   - When `disable_right_click=true` (default), swallows physical right-click down/up events and re-injects them as left clicks, effectively forcing users to rely on the modifier-trigger workflow for context menus (Mac-style single-button behavior).

2. **Tray Experience (optional)**
   - Hidden tool window + tray icon with tooltip “AltRightClick running (Alt+Left => Right)”.
   - Context menu actions: toggle enable, adjust click timing (+/- 10 ms within 10–5000), adjust move radius (+/- 1 px within 0–100), toggle ignore-injected, toggle persistence monitor (shows current monitor state), save settings to disk, open config folder, exit.
   - Exit command sets an atomic flag read by the controller loop.
   - Displays balloon notifications for config reloads and persistence toggles.

3. **Configuration Management**
   - Default lookup order: `<exe_dir>\config.ini`, then `%APPDATA%\altrightclick\config.ini`.
   - First run auto-generates defaults with comments if file is absent.
   - Optional live reload (`watch_config=true`) polls last-write timestamp every 500 ms; on change it reloads config, reapplies hook settings, updates logging, and notifies the user.
   - Tray “Save Settings” writes the current runtime config back to disk.

4. **CLI & Automation**
   - Full CLI handles service installation/lifecycle, scheduled task lifecycle, config generation, log tuning, and persistence overrides.
   - CLI arguments sanitize user input (e.g., service binpath quoting, config path validation).

5. **Logging & Diagnostics**
   - Async-capable logger (`arc::log`) with severity filtering (error/warn/info/debug) and optional file sink.
   - Default interactive run starts async logging and writes version/config path on startup.
   - Utility helper returns Windows error strings for richer diagnostics in CLI/service modes.

6. **Resiliency & Persistence**
   - Optional persistence monitor relaunches the app after crashes, applying exponential backoff capped by config values and respecting `persistence_max_restarts` within a rolling window.
   - Monitor and main app coordinate via a `%APPDATA%\altrightclick\intentional_exit` marker and a named stop event (`Local\altrightclick_stop_<pid>`).

## 5. User Workflows
- **Default interactive session**: User runs `altrightclick.exe`, optionally edits `config.ini`, uses tray to tweak thresholds, and exits via `ESC` (default exit key), tray menu, or console Ctrl+C.
- **Service deployment**: Admin runs elevated shell with `--install --start` to register `AltRightClickService` pointing to `altrightclick --service --config ...`. CLI validates admin token before SCM operations.
- **Scheduled task autostart**: User executes `--task-install` to create `AltRightClickTask` that runs at logon (highest privileges). `--task-status` reports `PRESENT/MISSING`.
- **Persistence toggle**: User enables `persistence=true` in config or toggles via tray. Monitor spawns once per interactive run (unless `--launched-by-monitor` flag is present).
- **Config live editing**: With `watch_config=true`, user edits config in a text editor; the watcher reloads values and updates hook/logging without restart.

## 6. Configuration Reference (`config.ini`)
| Key | Type/Values | Default | Notes |
| --- | --- | --- | --- |
| `enabled` | bool | `true` | Master switch; when false the app logs and exits. |
| `show_tray` | bool | `true` | Controls tray worker start. |
| `modifier` | `ALT`, `CTRL`, `SHIFT`, `WIN`, or combos (`ALT+CTRL`) | `ALT` | Accepts comma/plus delimiters; combos stored in priority order. |
| `trigger` | `LEFT`, `MIDDLE`, `X1`, `X2` | `LEFT` | Indicates source button to translate. |
| `exit_key` | `<key|combo|DISABLED>` | `ESC` | Keyboard exit in interactive mode. Accepts single keys or multi-key combos (e.g., `CTRL+ALT+Q`); `DISABLED` removes the hotkey entirely. |
| `ignore_injected` | bool | `true` | Skips mouse events flagged as injected by other processes. |
| `disable_right_click` | bool | `true` | Converts physical right-clicks into left-clicks; only modifier-triggered clicks can produce actual right-clicks. |
| `click_time_ms` | 10–5000 | `250` | Max press duration to treat as click. |
| `move_radius_px` | 0–100 | `6` | Max square distance threshold for translation. |
| `log_level` | `error`, `warn`, `info`, `debug` | `info` | Drives console/file verbosity. |
| `log_file` | path | empty | When set, logger appends here in addition to console. |
| `watch_config` | bool | `false` | Enables live reload watcher thread. |
| `persistence` / `persistence_enabled` | bool | `false` | Enables crash monitor for interactive runs. |
| `persistence_max_restarts` | int >= 0 | `5` | Max restarts considered within rolling window. |
| `persistence_window_sec` | int >= 1 | `60` | Window for restart counting. |
| `persistence_backoff_ms` | int >= 0 | `1000` | Initial restart delay. |
| `persistence_backoff_max_ms` | int >= 0 | `30000` | Cap for exponential backoff. |
| `persistence_stop_timeout_ms` | int >= 0 | `3000` | Timeout before the monitor is forcefully terminated when disabling. |

## 7. CLI Reference
| Argument | Purpose |
| --- | --- |
| `--config <path>` | Use explicit config file (UTF-8). |
| `--generate-config` | Emit default config (with comments) to resolved path and exit. |
| `--log-level <lvl>` | Override log level for this run (also applied on reload). |
| `--log-file <path>` | Append logs to file in addition to console. |
| `--install / --uninstall / --start / --stop` | SCM commands for `AltRightClickService` (requires elevation). |
| `--service-status` | Prints `RUNNING` or `STOPPED`; exit code reflects success. |
| `--service` | Internal flag used when SCM launches the service host. |
| `--persistence-enable / --persistence-disable` (`--no-persistence`) | CLI override of persistence flag for the session. |
| `--task-install / --task-uninstall / --task-update / --task-status` | Manage `AltRightClickTask` via schtasks.exe; outputs textual status plus `OK/FAILED`. |
| `--monitor` | Runs persistence monitor loop (internal). |
| `--parent <pid>` | Paired with `--monitor`; indicates monitored parent process id. |
| `--launched-by-monitor` | Internal marker to avoid recursively spawning monitors. |
| `--help`, `-h`, `-?` | Print usage help. |

All management commands sanitize inputs (e.g., reject unsafe config paths for service registration) and exit with non-zero status on failure.

## 8. Runtime Components & Interactions
1. **Main Controller (`src/main.cpp`)**
   - Parses CLI, resolves config file, sets up logging, enforces singleton, spawns workers, and runs the controller loop that polls exit conditions every 50 ms.
   - Installs console control handler to flip `g_console_shutdown` on Ctrl+C/close/logoff events.

2. **Hook Worker (`arc::hook`)**
   - Dedicated thread with private message loop.
   - Maintains atomic configuration (modifier VKs, trigger, timing thresholds, enable flag) updated via `apply_hook_config`.
   - On failure to install the hook, logs error and prevents interactive mode from continuing.

3. **Tray Worker (`arc::tray`)**
   - Dedicated thread running a hidden tool window and tray icon.
   - Uses shared `TrayContext` to mutate live `arc::config::Config` in place and signal exit.

4. **Config Watcher**
   - Optional thread that polls file timestamps to reload config, reapply logging, refresh tray context, and notify the user.

5. **Persistence Monitor (`arc::persistence`)**
   - Spawned as detached process with `--monitor --parent <pid>` and optional `--config`.
   - Watches parent exit, enforces restart rate limits/backoff, writes/reads intent marker to differentiate intentional exit vs crash, and exposes APIs to query/stop monitor state for the tray.

6. **Singleton Guards (`arc::singleton`)**
   - Interactive runs use `Local\AltRightClick.Singleton`; services use `Global\AltRightClick.Service`.
   - Guard ensures only one instance per scope; failure logs warning and exits gracefully.

7. **Logging Subsystem (`arc::log`)**
   - Provides severity filtering, asynchronous queue, and optional file sink.
   - Shared by all components, including persistence and service helpers, to produce uniform console/file output.

## 9. Service & Scheduled Task Behavior
- **Service (`AltRightClickService`)**
  - Install command builds `"<exe>" --service [--config "<path>"]` as binpath.
  - Service main registers control handler, starts hook worker, runs message loop until SCM issues stop/shutdown, then stops hook worker.
  - Limitations: WH_MOUSE_LL hooks require interactive session; due to Session 0 isolation the hook cannot see user desktop input when running purely as a service. Documentation steers users toward scheduled tasks for auto-start in user sessions.

- **Scheduled Task (`AltRightClickTask`)**
  - Created via `schtasks.exe /Create /SC ONLOGON /IT /RL HIGHEST` to run at user logon (interactive token).
  - Update operation deletes and recreates the task to refresh target arguments.
  - Existence check relies on schtasks exit code; CLI surfaces `PRESENT/MISSING`.

## 10. Persistence & Resiliency Requirements
- Monitor only participates in interactive sessions (skipped in service mode).
- On monitor start, attaches to parent process or waits for stop event.
- Restart policy:
  - Maintains vector of restart timestamps; prevents more than `persistence_max_restarts` within `persistence_window_sec`.
  - Exponential backoff doubles delay between relaunches until capped at `persistence_backoff_max_ms`.
- Intent marker (`%APPDATA%\altrightclick\intentional_exit`) is written just before the main app exits normally; monitor clears it before relaunch.
- Tray toggle ensures monitor can be started/stopped at runtime (stop uses graceful wait then forced termination after `persistence_stop_timeout_ms`).

## 11. Logging & Diagnostics
- Severity names accepted: `error`, `warn`/`warning`, `info`, `debug`.
- Async logging is enabled by default in interactive mode; service mode uses synchronous logging unless explicitly started.
- Log lines formatted as `[YYYY-MM-DD HH:MM:SS] [LEVEL] message`.
- File logging uses append mode and flushes after each write; concurrency protected by mutex.
- Errors interacting with Windows APIs reference `arc::log::last_error_message(GetLastError())` for human-readable detail.

## 12. Build & Release Requirements
- **CMake presets**: `windows-x64`, `windows-arm64`, `build-*` configurations produce Release binaries.
- **Manual commands**: `cmake -S . -B build/x64 -G "Visual Studio 17 2022" -A x64` followed by `cmake --build build/x64 --config Release`.
- **Scripts**: `scripts\compile.bat` builds both architectures.
- **Icon generation**: `icon_gen` target outputs `altrightclick.ico` under build tree; build embeds icon automatically.
- **Tests**: Use `ctest -C Release --output-on-failure` from build dir (tests live in `tests/`).
- **Lint/Format**: `cpplint` configuration in repo; `clang-format` enforced by CI workflows.
- **CI/CD**: Workflows under `.github/workflows/` cover formatting, linting, testing, coverage, build, releases, and Scoop manifest updates.

## 13. Security, Permissions & Limitations
- **Elevation**: Service commands require Administrator privileges; CLI checks `TOKEN_ELEVATION`/Administrators group membership before proceeding.
- **Session isolation**: Mouse hook cannot function from Session 0; service mode is provided for completeness but is not expected to manipulate user input.
- **Input injection safety**: Default `ignore_injected=true` avoids responding to synthetic events from other processes, preventing feedback loops with other automation tools.
- **Argument safety**: Service command builder rejects control characters/quotes in config paths before embedding them into SCM binpath.
- **Single-instance enforcement**: Prevents conflicting hooks or duplicated tray icons.

## 14. Non-Functional Requirements
- **Responsiveness**: Hook worker must install within milliseconds and run on a dedicated thread to avoid blocking UI. Controller loop polls exit flags every 50 ms to provide quick shutdown.
- **Resource usage**: Minimal CPU overhead; watchers sleep between polls (500 ms), logging flushes asynchronously, tray operates on demand.
- **Stability**: Persistence monitor ensures automatic recovery from crashes while respecting restart throttling; watchers catch config errors by falling back to defaults.

## 15. Known Limitations & Future Considerations
- Service mode cannot translate clicks in Session 0; users should prefer scheduled tasks or other user-session auto-start methods.
- Only `ESC` or `F12` can be configured as exit keys without code changes.
- Modifier parsing currently recognizes ALT/CTRL/SHIFT/WIN; additional keys would require code updates.
- Config watcher uses timestamp polling rather than filesystem events; extremely frequent edits within <500 ms may be coalesced.
- Hook translation exclusively targets right-click injection; additional target buttons (e.g., middle-click emulation) would require new configuration surfaces.

This specification reflects the behavior of the current codebase (`src/`, `include/arc/`, and supporting scripts) and should be kept in sync as features evolve.
