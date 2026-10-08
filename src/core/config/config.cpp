#include "core/config/config.hpp"
#include "core/common/util.hpp"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <algorithm>
#include <cctype>

namespace miquland {

namespace fs = std::filesystem;

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n\"'");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n\"'");
    return str.substr(first, (last - first + 1));
}

Config& Config::get() {
    static Config instance;
    return instance;
}

Config::Config() {
    set_defaults();
    load();
}

void Config::set_defaults() {
    m_keybindings.clear();
    m_gesture_bindings.clear();
    m_window_rules.clear();
    m_monitor_rules.clear();
    m_exec_commands.clear();
    m_exec_once_commands.clear();
    m_blurred_layers.clear();
    m_plugins.clear();

    const char* env_term = getenv("TERMINAL");
    std::string term = (env_term && *env_term) ? env_term : "kitty || foot || alacritty || wezterm || weston-terminal || xterm";
    m_terminal = term;

    m_tap_to_click = true;
    m_natural_scroll = false;
    m_dwt = true;
    m_accel_speed = 0.0;
    m_accel_profile = "adaptive";
    m_touchscreen_output = "";
    m_touchpad_workspace_swipe_multiplier = 1.0;
    m_touchpad_swipe_threshold = 50.0;
    m_touchscreen_workspace_swipe_multiplier = 1.0;
    m_touchscreen_swipe_threshold = 100.0;
    m_workspace_swipe_cancel_ratio = 0.30;
    m_workspace_swipe_min_speed_to_force = 0.3;
    m_workspace_swipe_edge_resistance = 0.15;
    m_kb_layout = "us";
    m_kb_variant = "";
    m_kb_options = "";
    m_kb_model = "";
    m_repeat_rate = 25;
    m_repeat_delay = 600;
    m_window_opacity_active = 1.0f;
    m_window_opacity_inactive = 0.85f;
    m_animations_enabled = true;
    m_workspace_animations_enabled = true;
    m_workspace_animation_duration_ms = 200;
    m_workspace_animation_curve = "smooth_out";
    m_window_animations_enabled = true;
    m_window_open_animation_enabled = true;
    m_window_close_animation_enabled = true;
    m_window_animation_duration_ms = 180;
    m_window_animation_open_duration_ms = 180;
    m_window_animation_close_duration_ms = 140;
    m_window_animation_curve = "default";
    m_window_animation_open_curve = "default";
    m_window_animation_close_curve = "smooth_out";
    m_window_animation_open_scale = 0.85;
    m_window_animation_close_scale = 0.85;
    m_window_open_fade_enabled = true;
    m_window_close_fade_enabled = true;
    m_window_animation_fade_in_duration_ms = 0;
    m_window_animation_fade_out_duration_ms = 0;
    m_window_animation_fade_in_curve = "";
    m_window_animation_fade_out_curve = "";
    m_layer_animations_enabled = true;
    m_layer_animation_duration_ms = 200;
    m_layer_animation_curve = "ease_out_cubic";
    m_layer_animation_popin_scale = 0.90;
    m_layer_rules.clear();
    m_blur_enabled = true;
    m_blur_radius = 5;
    m_blur_num_passes = 3;
    m_blur_noise = 0.02f;
    m_blur_brightness = 0.9f;
    m_blur_contrast = 0.9f;
    m_blur_saturation = 1.1f;

    m_window_border_color_active = "#0066ff";
    m_window_border_color_inactive = "#99c2ff";
    m_border_paint_active = BorderPaint::parse(m_window_border_color_active);
    m_border_paint_inactive = BorderPaint::parse(m_window_border_color_inactive);
    m_window_border_shading = false;
    m_window_border_shading_strength = 0.35f;

    m_shadow_enabled = true;
    m_shadow_blur_sigma = 12.0f;
    m_shadow_color_active = "#00000066";
    m_shadow_color_inactive = "#00000044";
    m_shadow_offset_x = 0;
    m_shadow_offset_y = 4;

    m_window_border_width = 2;
    m_window_border_radius = 10;
    m_space_between_windows = 8;
    m_screen_edge_padding = 12;
    m_layout_mode = LayoutMode::Spiral;
    m_resize_on_border = true;
    m_border_grab_area = 6;

    m_smart_gaps = false;
    m_allow_tearing = false;
    m_xwayland_force_zero_scaling = false;
    m_cursor_theme = "";
    m_cursor_size = 24;
    m_default_split_ratio = 0.5;
    m_workspace_cycle = false;
}

void Config::add_or_update_binding(uint32_t mods, xkb_keysym_t sym, const std::string& action, const std::string& combo) {
    xkb_keysym_t norm_sym = (sym >= XKB_KEY_A && sym <= XKB_KEY_Z) ? (sym - XKB_KEY_A + XKB_KEY_a) : sym;

    for (auto& kb : m_keybindings) {
        xkb_keysym_t kb_norm = (kb.keysym >= XKB_KEY_A && kb.keysym <= XKB_KEY_Z) ? (kb.keysym - XKB_KEY_A + XKB_KEY_a) : kb.keysym;
        if (kb.modifiers == mods && kb_norm == norm_sym) {
            kb.action = action;
            kb.combo_str = combo;
            return;
        }
    }
    m_keybindings.push_back({ mods, norm_sym, action, combo });
}

std::string Config::find_gesture_action(const std::string& pattern) const {
    std::string lower_pat = pattern;
    std::transform(lower_pat.begin(), lower_pat.end(), lower_pat.begin(), ::tolower);

    for (const auto& g : m_gesture_bindings) {
        std::string cur_pat = g.pattern;
        std::transform(cur_pat.begin(), cur_pat.end(), cur_pat.begin(), ::tolower);
        if (cur_pat == lower_pat) {
            return g.action;
        }
    }
    return "";
}

bool Config::has_gesture_for_fingers(int fingers) const {
    std::string prefix = "swipe:" + std::to_string(fingers) + ":";
    for (const auto& g : m_gesture_bindings) {
        std::string cur_pat = g.pattern;
        std::transform(cur_pat.begin(), cur_pat.end(), cur_pat.begin(), ::tolower);
        if (cur_pat.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}

void Config::add_or_update_gesture_binding(const std::string& pattern, const std::string& action) {
    std::string lower_pat = pattern;
    std::transform(lower_pat.begin(), lower_pat.end(), lower_pat.begin(), ::tolower);

    for (auto& g : m_gesture_bindings) {
        std::string cur_pat = g.pattern;
        std::transform(cur_pat.begin(), cur_pat.end(), cur_pat.begin(), ::tolower);
        if (cur_pat == lower_pat) {
            g.action = action;
            return;
        }
    }
    m_gesture_bindings.push_back({ pattern, action });
}

static bool matches_target(const std::string& pattern, const std::string& value) {
    if (pattern.empty() || value.empty()) return false;
    std::string l_pat = pattern;
    std::string l_val = value;
    std::transform(l_pat.begin(), l_pat.end(), l_pat.begin(), ::tolower);
    std::transform(l_val.begin(), l_val.end(), l_val.begin(), ::tolower);

    if (l_pat.rfind("class:", 0) == 0) {
        l_pat = l_pat.substr(6);
    } else if (l_pat.rfind("title:", 0) == 0) {
        l_pat = l_pat.substr(6);
    }

    if (l_pat.empty()) return false;
    return (l_val.find(l_pat) != std::string::npos);
}

bool Config::should_float(const std::string& app_id, const std::string& title) const {
    for (const auto& r : m_window_rules) {
        if (r.rule == "float") {
            if (matches_target(r.target, app_id) || matches_target(r.target, title)) {
                return true;
            }
        }
    }
    return false;
}

bool Config::should_tear(const std::string& app_id, const std::string& title) const {
    if (!m_allow_tearing) return false;
    for (const auto& r : m_window_rules) {
        if ((r.rule == "immediate" || r.rule == "tearing") &&
            (matches_target(r.target, app_id) || matches_target(r.target, title))) {
            return (r.extra.empty() || r.extra == "true" || r.extra == "1" || r.extra == "yes");
        }
    }
    return false;
}

int Config::get_target_workspace(const std::string& app_id, const std::string& title) const {
    for (const auto& r : m_window_rules) {
        if (r.rule == "workspace") {
            if (matches_target(r.target, app_id) || matches_target(r.target, title)) {
                try {
                    return std::stoi(r.extra);
                } catch (...) {}
            }
        }
    }
    return 0;
}

float Config::get_rule_opacity(const std::string& app_id, const std::string& title, float default_val) const {
    for (const auto& r : m_window_rules) {
        if (r.rule == "opacity") {
            if (matches_target(r.target, app_id) || matches_target(r.target, title)) {
                try {
                    return std::clamp(std::stof(r.extra), 0.0f, 1.0f);
                } catch (...) {}
            }
        }
    }
    return default_val;
}

const MonitorRule* Config::find_monitor_rule(const std::string& name) const {
    const MonitorRule* fallback = nullptr;
    for (const auto& r : m_monitor_rules) {
        if (r.name == name) {
            return &r;
        }
        if ((r.name.empty() || r.name == "*") && !fallback) {
            fallback = &r;
        }
    }
    return fallback;
}

bool Config::is_layer_blur_enabled(const std::string& ns) const {
    if (ns.empty()) return false;
    std::string lower_ns = ns;
    std::transform(lower_ns.begin(), lower_ns.end(), lower_ns.begin(), ::tolower);

    for (const auto& layer : m_blurred_layers) {
        std::string lower_layer = layer;
        std::transform(lower_layer.begin(), lower_layer.end(), lower_layer.begin(), ::tolower);
        if (lower_layer == lower_ns) {
            return true;
        }
    }
    return false;
}

void Config::add_blurred_layer(const std::string& ns) {
    if (ns.empty()) return;
    if (!is_layer_blur_enabled(ns)) {
        m_blurred_layers.push_back(ns);
    }
}

Config::LayerRule Config::get_layer_rule(const std::string& ns) const {
    if (ns.empty()) return LayerRule{};
    std::string lower_ns = ns;
    std::transform(lower_ns.begin(), lower_ns.end(), lower_ns.begin(), ::tolower);
    for (const auto& rule : m_layer_rules) {
        std::string lower_pattern = rule.ns_pattern;
        std::transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(), ::tolower);
        if (lower_pattern == lower_ns || lower_pattern == "*" || lower_ns.find(lower_pattern) != std::string::npos) {
            return rule;
        }
    }
    return LayerRule{};
}

void Config::add_layer_rule(const LayerRule& rule) {
    for (auto& r : m_layer_rules) {
        if (r.ns_pattern == rule.ns_pattern) {
            if (rule.anim_style_open != LayerAnimStyle::DefaultAuto) r.anim_style_open = rule.anim_style_open;
            if (rule.anim_style_close != LayerAnimStyle::DefaultAuto) r.anim_style_close = rule.anim_style_close;
            if (rule.duration_open_ms > 0) r.duration_open_ms = rule.duration_open_ms;
            if (rule.duration_close_ms > 0) r.duration_close_ms = rule.duration_close_ms;
            if (!rule.curve_open.empty()) r.curve_open = rule.curve_open;
            if (!rule.curve_close.empty()) r.curve_close = rule.curve_close;
            if (rule.popin_scale > 0.0) r.popin_scale = rule.popin_scale;
            if (rule.noanim_open) { r.noanim_open = true; r.anim_style_open = LayerAnimStyle::None; }
            if (rule.noanim_close) { r.noanim_close = true; r.anim_style_close = LayerAnimStyle::None; }
            if (rule.fade_open_duration_ms >= 0) r.fade_open_duration_ms = rule.fade_open_duration_ms;
            if (rule.fade_close_duration_ms >= 0) r.fade_close_duration_ms = rule.fade_close_duration_ms;
            if (!rule.fade_open_curve.empty()) r.fade_open_curve = rule.fade_open_curve;
            if (!rule.fade_close_curve.empty()) r.fade_close_curve = rule.fade_close_curve;
            if (!rule.fade_open_enabled) r.fade_open_enabled = false;
            if (!rule.fade_close_enabled) r.fade_close_enabled = false;
            return;
        }
    }
    m_layer_rules.push_back(rule);
}

bool Config::is_window_open_animation_enabled(const std::string& app_id, const std::string& title) const {
    for (const auto& r : m_window_rules) {
        if (r.rule == "animation_open_enabled" && (matches_target(r.target, app_id) || matches_target(r.target, title))) {
            return (r.extra == "true" || r.extra == "1" || r.extra == "yes");
        }
    }
    return is_window_open_animation_enabled();
}

bool Config::is_window_close_animation_enabled(const std::string& app_id, const std::string& title) const {
    for (const auto& r : m_window_rules) {
        if (r.rule == "animation_close_enabled" && (matches_target(r.target, app_id) || matches_target(r.target, title))) {
            return (r.extra == "true" || r.extra == "1" || r.extra == "yes");
        }
    }
    return is_window_close_animation_enabled();
}

int Config::get_window_open_duration_ms(const std::string& app_id, const std::string& title) const {
    for (const auto& r : m_window_rules) {
        if (r.rule == "animation_open_duration" && (matches_target(r.target, app_id) || matches_target(r.target, title))) {
            try { return std::clamp(std::stoi(r.extra), 10, 2000); } catch (...) {}
        }
    }
    return get_window_animation_open_duration_ms();
}

int Config::get_window_close_duration_ms(const std::string& app_id, const std::string& title) const {
    for (const auto& r : m_window_rules) {
        if (r.rule == "animation_close_duration" && (matches_target(r.target, app_id) || matches_target(r.target, title))) {
            try { return std::clamp(std::stoi(r.extra), 10, 2000); } catch (...) {}
        }
    }
    return get_window_animation_close_duration_ms();
}

std::string Config::get_window_open_curve(const std::string& app_id, const std::string& title) const {
    for (const auto& r : m_window_rules) {
        if (r.rule == "animation_open_curve" && (matches_target(r.target, app_id) || matches_target(r.target, title))) {
            if (!r.extra.empty()) return r.extra;
        }
    }
    return get_window_animation_open_curve();
}

std::string Config::get_window_close_curve(const std::string& app_id, const std::string& title) const {
    for (const auto& r : m_window_rules) {
        if (r.rule == "animation_close_curve" && (matches_target(r.target, app_id) || matches_target(r.target, title))) {
            if (!r.extra.empty()) return r.extra;
        }
    }
    return get_window_animation_close_curve();
}

double Config::get_window_open_scale(const std::string& app_id, const std::string& title) const {
    for (const auto& r : m_window_rules) {
        if (r.rule == "animation_open_scale" && (matches_target(r.target, app_id) || matches_target(r.target, title))) {
            try {
                double s = std::stod(r.extra);
                if (s > 1.0) s /= 100.0;
                return std::clamp(s, 0.1, 1.0);
            } catch (...) {}
        }
    }
    return get_window_animation_open_scale();
}

double Config::get_window_close_scale(const std::string& app_id, const std::string& title) const {
    for (const auto& r : m_window_rules) {
        if (r.rule == "animation_close_scale" && (matches_target(r.target, app_id) || matches_target(r.target, title))) {
            try {
                double s = std::stod(r.extra);
                if (s > 1.0) s /= 100.0;
                return std::clamp(s, 0.1, 1.0);
            } catch (...) {}
        }
    }
    return get_window_animation_close_scale();
}

bool Config::is_window_open_fade_enabled(const std::string& app_id, const std::string& title) const {
    for (const auto& r : m_window_rules) {
        if ((r.rule == "animation_open_fade_enabled" || r.rule == "animation_fade_enabled") &&
            (matches_target(r.target, app_id) || matches_target(r.target, title))) {
            return (r.extra == "true" || r.extra == "1" || r.extra == "yes");
        }
    }
    return is_window_open_fade_enabled();
}

bool Config::is_window_close_fade_enabled(const std::string& app_id, const std::string& title) const {
    for (const auto& r : m_window_rules) {
        if ((r.rule == "animation_close_fade_enabled" || r.rule == "animation_fade_enabled") &&
            (matches_target(r.target, app_id) || matches_target(r.target, title))) {
            return (r.extra == "true" || r.extra == "1" || r.extra == "yes");
        }
    }
    return is_window_close_fade_enabled();
}

int Config::get_window_open_fade_duration_ms(const std::string& app_id, const std::string& title) const {
    for (const auto& r : m_window_rules) {
        if ((r.rule == "animation_open_fade_duration" || r.rule == "animation_fade_duration") &&
            (matches_target(r.target, app_id) || matches_target(r.target, title))) {
            try { return std::clamp(std::stoi(r.extra), 0, 2000); } catch (...) {}
        }
    }
    int d = get_window_animation_fade_in_duration_ms();
    return (d > 0) ? d : get_window_open_duration_ms(app_id, title);
}

int Config::get_window_close_fade_duration_ms(const std::string& app_id, const std::string& title) const {
    for (const auto& r : m_window_rules) {
        if ((r.rule == "animation_close_fade_duration" || r.rule == "animation_fade_duration") &&
            (matches_target(r.target, app_id) || matches_target(r.target, title))) {
            try { return std::clamp(std::stoi(r.extra), 0, 2000); } catch (...) {}
        }
    }
    int d = get_window_animation_fade_out_duration_ms();
    return (d > 0) ? d : get_window_close_duration_ms(app_id, title);
}

std::string Config::get_window_open_fade_curve(const std::string& app_id, const std::string& title) const {
    for (const auto& r : m_window_rules) {
        if ((r.rule == "animation_open_fade_curve" || r.rule == "animation_fade_curve") &&
            (matches_target(r.target, app_id) || matches_target(r.target, title))) {
            if (!r.extra.empty()) return r.extra;
        }
    }
    return get_window_animation_fade_in_curve();
}

std::string Config::get_window_close_fade_curve(const std::string& app_id, const std::string& title) const {
    for (const auto& r : m_window_rules) {
        if ((r.rule == "animation_close_fade_curve" || r.rule == "animation_fade_curve") &&
            (matches_target(r.target, app_id) || matches_target(r.target, title))) {
            if (!r.extra.empty()) return r.extra;
        }
    }
    return get_window_animation_fade_out_curve();
}

bool Config::parse_binding_combo(const std::string& combo, uint32_t& out_modifiers, xkb_keysym_t& out_keysym) {
    out_modifiers = 0;
    out_keysym = XKB_KEY_NoSymbol;

    std::string s = combo;
    for (char& c : s) {
        if (c == '+' || c == '-' || c == ',') c = ' ';
    }

    std::stringstream ss(s);
    std::string item;
    std::vector<std::string> parts;
    while (ss >> item) {
        item = trim(item);
        if (!item.empty()) {
            parts.push_back(item);
        }
    }

    if (parts.empty()) return false;

    std::string key_part = parts.back();
    parts.pop_back();

    for (const auto& mod_name : parts) {
        std::string m = mod_name;
        std::transform(m.begin(), m.end(), m.begin(), ::tolower);
        if (m == "super" || m == "logo" || m == "mod4" || m == "win") {
            out_modifiers |= WLR_MODIFIER_LOGO;
        } else if (m == "shift") {
            out_modifiers |= WLR_MODIFIER_SHIFT;
        } else if (m == "ctrl" || m == "control") {
            out_modifiers |= WLR_MODIFIER_CTRL;
        } else if (m == "alt" || m == "mod1") {
            out_modifiers |= WLR_MODIFIER_ALT;
        }
    }

    out_keysym = xkb_keysym_from_name(key_part.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
    if (out_keysym == XKB_KEY_NoSymbol) {
        std::string lower = key_part;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower == "return" || lower == "enter") out_keysym = XKB_KEY_Return;
        else if (lower == "space") out_keysym = XKB_KEY_space;
        else if (lower == "tab") out_keysym = XKB_KEY_Tab;
        else if (lower == "escape" || lower == "esc") out_keysym = XKB_KEY_Escape;
        else if (lower == "backspace") out_keysym = XKB_KEY_BackSpace;
        else if (lower == "left") out_keysym = XKB_KEY_Left;
        else if (lower == "right") out_keysym = XKB_KEY_Right;
        else if (lower == "up") out_keysym = XKB_KEY_Up;
        else if (lower == "down") out_keysym = XKB_KEY_Down;
        else if (lower == "audioplay" || lower == "play") out_keysym = XKB_KEY_XF86AudioPlay;
        else if (lower == "audionext" || lower == "next") out_keysym = XKB_KEY_XF86AudioNext;
        else if (lower == "audioprev" || lower == "prev") out_keysym = XKB_KEY_XF86AudioPrev;
        else if (lower == "audiostop" || lower == "stop") out_keysym = XKB_KEY_XF86AudioStop;
        else if (lower == "audiomute" || lower == "mute") out_keysym = XKB_KEY_XF86AudioMute;
        else if (lower == "audiolowervolume" || lower == "voldown" || lower == "volumedown") out_keysym = XKB_KEY_XF86AudioLowerVolume;
        else if (lower == "audioraisevolume" || lower == "volup" || lower == "volumeup") out_keysym = XKB_KEY_XF86AudioRaiseVolume;
        else if (lower == "monbrightnessup" || lower == "brightnessup" || lower == "brightup") out_keysym = XKB_KEY_XF86MonBrightnessUp;
        else if (lower == "monbrightnessdown" || lower == "brightnessdown" || lower == "brightdown") out_keysym = XKB_KEY_XF86MonBrightnessDown;
        else if (key_part.length() == 1) {
            out_keysym = static_cast<xkb_keysym_t>(key_part[0]);
        }
    }

    if (out_keysym >= XKB_KEY_A && out_keysym <= XKB_KEY_Z) {
        out_keysym = (out_keysym - XKB_KEY_A) + XKB_KEY_a;
    }

    return (out_keysym != XKB_KEY_NoSymbol);
}

bool Config::parse_hex_color(const std::string& hex, float& r, float& g, float& b, float& a) {
    if (hex.empty()) return false;
    std::string s = hex;
    // Strip surrounding whitespace and quotes
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '"' || s.front() == '\'')) {
        s.erase(0, 1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '"' || s.back() == '\'')) {
        s.pop_back();
    }
    // Strip trailing comment if present
    size_t comment = s.find('#', 1);
    if (comment != std::string::npos) {
        s = s.substr(0, comment);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    }

    if (s.empty()) return false;
    if (s[0] == '#') {
        s = s.substr(1);
    }

    if (s.length() == 6) {
        try {
            unsigned long val = std::stoul(s, nullptr, 16);
            r = ((val >> 16) & 0xFF) / 255.0f;
            g = ((val >> 8) & 0xFF) / 255.0f;
            b = (val & 0xFF) / 255.0f;
            a = 1.0f;
            return true;
        } catch (...) {
            return false;
        }
    } else if (s.length() == 8) {
        try {
            unsigned long val = std::stoul(s, nullptr, 16);
            r = ((val >> 24) & 0xFF) / 255.0f;
            g = ((val >> 16) & 0xFF) / 255.0f;
            b = ((val >> 8) & 0xFF) / 255.0f;
            a = (val & 0xFF) / 255.0f;
            return true;
        } catch (...) {
            return false;
        }
    } else if (s.length() == 3) {
        try {
            unsigned long val = std::stoul(s, nullptr, 16);
            r = (((val >> 8) & 0xF) * 17) / 255.0f;
            g = (((val >> 4) & 0xF) * 17) / 255.0f;
            b = ((val & 0xF) * 17) / 255.0f;
            a = 1.0f;
            return true;
        } catch (...) {
            return false;
        }
    } else if (s.length() == 4) {
        try {
            unsigned long val = std::stoul(s, nullptr, 16);
            r = (((val >> 12) & 0xF) * 17) / 255.0f;
            g = (((val >> 8) & 0xF) * 17) / 255.0f;
            b = (((val >> 4) & 0xF) * 17) / 255.0f;
            a = ((val & 0xF) * 17) / 255.0f;
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

std::string Config::get_config_dir_path() {
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        return std::string(xdg) + "/miquland";
    }
    const char* home = getenv("HOME");
    if (home && *home) {
        return std::string(home) + "/.config/miquland";
    }
    return "/tmp/miquland";
}

std::string Config::get_config_file_path() {
    return get_config_dir_path() + "/miquland.conf";
}

std::string Config::resolve_path(const std::string& path) const {
    if (path.empty()) return "";
    std::string p = path;

    // Expand ~ home directory
    if (p[0] == '~') {
        const char* home = getenv("HOME");
        if (home) {
            p = std::string(home) + p.substr(1);
        }
    } else if (p[0] != '/') {
        // Relative path -> relative to config directory
        p = get_config_dir_path() + "/" + p;
    }
    return p;
}

void Config::ensure_default_files() {
    std::string dir = get_config_dir_path();
    std::error_code ec;
    
    // Create ~/.config/miquland/ if not present
    fs::create_directories(dir, ec);

    // Copy system default miquland.conf if user copy does not exist
    std::string config_path = get_config_file_path();
    if (!fs::exists(config_path)) {
        bool copied = false;
        for (const char* t_dir : {"/usr/share/miquland", "/etc/xdg/miquland", "/etc/miquland", "assets", "/usr/local/share/miquland"}) {
            std::string cand = std::string(t_dir) + "/miquland.conf";
            if (fs::exists(cand)) {
                fs::copy_file(cand, config_path, fs::copy_options::overwrite_existing, ec);
                if (!ec) {
                    copied = true;
                    log_info("Installed default configuration to " + config_path + " from " + cand);
                    break;
                }
            }
        }
        // Absolute last resort if system assets are missing
        if (!copied) save(); 
    }
}

static std::string resolve_exec_command(const std::string& raw_cmd) {
    std::string cmd = trim(raw_cmd);
    if (cmd.empty()) return "";

    // Find the first token (binary or script path)
    size_t space_pos = cmd.find_first_of(" \t");
    std::string first_token = (space_pos == std::string::npos) ? cmd : cmd.substr(0, space_pos);
    std::string rest = (space_pos == std::string::npos) ? "" : cmd.substr(space_pos);

    // If first_token is quoted, unquote for path checking
    bool is_quoted = false;
    char q = '\0';
    if (first_token.size() >= 2 && (first_token.front() == '"' || first_token.front() == '\'') && first_token.back() == first_token.front()) {
        is_quoted = true;
        q = first_token.front();
        first_token = first_token.substr(1, first_token.size() - 2);
    }

    // Check if first_token starts with ~
    if (first_token[0] == '~') {
        const char* home = getenv("HOME");
        if (home) {
            first_token = std::string(home) + first_token.substr(1);
        }
    } else if (first_token[0] != '/') {
        // Check if it exists relative to the miquland config directory
        std::string config_rel = Config::get_config_dir_path() + "/" + first_token;
        if (fs::exists(config_rel)) {
            first_token = config_rel;
        }
    }

    // If it is a file on disk, make sure it can be executed
    if (fs::exists(first_token) && fs::is_regular_file(first_token)) {
        std::error_code ec;
        auto perms = fs::status(first_token, ec).permissions();
        if ((perms & fs::perms::owner_exec) == fs::perms::none &&
            (perms & fs::perms::group_exec) == fs::perms::none &&
            (perms & fs::perms::others_exec) == fs::perms::none) {
            return "sh \"" + first_token + "\"" + rest;
        }
        return "\"" + first_token + "\"" + rest;
    }

    if (is_quoted) {
        return std::string(1, q) + first_token + std::string(1, q) + rest;
    }
    return cmd;
}

struct SectionFrame {
    std::string type;
    std::string param;
};

static void parse_section_header(const std::string& line, std::string& out_type, std::string& out_param) {
    std::string raw = trim(line);
    if (!raw.empty() && raw.back() == '{') {
        raw = trim(raw.substr(0, raw.size() - 1));
    }
    size_t q1 = raw.find('"');
    size_t q2 = raw.rfind('"');
    if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1) {
        out_type = trim(raw.substr(0, q1));
        out_param = raw.substr(q1 + 1, q2 - q1 - 1);
    } else {
        size_t sp = raw.find_first_of(" \t");
        if (sp != std::string::npos) {
            out_type = trim(raw.substr(0, sp));
            out_param = trim(raw.substr(sp + 1));
        } else {
            out_type = raw;
            out_param = "";
        }
    }
    std::transform(out_type.begin(), out_type.end(), out_type.begin(), ::tolower);
}

static void parse_monitor_rule_str(const std::string& value, MonitorRule& rule) {
    std::vector<std::string> tokens;
    std::stringstream ss(value);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        tokens.push_back(trim(tok));
    }
    if (tokens.empty()) return;

    rule.name = tokens[0];
    if (rule.name == "*") rule.name = "";

    if (tokens.size() >= 2) {
        std::string mode_str = tokens[1];
        std::string lower_mode = mode_str;
        std::transform(lower_mode.begin(), lower_mode.end(), lower_mode.begin(), ::tolower);
        if (lower_mode == "disable" || lower_mode == "off") {
            rule.disabled = true;
        } else if (lower_mode == "preferred" || lower_mode == "auto" || lower_mode.empty() || lower_mode == "highrr" || lower_mode == "highres") {
            rule.width = 0;
            rule.height = 0;
            rule.refresh_rate = 0.0;
        } else {
            size_t x_idx = lower_mode.find('x');
            if (x_idx != std::string::npos) {
                try {
                    rule.width = std::stoi(lower_mode.substr(0, x_idx));
                    size_t at_idx = lower_mode.find('@', x_idx);
                    if (at_idx != std::string::npos) {
                        rule.height = std::stoi(lower_mode.substr(x_idx + 1, at_idx - x_idx - 1));
                        rule.refresh_rate = std::stod(lower_mode.substr(at_idx + 1));
                    } else {
                        rule.height = std::stoi(lower_mode.substr(x_idx + 1));
                        rule.refresh_rate = 0.0;
                    }
                } catch (...) {}
            }
        }
    }

    if (!rule.disabled && tokens.size() >= 3) {
        std::string pos_str = tokens[2];
        std::string lower_pos = pos_str;
        std::transform(lower_pos.begin(), lower_pos.end(), lower_pos.begin(), ::tolower);
        if (lower_pos == "auto" || lower_pos.empty()) {
            rule.x = -1;
            rule.y = -1;
        } else {
            size_t x_idx = lower_pos.find('x');
            if (x_idx != std::string::npos) {
                try {
                    rule.x = std::stoi(lower_pos.substr(0, x_idx));
                    rule.y = std::stoi(lower_pos.substr(x_idx + 1));
                } catch (...) {}
            }
        }
    }

    if (!rule.disabled && tokens.size() >= 4) {
        std::string scale_str = tokens[3];
        std::string lower_scale = scale_str;
        std::transform(lower_scale.begin(), lower_scale.end(), lower_scale.begin(), ::tolower);
        if (lower_scale == "auto" || lower_scale.empty()) {
            rule.scale = 1.0;
        } else {
            try {
                rule.scale = std::stod(scale_str);
                if (rule.scale <= 0.0) rule.scale = 1.0;
            } catch (...) {}
        }
    }

    if (!rule.disabled && tokens.size() >= 5) {
        std::string trans_str = tokens[4];
        std::string lower_trans = trans_str;
        std::transform(lower_trans.begin(), lower_trans.end(), lower_trans.begin(), ::tolower);
        if (lower_trans == "90" || lower_trans == "1") {
            rule.transform = WL_OUTPUT_TRANSFORM_90;
        } else if (lower_trans == "180" || lower_trans == "2") {
            rule.transform = WL_OUTPUT_TRANSFORM_180;
        } else if (lower_trans == "270" || lower_trans == "3") {
            rule.transform = WL_OUTPUT_TRANSFORM_270;
        } else if (lower_trans == "flipped" || lower_trans == "4") {
            rule.transform = WL_OUTPUT_TRANSFORM_FLIPPED;
        } else if (lower_trans == "flipped-90" || lower_trans == "5") {
            rule.transform = WL_OUTPUT_TRANSFORM_FLIPPED_90;
        } else if (lower_trans == "flipped-180" || lower_trans == "6") {
            rule.transform = WL_OUTPUT_TRANSFORM_FLIPPED_180;
        } else if (lower_trans == "flipped-270" || lower_trans == "7") {
            rule.transform = WL_OUTPUT_TRANSFORM_FLIPPED_270;
        } else {
            rule.transform = WL_OUTPUT_TRANSFORM_NORMAL;
        }
    }
}

static bool is_in_path(const std::vector<Config::SectionFrame>& stack, const std::string& target_type) {
    for (const auto& f : stack) {
        if (f.type == target_type) return true;
    }
    return false;
}

static std::string get_current_section_path(const std::vector<Config::SectionFrame>& section_stack) {
    std::string p;
    for (size_t i = 0; i < section_stack.size(); ++i) {
        if (i > 0) p += ".";
        p += section_stack[i].type;
    }
    return p;
}

static bool is_hex_color_at(const std::string& str, size_t pos) {
    if (pos >= str.size() || str[pos] != '#') return false;
    size_t i = pos + 1;
    while (i < str.size() && std::isxdigit(static_cast<unsigned char>(str[i]))) {
        ++i;
    }
    size_t len = i - (pos + 1);
    if (len == 3 || len == 4 || len == 6 || len == 8) {
        if (i == str.size() || std::isspace(static_cast<unsigned char>(str[i])) ||
            str[i] == ',' || str[i] == '"' || str[i] == '\'') {
            return true;
        }
    }
    return false;
}

static void strip_inline_comment(const std::string& key, std::string& value) {
    if (key == "bind" || key == "gesture" || key == "exec" || key == "exec_once" ||
        key == "exec-once" || key == "exec_always" || key == "exec-always" || key == "autostart") {
        return;
    }

    bool in_quotes = false;
    char quote_char = 0;
    size_t comment_pos = std::string::npos;

    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (!in_quotes && (c == '"' || c == '\'')) {
            in_quotes = true;
            quote_char = c;
        } else if (in_quotes && c == quote_char) {
            in_quotes = false;
        } else if (!in_quotes && c == '#') {
            if (!is_hex_color_at(value, i)) {
                comment_pos = i;
                break;
            }
        }
    }

    if (comment_pos != std::string::npos) {
        value = trim(value.substr(0, comment_pos));
    }
}

void Config::parse_plugins_entry(const std::string& key, const std::string& value) {
    if (key == "load" || key == "plugin" || key == "path") {
        m_plugins.push_back(value);
    }
}

void Config::parse_autostart_entry(const std::string& key, const std::string& value,
                                   std::vector<std::string>& file_exec_cmds, std::vector<std::string>& file_exec_once_cmds) {
    if (key == "exec_once" || key == "exec-once" || key == "autostart") {
        std::string resolved_cmd = resolve_exec_command(value);
        if (!resolved_cmd.empty()) file_exec_once_cmds.push_back(resolved_cmd);
    } else if (key == "exec" || key == "exec_always" || key == "exec-always") {
        std::string resolved_cmd = resolve_exec_command(value);
        if (!resolved_cmd.empty()) file_exec_cmds.push_back(resolved_cmd);
    }
}

void Config::parse_monitors_entry(const std::string& key, const std::string& value,
                                  std::vector<MonitorRule>& file_monitors, bool& has_monitors_in_file) {
    if (key == "default" || key == "monitor" || key == "output") {
        MonitorRule rule;
        parse_monitor_rule_str(value, rule);
        has_monitors_in_file = true;
        file_monitors.push_back(rule);
    }
}

void Config::parse_cursor_entry(const std::string& key, const std::string& value) {
    if (key == "theme") m_cursor_theme = value;
    else if (key == "size") {
        try { m_cursor_size = std::clamp(std::stoi(value), 8, 128); } catch (...) {}
    }
}

void Config::parse_general_entry(const std::string& path_str, const std::string& top_type,
                                 const std::vector<SectionFrame>& stack, const std::string& key, const std::string& value) {
    if (path_str == "general" || top_type == "general") {
        if (key == "layout" || key == "tiling_layout") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            m_layout_mode = (lower == "stack" || lower == "master") ? LayoutMode::Stack : LayoutMode::Spiral;
        } else if (key == "default_split_ratio" || key == "split_ratio") {
            try { m_default_split_ratio = std::clamp(std::stod(value), 0.1, 0.9); } catch (...) {}
        } else if (key == "smart_gaps") {
            m_smart_gaps = (value == "true" || value == "1" || value == "yes");
        } else if (key == "allow_tearing" || key == "tearing") {
            m_allow_tearing = (value == "true" || value == "1" || value == "yes");
        } else if (key == "workspace_cycle") {
            m_workspace_cycle = (value == "true" || value == "1" || value == "yes");
        } else if (key == "auto_float_fixed_size") {
            m_auto_float_fixed_size = (value == "true" || value == "1" || value == "yes");
        } else if (key == "resize_on_border") {
            m_resize_on_border = (value == "true" || value == "1" || value == "yes");
        } else if (key == "border_grab_area") {
            try { m_border_grab_area = std::clamp(std::stoi(value), 1, 30); } catch (...) {}
        }
    } else if (path_str == "general.gaps" || (top_type == "gaps" && is_in_path(stack, "general"))) {
        if (key == "inner" || key == "space_between_windows") {
            try { m_space_between_windows = std::max(0, std::stoi(value)); } catch (...) {}
        } else if (key == "outer" || key == "screen_edge_padding") {
            try { m_screen_edge_padding = std::max(0, std::stoi(value)); } catch (...) {}
        }
    } else if (path_str == "general.border" || (top_type == "border" && is_in_path(stack, "general"))) {
        if (key == "width") {
            try { m_window_border_width = std::max(0, std::stoi(value)); } catch (...) {}
        } else if (key == "radius") {
            try { m_window_border_radius = std::max(0, std::stoi(value)); } catch (...) {}
        } else if (key == "active") {
            set_window_border_color_active(value);
        } else if (key == "inactive") {
            set_window_border_color_inactive(value);
        } else if (key == "shading") {
            m_window_border_shading = (value == "true" || value == "1" || value == "yes");
        } else if (key == "shading_strength") {
            try { m_window_border_shading_strength = std::clamp(std::stof(value), 0.0f, 1.0f); } catch (...) {}
        }
    }
}

void Config::set_window_border_color_active(const std::string& color) {
    m_window_border_color_active = color;
    m_border_paint_active = BorderPaint::parse(color);
}

void Config::set_window_border_color_inactive(const std::string& color) {
    m_window_border_color_inactive = color;
    m_border_paint_inactive = BorderPaint::parse(color);
}

void Config::parse_decoration_entry(const std::string& path_str, const std::string& top_type,
                                    const std::vector<SectionFrame>& stack, const std::string& key, const std::string& value) {
    if (path_str == "decoration" || top_type == "decoration") {
        if (key == "active_opacity") {
            try { m_window_opacity_active = std::clamp(std::stof(value), 0.0f, 1.0f); } catch (...) {}
        } else if (key == "inactive_opacity") {
            try { m_window_opacity_inactive = std::clamp(std::stof(value), 0.0f, 1.0f); } catch (...) {}
        } else if (key == "rounding") {
            try { m_window_border_radius = std::max(0, std::stoi(value)); } catch (...) {}
        }
    } else if (path_str == "decoration.blur" || (top_type == "blur" && is_in_path(stack, "decoration"))) {
        if (key == "enabled") {
            m_blur_enabled = (value == "true" || value == "1" || value == "yes");
        } else if (key == "radius") {
            try { m_blur_radius = std::max(1, std::stoi(value)); } catch (...) {}
        } else if (key == "passes") {
            try { m_blur_num_passes = std::max(1, std::stoi(value)); } catch (...) {}
        } else if (key == "noise") {
            try { m_blur_noise = std::stof(value); } catch (...) {}
        } else if (key == "brightness") {
            try { m_blur_brightness = std::stof(value); } catch (...) {}
        } else if (key == "contrast") {
            try { m_blur_contrast = std::stof(value); } catch (...) {}
        } else if (key == "saturation") {
            try { m_blur_saturation = std::stof(value); } catch (...) {}
        }
    } else if (path_str == "decoration.shadow" || (top_type == "shadow" && is_in_path(stack, "decoration"))) {
        if (key == "enabled") {
            m_shadow_enabled = (value == "true" || value == "1" || value == "yes");
        } else if (key == "range" || key == "blur_sigma" || key == "radius" || key == "size") {
            try { m_shadow_blur_sigma = std::max(0.0f, std::stof(value)); } catch (...) {}
        } else if (key == "color") {
            m_shadow_color_active = value;
        } else if (key == "color_inactive" || key == "inactive_color") {
            m_shadow_color_inactive = value;
        } else if (key == "offset") {
            std::istringstream iss(value);
            int ox = 0, oy = 0;
            if (iss >> ox >> oy) {
                m_shadow_offset_x = ox;
                m_shadow_offset_y = oy;
            }
        }
    }
}

static void parse_window_animation_entry(Config& cfg, const std::string& path_str, const std::string& top_type,
                                         const std::vector<Config::SectionFrame>& stack,
                                         const std::string& key, const std::string& value) {
    bool in_open = (top_type == "open" || is_in_path(stack, "open"));
    bool in_close = (top_type == "close" || is_in_path(stack, "close"));
    bool in_fade = (top_type == "fade" || is_in_path(stack, "fade"));

    if (in_fade) {
        if (key == "enabled") {
            bool en = (value == "true" || value == "1" || value == "yes");
            if (in_open) cfg.set_window_open_fade_enabled(en);
            if (in_close) cfg.set_window_close_fade_enabled(en);
            if (!in_open && !in_close) {
                cfg.set_window_open_fade_enabled(en);
                cfg.set_window_close_fade_enabled(en);
            }
        } else if (key == "duration") {
            try {
                int d = std::stoi(value);
                if (in_open) cfg.set_window_animation_fade_in_duration_ms(d);
                if (in_close) cfg.set_window_animation_fade_out_duration_ms(d);
                if (!in_open && !in_close) {
                    cfg.set_window_animation_fade_in_duration_ms(d);
                    cfg.set_window_animation_fade_out_duration_ms(d);
                }
            } catch (...) {}
        } else if (key == "curve") {
            if (in_open) cfg.set_window_animation_fade_in_curve(value);
            if (in_close) cfg.set_window_animation_fade_out_curve(value);
            if (!in_open && !in_close) {
                cfg.set_window_animation_fade_in_curve(value);
                cfg.set_window_animation_fade_out_curve(value);
            }
        }
        return;
    }

    if (in_open) {
        if (key == "enabled") cfg.set_window_open_animation_enabled(value == "true" || value == "1" || value == "yes");
        else if (key == "duration") { try { cfg.set_window_animation_open_duration_ms(std::stoi(value)); } catch (...) {} }
        else if (key == "curve") cfg.set_window_animation_open_curve(value);
        else if (key == "scale") { try { cfg.set_window_animation_open_scale(std::stod(value)); } catch (...) {} }
    } else if (in_close) {
        if (key == "enabled") cfg.set_window_close_animation_enabled(value == "true" || value == "1" || value == "yes");
        else if (key == "duration") { try { cfg.set_window_animation_close_duration_ms(std::stoi(value)); } catch (...) {} }
        else if (key == "curve") cfg.set_window_animation_close_curve(value);
        else if (key == "scale") { try { cfg.set_window_animation_close_scale(std::stod(value)); } catch (...) {} }
    } else {
        if (key == "enabled") cfg.set_window_animations_enabled(value == "true" || value == "1" || value == "yes");
        else if (key == "duration") { try { cfg.set_window_animation_duration_ms(std::stoi(value)); } catch (...) {} }
        else if (key == "curve") cfg.set_window_animation_curve(value);
        else if (key == "fade") cfg.set_window_animation_fade_enabled(value == "true" || value == "1" || value == "yes");
        else if (key == "open_scale") { try { cfg.set_window_animation_open_scale(std::stod(value)); } catch (...) {} }
        else if (key == "close_scale") { try { cfg.set_window_animation_close_scale(std::stod(value)); } catch (...) {} }
    }
}

static void parse_layer_animation_entry(Config& cfg, const std::string& path_str, const std::string& top_type,
                                        const std::vector<Config::SectionFrame>& stack,
                                        const std::string& key, const std::string& value) {
    bool in_open = (top_type == "open" || is_in_path(stack, "open"));
    bool in_close = (top_type == "close" || is_in_path(stack, "close"));
    bool in_fade = (top_type == "fade" || is_in_path(stack, "fade"));

    if (in_fade) {
        if (key == "enabled") {
            cfg.set_layer_animation_fade_enabled(value == "true" || value == "1" || value == "yes");
        } else if (key == "duration") {
            try {
                int d = std::stoi(value);
                if (in_open) cfg.set_layer_animation_fade_in_duration_ms(d);
                if (in_close) cfg.set_layer_animation_fade_out_duration_ms(d);
                if (!in_open && !in_close) {
                    cfg.set_layer_animation_fade_in_duration_ms(d);
                    cfg.set_layer_animation_fade_out_duration_ms(d);
                }
            } catch (...) {}
        } else if (key == "curve") {
            if (in_open) cfg.set_layer_animation_fade_in_curve(value);
            if (in_close) cfg.set_layer_animation_fade_out_curve(value);
            if (!in_open && !in_close) {
                cfg.set_layer_animation_fade_in_curve(value);
                cfg.set_layer_animation_fade_out_curve(value);
            }
        }
        return;
    }

    if (in_open) {
        if (key == "enabled") cfg.set_layer_open_animation_enabled(value == "true" || value == "1" || value == "yes");
        else if (key == "duration") { try { cfg.set_layer_open_duration_ms(std::stoi(value)); } catch (...) {} }
        else if (key == "curve") cfg.set_layer_open_curve(value);
    } else if (in_close) {
        if (key == "enabled") cfg.set_layer_close_animation_enabled(value == "true" || value == "1" || value == "yes");
        else if (key == "duration") { try { cfg.set_layer_close_duration_ms(std::stoi(value)); } catch (...) {} }
        else if (key == "curve") cfg.set_layer_close_curve(value);
    } else {
        if (key == "enabled") cfg.set_layer_animations_enabled(value == "true" || value == "1" || value == "yes");
        else if (key == "duration") { try { cfg.set_layer_animation_duration_ms(std::stoi(value)); } catch (...) {} }
        else if (key == "curve") cfg.set_layer_animation_curve(value);
        else if (key == "popin_scale") {
            try {
                double s = std::stod(value);
                if (s > 1.0) s /= 100.0;
                cfg.set_layer_animation_popin_scale(s);
            } catch (...) {}
        }
    }
}

static void parse_workspace_animation_entry(Config& cfg, const std::string& key, const std::string& value) {
    if (key == "enabled") cfg.set_workspace_animations_enabled(value == "true" || value == "1" || value == "yes");
    else if (key == "duration") { try { cfg.set_workspace_animation_duration_ms(std::stoi(value)); } catch (...) {} }
    else if (key == "curve") cfg.set_workspace_animation_curve(value);
}

void Config::parse_animations_entry(const std::string& path_str, const std::string& top_type,
                                    const std::vector<SectionFrame>& stack, const std::string& key, const std::string& value) {
    if (path_str == "animations" || (top_type == "animations" && stack.size() <= 1)) {
        if (key == "enabled") m_animations_enabled = (value == "true" || value == "1" || value == "yes");
    } else if (is_in_path(stack, "windows") || top_type == "windows") {
        parse_window_animation_entry(*this, path_str, top_type, stack, key, value);
    } else if (is_in_path(stack, "layers") || top_type == "layers") {
        parse_layer_animation_entry(*this, path_str, top_type, stack, key, value);
    } else if (is_in_path(stack, "workspaces") || top_type == "workspaces") {
        parse_workspace_animation_entry(*this, key, value);
    }
}

void Config::parse_input_entry(const std::string& path_str, const std::string& top_type,
                               const std::vector<SectionFrame>& stack, const std::string& key, const std::string& value,
                               std::vector<GestureBinding>& file_gestures, bool& has_gestures_in_file) {
    if (path_str == "input" || top_type == "input") {
        if (key == "terminal") m_terminal = value;
    } else if (path_str == "input.keyboard" || (top_type == "keyboard" && is_in_path(stack, "input"))) {
        if (key == "layout") m_kb_layout = value;
        else if (key == "variant") m_kb_variant = value;
        else if (key == "options") m_kb_options = value;
        else if (key == "model") m_kb_model = value;
        else if (key == "repeat_rate") { try { m_repeat_rate = std::max(1, std::stoi(value)); } catch (...) {} }
        else if (key == "repeat_delay") { try { m_repeat_delay = std::max(100, std::stoi(value)); } catch (...) {} }
    } else if (path_str == "input.touchpad" || (top_type == "touchpad" && is_in_path(stack, "input"))) {
        if (key == "tap_to_click") m_tap_to_click = (value == "true" || value == "1" || value == "yes");
        else if (key == "natural_scroll") m_natural_scroll = (value == "true" || value == "1" || value == "yes");
        else if (key == "disable_while_typing" || key == "dwt") m_dwt = (value == "true" || value == "1" || value == "yes");
        else if (key == "accel_speed") { try { m_accel_speed = std::clamp(std::stod(value), -1.0, 1.0); } catch (...) {} }
        else if (key == "accel_profile") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            m_accel_profile = (lower == "flat") ? "flat" : "adaptive";
        }
    } else if (path_str == "input.mouse" || (top_type == "mouse" && is_in_path(stack, "input"))) {
        if (key == "sensitivity" || key == "accel_speed") { try { m_accel_speed = std::clamp(std::stod(value), -1.0, 1.0); } catch (...) {} }
        else if (key == "natural_scroll") m_natural_scroll = (value == "true" || value == "1" || value == "yes");
        else if (key == "accel_profile") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            m_accel_profile = (lower == "flat") ? "flat" : "adaptive";
        }
    } else if (path_str == "input.touchscreen" || (top_type == "touchscreen" && is_in_path(stack, "input"))) {
        if (key == "output") m_touchscreen_output = value;
    } else if (path_str == "input.gestures.workspace_swipe" || (top_type == "workspace_swipe" && is_in_path(stack, "gestures"))) {
        if (key == "touchpad_multiplier") { try { m_touchpad_workspace_swipe_multiplier = std::clamp(std::stod(value), 0.1, 10.0); } catch (...) {} }
        else if (key == "touchscreen_multiplier") { try { m_touchscreen_workspace_swipe_multiplier = std::clamp(std::stod(value), 0.1, 10.0); } catch (...) {} }
        else if (key == "swipe_threshold") { try { m_touchpad_swipe_threshold = std::max(5.0, std::stod(value)); } catch (...) {} }
        else if (key == "touchscreen_threshold") { try { m_touchscreen_swipe_threshold = std::max(5.0, std::stod(value)); } catch (...) {} }
        else if (key == "cancel_ratio") { try { m_workspace_swipe_cancel_ratio = std::clamp(std::stod(value), 0.05, 0.95); } catch (...) {} }
        else if (key == "min_speed_to_force") { try { m_workspace_swipe_min_speed_to_force = std::clamp(std::stod(value), 0.05, 10.0); } catch (...) {} }
        else if (key == "edge_resistance") { try { m_workspace_swipe_edge_resistance = std::clamp(std::stod(value), 0.0, 0.5); } catch (...) {} }
    } else if (path_str == "input.gestures" || top_type == "gestures") {
        if (key == "bind" || key == "gesture") {
            size_t comma = value.rfind(',');
            if (comma != std::string::npos) {
                std::string pattern = trim(value.substr(0, comma));
                std::string action = trim(value.substr(comma + 1));
                std::transform(pattern.begin(), pattern.end(), pattern.begin(), ::tolower);
                if (!pattern.empty() && !action.empty()) {
                    has_gestures_in_file = true;
                    file_gestures.push_back({ pattern, action });
                }
            }
        }
    }
}

void Config::parse_xwayland_entry(const std::string& key, const std::string& value) {
    if (key == "force_zero_scaling") {
        m_xwayland_force_zero_scaling = (value == "true" || value == "1" || value == "yes");
    }
}

static Config::LayerAnimStyle parse_layer_anim_style(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "none" || lower == "noanim") return Config::LayerAnimStyle::None;
    if (lower == "fade") return Config::LayerAnimStyle::Fade;
    if (lower == "unroll") return Config::LayerAnimStyle::Unroll;
    if (lower == "fold" || lower == "roll") return Config::LayerAnimStyle::Unroll;
    if (lower == "unfold" || lower == "unroll_center" || lower == "expand_vertical") return Config::LayerAnimStyle::Unfold;
    if (lower == "popin") return Config::LayerAnimStyle::Popin;
    if (lower == "slidetop" || lower == "slide_top" || lower == "slide top" || lower == "top") return Config::LayerAnimStyle::SlideTop;
    if (lower == "slidebottom" || lower == "slide_bottom" || lower == "slide bottom" || lower == "bottom") return Config::LayerAnimStyle::SlideBottom;
    if (lower == "slideleft" || lower == "slide_left" || lower == "slide left" || lower == "left") return Config::LayerAnimStyle::SlideLeft;
    if (lower == "slideright" || lower == "slide_right" || lower == "slide right" || lower == "right") return Config::LayerAnimStyle::SlideRight;
    if (lower == "slide") return Config::LayerAnimStyle::Slide;
    return Config::LayerAnimStyle::DefaultAuto;
}

static std::string layer_anim_style_to_string(Config::LayerAnimStyle style) {
    switch (style) {
        case Config::LayerAnimStyle::None: return "none";
        case Config::LayerAnimStyle::Fade: return "fade";
        case Config::LayerAnimStyle::Unroll: return "unroll";
        case Config::LayerAnimStyle::Unfold: return "unfold";
        case Config::LayerAnimStyle::Popin: return "popin";
        case Config::LayerAnimStyle::SlideTop: return "slide top";
        case Config::LayerAnimStyle::SlideBottom: return "slide bottom";
        case Config::LayerAnimStyle::SlideLeft: return "slide left";
        case Config::LayerAnimStyle::SlideRight: return "slide right";
        case Config::LayerAnimStyle::Slide: return "slide";
        default: return "default";
    }
}

static std::string find_section_param(const std::vector<Config::SectionFrame>& stack, const std::string& type) {
    for (const auto& f : stack) {
        if (f.type == type) return f.param;
    }
    return "";
}

void Config::parse_layerrule_entry(const std::string& ns, const std::string& path_str, const std::string& top_type,
                                   const std::vector<SectionFrame>& stack, const std::string& key, const std::string& value) {
    if (ns.empty()) return;
    bool in_open = (top_type == "open" || is_in_path(stack, "open"));
    bool in_close = (top_type == "close" || is_in_path(stack, "close"));
    bool in_fade = (top_type == "fade" || is_in_path(stack, "fade"));
    bool in_blur = (top_type == "blur" || is_in_path(stack, "blur"));

    if (in_blur) {
        if (key == "enabled") {
            if (value == "true" || value == "1" || value == "yes") add_blurred_layer(ns);
        }
        return;
    }

    if (in_fade) {
        LayerRule r;
        r.ns_pattern = ns;
        if (key == "enabled") {
            bool en = (value == "true" || value == "1" || value == "yes");
            if (in_open) r.fade_open_enabled = en;
            if (in_close) r.fade_close_enabled = en;
            if (!in_open && !in_close) { r.fade_open_enabled = en; r.fade_close_enabled = en; }
        } else if (key == "duration") {
            try {
                int d = std::stoi(value);
                if (in_open) r.fade_open_duration_ms = d;
                if (in_close) r.fade_close_duration_ms = d;
                if (!in_open && !in_close) { r.fade_open_duration_ms = d; r.fade_close_duration_ms = d; }
            } catch (...) {}
        } else if (key == "curve") {
            if (in_open) r.fade_open_curve = value;
            if (in_close) r.fade_close_curve = value;
            if (!in_open && !in_close) { r.fade_open_curve = value; r.fade_close_curve = value; }
        }
        add_layer_rule(r);
        return;
    }

    if (in_open || in_close) {
        LayerRule r;
        r.ns_pattern = ns;
        if (key == "enabled") {
            bool en = (value == "true" || value == "1" || value == "yes");
            if (in_open) { r.noanim_open = !en; if (!en) r.anim_style_open = LayerAnimStyle::None; }
            if (in_close) { r.noanim_close = !en; if (!en) r.anim_style_close = LayerAnimStyle::None; }
        } else if (key == "style" || key == "animation") {
            LayerAnimStyle style = parse_layer_anim_style(value);
            if (in_open) r.anim_style_open = style;
            if (in_close) r.anim_style_close = style;
        } else if (key == "duration") {
            try {
                int d = std::stoi(value);
                if (in_open) r.duration_open_ms = d;
                if (in_close) r.duration_close_ms = d;
            } catch (...) {}
        } else if (key == "curve") {
            LayerAnimStyle style = parse_layer_anim_style(value);
            if (style != LayerAnimStyle::DefaultAuto) {
                if (in_open) r.anim_style_open = style;
                if (in_close) r.anim_style_close = style;
            } else {
                if (in_open) r.curve_open = value;
                if (in_close) r.curve_close = value;
            }
        } else if (key == "scale" || key == "popin_scale") {
            try {
                double s = std::stod(value);
                if (s > 1.0) s /= 100.0;
                r.popin_scale = std::clamp(s, 0.1, 1.0);
            } catch (...) {}
        } else if (key == "slide_edge") {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            LayerAnimStyle style = (lower == "top") ? LayerAnimStyle::SlideTop :
                                   (lower == "bottom") ? LayerAnimStyle::SlideBottom :
                                   (lower == "left") ? LayerAnimStyle::SlideLeft :
                                   (lower == "right") ? LayerAnimStyle::SlideRight : LayerAnimStyle::Slide;
            if (in_open) r.anim_style_open = style;
            if (in_close) r.anim_style_close = style;
        }
        add_layer_rule(r);
        return;
    }

    // Top-level properties in layerrule
    if (key == "blur") {
        if (value == "true" || value == "1" || value == "yes") add_blurred_layer(ns);
    } else if (key == "animation" || key == "animation_open" || key == "animation_close") {
        LayerRule r;
        r.ns_pattern = ns;
        int dur = -1;
        std::string curve;
        double pop_scale = -1.0;
        bool noanim = false;
        LayerAnimStyle style = LayerAnimStyle::DefaultAuto;

        std::stringstream ss(value);
        std::string token;
        while (ss >> token) {
            std::string lower = token;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            LayerAnimStyle s = parse_layer_anim_style(lower);
            if (s != LayerAnimStyle::DefaultAuto) {
                style = s;
                if (s == LayerAnimStyle::None) noanim = true;
            } else if (lower.back() == '%' || (lower.size() >= 3 && lower.substr(0, 2) == "0.")) {
                try {
                    std::string st = lower;
                    if (st.back() == '%') st.pop_back();
                    double v = std::stod(st);
                    if (v > 1.0) v /= 100.0;
                    pop_scale = std::clamp(v, 0.1, 1.0);
                } catch (...) {}
            } else if (lower.size() > 2 && lower.substr(lower.size() - 2) == "ms") {
                try { dur = std::stoi(lower.substr(0, lower.size() - 2)); } catch (...) {}
            } else if (!lower.empty() && std::isdigit(lower[0])) {
                try { dur = std::stoi(lower); } catch (...) {}
            } else {
                curve = lower;
            }
        }

        if (key == "animation_open") {
            r.anim_style_open = style;
            if (dur > 0) r.duration_open_ms = dur;
            if (!curve.empty()) r.curve_open = curve;
            if (noanim) { r.noanim_open = true; r.anim_style_open = LayerAnimStyle::None; }
        } else if (key == "animation_close") {
            r.anim_style_close = style;
            if (dur > 0) r.duration_close_ms = dur;
            if (!curve.empty()) r.curve_close = curve;
            if (noanim) { r.noanim_close = true; r.anim_style_close = LayerAnimStyle::None; }
        } else {
            r.anim_style_open = style;
            r.anim_style_close = style;
            if (dur > 0) { r.duration_open_ms = dur; r.duration_close_ms = dur; }
            if (!curve.empty()) { r.curve_open = curve; r.curve_close = curve; }
            if (noanim) {
                r.noanim_open = true; r.noanim_close = true;
                r.anim_style_open = LayerAnimStyle::None; r.anim_style_close = LayerAnimStyle::None;
            }
        }
        if (pop_scale > 0.0) r.popin_scale = pop_scale;
        add_layer_rule(r);
    } else if (key == "duration") {
        try { int d = std::stoi(value); LayerRule r; r.ns_pattern = ns; r.duration_open_ms = d; r.duration_close_ms = d; add_layer_rule(r); } catch (...) {}
    } else if (key == "open_duration") {
        try { int d = std::stoi(value); LayerRule r; r.ns_pattern = ns; r.duration_open_ms = d; add_layer_rule(r); } catch (...) {}
    } else if (key == "close_duration") {
        try { int d = std::stoi(value); LayerRule r; r.ns_pattern = ns; r.duration_close_ms = d; add_layer_rule(r); } catch (...) {}
    } else if (key == "curve") {
        LayerRule r; r.ns_pattern = ns; r.curve_open = value; r.curve_close = value; add_layer_rule(r);
    } else if (key == "open_curve") {
        LayerRule r; r.ns_pattern = ns; r.curve_open = value; add_layer_rule(r);
    } else if (key == "close_curve") {
        LayerRule r; r.ns_pattern = ns; r.curve_close = value; add_layer_rule(r);
    } else if (key == "popin_scale") {
        try {
            double s = std::stod(value);
            if (s > 1.0) s /= 100.0;
            LayerRule r; r.ns_pattern = ns; r.popin_scale = std::clamp(s, 0.1, 1.0); add_layer_rule(r);
        } catch (...) {}
    }
}

void Config::parse_windowrule_entry(const std::string& target, const std::string& path_str, const std::string& top_type,
                                    const std::vector<SectionFrame>& stack, const std::string& key, const std::string& value,
                                    std::vector<WindowRule>& file_rules, bool& has_rules_in_file) {
    if (target.empty()) return;
    bool in_open = (top_type == "open" || is_in_path(stack, "open"));
    bool in_close = (top_type == "close" || is_in_path(stack, "close"));
    bool in_fade = (top_type == "fade" || is_in_path(stack, "fade"));
    bool in_blur = (top_type == "blur" || is_in_path(stack, "blur"));

    if (in_blur) {
        if (key == "enabled") {
            file_rules.push_back({ "blur", target, value });
            has_rules_in_file = true;
        } else if (key == "radius" || key == "passes") {
            file_rules.push_back({ "blur_" + key, target, value });
            has_rules_in_file = true;
        }
        return;
    }

    if (in_fade) {
        if (in_open) {
            if (key == "enabled") file_rules.push_back({ "animation_open_fade_enabled", target, value });
            else if (key == "duration") file_rules.push_back({ "animation_open_fade_duration", target, value });
            else if (key == "curve") file_rules.push_back({ "animation_open_fade_curve", target, value });
        } else if (in_close) {
            if (key == "enabled") file_rules.push_back({ "animation_close_fade_enabled", target, value });
            else if (key == "duration") file_rules.push_back({ "animation_close_fade_duration", target, value });
            else if (key == "curve") file_rules.push_back({ "animation_close_fade_curve", target, value });
        } else {
            if (key == "enabled") file_rules.push_back({ "animation_fade_enabled", target, value });
            else if (key == "duration") file_rules.push_back({ "animation_fade_duration", target, value });
            else if (key == "curve") file_rules.push_back({ "animation_fade_curve", target, value });
        }
        has_rules_in_file = true;
        return;
    }

    if (in_open) {
        if (key == "enabled") {
            file_rules.push_back({ "animation_open_enabled", target, value });
            has_rules_in_file = true;
        } else if (key == "style" || key == "animation") {
            file_rules.push_back({ "animation_open_style", target, value });
            has_rules_in_file = true;
        } else if (key == "duration") {
            file_rules.push_back({ "animation_open_duration", target, value });
            has_rules_in_file = true;
        } else if (key == "curve") {
            file_rules.push_back({ "animation_open_curve", target, value });
            has_rules_in_file = true;
        } else if (key == "scale") {
            file_rules.push_back({ "animation_open_scale", target, value });
            has_rules_in_file = true;
        }
        return;
    }

    if (in_close) {
        if (key == "enabled") {
            file_rules.push_back({ "animation_close_enabled", target, value });
            has_rules_in_file = true;
        } else if (key == "style" || key == "animation") {
            file_rules.push_back({ "animation_close_style", target, value });
            has_rules_in_file = true;
        } else if (key == "duration") {
            file_rules.push_back({ "animation_close_duration", target, value });
            has_rules_in_file = true;
        } else if (key == "curve") {
            file_rules.push_back({ "animation_close_curve", target, value });
            has_rules_in_file = true;
        } else if (key == "scale") {
            file_rules.push_back({ "animation_close_scale", target, value });
            has_rules_in_file = true;
        }
        return;
    }

    // Top-level properties in windowrule
    if (key == "float") {
        file_rules.push_back({ "float", target, "" });
        has_rules_in_file = true;
    } else if (key == "workspace") {
        file_rules.push_back({ "workspace", target, value });
        has_rules_in_file = true;
    } else if (key == "opacity") {
        file_rules.push_back({ "opacity", target, value });
        has_rules_in_file = true;
    } else if (key == "center") {
        file_rules.push_back({ "center", target, "" });
        has_rules_in_file = true;
    } else if (key == "size") {
        file_rules.push_back({ "size", target, value });
        has_rules_in_file = true;
    } else if (key == "blur") {
        file_rules.push_back({ "blur", target, value });
        has_rules_in_file = true;
    } else if (key == "immediate" || key == "tearing") {
        file_rules.push_back({ "immediate", target, value });
        has_rules_in_file = true;
    }
}

void Config::parse_binds_entry(const std::string& key, const std::string& value,
                               std::vector<KeyBinding>& file_bindings, bool& has_bindings_in_file) {
    if (key == "bind") {
        size_t comma = value.rfind(',');
        if (comma != std::string::npos) {
            std::string combo = trim(value.substr(0, comma));
            std::string action = trim(value.substr(comma + 1));
            uint32_t mods = 0;
            xkb_keysym_t sym = XKB_KEY_NoSymbol;
            if (parse_binding_combo(combo, mods, sym)) {
                has_bindings_in_file = true;
                xkb_keysym_t norm_sym = (sym >= XKB_KEY_A && sym <= XKB_KEY_Z) ? (sym - XKB_KEY_A + XKB_KEY_a) : sym;
                file_bindings.push_back({ mods, norm_sym, action, combo });
            }
        }
    }
}

void Config::load_file(const std::string& path, std::vector<KeyBinding>& file_bindings, bool& has_bindings_in_file,
                       std::vector<GestureBinding>& file_gestures, bool& has_gestures_in_file,
                       std::vector<WindowRule>& file_rules, bool& has_rules_in_file,
                       std::vector<MonitorRule>& file_monitors, bool& has_monitors_in_file,
                       std::vector<std::string>& file_exec_cmds, std::vector<std::string>& file_exec_once_cmds, int depth) {
    if (depth > 5) {
        log_error("Maximum config include depth exceeded for " + path);
        return;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        log_error("Could not open config file: " + path);
        return;
    }

    std::vector<SectionFrame> section_stack;
    std::string line;

    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        if (trimmed.back() == '{') {
            std::string stype, sparam;
            parse_section_header(trimmed, stype, sparam);
            section_stack.push_back({ stype, sparam });
            continue;
        }

        if (trimmed == "}") {
            if (!section_stack.empty()) section_stack.pop_back();
            continue;
        }

        size_t eq_pos = trimmed.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = trim(trimmed.substr(0, eq_pos));
        std::string value = trim(trimmed.substr(eq_pos + 1));
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        strip_inline_comment(key, value);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }

        std::string path_str = get_current_section_path(section_stack);
        std::string top_type = section_stack.empty() ? "" : section_stack.back().type;

        if (key == "source" || key == "include") {
            std::string resolved = resolve_path(value);
            if (!resolved.empty() && fs::exists(resolved)) {
                log_info("Sourcing configuration from " + resolved);
                load_file(resolved, file_bindings, has_bindings_in_file, file_gestures, has_gestures_in_file,
                          file_rules, has_rules_in_file, file_monitors, has_monitors_in_file,
                          file_exec_cmds, file_exec_once_cmds, depth + 1);
            } else {
                log_error("Config source file not found: " + value + " (resolved to " + resolved + ")");
            }
            continue;
        }

        if (is_in_path(section_stack, "layerrule")) {
            std::string ns = find_section_param(section_stack, "layerrule");
            parse_layerrule_entry(ns, path_str, top_type, section_stack, key, value);
        } else if (is_in_path(section_stack, "windowrule")) {
            std::string target = find_section_param(section_stack, "windowrule");
            parse_windowrule_entry(target, path_str, top_type, section_stack, key, value, file_rules, has_rules_in_file);
        } else if (path_str == "plugins" || top_type == "plugins" || key == "plugin") {
            parse_plugins_entry(key, value);
        } else if (path_str == "autostart" || top_type == "autostart" || key == "autostart" ||
                   key == "exec" || key == "exec_once" || key == "exec_always") {
            parse_autostart_entry(key, value, file_exec_cmds, file_exec_once_cmds);
        } else if (path_str == "monitors" || top_type == "monitors" || key == "monitor" || key == "output") {
            parse_monitors_entry(key, value, file_monitors, has_monitors_in_file);
        } else if (path_str == "cursor" || top_type == "cursor") {
            parse_cursor_entry(key, value);
        } else if (path_str.rfind("general", 0) == 0 || top_type == "general" || top_type == "gaps" || top_type == "border") {
            parse_general_entry(path_str, top_type, section_stack, key, value);
        } else if (path_str.rfind("decoration", 0) == 0 || top_type == "decoration" || top_type == "blur" || top_type == "shadow") {
            parse_decoration_entry(path_str, top_type, section_stack, key, value);
        } else if (path_str.rfind("animations", 0) == 0 || top_type == "animations" || top_type == "windows" || top_type == "layers" || top_type == "workspaces") {
            parse_animations_entry(path_str, top_type, section_stack, key, value);
        } else if (path_str.rfind("input", 0) == 0 || top_type == "input" || top_type == "keyboard" || top_type == "touchpad" || top_type == "mouse" || top_type == "touchscreen" || top_type == "gestures" || top_type == "workspace_swipe") {
            parse_input_entry(path_str, top_type, section_stack, key, value, file_gestures, has_gestures_in_file);
        } else if (path_str == "xwayland" || top_type == "xwayland") {
            parse_xwayland_entry(key, value);
        } else if (path_str == "binds" || top_type == "binds" || key == "bind") {
            parse_binds_entry(key, value, file_bindings, has_bindings_in_file);
        }
    }
}

void Config::load() {
    ensure_default_files();

    std::string path = get_config_file_path();
    bool has_bindings_in_file = false;
    bool has_gestures_in_file = false;
    bool has_rules_in_file = false;
    bool has_monitors_in_file = false;
    std::vector<KeyBinding> file_bindings;
    std::vector<GestureBinding> file_gestures;
    std::vector<WindowRule> file_rules;
    std::vector<MonitorRule> file_monitors;
    std::vector<std::string> file_exec_cmds;
    std::vector<std::string> file_exec_once_cmds;
    m_blurred_layers.clear();
    m_layer_rules.clear();
    m_plugins.clear();

    load_file(path, file_bindings, has_bindings_in_file, file_gestures, has_gestures_in_file, file_rules, has_rules_in_file, file_monitors, has_monitors_in_file, file_exec_cmds, file_exec_once_cmds, 0);

    m_keybindings = std::move(file_bindings);
    m_gesture_bindings = std::move(file_gestures);
    m_window_rules = std::move(file_rules);
    m_monitor_rules = std::move(file_monitors);
    m_exec_commands = std::move(file_exec_cmds);
    m_exec_once_commands = std::move(file_exec_once_cmds);

    log_info("Loaded configuration from " + path);
}

void Config::save() {
    std::string dir = get_config_dir_path();
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::string path = get_config_file_path();
    std::ofstream file(path);
    if (!file.is_open()) {
        log_error("Failed to write configuration file at " + path);
        return;
    }

    file << "# miquland configuration file\n\n";

    if (!m_plugins.empty()) {
        file << "plugins {\n";
        for (const auto& p : m_plugins) {
            file << "    load = " << p << "\n";
        }
        file << "}\n\n";
    }

    file << "autostart {\n";
    for (const auto& cmd : m_exec_once_commands) {
        file << "    exec_once = " << cmd << "\n";
    }
    for (const auto& cmd : m_exec_commands) {
        file << "    exec = " << cmd << "\n";
    }
    file << "}\n\n";

    file << "cursor {\n";
    if (!m_cursor_theme.empty()) file << "    theme = " << m_cursor_theme << "\n";
    file << "    size = " << m_cursor_size << "\n";
    file << "}\n\n";

    file << "monitors {\n";
    for (const auto& m : m_monitor_rules) {
        if (m.disabled) {
            file << "    monitor = " << (m.name.empty() ? "" : m.name) << ", disable\n";
        } else {
            file << "    monitor = " << (m.name.empty() ? "" : m.name) << ", ";
            if (m.width > 0 && m.height > 0) {
                file << m.width << "x" << m.height;
                if (m.refresh_rate > 0.0) file << "@" << m.refresh_rate;
            } else {
                file << "preferred";
            }
            file << ", ";
            if (m.x >= 0 && m.y >= 0) file << m.x << "x" << m.y;
            else file << "auto";
            file << ", " << m.scale;
            if (m.transform != WL_OUTPUT_TRANSFORM_NORMAL) {
                file << ", " << (int)m.transform;
            }
            file << "\n";
        }
    }
    file << "}\n\n";

    file << "general {\n";
    file << "    layout = " << (m_layout_mode == LayoutMode::Stack ? "stack" : "spiral") << "\n";
    file << "    default_split_ratio = " << m_default_split_ratio << "\n";
    file << "    smart_gaps = " << (m_smart_gaps ? "true" : "false") << "\n";
    file << "    allow_tearing = " << (m_allow_tearing ? "true" : "false") << "\n";
    file << "    workspace_cycle = " << (m_workspace_cycle ? "true" : "false") << "\n";
    file << "    resize_on_border = " << (m_resize_on_border ? "true" : "false") << "\n";
    file << "    border_grab_area = " << m_border_grab_area << "\n";
    file << "    gaps {\n";
    file << "        inner = " << m_space_between_windows << "\n";
    file << "        outer = " << m_screen_edge_padding << "\n";
    file << "    }\n";
    file << "    border {\n";
    file << "        width = " << m_window_border_width << "\n";
    file << "        radius = " << m_window_border_radius << "\n";
    file << "        active = " << m_window_border_color_active << "\n";
    file << "        inactive = " << m_window_border_color_inactive << "\n";
    file << "        shading = " << (m_window_border_shading ? "true" : "false") << "\n";
    file << "        shading_strength = " << m_window_border_shading_strength << "\n";
    file << "    }\n";
    file << "}\n\n";

    file << "decoration {\n";
    file << "    active_opacity = " << m_window_opacity_active << "\n";
    file << "    inactive_opacity = " << m_window_opacity_inactive << "\n";
    file << "    blur {\n";
    file << "        enabled = " << (m_blur_enabled ? "true" : "false") << "\n";
    file << "        radius = " << m_blur_radius << "\n";
    file << "        passes = " << m_blur_num_passes << "\n";
    file << "        noise = " << m_blur_noise << "\n";
    file << "        brightness = " << m_blur_brightness << "\n";
    file << "        contrast = " << m_blur_contrast << "\n";
    file << "        saturation = " << m_blur_saturation << "\n";
    file << "    }\n";
    file << "    shadow {\n";
    file << "        enabled = " << (m_shadow_enabled ? "true" : "false") << "\n";
    file << "        range = " << m_shadow_blur_sigma << "\n";
    file << "        color = " << m_shadow_color_active << "\n";
    file << "        color_inactive = " << m_shadow_color_inactive << "\n";
    file << "        offset = " << m_shadow_offset_x << " " << m_shadow_offset_y << "\n";
    file << "    }\n";
    file << "}\n\n";

    file << "animations {\n";
    file << "    enabled = " << (m_animations_enabled ? "true" : "false") << "\n";
    file << "    windows {\n";
    file << "        enabled = " << (m_window_animations_enabled ? "true" : "false") << "\n";
    file << "        duration = " << m_window_animation_duration_ms << "\n";
    file << "        curve = " << m_window_animation_curve << "\n";
    file << "        open {\n";
    file << "            duration = " << m_window_animation_open_duration_ms << "\n";
    file << "            curve = " << m_window_animation_open_curve << "\n";
    file << "            scale = " << m_window_animation_open_scale << "\n";
    file << "        }\n";
    file << "        close {\n";
    file << "            duration = " << m_window_animation_close_duration_ms << "\n";
    file << "            curve = " << m_window_animation_close_curve << "\n";
    file << "            scale = " << m_window_animation_close_scale << "\n";
    file << "        }\n";
    file << "    }\n";
    file << "    layers {\n";
    file << "        enabled = " << (m_layer_animations_enabled ? "true" : "false") << "\n";
    file << "        duration = " << m_layer_animation_duration_ms << "\n";
    file << "        curve = " << m_layer_animation_curve << "\n";
    file << "        popin_scale = " << m_layer_animation_popin_scale << "\n";
    file << "        open {\n";
    file << "            enabled = " << (m_layer_animation_open_enabled ? "true" : "false") << "\n";
    file << "            duration = " << m_layer_animation_open_duration_ms << "\n";
    file << "            curve = " << m_layer_animation_open_curve << "\n";
    file << "        }\n";
    file << "        close {\n";
    file << "            enabled = " << (m_layer_animation_close_enabled ? "true" : "false") << "\n";
    file << "            duration = " << m_layer_animation_close_duration_ms << "\n";
    file << "            curve = " << m_layer_animation_close_curve << "\n";
    file << "        }\n";
    file << "    }\n";
    file << "    workspaces {\n";
    file << "        enabled = " << (m_workspace_animations_enabled ? "true" : "false") << "\n";
    file << "        duration = " << m_workspace_animation_duration_ms << "\n";
    file << "        curve = " << m_workspace_animation_curve << "\n";
    file << "    }\n";
    file << "}\n\n";

    file << "input {\n";
    file << "    terminal = " << m_terminal << "\n";
    file << "    keyboard {\n";
    file << "        layout = " << m_kb_layout << "\n";
    if (!m_kb_variant.empty()) file << "        variant = " << m_kb_variant << "\n";
    if (!m_kb_options.empty()) file << "        options = " << m_kb_options << "\n";
    if (!m_kb_model.empty()) file << "        model = " << m_kb_model << "\n";
    file << "        repeat_rate = " << m_repeat_rate << "\n";
    file << "        repeat_delay = " << m_repeat_delay << "\n";
    file << "    }\n";
    file << "    touchpad {\n";
    file << "        tap_to_click = " << (m_tap_to_click ? "true" : "false") << "\n";
    file << "        natural_scroll = " << (m_natural_scroll ? "true" : "false") << "\n";
    file << "        disable_while_typing = " << (m_dwt ? "true" : "false") << "\n";
    file << "        accel_speed = " << m_accel_speed << "\n";
    file << "        accel_profile = " << m_accel_profile << "\n";
    file << "    }\n";
    file << "    mouse {\n";
    file << "        accel_speed = " << m_accel_speed << "\n";
    file << "        accel_profile = " << m_accel_profile << "\n";
    file << "        natural_scroll = " << (m_natural_scroll ? "true" : "false") << "\n";
    file << "    }\n";
    if (!m_touchscreen_output.empty()) {
        file << "    touchscreen {\n";
        file << "        output = " << m_touchscreen_output << "\n";
        file << "    }\n";
    }
    file << "    gestures {\n";
    file << "        workspace_swipe {\n";
    file << "            touchpad_multiplier = " << m_touchpad_workspace_swipe_multiplier << "\n";
    file << "            touchscreen_multiplier = " << m_touchscreen_workspace_swipe_multiplier << "\n";
    file << "            swipe_threshold = " << m_touchpad_swipe_threshold << "\n";
    file << "            touchscreen_threshold = " << m_touchscreen_swipe_threshold << "\n";
    file << "            cancel_ratio = " << m_workspace_swipe_cancel_ratio << "\n";
    file << "            min_speed_to_force = " << m_workspace_swipe_min_speed_to_force << "\n";
    file << "            edge_resistance = " << m_workspace_swipe_edge_resistance << "\n";
    file << "        }\n";
    for (const auto& g : m_gesture_bindings) {
        file << "        gesture = " << g.pattern << ", " << g.action << "\n";
    }
    file << "    }\n";
    file << "}\n\n";

    file << "xwayland {\n";
    file << "    force_zero_scaling = " << (m_xwayland_force_zero_scaling ? "true" : "false") << "\n";
    file << "}\n\n";

    for (const auto& lr : m_layer_rules) {
        file << "layerrule \"" << lr.ns_pattern << "\" {\n";
        if (is_layer_blur_enabled(lr.ns_pattern)) {
            file << "    blur = true\n";
        }
        if (lr.anim_style_open != LayerAnimStyle::DefaultAuto || lr.duration_open_ms > 0 || !lr.curve_open.empty()) {
            file << "    open {\n";
            if (lr.noanim_open) file << "        enabled = false\n";
            if (lr.anim_style_open != LayerAnimStyle::DefaultAuto) {
                file << "        animation = " << layer_anim_style_to_string(lr.anim_style_open) << "\n";
            }
            if (lr.duration_open_ms > 0) file << "        duration = " << lr.duration_open_ms << "\n";
            if (!lr.curve_open.empty()) file << "        curve = " << lr.curve_open << "\n";
            file << "    }\n";
        }
        if (lr.anim_style_close != LayerAnimStyle::DefaultAuto || lr.duration_close_ms > 0 || !lr.curve_close.empty()) {
            file << "    close {\n";
            if (lr.noanim_close) file << "        enabled = false\n";
            if (lr.anim_style_close != LayerAnimStyle::DefaultAuto) {
                file << "        animation = " << (lr.anim_style_close == LayerAnimStyle::Unroll ? "fold" : layer_anim_style_to_string(lr.anim_style_close)) << "\n";
            }
            if (lr.duration_close_ms > 0) file << "        duration = " << lr.duration_close_ms << "\n";
            if (!lr.curve_close.empty()) file << "        curve = " << lr.curve_close << "\n";
            file << "    }\n";
        }
        file << "}\n\n";
    }

    for (const auto& layer : m_blurred_layers) {
        if (get_layer_rule(layer).ns_pattern.empty()) {
            file << "layerrule \"" << layer << "\" {\n";
            file << "    blur = true\n";
            file << "}\n\n";
        }
    }

    for (const auto& r : m_window_rules) {
        file << "windowrule \"" << r.target << "\" {\n";
        if (r.rule == "float") file << "    float = true\n";
        else if (r.rule == "workspace") file << "    workspace = " << r.extra << "\n";
        else if (r.rule == "opacity") file << "    opacity = " << r.extra << "\n";
        else if (r.rule == "center") file << "    center = true\n";
        else if (r.rule == "size") file << "    size = " << r.extra << "\n";
        else if (r.rule == "immediate" || r.rule == "tearing") file << "    immediate = true\n";
        file << "}\n\n";
    }

    file << "binds {\n";
    for (const auto& kb : m_keybindings) {
        file << "    bind = " << kb.combo_str << ", " << kb.action << "\n";
    }
    file << "}\n";

    log_info("Saved configuration to " + path);
}

} // namespace miquland
