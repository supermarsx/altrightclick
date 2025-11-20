@echo off
setlocal

rem =============================================================================
rem AltRightClick — Friendly Build Helper
rem
rem This script configures and builds the project for multiple architectures
rem (x64 and ARM64) using CMake and the Visual Studio 2022 generator. It is
rem intended to be run on Windows where CMake and the Visual Studio build tools
rem are available (Developer Command Prompt or an environment with the tools
rem on PATH).
rem
rem What this script does:
rem  - Generates out-of-source CMake build trees for x64 and ARM64
rem  - Builds the Release configuration for each architecture
rem  - Prints friendly step headers, timestamps, and clear error hints when
rem    something goes wrong so you can quickly reproduce failures.
rem
rem Usage:
rem   Call this script from the repository's scripts directory, or run it from
rem   any shell; it will resolve the repository root relative to the script.
rem
rem Tip: If a build fails, re-run the failing cmake or cmake --build command
rem manually in a Developer Command Prompt to see full diagnostics.
rem =============================================================================

rem -----------------------------------------------------------------------------
rem AltRightClick - Build helper script with improved logging
rem -----------------------------------------------------------------------------

rem Resolve repository root relative to the script's directory (one level up)
set ROOT=%~dp0..

rem Record start time
set START_DATE=%DATE%
set START_TIME=%TIME%
echo -----------------------------------------------------------------------------
echo AltRightClick build helper
echo Started: %START_DATE% %START_TIME%
echo Repository root: %ROOT%
echo -----------------------------------------------------------------------------

rem Helper to display step header
set STEP=

rem -----------------------------------------------------------------------------
rem Configure and build x64
rem -----------------------------------------------------------------------------
set STEP=Configure x64
echo [STEP] %STEP% - Generating Visual Studio solution for x64...
cmake -S "%ROOT%" -B "%ROOT%\build\x64" -G "Visual Studio 17 2022" -A x64 || goto :error
echo [OK] %STEP%

set STEP=Build x64
echo [STEP] %STEP% - Building Release configuration (x64)...
cmake --build "%ROOT%\build\x64" --config Release || goto :error
echo [OK] %STEP%

echo.

rem -----------------------------------------------------------------------------
rem Configure and build ARM64
rem -----------------------------------------------------------------------------
set STEP=Configure ARM64
echo [STEP] %STEP% - Generating Visual Studio solution for ARM64...
cmake -S "%ROOT%" -B "%ROOT%\build\arm64" -G "Visual Studio 17 2022" -A ARM64 || goto :error
echo [OK] %STEP%

set STEP=Build ARM64
echo [STEP] %STEP% - Building Release configuration (ARM64)...
cmake --build "%ROOT%\build\arm64" --config Release || goto :error
echo [OK] %STEP%

rem Record end time
set END_DATE=%DATE%
set END_TIME=%TIME%
echo -----------------------------------------------------------------------------
echo Build completed successfully.
echo Started: %START_DATE% %START_TIME%
echo Finished: %END_DATE% %END_TIME%
echo -----------------------------------------------------------------------------
endlocal
exit /b 0

:error
echo -----------------------------------------------------------------------------
echo [FAILED] Step: %STEP%
echo ERRORLEVEL=%ERRORLEVEL%
echo To reproduce the failure, run the failing command above from a Developer Command Prompt
echo and inspect the CMake/Visual Studio output for errors.
echo -----------------------------------------------------------------------------
endlocal
exit /b 1

rem The script records START_DATE and START_TIME for a simple timing summary.
rem The STEP variable holds a short description of the current operation so the
rem error handler can report which step failed. These values are purely for
rem developer convenience and do not affect build behavior.

rem -----------------------------------------------------------------------------
rem Error handler: prints failed step and error context.
rem
rem ERRORLEVEL will contain the exit code of the failing command. For CMake
rem and MSVC builds, non-zero typically indicates configuration or compile
rem errors; run the failing command manually in a Developer Command Prompt to
rem view full diagnostics. The messages below are intentionally brief.
