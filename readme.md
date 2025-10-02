# altrightclick

[![CI](https://github.com/supermarsx/altrightclick/actions/workflows/ci.yml/badge.svg)](https://github.com/supermarsx/altrightclick/actions/workflows/ci.yml)
[![Rolling Release](https://github.com/supermarsx/altrightclick/actions/workflows/release.yml/badge.svg)](https://github.com/supermarsx/altrightclick/actions/workflows/release.yml)
[![Downloads](https://img.shields.io/github/downloads/supermarsx/altrightclick/total.svg)](https://github.com/supermarsx/altrightclick/releases)
[![Stars](https://img.shields.io/github/stars/supermarsx/altrightclick.svg?style=social)](https://github.com/supermarsx/altrightclick/stargazers)
[![Watchers](https://img.shields.io/github/watchers/supermarsx/altrightclick.svg?style=social)](https://github.com/supermarsx/altrightclick/watchers)
[![Forks](https://img.shields.io/github/forks/supermarsx/altrightclick.svg?style=social)](https://github.com/supermarsx/altrightclick/network/members)
[![Issues](https://img.shields.io/github/issues/supermarsx/altrightclick.svg)](https://github.com/supermarsx/altrightclick/issues)
[![License](https://img.shields.io/github/license/supermarsx/altrightclick.svg)](license.md)
[![Built with](https://img.shields.io/badge/Built%20with-C%2B%2B17%20%7C%20CMake-blue)](#build-windows)
[![Windows](https://img.shields.io/badge/Windows-x64%20%7C%20ARM64-0078D6?logo=windows&logoColor=white)](#build-windows)

altrightclick turns Alt + Left Click into a Right Click on Windows by installing a low-level mouse hook. It can run as a tray app with a simple config file, and provides CLI commands for installing/uninstalling a Windows service (with caveats about Session 0 isolation).

## Features
- Alt + Left Click triggers a Right Click (default ALT modifier)
- Configurable modifier and exit key via simple `config.ini`
- Tray icon with basic context menu (Exit)
- CLI for service install/uninstall/start/stop
- 64-bit x64 and ARM64 builds via CMake (VS 2022)
- Modular code: separate headers under `include/arc` and sources in `src`
 - Single-instance guard to prevent multiple concurrent instances

## Quick Start
- Run interactively with tray: place `altrightclick.exe` somewhere and run it. Press `ESC` to exit (configurable).
- Install service (see limitations below):
  - Install: `altrightclick --install`
  - Start: `altrightclick --start`
  - Stop: `altrightclick --stop`
  - Uninstall: `altrightclick --uninstall`

## Install via Scoop
- One‑shot install from manifest URL:
  - `scoop install https://raw.githubusercontent.com/supermarsx/altrightclick/refs/heads/main/scoop/altrightclick.json`
- Or add to a bucket (recommended if you fork):
  - `scoop bucket add altrightclick https://github.com/supermarsx/altrightclick`
  - `scoop install altrightclick`
Notes:
- Releases are tagged by date (e.g., `v0.1.yy.mm.dd`). Scoop manifest updates automatically after each rolling release and via a nightly sync job.

## CLI Arguments
- `--config <path>`: use a specific config file
- `--log-level <error|warn|info|debug>`: override logging level (default: info)
- `--log-file <path>`: append logs to a file in addition to console
- `--install`: install a Windows service (auto-start)
- `--uninstall`: uninstall the Windows service
- `--start`: start the Windows service
- `--stop`: stop the Windows service
- `--service-status`: print `RUNNING` or `STOPPED` and set exit code
- `--service`: internal; run service mode (invoked by the SCM)
- `--task-install`: create a Scheduled Task to run at user logon (highest privileges)
- `--task-uninstall`: remove the Scheduled Task
- `--task-update`: update the Scheduled Task target/args
- `--task-status`: print `PRESENT` if the Scheduled Task exists
- `--help`: show usage

Examples:
- Run with custom config: `altrightclick --config "C:\Users\you\config.ini"`
- Install service with custom config on the command line: the installer will register the service to call `altrightclick --service --config <path>` automatically.

## Configuration
Default search order:
1) `<exe_dir>\config.ini`
2) `%APPDATA%\altrightclick\config.ini`

Keys (case-insensitive):
- `enabled=true|false` (default: true)
- `show_tray=true|false` (default: true)
- `modifier=ALT|CTRL|SHIFT|WIN` (default: ALT)
- `exit_key=ESC|F12` (default: ESC)
- `ignore_injected=true|false` (default: true) — ignore externally injected mouse events
- `click_time_ms=<uint>` (default: 250) — max press duration to translate click
- `move_radius_px=<int>` (default: 6) — max pointer movement radius to still translate as click
- `log_level=error|warn|info|debug` (default: info)
- `log_file=<path>` (default: empty; console only)

Example: see `config.example.ini:1`.

## Build (Windows)
Prerequisites:
- Visual Studio 2022 with “Desktop development with C++”
- CMake 3.21+

With CMake presets (recommended):
- x64: `cmake --preset windows-x64 && cmake --build --preset build-x64`
- ARM64: `cmake --preset windows-arm64 && cmake --build --preset build-arm64`

Manual CMake invocations:
- x64: `cmake -S . -B build/x64 -G "Visual Studio 17 2022" -A x64 && cmake --build build/x64 --config Release`
- ARM64: `cmake -S . -B build/arm64 -G "Visual Studio 17 2022" -A ARM64 && cmake --build build/arm64 --config Release`

Scripted builds:
- `scripts\compile.bat` builds both x64 and ARM64 from anywhere (resolves repo root automatically).

Output:
- Executable: `altrightclick` (under your chosen build directory/config)

## Development
- Structure
  - `src/main.cpp`: entrypoint and CLI parsing
  - `include/arc/*.h`, `src/*.cpp`: modules (`hook`, `app`, `config`, `tray`, `service`)
  - `scripts/`: helper scripts (e.g., `compile.bat`)
- Code style
  - C++17, UNICODE, warnings enabled (`/W4`)
- Build & run
  - Configure with a preset, build, then run `altrightclick` from `build/<arch>/<config>`
  - For a custom icon, update `src/tray.cpp` to load an `.ico` (or add a `.rc` resource and link it)

## System Tray Icon
The app shows a tray icon (default Windows application icon) with a context menu:
- Click Time +/−: adjust `click_time_ms` in 10ms steps (10–5000ms)
- Move Radius +/−: adjust `move_radius_px` in 1px steps (0–100px)
- Ignore Injected: toggle `ignore_injected`
- Save Settings: write current settings to the config file if available
- Open Config Folder: opens the directory containing the current config file
- Exit

Replace the icon by changing `src/tray.cpp` to load a custom `.ico` or by adding a resource script.

Tray window
- The tray owner window is a hidden tool window (`WS_EX_TOOLWINDOW` with `WS_POPUP`), not a visible overlapped window.
- It has no taskbar icon and exists only to receive notification area messages.

## Service Limitations (Important)
Global low-level hooks and input injection generally require an interactive user session. Due to Session 0 isolation, services cannot interact with the desktop. The provided service management commands are included, but hook behavior will not function as expected when run as a traditional service. For auto-start in a user session, prefer:
- A Startup folder shortcut
- A Scheduled Task (At logon) running in the user context

## License
This project is provided under the license in `license.md:1`.

## Structure (Reference)
- `src/main.cpp` — application entrypoint
- `include/arc/hook.h` + `src/hook.cpp` — mouse hook (Alt+Left -> Right)
- `include/arc/app.h` + `src/app.cpp` — message loop (custom exit key)
- `include/arc/config.h` + `src/config.cpp` — INI-style configuration
- `include/arc/tray.h` + `src/tray.cpp` — tray icon and menu
- `include/arc/service.h` + `src/service.cpp` — service management and runtime
