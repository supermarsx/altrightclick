#pragma once

#include <string>

namespace arc {

// Service management API
bool service_install(const std::wstring& name, const std::wstring& display_name, const std::wstring& bin_path_with_args);
bool service_uninstall(const std::wstring& name);
bool service_start(const std::wstring& name);
bool service_stop(const std::wstring& name);

// Entry point to run as a Windows service. Blocks until stop.
int service_run(const std::wstring& name);

} // namespace arc
