/**
 * @file config_test.cpp
 * @brief Regression tests for arc::config load/save helpers.
 */

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "arc/config.h"

using arc::config::Config;

/**
 * @brief Write a temporary config file to exercise parser behavior.
 *
 * @param name Relative file name used for writing.
 * @param content File content.
 * @return Path of the file that was written.
 */
static std::string write_temp_file(const std::string &name, const std::string &content) {
    // Write to current working directory used by ctest
    std::string path = name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
    out.close();
    return path;
}

/**
 * @brief Minimal assertion helper printing failures to stderr.
 *
 * @param cond Condition that must hold.
 * @param msg Description printed on failure.
 */
static void expect(bool cond, const char *msg) {
    if (!cond) {
        std::fprintf(stderr, "[FAIL] %s\n", msg);
        std::exit(1);
    }
}

/** @brief Entry point for config load/save regression tests. */
int main() {
    // Defaults
    {
        Config defaults = arc::config::load("nonexistent.ini");
        expect(defaults.enabled == true, "enabled default true");
        expect(defaults.show_tray == true, "show_tray default true");
        expect(defaults.modifier_vk != 0u, "modifier default non-zero");
        expect(defaults.exit_vk != 0u, "exit default non-zero");
        expect(defaults.ignore_injected == true, "ignore_injected default true");
        expect(defaults.click_time_ms == 250u, "click_time_ms default 250");
        expect(defaults.move_radius_px == 6, "move_radius_px default 6");
    }

    // Parse a custom config
    {
        const char *cfg = "enabled=false\n"
                          "show_tray=false\n"
                          "modifier=ALT+CTRL\n"
                          "exit_key=F12\n"
                          "ignore_injected=false\n"
                          "click_time_ms=333\n"
                          "move_radius_px=9\n"
                          "trigger=X2\n"
                          "log_level=debug\n"
                          "watch_config=true\n";
        std::string path = write_temp_file("config_test.ini", cfg);
        Config c = arc::config::load(path);
        expect(c.enabled == false, "enabled parsed false");
        expect(c.show_tray == false, "show_tray parsed false");
        expect(!c.modifier_combo_vks.empty(), "modifier combo parsed");
        expect(c.modifier_combo_vks.size() == 2, "modifier combo size 2");
        expect(c.exit_vk != 0u, "exit key parsed");
        expect(c.ignore_injected == false, "ignore_injected parsed false");
        expect(c.click_time_ms == 333u, "click_time_ms parsed 333");
        expect(c.move_radius_px == 9, "move_radius_px parsed 9");
        expect(c.trigger == Config::Trigger::X2, "trigger parsed X2");
        expect(c.log_level == std::string("debug"), "log_level parsed debug");
        expect(c.watch_config == true, "watch_config parsed true");
        std::remove(path.c_str());
    }

    // Round-trip save and load
    {
        Config w;
        w.enabled = false;
        w.show_tray = true;
        w.modifier_combo_vks = {0x12 /*ALT*/, 0x11 /*CTRL*/};
        w.modifier_vk = 0x12;
        w.exit_vk = 0x7B;  // F12
        w.ignore_injected = true;
        w.click_time_ms = 123;
        w.move_radius_px = 7;
        w.trigger = Config::Trigger::Middle;
        w.log_level = "warn";
        w.watch_config = false;
        std::string out = "config_roundtrip.ini";
        expect(arc::config::save(out, w), "save_config success");
        Config r = arc::config::load(out);
        expect(r.enabled == w.enabled, "roundtrip enabled");
        expect(r.show_tray == w.show_tray, "roundtrip show_tray");
        expect(r.modifier_vk == w.modifier_vk, "roundtrip modifier_vk representative");
        expect(r.exit_vk == w.exit_vk, "roundtrip exit_vk");
        expect(r.ignore_injected == w.ignore_injected, "roundtrip ignore_injected");
        expect(r.click_time_ms == w.click_time_ms, "roundtrip click_time_ms");
        expect(r.move_radius_px == w.move_radius_px, "roundtrip move_radius_px");
        expect(r.trigger == w.trigger, "roundtrip trigger");
        expect(r.log_level == w.log_level, "roundtrip log_level");
        std::remove(out.c_str());
    }

    // default_path prefers exe_dir\\config.ini when present
    {
        std::string exe_dir_cfg = arc::config::default_path().u8string();
        // Ensure file exists at exe dir by writing to resolved path
        std::ofstream f(exe_dir_cfg, std::ios::app);
        f << "# temp\n";
        f.close();
        std::string pick = arc::config::default_path().u8string();
        expect(pick == exe_dir_cfg, "default_path prefers exe dir when present");
        std::remove(exe_dir_cfg.c_str());
    }

    std::printf("[OK] config tests passed\n");
    return 0;
}
