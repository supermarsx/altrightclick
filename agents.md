# Repository Guidelines

## Project Structure & Module Organization
- `src/`: C++ sources (app, hook, tray, service, config, log, task, singleton).
- `include/arc/`: Public headers for modules (pragma once, namespace `arc`).
- `res/`: App resources (icon, `.rc`).
- `scripts/`: Helper scripts (local dev/build tasks).
- `scoop/`: Scoop manifest (`altrightclick.json`).
- `.github/workflows/`: CI and release pipelines.
- `config.example.ini`: Example runtime configuration.

## Build, Test, and Development Commands
- Configure (Windows x64):
  - `cmake -S . -B build/x64 -G "Visual Studio 17 2022" -A x64`
- Build (Release):
  - `cmake --build build/x64 --config Release -- /m`
- ARM64: use `-A ARM64` and `build/arm64` similarly.
- Run tests (if present):
  - `cd build/x64 && ctest -C Release --output-on-failure`
- Lint: `cpplint $(git ls-files 'src/*.cpp' 'include/**/*.h')`
- Format: `clang-format -i $(git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp')`

## Coding Style & Naming Conventions
- C++17; headers in `include/arc/`, sources in `src/` with matching names.
- Files: lower_snake_case; Types: PascalCase; functions: lower_snake_case; constants: ALL_CAPS if needed.
- Line length ≤ 120; prefer `static_cast<>`; correct include order (project → C → C++ → other).
- Use `.clang-format` and keep cpplint clean (no trailing-space issues; two spaces before end-of-line comments).

## Testing Guidelines
- Framework: CTest via CMake. Place tests under `tests/` and register with `add_test`.
- Name tests `*_test.cpp` and make them deterministic. Ensure `ctest` passes in CI.

## Commit & Pull Request Guidelines
- Conventional Commits: `feat:`, `fix:`, `docs:`, `ci:`, `build:`, `style:`, `refactor:`, `test:`, `chore:`.
- PRs must: describe the change, link issues, include commands to reproduce, and pass CI.
- Rolling release runs only after CI success; changes that affect artifacts must update CI and Scoop.

## Security & Configuration Tips
- Config lives at `%APPDATA%\\altrightclick\\config.ini` (auto-created) or alongside the executable. Generate with `--generate-config`.
- Service/scheduled-task commands require elevation; validate before running. Logging level and behavior are configurable via CLI or config.

