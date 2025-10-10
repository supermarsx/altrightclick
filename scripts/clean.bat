@echo off
setlocal

rem Resolve repo root relative to this script's directory
set ROOT=%~dp0..

echo === AltRightClick Cleanup ===

echo Removing build directories...
for %%D in ("build" "x64" "arm64" ".vs") do (
  if exist "%ROOT%\%%~D" (
    echo   - %%~D
    rmdir /s /q "%ROOT%\%%~D"
  )
)

echo Removing coverage artifacts...
if exist "%ROOT%\coverage.xml" del /q "%ROOT%\coverage.xml"
if exist "%ROOT%\badges\coverage.svg" del /q "%ROOT%\badges\coverage.svg"

echo Removing CMake droppings in root (if any)...
if exist "%ROOT%\CMakeFiles" rmdir /s /q "%ROOT%\CMakeFiles"
if exist "%ROOT%\CMakeCache.txt" del /q "%ROOT%\CMakeCache.txt"
if exist "%ROOT%\cmake_install.cmake" del /q "%ROOT%\cmake_install.cmake"
if exist "%ROOT%\CTestTestfile.cmake" del /q "%ROOT%\CTestTestfile.cmake"
if exist "%ROOT%\DartConfiguration.tcl" del /q "%ROOT%\DartConfiguration.tcl"

echo Removing temp and log files...
if exist "%ROOT%\error.log" del /q "%ROOT%\error.log"
del /q "%ROOT%\config_*.ini" 2>nul

echo Done.
endlocal
exit /b 0

