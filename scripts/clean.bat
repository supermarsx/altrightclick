@echo off
setlocal enabledelayedexpansion

rem -----------------------------------------------------------------------------
rem AltRightClick - Cleanup script (improved logging and error reporting)
rem
rem This script removes common build artifacts, temporary files, and generated
rem outputs created during development and CI runs. It resolves the repository
rem root relative to this script's location and reports success/failure for each
rem deletion step. The script exits with non-zero status if any deletion fails.
rem -----------------------------------------------------------------------------

rem Resolve repo root relative to this script's directory (one level up)
set ROOT=%~dp0..

rem Record start time
set START_DATE=%DATE%
set START_TIME=%TIME%
set FAILED=0

echo -----------------------------------------------------------------------------
echo AltRightClick cleanup
echo Started: %START_DATE% %START_TIME%
echo Repository root: %ROOT%
echo -----------------------------------------------------------------------------

rem -----------------------------------------------------------------------------
rem Remove common build output directories. These are safe to remove and will
rem be recreated by CMake / build tools when needed.
rem -----------------------------------------------------------------------------

echo Removing build directories...
for %%D in ("build" "x64" "arm64" ".vs") do (
  if exist "%ROOT%\%%~D" (
    echo   - %%~D ...
    rmdir /s /q "%ROOT%\%%~D"
    if exist "%ROOT%\%%~D" (
      echo     [FAILED] could not remove %ROOT%\%%~D
      set FAILED=1
    ) else (
      echo     [OK]
    )
  ) else (
    echo   - %%~D (not present)
  )
)

rem -----------------------------------------------------------------------------
rem Coverage and badge artefacts produced by CI/tests.
rem -----------------------------------------------------------------------------

echo Removing coverage artifacts...
if exist "%ROOT%\coverage.xml" (
  del /q "%ROOT%\coverage.xml"
  if exist "%ROOT%\coverage.xml" (
    echo   [FAILED] coverage.xml could not be deleted
    set FAILED=1
  ) else (
    echo   [OK] coverage.xml removed
  )
) else (
  echo   coverage.xml (not present)
)

if exist "%ROOT%\badges\coverage.svg" (
  del /q "%ROOT%\badges\coverage.svg"
  if exist "%ROOT%\badges\coverage.svg" (
    echo   [FAILED] badges\coverage.svg could not be deleted
    set FAILED=1
  ) else (
    echo   [OK] badges\coverage.svg removed
  )
) else (
  echo   badges\coverage.svg (not present)
)

rem -----------------------------------------------------------------------------
rem CMake generated files that may live at the repo root. Removing these allows
rem a fresh out-of-source configuration on next build.
rem -----------------------------------------------------------------------------

echo Removing CMake droppings in root (if any)...
if exist "%ROOT%\CMakeFiles" (
  rmdir /s /q "%ROOT%\CMakeFiles"
  if exist "%ROOT%\CMakeFiles" (
    echo   [FAILED] CMakeFiles could not be removed
    set FAILED=1
  ) else (
    echo   [OK] CMakeFiles removed
  )
) else (
  echo   CMakeFiles (not present)
)

if exist "%ROOT%\CMakeCache.txt" (
  del /q "%ROOT%\CMakeCache.txt"
  if exist "%ROOT%\CMakeCache.txt" (
    echo   [FAILED] CMakeCache.txt could not be deleted
    set FAILED=1
  ) else (
    echo   [OK] CMakeCache.txt removed
  )
) else (
  echo   CMakeCache.txt (not present)
)

if exist "%ROOT%\cmake_install.cmake" (
  del /q "%ROOT%\cmake_install.cmake"
  if exist "%ROOT%\cmake_install.cmake" (
    echo   [FAILED] cmake_install.cmake could not be deleted
    set FAILED=1
  ) else (
    echo   [OK] cmake_install.cmake removed
  )
) else (
  echo   cmake_install.cmake (not present)
)

if exist "%ROOT%\CTestTestfile.cmake" (
  del /q "%ROOT%\CTestTestfile.cmake"
  if exist "%ROOT%\CTestTestfile.cmake" (
    echo   [FAILED] CTestTestfile.cmake could not be deleted
    set FAILED=1
  ) else (
    echo   [OK] CTestTestfile.cmake removed
  )
) else (
  echo   CTestTestfile.cmake (not present)
)

if exist "%ROOT%\DartConfiguration.tcl" (
  del /q "%ROOT%\DartConfiguration.tcl"
  if exist "%ROOT%\DartConfiguration.tcl" (
    echo   [FAILED] DartConfiguration.tcl could not be deleted
    set FAILED=1
  ) else (
    echo   [OK] DartConfiguration.tcl removed
  )
) else (
  echo   DartConfiguration.tcl (not present)
)

rem -----------------------------------------------------------------------------
rem Misc temporary and log files generated during development.
rem The config_*.ini pattern is used by local testing; deletion is best-effort.
rem -----------------------------------------------------------------------------

echo Removing temp and log files...
if exist "%ROOT%\error.log" (
  del /q "%ROOT%\error.log"
  if exist "%ROOT%\error.log" (
    echo   [FAILED] error.log could not be deleted
    set FAILED=1
  ) else (
    echo   [OK] error.log removed
  )
) else (
  echo   error.log (not present)
)
nrem Attempt to delete config_*.ini files (best effort)
echo   Deleting config_*.ini (if present)...ndel /q "%ROOT%\config_*.ini" 2>nulnif exist "%ROOT%\config_*.ini" (n  echo   [FAILED] Some config_*.ini files could not be deletedn  set FAILED=1n) else (necho   [OK] config_*.ini removed (or none present)n)

rem Final summary
set END_DATE=%DATE%
set END_TIME=%TIME%
echo -----------------------------------------------------------------------------necho Finished: %END_DATE% %END_TIME%
if %FAILED%==1 (  echo Some cleanup operations failed. Inspect the messages above.  endlocal  exit /b 1) else (  echo All requested cleanup operations completed successfully.  endlocal  exit /b 0)

