#include <windows.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "arc/config.h"

using arc::Config;

static std::string write_tmp(const std::string &name, const std::string &content) {
    std::ofstream out(name, std::ios::binary | std::ios::trunc);
    out << content;
    out.close();
    return name;
}

static void expect(bool cond, const char *msg) {
    if (!cond) {
        std::fprintf(stderr, "[FAIL] %s\n", msg);
        std::exit(1);
    }
}

int main() {
    // Case-insensitive keys/values, whitespace, comments
    {
        const char *cfg = "; comment\n"
                          " # another\n"
                          "  Enabled = FaLsE  \n"
                          "  SHOW_TRAY = true\n"
                          "  modifier = alt , ctrl \n"
                          "  exit_key = esc \n"
                          "  trigger = mbutton \n"
                          "  click_time_ms = 99999  \n"  // ignored (out of range)
                          "  move_radius_px = -2 \n";  // ignored (out of range), stays default
        std::string path = write_tmp("config_edge_case.ini", cfg);
        Config c = arc::load_config(path);
        expect(c.enabled == false, "enabled parsed false (case-insensitive)");
        expect(c.show_tray == true, "show_tray parsed true");
        expect(c.modifier_combo_vks.size() == 2, "modifier combo via commas and whitespace");
        expect(c.exit_vk != 0u, "exit esc parsed");
        expect(c.trigger == Config::Trigger::Middle, "trigger synonyms parsed (mbutton->Middle)");
        expect(c.click_time_ms == 250u, "out-of-range click_time_ms ignored -> default 250");
        expect(c.move_radius_px == 6, "negative radius ignored -> default 6");
        std::remove(path.c_str());
    }

    // Unknown modifier should keep default; unknown tokens in combo are ignored
    {
        const char *cfg = "modifier=UNKNOWN\n";
        std::string path = write_tmp("config_unknown_modifier.ini", cfg);
        Config c = arc::load_config(path);
        expect(c.modifier_vk != 0u, "unknown modifier leaves default non-zero");
        std::remove(path.c_str());
    }

    // Combo list via '+'
    {
        const char *cfg = "modifier=ALT+CTRL+SHIFT\n";
        std::string path = write_tmp("config_combo_plus.ini", cfg);
        Config c = arc::load_config(path);
        expect(c.modifier_combo_vks.size() >= 3, "modifier combo via plus parsed");
        std::remove(path.c_str());
    }

    // Save includes recomposed combo with '+'
    {
        Config w;
        w.modifier_combo_vks = {VK_MENU, VK_CONTROL};
        w.modifier_vk = VK_MENU;
        std::string out = "config_save_format.ini";
        expect(arc::save_config(out, w), "save_config success for combo");
        std::ifstream in(out);
        std::string txt((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        expect(txt.find("modifier=ALT+CTRL") != std::string::npos, "saved modifier combo contains ALT+CTRL");
        std::remove(out.c_str());
    }

    // Trigger synonyms X buttons
    {
        const char *cfg = "trigger=xbutton1\n";
        std::string p1 = write_tmp("config_x1.ini", cfg);
        Config c1 = arc::load_config(p1);
        expect(c1.trigger == Config::Trigger::X1, "xbutton1 -> X1");
        std::remove(p1.c_str());

        const char *cfg2 = "trigger=X2\n";
        std::string p2 = write_tmp("config_x2.ini", cfg2);
        Config c2 = arc::load_config(p2);
        expect(c2.trigger == Config::Trigger::X2, "X2 parsed");
        std::remove(p2.c_str());
    }

    std::printf("[OK] config edge tests passed\n");
    return 0;
}
