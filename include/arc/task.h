#pragma once

#include <string>

namespace arc {

// Scheduled Task management using schtasks.exe
bool task_install(const std::wstring &name, const std::wstring &exe_with_args, bool highest = true);
bool task_uninstall(const std::wstring &name);
bool task_exists(const std::wstring &name);
bool task_update(const std::wstring &name, const std::wstring &exe_with_args, bool highest = true);

}  // namespace arc
