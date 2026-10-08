#pragma once
#include "core/common/util.hpp"
#include <vector>
#include <map>

namespace miquland {

class Server;
class View;
class Keyboard;
class InputManager;

struct InputDeviceNode {
    struct wlr_input_device* device = nullptr;
    InputManager* manager = nullptr;
    bool is_touch = false;
    struct wl_listener destroy_listener;
};

enum class CursorMode {
    Passthrough,
    Move,
    Resize
};

class InputManager {
public:
    explicit InputManager(Server* server);
    ~InputManager();

    struct wlr_seat* get_seat() const { return m_seat; }
    struct wlr_cursor* get_cursor() const { return m_cursor; }

    bool handle_keybinding(uint32_t modifiers, xkb_keysym_t keysym);
    void spawn_command(const char* cmd);
    void remove_keyboard(Keyboard* kb);
    void remove_pointer(struct wlr_input_device* device);
    void remove_touch_device(struct wlr_input_device* device);
    void reapply_device_config();
    void reapply_cursor_config();
    void load_cursor_scale(float scale);
    void configure_pointer_device(struct wlr_input_device* device);
    void map_touch_device_to_output(struct wlr_input_device* device);
    void set_cursor_icon(const char* name);

    void begin_interactive(View* view, CursorMode mode, uint32_t edges);
    void end_interactive();
    void notify_view_destroyed(View* view);
    void recheck_cursor_focus();
    CursorMode get_cursor_mode() const { return m_cursor_mode; }
    bool execute_action(const std::string& action);

private:
    static void handle_new_input(struct wl_listener* listener, void* data);
    static void handle_cursor_motion(struct wl_listener* listener, void* data);
    static void handle_cursor_motion_absolute(struct wl_listener* listener, void* data);
    static void handle_cursor_button(struct wl_listener* listener, void* data);
    static void handle_cursor_axis(struct wl_listener* listener, void* data);
    static void handle_cursor_frame(struct wl_listener* listener, void* data);
    static void handle_request_set_cursor(struct wl_listener* listener, void* data);
    
    // Clipboard selection handlers
    static void handle_request_set_selection(struct wl_listener* listener, void* data);
 
    static void handle_selection_destroy(struct wl_listener* listener, void* data);

    // Touchpad swipe & pinch gesture handlers
    static void handle_cursor_swipe_begin(struct wl_listener* listener, void* data);
    static void handle_cursor_swipe_update(struct wl_listener* listener, void* data);
    static void handle_cursor_swipe_end(struct wl_listener* listener, void* data);
    static void handle_cursor_pinch_begin(struct wl_listener* listener, void* data);
    static void handle_cursor_pinch_update(struct wl_listener* listener, void* data);
    static void handle_cursor_pinch_end(struct wl_listener* listener, void* data);
    static void handle_cursor_hold_begin(struct wl_listener* listener, void* data);
    static void handle_cursor_hold_end(struct wl_listener* listener, void* data);

    // Touchscreen handlers
    static void handle_cursor_touch_down(struct wl_listener* listener, void* data);
    static void handle_cursor_touch_up(struct wl_listener* listener, void* data);
    static void handle_cursor_touch_motion(struct wl_listener* listener, void* data);
    static void handle_cursor_touch_cancel(struct wl_listener* listener, void* data);
    static void handle_cursor_touch_frame(struct wl_listener* listener, void* data);

    void process_cursor_motion(uint32_t time);

    Server* m_server = nullptr;
    struct wlr_seat* m_seat = nullptr;
    struct wlr_cursor* m_cursor = nullptr;
    struct wlr_xcursor_manager* m_cursor_mgr = nullptr;
    std::string m_current_cursor_name = "default";

    CursorMode m_cursor_mode = CursorMode::Passthrough;
    View* m_grabbed_view = nullptr;
    bool m_grabbed_view_was_tiled = false;
    double m_grab_x = 0;
    double m_grab_y = 0;
    struct wlr_box m_grab_initial_view_box = { 0, 0, 0, 0 };
    double m_grab_initial_split_ratio = 0.5;
    double m_grab_initial_secondary_ratio = 0.5;
    bool m_grab_resize_is_secondary = false;
    uint32_t m_resize_edges = 0;

    View* m_border_hover_view = nullptr;
    uint32_t m_border_hover_edges = 0;

    std::vector<std::unique_ptr<Keyboard>> m_keyboards;
    std::vector<struct wlr_input_device*> m_pointers;
    std::vector<struct wlr_input_device*> m_touch_devices;
    struct InputDeviceNodeWrapper;
    std::vector<std::unique_ptr<InputDeviceNode>> m_device_nodes;
    friend void handle_input_device_destroy(struct wl_listener* listener, void* data);

    // Gesture tracking state (touchpad)
    double m_swipe_dx = 0.0;
    double m_swipe_dy = 0.0;
    bool m_swipe_triggered = false;
    bool m_touchpad_workspace_swipe_active = false;

    // Touchscreen multi-finger gesture tracking
    struct TouchPoint {
        int32_t id = 0;
        double start_lx = 0.0;
        double start_ly = 0.0;
        double current_lx = 0.0;
        double current_ly = 0.0;
    };
    std::map<int32_t, TouchPoint> m_touch_points;
    bool m_touch_gesture_active = false;
    bool m_touch_workspace_swipe_active = false;
    double m_last_touch_center_x = 0.0;

    struct wl_listener m_new_input_listener;
    struct wl_listener m_cursor_motion_listener;
    struct wl_listener m_cursor_motion_absolute_listener;
    struct wl_listener m_cursor_button_listener;
    struct wl_listener m_cursor_axis_listener;
    struct wl_listener m_cursor_frame_listener;
    struct wl_listener m_request_set_cursor_listener;
    
    // Clipboard selection listeners
    struct wl_listener m_request_set_selection_listener;
    struct wl_listener m_selection_destroy_listener;

    struct wl_listener m_cursor_swipe_begin_listener;
    struct wl_listener m_cursor_swipe_update_listener;
    struct wl_listener m_cursor_swipe_end_listener;
    struct wl_listener m_cursor_pinch_begin_listener;
    struct wl_listener m_cursor_pinch_update_listener;
    struct wl_listener m_cursor_pinch_end_listener;
    struct wl_listener m_cursor_hold_begin_listener;
    struct wl_listener m_cursor_hold_end_listener;

    struct wl_listener m_cursor_touch_down_listener;
    struct wl_listener m_cursor_touch_up_listener;
    struct wl_listener m_cursor_touch_motion_listener;
    struct wl_listener m_cursor_touch_cancel_listener;
    struct wl_listener m_cursor_touch_frame_listener;
};

class Keyboard {
public:
    Keyboard(Server* server, struct wlr_input_device* device);
    ~Keyboard();

    struct wlr_keyboard* get_wlr_keyboard() const { return m_keyboard; }
    struct wlr_input_device* get_device() const { return m_device; }

    void reapply_keymap();

private:
    static void handle_modifiers(struct wl_listener* listener, void* data);
    static void handle_key(struct wl_listener* listener, void* data);
    static void handle_destroy(struct wl_listener* listener, void* data);

    Server* m_server = nullptr;
    struct wlr_input_device* m_device = nullptr;
    struct wlr_keyboard* m_keyboard = nullptr;

    struct wl_listener m_modifiers_listener;
    struct wl_listener m_key_listener;
    struct wl_listener m_destroy_listener;
};

} // namespace miquland
