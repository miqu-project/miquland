#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <algorithm>
#include <xkbcommon/xkbcommon.h>
#include <wayland-server-protocol.h>
#include "core/common/border_paint.hpp"

namespace miquland {

struct MonitorRule {
    std::string name;             // eDP-1, HDMI-A-1, or "" / "*" for default
    bool disabled = false;        // true if "disable"
    int width = 0;                // 0 = preferred
    int height = 0;
    double refresh_rate = 0.0;    // 0 = preferred, e.g. 60.0
    int x = -1;                   // -1 = auto placement
    int y = -1;
    double scale = 1.0;
    enum wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
};

struct KeyBinding {
    uint32_t modifiers = 0;
    xkb_keysym_t keysym = XKB_KEY_NoSymbol;
    std::string action; // e.g. "firefox", "foot -e yazi", or built-in "menu", "close", "toggle_bar", "prev_window", "next_window", "ws_1", etc.
    std::string combo_str;
};

struct GestureBinding {
    std::string pattern; // e.g. "swipe:3:left"
    std::string action;  // e.g. "next_ws"
};

struct WindowRule {
    std::string rule;    // e.g. "float", "workspace", "opacity"
    std::string target;  // app_id or title
    std::string extra;   // e.g. workspace id or opacity float string
};

class Config {
public:
    static Config& get();

    void load();
    void save();

    bool is_focus_follows_mouse_enabled() const { return true; }

    bool is_smart_gaps_enabled() const { return m_smart_gaps; }
    void set_smart_gaps_enabled(bool enabled) { m_smart_gaps = enabled; }

    bool is_tearing_allowed() const { return m_allow_tearing; }
    void set_tearing_allowed(bool enabled) { m_allow_tearing = enabled; }

    bool is_animations_enabled() const { return m_animations_enabled; }
    void set_animations_enabled(bool enabled) { m_animations_enabled = enabled; }

    // Workspace animations
    bool is_workspace_animations_enabled() const { return m_animations_enabled && m_workspace_animations_enabled; }
    void set_workspace_animations_enabled(bool enabled) { m_workspace_animations_enabled = enabled; }

    int get_workspace_animation_duration_ms() const { return m_workspace_animation_duration_ms; }
    void set_workspace_animation_duration_ms(int ms) { m_workspace_animation_duration_ms = std::clamp(ms, 10, 2000); }

    const std::string& get_workspace_animation_curve() const { return m_workspace_animation_curve; }
    void set_workspace_animation_curve(const std::string& curve) { m_workspace_animation_curve = curve; }

    // Window animations
    bool is_window_animations_enabled() const { return m_animations_enabled && m_window_animations_enabled; }
    void set_window_animations_enabled(bool enabled) { m_window_animations_enabled = enabled; }

    int get_window_animation_duration_ms() const { return m_window_animation_duration_ms; }
    void set_window_animation_duration_ms(int ms) {
        m_window_animation_duration_ms = std::clamp(ms, 10, 2000);
        m_window_animation_open_duration_ms = m_window_animation_duration_ms;
        m_window_animation_close_duration_ms = m_window_animation_duration_ms;
    }

    int get_window_animation_open_duration_ms() const { return m_window_animation_open_duration_ms; }
    void set_window_animation_open_duration_ms(int ms) { m_window_animation_open_duration_ms = std::clamp(ms, 10, 2000); }

    int get_window_animation_close_duration_ms() const { return m_window_animation_close_duration_ms; }
    void set_window_animation_close_duration_ms(int ms) { m_window_animation_close_duration_ms = std::clamp(ms, 10, 2000); }

    int get_window_animation_fade_in_duration_ms() const {
        return (m_window_animation_fade_in_duration_ms > 0) ? m_window_animation_fade_in_duration_ms : m_window_animation_open_duration_ms;
    }
    void set_window_animation_fade_in_duration_ms(int ms) { m_window_animation_fade_in_duration_ms = std::clamp(ms, 10, 2000); }

    int get_window_animation_fade_out_duration_ms() const {
        return (m_window_animation_fade_out_duration_ms > 0) ? m_window_animation_fade_out_duration_ms : m_window_animation_close_duration_ms;
    }
    void set_window_animation_fade_out_duration_ms(int ms) { m_window_animation_fade_out_duration_ms = std::clamp(ms, 10, 2000); }

    const std::string& get_window_animation_curve() const { return m_window_animation_curve; }
    void set_window_animation_curve(const std::string& curve) {
        m_window_animation_curve = curve;
        m_window_animation_open_curve = curve;
        m_window_animation_close_curve = curve;
    }

    const std::string& get_window_animation_open_curve() const { return m_window_animation_open_curve; }
    void set_window_animation_open_curve(const std::string& curve) { m_window_animation_open_curve = curve; }

    const std::string& get_window_animation_close_curve() const { return m_window_animation_close_curve; }
    void set_window_animation_close_curve(const std::string& curve) { m_window_animation_close_curve = curve; }

    const std::string& get_window_animation_fade_in_curve() const {
        return m_window_animation_fade_in_curve.empty() ? m_window_animation_open_curve : m_window_animation_fade_in_curve;
    }
    void set_window_animation_fade_in_curve(const std::string& curve) { m_window_animation_fade_in_curve = curve; }

    const std::string& get_window_animation_fade_out_curve() const {
        return m_window_animation_fade_out_curve.empty() ? m_window_animation_close_curve : m_window_animation_fade_out_curve;
    }
    void set_window_animation_fade_out_curve(const std::string& curve) { m_window_animation_fade_out_curve = curve; }

    double get_window_animation_open_scale() const { return m_window_animation_open_scale; }
    void set_window_animation_open_scale(double s) { m_window_animation_open_scale = std::clamp(s, 0.1, 1.0); }

    double get_window_animation_close_scale() const { return m_window_animation_close_scale; }
    void set_window_animation_close_scale(double s) { m_window_animation_close_scale = std::clamp(s, 0.1, 1.0); }

    bool is_window_open_animation_enabled() const { return is_window_animations_enabled() && m_window_open_animation_enabled; }
    void set_window_open_animation_enabled(bool enabled) { m_window_open_animation_enabled = enabled; }

    bool is_window_close_animation_enabled() const { return is_window_animations_enabled() && m_window_close_animation_enabled; }
    void set_window_close_animation_enabled(bool enabled) { m_window_close_animation_enabled = enabled; }

    bool is_window_open_fade_enabled() const { return is_window_animations_enabled() && m_window_open_fade_enabled; }
    void set_window_open_fade_enabled(bool enabled) { m_window_open_fade_enabled = enabled; }

    bool is_window_close_fade_enabled() const { return is_window_animations_enabled() && m_window_close_fade_enabled; }
    void set_window_close_fade_enabled(bool enabled) { m_window_close_fade_enabled = enabled; }

    bool is_window_animation_fade_enabled() const { return m_window_open_fade_enabled || m_window_close_fade_enabled; }
    void set_window_animation_fade_enabled(bool enabled) {
        m_window_open_fade_enabled = enabled;
        m_window_close_fade_enabled = enabled;
    }

    // Backward compatibility aliases
    int get_animation_duration_ms() const { return m_workspace_animation_duration_ms; }
    void set_animation_duration_ms(int ms) { set_workspace_animation_duration_ms(ms); }

    const std::string& get_animation_curve() const { return m_workspace_animation_curve; }
    void set_animation_curve(const std::string& curve) { set_workspace_animation_curve(curve); }

    bool is_xwayland_force_zero_scaling_enabled() const { return m_xwayland_force_zero_scaling; }
    void set_xwayland_force_zero_scaling_enabled(bool enabled) { m_xwayland_force_zero_scaling = enabled; }

    const std::string& get_cursor_theme() const { return m_cursor_theme; }
    void set_cursor_theme(const std::string& theme) { m_cursor_theme = theme; }

    int get_cursor_size() const { return m_cursor_size; }
    void set_cursor_size(int size) { m_cursor_size = std::clamp(size, 8, 128); }

    double get_default_split_ratio() const { return m_default_split_ratio; }
    void set_default_split_ratio(double ratio) { m_default_split_ratio = std::clamp(ratio, 0.1, 0.9); }

    bool is_workspace_cycle_enabled() const { return m_workspace_cycle; }
    void set_workspace_cycle_enabled(bool enabled) { m_workspace_cycle = enabled; }

    // Auto-float fixed-size windows: when a toplevel declares min==max size constraints
    // (i.e. a non-resizable window), automatically treat it as floating and center it.
    bool is_auto_float_fixed_size_enabled() const { return m_auto_float_fixed_size; }
    void set_auto_float_fixed_size_enabled(bool enabled) { m_auto_float_fixed_size = enabled; }

    bool is_tap_to_click_enabled() const { return m_tap_to_click; }
    void set_tap_to_click_enabled(bool enabled) { m_tap_to_click = enabled; }

    bool is_natural_scroll_enabled() const { return m_natural_scroll; }
    void set_natural_scroll_enabled(bool enabled) { m_natural_scroll = enabled; }

    bool is_dwt_enabled() const { return m_dwt; }
    void set_dwt_enabled(bool enabled) { m_dwt = enabled; }

    double get_accel_speed() const { return m_accel_speed; }
    void set_accel_speed(double speed) { m_accel_speed = std::clamp(speed, -1.0, 1.0); }

    const std::string& get_accel_profile() const { return m_accel_profile; }
    void set_accel_profile(const std::string& profile) { m_accel_profile = profile; }

    const std::string& get_touchscreen_output() const { return m_touchscreen_output; }
    void set_touchscreen_output(const std::string& output) { m_touchscreen_output = output; }

    const std::string& get_kb_layout() const { return m_kb_layout; }
    void set_kb_layout(const std::string& l) { m_kb_layout = l; }

    const std::string& get_kb_variant() const { return m_kb_variant; }
    void set_kb_variant(const std::string& v) { m_kb_variant = v; }

    const std::string& get_kb_options() const { return m_kb_options; }
    void set_kb_options(const std::string& opt) { m_kb_options = opt; }

    const std::string& get_kb_model() const { return m_kb_model; }
    void set_kb_model(const std::string& m) { m_kb_model = m; }

    int get_repeat_rate() const { return m_repeat_rate; }
    void set_repeat_rate(int rate) { m_repeat_rate = std::max(1, rate); }

    int get_repeat_delay() const { return m_repeat_delay; }
    void set_repeat_delay(int delay) { m_repeat_delay = std::max(100, delay); }

    const std::string& get_terminal() const { return m_terminal; }
    void set_terminal(const std::string& term) { m_terminal = term; }

    int get_window_border_width() const { return m_window_border_width; }
    void set_window_border_width(int w) { m_window_border_width = w; }

    int get_window_border_radius() const { return m_window_border_radius; }
    void set_window_border_radius(int r) { m_window_border_radius = r; }

    bool is_resize_on_border_enabled() const { return m_resize_on_border; }
    void set_resize_on_border_enabled(bool enabled) { m_resize_on_border = enabled; }

    int get_border_grab_area() const { return m_border_grab_area; }
    void set_border_grab_area(int area) { m_border_grab_area = area; }

    const std::string& get_window_border_color_active() const { return m_window_border_color_active; }
    void set_window_border_color_active(const std::string& color);

    const std::string& get_window_border_color_inactive() const { return m_window_border_color_inactive; }
    void set_window_border_color_inactive(const std::string& color);

    const BorderPaint& get_window_border_paint_active() const { return m_border_paint_active; }
    const BorderPaint& get_window_border_paint_inactive() const { return m_border_paint_inactive; }

    bool is_window_border_shading_enabled() const { return m_window_border_shading; }
    void set_window_border_shading_enabled(bool enabled) { m_window_border_shading = enabled; }

    float get_window_border_shading_strength() const { return m_window_border_shading_strength; }
    void set_window_border_shading_strength(float strength) { m_window_border_shading_strength = std::clamp(strength, 0.0f, 1.0f); }

    // Drop shadows (SceneFX)
    bool is_shadow_enabled() const { return m_shadow_enabled; }
    void set_shadow_enabled(bool enabled) { m_shadow_enabled = enabled; }

    float get_shadow_blur_sigma() const { return m_shadow_blur_sigma; }
    void set_shadow_blur_sigma(float sigma) { m_shadow_blur_sigma = std::max(0.0f, sigma); }

    const std::string& get_shadow_color_active() const { return m_shadow_color_active; }
    void set_shadow_color_active(const std::string& color) { m_shadow_color_active = color; }

    const std::string& get_shadow_color_inactive() const { return m_shadow_color_inactive; }
    void set_shadow_color_inactive(const std::string& color) { m_shadow_color_inactive = color; }

    int get_shadow_offset_x() const { return m_shadow_offset_x; }
    int get_shadow_offset_y() const { return m_shadow_offset_y; }
    void set_shadow_offset(int x, int y) { m_shadow_offset_x = x; m_shadow_offset_y = y; }

    enum class LayoutMode {
        Spiral, // Recursive binary space partitioning (Fibonacci / BSP)
        Stack   // Main prominent window on left, vertical stack on right
    };

    LayoutMode get_layout_mode() const { return m_layout_mode; }
    void set_layout_mode(LayoutMode mode) { m_layout_mode = mode; }
    void toggle_layout_mode() { m_layout_mode = (m_layout_mode == LayoutMode::Spiral) ? LayoutMode::Stack : LayoutMode::Spiral; }

    int get_space_between_windows() const { return m_space_between_windows; }
    void set_space_between_windows(int space) { m_space_between_windows = space; }

    int get_screen_edge_padding() const { return m_screen_edge_padding; }
    void set_screen_edge_padding(int pad) { m_screen_edge_padding = pad; }

    float get_window_opacity_active() const { return m_window_opacity_active; }
    void set_window_opacity_active(float op) { m_window_opacity_active = std::clamp(op, 0.0f, 1.0f); }

    float get_window_opacity_inactive() const { return m_window_opacity_inactive; }
    void set_window_opacity_inactive(float op) { m_window_opacity_inactive = std::clamp(op, 0.0f, 1.0f); }

    bool is_blur_enabled() const { return m_blur_enabled; }
    void set_blur_enabled(bool enabled) { m_blur_enabled = enabled; }

    int get_blur_radius() const { return m_blur_radius; }
    void set_blur_radius(int r) { m_blur_radius = std::max(1, r); }

    int get_blur_passes() const { return m_blur_num_passes; }
    int get_blur_num_passes() const { return m_blur_num_passes; }
    void set_blur_passes(int p) { m_blur_num_passes = std::max(1, p); }

    float get_blur_noise() const { return m_blur_noise; }
    void set_blur_noise(float n) { m_blur_noise = n; }

    float get_blur_brightness() const { return m_blur_brightness; }
    void set_blur_brightness(float b) { m_blur_brightness = b; }

    float get_blur_contrast() const { return m_blur_contrast; }
    void set_blur_contrast(float c) { m_blur_contrast = c; }

    float get_blur_saturation() const { return m_blur_saturation; }
    void set_blur_saturation(float s) { m_blur_saturation = s; }

    bool is_layer_blur_enabled(const std::string& ns) const;
    void add_blurred_layer(const std::string& ns);
    const std::vector<std::string>& get_blurred_layers() const { return m_blurred_layers; }

    enum class LayerAnimStyle {
        DefaultAuto,
        Slide,
        SlideTop,
        SlideBottom,
        SlideLeft,
        SlideRight,
        Popin,
        Fade,
        Unroll,
        Unfold,
        None
    };

    struct LayerRule {
        std::string ns_pattern;
        LayerAnimStyle anim_style_open = LayerAnimStyle::DefaultAuto;
        LayerAnimStyle anim_style_close = LayerAnimStyle::DefaultAuto;
        double popin_scale = 0.90;
        int duration_open_ms = -1;
        int duration_close_ms = -1;
        std::string curve_open = "";
        std::string curve_close = "";
        bool noanim_open = false;
        bool noanim_close = false;
        bool fade_open_enabled = true;
        int fade_open_duration_ms = -1;
        std::string fade_open_curve = "";
        bool fade_close_enabled = true;
        int fade_close_duration_ms = -1;
        std::string fade_close_curve = "";
    };

    bool is_layer_animations_enabled() const { return m_layer_animations_enabled; }
    void set_layer_animations_enabled(bool val) { m_layer_animations_enabled = val; }

    bool is_layer_open_animation_enabled() const { return m_layer_animations_enabled && m_layer_animation_open_enabled; }
    void set_layer_open_animation_enabled(bool val) { m_layer_animation_open_enabled = val; }

    bool is_layer_close_animation_enabled() const { return m_layer_animations_enabled && m_layer_animation_close_enabled; }
    void set_layer_close_animation_enabled(bool val) { m_layer_animation_close_enabled = val; }

    int get_layer_animation_duration_ms() const { return m_layer_animation_duration_ms; }
    void set_layer_animation_duration_ms(int d) { m_layer_animation_duration_ms = std::max(10, d); }

    int get_layer_open_duration_ms() const {
        return (m_layer_animation_open_duration_ms > 0) ? m_layer_animation_open_duration_ms : m_layer_animation_duration_ms;
    }
    void set_layer_open_duration_ms(int d) { m_layer_animation_open_duration_ms = d; }

    int get_layer_close_duration_ms() const {
        return (m_layer_animation_close_duration_ms > 0) ? m_layer_animation_close_duration_ms : m_layer_animation_duration_ms;
    }
    void set_layer_close_duration_ms(int d) { m_layer_animation_close_duration_ms = d; }

    const std::string& get_layer_animation_curve() const { return m_layer_animation_curve; }
    void set_layer_animation_curve(const std::string& c) { m_layer_animation_curve = c; }

    std::string get_layer_open_curve() const {
        return (!m_layer_animation_open_curve.empty()) ? m_layer_animation_open_curve : m_layer_animation_curve;
    }
    void set_layer_open_curve(const std::string& c) { m_layer_animation_open_curve = c; }

    std::string get_layer_close_curve() const {
        return (!m_layer_animation_close_curve.empty()) ? m_layer_animation_close_curve : m_layer_animation_curve;
    }
    void set_layer_close_curve(const std::string& c) { m_layer_animation_close_curve = c; }

    double get_layer_animation_popin_scale() const { return m_layer_animation_popin_scale; }
    void set_layer_animation_popin_scale(double s) { m_layer_animation_popin_scale = std::clamp(s, 0.1, 1.0); }

    bool is_layer_animation_fade_enabled() const { return m_layer_animation_fade_enabled; }
    void set_layer_animation_fade_enabled(bool val) { m_layer_animation_fade_enabled = val; }

    int get_layer_animation_fade_in_duration_ms() const {
        return (m_layer_animation_fade_in_duration_ms > 0) ? m_layer_animation_fade_in_duration_ms : get_layer_open_duration_ms();
    }
    void set_layer_animation_fade_in_duration_ms(int ms) { m_layer_animation_fade_in_duration_ms = std::clamp(ms, 0, 2000); }

    int get_layer_animation_fade_out_duration_ms() const {
        return (m_layer_animation_fade_out_duration_ms > 0) ? m_layer_animation_fade_out_duration_ms : get_layer_close_duration_ms();
    }
    void set_layer_animation_fade_out_duration_ms(int ms) { m_layer_animation_fade_out_duration_ms = std::clamp(ms, 0, 2000); }

    std::string get_layer_animation_fade_in_curve() const {
        return (!m_layer_animation_fade_in_curve.empty()) ? m_layer_animation_fade_in_curve : get_layer_open_curve();
    }
    void set_layer_animation_fade_in_curve(const std::string& c) { m_layer_animation_fade_in_curve = c; }

    std::string get_layer_animation_fade_out_curve() const {
        return (!m_layer_animation_fade_out_curve.empty()) ? m_layer_animation_fade_out_curve : get_layer_close_curve();
    }
    void set_layer_animation_fade_out_curve(const std::string& c) { m_layer_animation_fade_out_curve = c; }

    LayerRule get_layer_rule(const std::string& ns) const;
    void add_layer_rule(const LayerRule& rule);
    const std::vector<LayerRule>& get_layer_rules() const { return m_layer_rules; }

    bool is_window_open_animation_enabled(const std::string& app_id, const std::string& title) const;
    bool is_window_close_animation_enabled(const std::string& app_id, const std::string& title) const;
    int get_window_open_duration_ms(const std::string& app_id, const std::string& title) const;
    int get_window_close_duration_ms(const std::string& app_id, const std::string& title) const;
    std::string get_window_open_curve(const std::string& app_id, const std::string& title) const;
    std::string get_window_close_curve(const std::string& app_id, const std::string& title) const;
    double get_window_open_scale(const std::string& app_id, const std::string& title) const;
    double get_window_close_scale(const std::string& app_id, const std::string& title) const;
    bool is_window_open_fade_enabled(const std::string& app_id, const std::string& title) const;
    bool is_window_close_fade_enabled(const std::string& app_id, const std::string& title) const;
    int get_window_open_fade_duration_ms(const std::string& app_id, const std::string& title) const;
    int get_window_close_fade_duration_ms(const std::string& app_id, const std::string& title) const;
    std::string get_window_open_fade_curve(const std::string& app_id, const std::string& title) const;
    std::string get_window_close_fade_curve(const std::string& app_id, const std::string& title) const;

    static bool parse_hex_color(const std::string& hex, float& r, float& g, float& b, float& a);

    const std::vector<KeyBinding>& get_keybindings() const { return m_keybindings; }
    const std::vector<GestureBinding>& get_gesture_bindings() const { return m_gesture_bindings; }
    std::string find_gesture_action(const std::string& pattern) const;
    bool has_gesture_for_fingers(int fingers) const;
    void add_or_update_gesture_binding(const std::string& pattern, const std::string& action);

    double get_touchpad_workspace_swipe_multiplier() const { return m_touchpad_workspace_swipe_multiplier; }
    void set_touchpad_workspace_swipe_multiplier(double mult) { m_touchpad_workspace_swipe_multiplier = std::clamp(mult, 0.1, 10.0); }

    double get_touchpad_swipe_threshold() const { return m_touchpad_swipe_threshold; }
    void set_touchpad_swipe_threshold(double threshold) { m_touchpad_swipe_threshold = std::max(5.0, threshold); }

    double get_touchscreen_workspace_swipe_multiplier() const { return m_touchscreen_workspace_swipe_multiplier; }
    void set_touchscreen_workspace_swipe_multiplier(double mult) { m_touchscreen_workspace_swipe_multiplier = std::clamp(mult, 0.1, 10.0); }

    double get_touchscreen_swipe_threshold() const { return m_touchscreen_swipe_threshold; }
    void set_touchscreen_swipe_threshold(double threshold) { m_touchscreen_swipe_threshold = std::max(5.0, threshold); }

    double get_workspace_swipe_cancel_ratio() const { return m_workspace_swipe_cancel_ratio; }
    void set_workspace_swipe_cancel_ratio(double ratio) { m_workspace_swipe_cancel_ratio = std::clamp(ratio, 0.05, 0.95); }

    double get_workspace_swipe_min_speed_to_force() const { return m_workspace_swipe_min_speed_to_force; }
    void set_workspace_swipe_min_speed_to_force(double speed) { m_workspace_swipe_min_speed_to_force = std::clamp(speed, 0.05, 10.0); }

    double get_workspace_swipe_edge_resistance() const { return m_workspace_swipe_edge_resistance; }
    void set_workspace_swipe_edge_resistance(double res) { m_workspace_swipe_edge_resistance = std::clamp(res, 0.0, 0.5); }

    const std::vector<WindowRule>& get_window_rules() const { return m_window_rules; }
    void add_window_rule(const WindowRule& rule) { m_window_rules.push_back(rule); }
    bool should_float(const std::string& app_id, const std::string& title) const;
    bool should_tear(const std::string& app_id, const std::string& title) const;
    int get_target_workspace(const std::string& app_id, const std::string& title) const;
    float get_rule_opacity(const std::string& app_id, const std::string& title, float default_val) const;

    const std::vector<MonitorRule>& get_monitor_rules() const { return m_monitor_rules; }
    void add_monitor_rule(const MonitorRule& rule) { m_monitor_rules.push_back(rule); }
    const MonitorRule* find_monitor_rule(const std::string& name) const;

    const std::vector<std::string>& get_exec_commands() const { return m_exec_commands; }
    const std::vector<std::string>& get_exec_once_commands() const { return m_exec_once_commands; }
    const std::vector<std::string>& get_plugins() const { return m_plugins; }

    static std::string get_config_file_path();
    static std::string get_config_dir_path();

    static bool parse_binding_combo(const std::string& combo, uint32_t& out_modifiers, xkb_keysym_t& out_keysym);
    void add_or_update_binding(uint32_t mods, xkb_keysym_t sym, const std::string& action, const std::string& combo);

    struct SectionFrame {
        std::string type;
        std::string param;
    };

private:
    Config();
    void set_defaults();
    void ensure_default_files();
    void load_file(const std::string& path, std::vector<KeyBinding>& file_bindings, bool& has_bindings_in_file,
                   std::vector<GestureBinding>& file_gestures, bool& has_gestures_in_file,
                   std::vector<WindowRule>& file_rules, bool& has_rules_in_file,
                   std::vector<MonitorRule>& file_monitors, bool& has_monitors_in_file,
                   std::vector<std::string>& file_exec_cmds, std::vector<std::string>& file_exec_once_cmds, int depth = 0);
    std::string resolve_path(const std::string& path) const;

    // Modular section parsers
    void parse_plugins_entry(const std::string& key, const std::string& value);
    void parse_autostart_entry(const std::string& key, const std::string& value,
                               std::vector<std::string>& file_exec_cmds, std::vector<std::string>& file_exec_once_cmds);
    void parse_monitors_entry(const std::string& key, const std::string& value,
                              std::vector<MonitorRule>& file_monitors, bool& has_monitors_in_file);
    void parse_cursor_entry(const std::string& key, const std::string& value);
    void parse_general_entry(const std::string& path_str, const std::string& top_type,
                             const std::vector<SectionFrame>& stack, const std::string& key, const std::string& value);
    void parse_decoration_entry(const std::string& path_str, const std::string& top_type,
                                const std::vector<SectionFrame>& stack, const std::string& key, const std::string& value);
    void parse_animations_entry(const std::string& path_str, const std::string& top_type,
                                const std::vector<SectionFrame>& stack, const std::string& key, const std::string& value);
    void parse_input_entry(const std::string& path_str, const std::string& top_type,
                           const std::vector<SectionFrame>& stack, const std::string& key, const std::string& value,
                           std::vector<GestureBinding>& file_gestures, bool& has_gestures_in_file);
    void parse_xwayland_entry(const std::string& key, const std::string& value);
    void parse_layerrule_entry(const std::string& ns, const std::string& path_str, const std::string& top_type,
                               const std::vector<SectionFrame>& stack, const std::string& key, const std::string& value);
    void parse_windowrule_entry(const std::string& target, const std::string& path_str, const std::string& top_type,
                                const std::vector<SectionFrame>& stack, const std::string& key, const std::string& value,
                                std::vector<WindowRule>& file_rules, bool& has_rules_in_file);
    void parse_binds_entry(const std::string& key, const std::string& value,
                           std::vector<KeyBinding>& file_bindings, bool& has_bindings_in_file);

    std::vector<MonitorRule> m_monitor_rules;
    std::vector<std::string> m_exec_commands;
    std::vector<std::string> m_exec_once_commands;

    bool m_smart_gaps = false;
    bool m_animations_enabled = true;
    bool m_workspace_animations_enabled = true;
    int m_workspace_animation_duration_ms = 200;
    std::string m_workspace_animation_curve = "ease_out_cubic";
    bool m_window_animations_enabled = true;
    bool m_window_open_animation_enabled = true;
    bool m_window_close_animation_enabled = true;
    int m_window_animation_duration_ms = 180;
    int m_window_animation_open_duration_ms = 180;
    int m_window_animation_close_duration_ms = 140;
    std::string m_window_animation_curve = "default";
    std::string m_window_animation_open_curve = "default";
    std::string m_window_animation_close_curve = "smooth_out";
    double m_window_animation_open_scale = 0.85;
    double m_window_animation_close_scale = 0.85;
    bool m_window_open_fade_enabled = true;
    bool m_window_close_fade_enabled = true;
    int m_window_animation_fade_in_duration_ms = 0;
    int m_window_animation_fade_out_duration_ms = 0;
    std::string m_window_animation_fade_in_curve = "";
    std::string m_window_animation_fade_out_curve = "";
    bool m_layer_animations_enabled = true;
    bool m_layer_animation_open_enabled = true;
    bool m_layer_animation_close_enabled = true;
    int m_layer_animation_duration_ms = 200;
    int m_layer_animation_open_duration_ms = -1;
    int m_layer_animation_close_duration_ms = -1;
    std::string m_layer_animation_curve = "ease_out_cubic";
    std::string m_layer_animation_open_curve = "";
    std::string m_layer_animation_close_curve = "";
    double m_layer_animation_popin_scale = 0.90;
    bool m_layer_animation_fade_enabled = true;
    int m_layer_animation_fade_in_duration_ms = 0;
    int m_layer_animation_fade_out_duration_ms = 0;
    std::string m_layer_animation_fade_in_curve = "";
    std::string m_layer_animation_fade_out_curve = "";
    std::vector<LayerRule> m_layer_rules;

    bool m_xwayland_force_zero_scaling = false;
    std::string m_cursor_theme = "";
    int m_cursor_size = 24;
    double m_default_split_ratio = 0.5;
    bool m_workspace_cycle = true;
    bool m_auto_float_fixed_size = true;

    bool m_tap_to_click = true;
    bool m_natural_scroll = true;
    bool m_dwt = true;
    double m_accel_speed = 0.0;
    std::string m_accel_profile = "adaptive";
    double m_touchpad_workspace_swipe_multiplier = 1.0;
    double m_touchpad_swipe_threshold = 50.0;
    double m_touchscreen_workspace_swipe_multiplier = 1.0;
    double m_touchscreen_swipe_threshold = 100.0;
    double m_workspace_swipe_cancel_ratio = 0.30;
    double m_workspace_swipe_min_speed_to_force = 0.3;
    double m_workspace_swipe_edge_resistance = 0.15;
    std::string m_touchscreen_output = "";
    std::string m_kb_layout = "us";
    std::string m_kb_variant = "";
    std::string m_kb_options = "";
    std::string m_kb_model = "";
    int m_repeat_rate = 25;
    int m_repeat_delay = 600;
    std::string m_terminal = "foot";

    int m_window_border_width = 2;
    int m_window_border_radius = 8;
    bool m_resize_on_border = true;
    int m_border_grab_area = 5;
    int m_space_between_windows = 8;
    int m_screen_edge_padding = 10;
    bool m_allow_tearing = false;
    float m_window_opacity_active = 1.0f;
    float m_window_opacity_inactive = 0.85f;
    bool m_blur_enabled = true;
    int m_blur_radius = 5;
    int m_blur_num_passes = 3;
    float m_blur_noise = 0.02f;
    float m_blur_brightness = 0.9f;
    float m_blur_contrast = 0.9f;
    float m_blur_saturation = 1.1f;
    std::vector<std::string> m_blurred_layers;
    LayoutMode m_layout_mode = LayoutMode::Spiral;

    std::string m_window_border_color_active = "#0066ff";
    std::string m_window_border_color_inactive = "#99c2ff";
    BorderPaint m_border_paint_active;
    BorderPaint m_border_paint_inactive;
    bool m_window_border_shading = false;
    float m_window_border_shading_strength = 0.35f;

    bool m_shadow_enabled = true;
    float m_shadow_blur_sigma = 12.0f;
    std::string m_shadow_color_active = "#00000066";
    std::string m_shadow_color_inactive = "#00000044";
    int m_shadow_offset_x = 0;
    int m_shadow_offset_y = 4;

    std::vector<KeyBinding> m_keybindings;
    std::vector<GestureBinding> m_gesture_bindings;
    std::vector<WindowRule> m_window_rules;
    std::vector<std::string> m_plugins;
};

} // namespace miquland
