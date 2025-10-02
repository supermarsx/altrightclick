@echo off
setlocal

rem Resolve repo root relative to this script's directory
set ROOT=%~dp0..

echo Building x64 with CMake...
cmake -S "%ROOT%" -B "%ROOT%\build\x64" -G "Visual Studio 17 2022" -A x64 || goto :error
cmake --build "%ROOT%\build\x64" --config Release || goto :error

echo.
echo Building ARM64 with CMake...
cmake -S "%ROOT%" -B "%ROOT%\build\arm64" -G "Visual Studio 17 2022" -A ARM64 || goto :error
cmake --build "%ROOT%\build\arm64" --config Release || goto :error

echo Done.
endlocal
exit /b 0

:error
echo Build failed.
endlocal
exit /b 1
