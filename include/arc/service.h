/**
 * @file service.h
 * @brief Windows Service management helpers for install/start/stop/query and run.
 *
 * The app can be deployed as a service. These helpers wrap common SCM
 * operations and provide an entry point to run inside the Service Control
 * Manager context.
 */
#pragma once

#include <string>

namespace arc { namespace service {

/**
 * @brief Installs a Windows service.
 *
 * @param name Internal service name (unique key in SCM).
 * @param display_name Human-friendly name shown in Service Manager.
 * @param bin_path_with_args Command line: fully quoted exe path plus args.
 * @return true on success.
 */
bool install(const std::wstring &name, const std::wstring &display_name,
             const std::wstring &bin_path_with_args);

/**
 * @brief Uninstalls a Windows service.
 * @param name Internal service name.
 * @return true on success.
 */
bool uninstall(const std::wstring &name);

/** @brief Starts a service by internal name. */
bool start(const std::wstring &name);

/** @brief Stops a service by internal name. */
bool stop(const std::wstring &name);

/**
 * @brief Queries RUNNING status from SCM.
 * @param name Internal service name.
 * @return true if the service is RUNNING.
 */
bool is_running(const std::wstring &name);

/**
 * @brief Enters the service main loop via the SCM dispatcher.
 *
 * Blocks until the SCM signals stop/shutdown. The service's SvcMain installs
 * the hook worker and then pumps a message loop.
 *
 * @param name Internal service name to register the main function under.
 * @return Process exit code (0 on success).
 */
int run(const std::wstring &name);

}  // namespace service

}  // namespace arc
