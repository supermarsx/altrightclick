#include "arc/log.h"

#include <windows.h>

#include <chrono>
#include <ctime>
#include <deque>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <cstdio>

namespace {
std::mutex g_logMutex;
arc::LogLevel g_level = arc::LogLevel::Info;
std::ofstream g_logFile;
bool g_logToFile = false;
bool g_async = false;
std::thread g_thread;
std::condition_variable g_cv;
bool g_stop = false;
std::deque<std::string> g_queue;

std::string level_name(arc::LogLevel lvl) {
    switch (lvl) {
    case arc::LogLevel::Error:
        return "ERROR";
    case arc::LogLevel::Warn:
        return "WARN";
    case arc::LogLevel::Info:
        return "INFO";
    case arc::LogLevel::Debug:
        return "DEBUG";
    }
    return "INFO";
}

std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}
}  // namespace

namespace arc {

void log_set_level(LogLevel lvl) { g_level = lvl; }

void log_set_level_by_name(const std::string& name) {
    std::string n = name;
    for (auto& c : n) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    if (n == "error")
        g_level = LogLevel::Error;
    else if (n == "warn" || n == "warning")
        g_level = LogLevel::Warn;
    else if (n == "info")
        g_level = LogLevel::Info;
    else if (n == "debug")
        g_level = LogLevel::Debug;
}

void log_set_file(const std::string& path) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_logFile.is_open()) g_logFile.close();
    g_logToFile = false;
    if (!path.empty()) {
        g_logFile.open(path, std::ios::app);
        if (g_logFile.is_open()) g_logToFile = true;
    }
}

std::string last_error_message(uint32_t err) {
    wchar_t* buf = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = FormatMessageW(flags, nullptr, static_cast<DWORD>(err), 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring wmsg = (len ? std::wstring(buf, len) : L"Unknown error");
    if (buf) LocalFree(buf);
    int bytes = WideCharToMultiByte(CP_UTF8, 0, wmsg.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(bytes > 0 ? bytes - 1 : 0, '\0');
    if (bytes > 0) WideCharToMultiByte(CP_UTF8, 0, wmsg.c_str(), -1, out.data(), bytes, nullptr, nullptr);
    return out;
}

void log_start_async() {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_async) return;
    g_stop = false;
    g_async = true;
    g_thread = std::thread([]() {
        std::unique_lock<std::mutex> lk(g_logMutex);
        while (!g_stop || !g_queue.empty()) {
            if (g_queue.empty()) {
                g_cv.wait(lk, [] { return g_stop || !g_queue.empty(); });
                if (g_stop && g_queue.empty()) break;
            }
            auto s = std::move(g_queue.front());
            g_queue.pop_front();
            // unlock during IO
            lk.unlock();
            // write to console
            fwrite(s.data(), 1, s.size(), stdout);
            fflush(stdout);
            if (g_logToFile) {
                g_logFile << s;
                g_logFile.flush();
            }
            lk.lock();
        }
    });
}

void log_stop_async() {
    std::unique_lock<std::mutex> lk(g_logMutex);
    if (!g_async) return;
    g_stop = true;
    g_cv.notify_all();
    lk.unlock();
    if (g_thread.joinable()) g_thread.join();
    lk.lock();
    g_async = false;
}

void log_msg(LogLevel lvl, const std::string& msg) {
    if (static_cast<int>(lvl) > static_cast<int>(g_level)) return;
    std::ostringstream line;
    line << '[' << timestamp() << "] [" << level_name(lvl) << "] " << msg << '\n';
    auto s = line.str();
    if (g_async) {
        std::lock_guard<std::mutex> lk(g_logMutex);
        g_queue.emplace_back(std::move(s));
        g_cv.notify_one();
    } else {
        FILE* stream = (lvl == LogLevel::Error || lvl == LogLevel::Warn) ? stderr : stdout;
        fwrite(s.data(), 1, s.size(), stream);
        fflush(stream);
        if (g_logToFile) {
            std::lock_guard<std::mutex> lk(g_logMutex);
            g_logFile << s;
            g_logFile.flush();
        }
    }
}

}  // namespace arc
