#include "core/input/input.hpp"
#include "core/server.hpp"
#include "core/workspace.hpp"
#include "core/view.hpp"
#include "core/output.hpp"
#include "core/session_lock.hpp"
#include "core/plugin_manager.hpp"
#include "core/animation/animation_manager.hpp"
#include "core/config/config.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <linux/input-event-codes.h>

namespace miquland {

namespace {

const char* edge_to_cursor_name(uint32_t edges) {
    if ((edges & WLR_EDGE_TOP) && (edges & WLR_EDGE_LEFT)) return "nw-resize";
    if ((edges & WLR_EDGE_TOP) && (edges & WLR_EDGE_RIGHT)) return "ne-resize";
    if ((edges & WLR_EDGE_BOTTOM) && (edges & WLR_EDGE_LEFT)) return "sw-resize";
    if ((edges & WLR_EDGE_BOTTOM) && (edges & WLR_EDGE_RIGHT)) return "se-resize";
    if (edges & WLR_EDGE_TOP) return "n-resize";
    if (edges & WLR_EDGE_BOTTOM) return "s-resize";
    if (edges & WLR_EDGE_LEFT) return "w-resize";
    if (edges & WLR_EDGE_RIGHT) return "e-resize";
    return "se-resize";
}

uint32_t detect_border_edges(const View* view, double sx, double sy, int grab_area) {
    if (!view) return 0;
    uint32_t edges = 0;
    if (sx < grab_area) edges |= WLR_EDGE_LEFT;
    else if (sx >= view->get_width() - grab_area) edges |= WLR_EDGE_RIGHT;
    if (sy < grab_area) edges |= WLR_EDGE_TOP;
    else if (sy >= view->get_height() - grab_area) edges |= WLR_EDGE_BOTTOM;
    return edges;
}

struct wlr_box calculate_interactive_move(const struct wlr_box& initial, double dx, double dy) {
    return {
        .x = initial.x + static_cast<int>(std::round(dx)),
        .y = initial.y + static_cast<int>(std::round(dy)),
        .width = initial.width,
        .height = initial.height,
    };
}

struct wlr_box calculate_interactive_resize(const struct wlr_box& initial, uint32_t edges, double dx, double dy, int min_w = 100, int min_h = 80) {
    struct wlr_box box = initial;

    if (edges & WLR_EDGE_RIGHT) {
        box.width = std::max(min_w, initial.width + static_cast<int>(std::round(dx)));
    } else if (edges & WLR_EDGE_LEFT) {
        int w = initial.width - static_cast<int>(std::round(dx));
        if (w >= min_w) {
            box.width = w;
            box.x = initial.x + static_cast<int>(std::round(dx));
        } else {
            box.width = min_w;
            box.x = initial.x + initial.width - min_w;
        }
    }

    if (edges & WLR_EDGE_BOTTOM) {
        box.height = std::max(min_h, initial.height + static_cast<int>(std::round(dy)));
    } else if (edges & WLR_EDGE_TOP) {
        int h = initial.height - static_cast<int>(std::round(dy));
        if (h >= min_h) {
            box.height = h;
            box.y = initial.y + static_cast<int>(std::round(dy));
        } else {
            box.height = min_h;
            box.y = initial.y + initial.height - min_h;
        }
    }

    return box;
}

} // namespace

void handle_input_device_destroy(struct wl_listener* listener, void* data) {
    InputDeviceNode* node = wl_container_of(listener, node, destroy_listener);
    wl_list_remove(&node->destroy_listener.link);
    InputManager* mgr = node->manager;
    struct wlr_input_device* dev = node->device;
    bool is_touch = node->is_touch;
    if (mgr) {
        if (is_touch) {
            mgr->remove_touch_device(dev);
        } else {
            mgr->remove_pointer(dev);
        }
        auto it = std::find_if(mgr->m_device_nodes.begin(), mgr->m_device_nodes.end(),
            [node](const auto& n) { return n.get() == node; });
        if (it != mgr->m_device_nodes.end()) {
            mgr->m_device_nodes.erase(it);
        }
    }
}

InputManager::InputManager(Server* server)
    : m_server(server)
{
    m_seat = wlr_seat_create(server->get_display(), "seat0");

    m_cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(m_cursor, server->get_output_manager()->get_layout());

    reapply_cursor_config();

    m_new_input_listener.notify = handle_new_input;
    wl_signal_add(&server->get_backend()->events.new_input, &m_new_input_listener);

    m_cursor_motion_listener.notify = handle_cursor_motion;
    wl_signal_add(&m_cursor->events.motion, &m_cursor_motion_listener);

    m_cursor_motion_absolute_listener.notify = handle_cursor_motion_absolute;
    wl_signal_add(&m_cursor->events.motion_absolute, &m_cursor_motion_absolute_listener);

    m_cursor_button_listener.notify = handle_cursor_button;
    wl_signal_add(&m_cursor->events.button, &m_cursor_button_listener);

    m_cursor_axis_listener.notify = handle_cursor_axis;
    wl_signal_add(&m_cursor->events.axis, &m_cursor_axis_listener);

    m_cursor_frame_listener.notify = handle_cursor_frame;
    wl_signal_add(&m_cursor->events.frame, &m_cursor_frame_listener);

    m_request_set_cursor_listener.notify = handle_request_set_cursor;
    wl_signal_add(&m_seat->events.request_set_cursor, &m_request_set_cursor_listener);
    
    // Clipboard Selection wiring
    m_request_set_selection_listener.notify = handle_request_set_selection;
    wl_signal_add(&m_seat->events.request_set_selection, &m_request_set_selection_listener);

    

    m_selection_destroy_listener.notify = handle_selection_destroy;
    wl_list_init(&m_selection_destroy_listener.link);

    // Touchpad swipe & pinch gesture listeners
    m_cursor_swipe_begin_listener.notify = handle_cursor_swipe_begin;
    wl_signal_add(&m_cursor->events.swipe_begin, &m_cursor_swipe_begin_listener);

    m_cursor_swipe_update_listener.notify = handle_cursor_swipe_update;
    wl_signal_add(&m_cursor->events.swipe_update, &m_cursor_swipe_update_listener);

    m_cursor_swipe_end_listener.notify = handle_cursor_swipe_end;
    wl_signal_add(&m_cursor->events.swipe_end, &m_cursor_swipe_end_listener);

    m_cursor_pinch_begin_listener.notify = handle_cursor_pinch_begin;
    wl_signal_add(&m_cursor->events.pinch_begin, &m_cursor_pinch_begin_listener);

    m_cursor_pinch_update_listener.notify = handle_cursor_pinch_update;
    wl_signal_add(&m_cursor->events.pinch_update, &m_cursor_pinch_update_listener);

    m_cursor_pinch_end_listener.notify = handle_cursor_pinch_end;
    wl_signal_add(&m_cursor->events.pinch_end, &m_cursor_pinch_end_listener);

    m_cursor_hold_begin_listener.notify = handle_cursor_hold_begin;
    wl_signal_add(&m_cursor->events.hold_begin, &m_cursor_hold_begin_listener);

    m_cursor_hold_end_listener.notify = handle_cursor_hold_end;
    wl_signal_add(&m_cursor->events.hold_end, &m_cursor_hold_end_listener);

    // Touchscreen listeners
    m_cursor_touch_down_listener.notify = handle_cursor_touch_down;
    wl_signal_add(&m_cursor->events.touch_down, &m_cursor_touch_down_listener);

    m_cursor_touch_up_listener.notify = handle_cursor_touch_up;
    wl_signal_add(&m_cursor->events.touch_up, &m_cursor_touch_up_listener);

    m_cursor_touch_motion_listener.notify = handle_cursor_touch_motion;
    wl_signal_add(&m_cursor->events.touch_motion, &m_cursor_touch_motion_listener);

    m_cursor_touch_cancel_listener.notify = handle_cursor_touch_cancel;
    wl_signal_add(&m_cursor->events.touch_cancel, &m_cursor_touch_cancel_listener);

    m_cursor_touch_frame_listener.notify = handle_cursor_touch_frame;
    wl_signal_add(&m_cursor->events.touch_frame, &m_cursor_touch_frame_listener);
}

InputManager::~InputManager() {
    wl_list_remove(&m_new_input_listener.link);
    wl_list_remove(&m_cursor_motion_listener.link);
    wl_list_remove(&m_cursor_motion_absolute_listener.link);
    wl_list_remove(&m_cursor_button_listener.link);
    wl_list_remove(&m_cursor_axis_listener.link);
    wl_list_remove(&m_cursor_frame_listener.link);
    wl_list_remove(&m_request_set_cursor_listener.link);
    
    // Cleanup Clipboard listeners
    wl_list_remove(&m_request_set_selection_listener.link);
    wl_list_remove(&m_selection_destroy_listener.link);

    wl_list_remove(&m_cursor_swipe_begin_listener.link);
    wl_list_remove(&m_cursor_swipe_update_listener.link);
    wl_list_remove(&m_cursor_swipe_end_listener.link);
    wl_list_remove(&m_cursor_pinch_begin_listener.link);
    wl_list_remove(&m_cursor_pinch_update_listener.link);
    wl_list_remove(&m_cursor_pinch_end_listener.link);
    wl_list_remove(&m_cursor_hold_begin_listener.link);
    wl_list_remove(&m_cursor_hold_end_listener.link);

    wl_list_remove(&m_cursor_touch_down_listener.link);
    wl_list_remove(&m_cursor_touch_up_listener.link);
    wl_list_remove(&m_cursor_touch_motion_listener.link);
    wl_list_remove(&m_cursor_touch_cancel_listener.link);
    wl_list_remove(&m_cursor_touch_frame_listener.link);
    for (auto& node : m_device_nodes) {
        if (node) {
            wl_list_remove(&node->destroy_listener.link);
        }
    }
    m_device_nodes.clear();

    if (m_cursor_mgr) wlr_xcursor_manager_destroy(m_cursor_mgr);
    if (m_cursor) wlr_cursor_destroy(m_cursor);
}

void InputManager::spawn_command(const char* cmd) {
    if (!cmd || !*cmd) return;
    log_info("Executing command: " + std::string(cmd));

    pid_t pid = fork();
    if (pid < 0) {
        log_error("Failed to fork process for command: " + std::string(cmd));
        return;
    }

    if (pid == 0) {
        // Double-fork so grandchild process is adopted by init/systemd
        pid_t grand_pid = fork();
        if (grand_pid == 0) {
            setsid();
            execl("/bin/sh", "sh", "-c", cmd, nullptr);
            _exit(127);
        }
        _exit(grand_pid < 0 ? 1 : 0);
    }

    // Parent waits for immediate child to prevent any zombie accumulation
    waitpid(pid, nullptr, 0);
}

void InputManager::remove_keyboard(Keyboard* kb) {
    for (auto it = m_keyboards.begin(); it != m_keyboards.end(); ++it) {
        if (it->get() == kb) {
            m_keyboards.erase(it);
            break;
        }
    }
}

void InputManager::remove_pointer(struct wlr_input_device* device) {
    auto it = std::find(m_pointers.begin(), m_pointers.end(), device);
    if (it != m_pointers.end()) {
        m_pointers.erase(it);
    }
}

void InputManager::remove_touch_device(struct wlr_input_device* device) {
    auto it = std::find(m_touch_devices.begin(), m_touch_devices.end(), device);
    if (it != m_touch_devices.end()) {
        m_touch_devices.erase(it);
    }
}

void InputManager::configure_pointer_device(struct wlr_input_device* device) {
    if (!device || !wlr_input_device_is_libinput(device)) return;

    struct libinput_device* libinput_dev = wlr_libinput_get_device_handle(device);
    if (!libinput_dev) return;

    // 1. Tap-to-click & Tap-and-drag
    if (libinput_device_config_tap_get_finger_count(libinput_dev) > 0) {
        bool tap_enabled = Config::get().is_tap_to_click_enabled();
        libinput_device_config_tap_set_enabled(libinput_dev, tap_enabled ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);
        libinput_device_config_tap_set_drag_enabled(libinput_dev, tap_enabled ? LIBINPUT_CONFIG_DRAG_ENABLED : LIBINPUT_CONFIG_DRAG_DISABLED);
    }

    // 2. Natural scrolling
    if (libinput_device_config_scroll_has_natural_scroll(libinput_dev)) {
        bool natural_scroll = Config::get().is_natural_scroll_enabled();
        libinput_device_config_scroll_set_natural_scroll_enabled(libinput_dev, natural_scroll ? 1 : 0);
    }

    // 3. Disable-While-Typing (DWT)
    if (libinput_device_config_dwt_is_available(libinput_dev)) {
        bool dwt = Config::get().is_dwt_enabled();
        libinput_device_config_dwt_set_enabled(libinput_dev, dwt ? LIBINPUT_CONFIG_DWT_ENABLED : LIBINPUT_CONFIG_DWT_DISABLED);
    }

    // 4. Pointer Acceleration Speed and Profile
    if (libinput_device_config_accel_is_available(libinput_dev)) {
        double speed = Config::get().get_accel_speed();
        libinput_device_config_accel_set_speed(libinput_dev, speed);

        const std::string& profile = Config::get().get_accel_profile();
        enum libinput_config_accel_profile prof_enum = (profile == "flat")
            ? LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT
            : LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
        libinput_device_config_accel_set_profile(libinput_dev, prof_enum);
    }
}

void InputManager::map_touch_device_to_output(struct wlr_input_device* device) {
    if (!device || device->type != WLR_INPUT_DEVICE_TOUCH) return;

    auto* out_mgr = m_server->get_output_manager();
    if (!out_mgr) return;

    struct wlr_output* target_wlr_out = nullptr;
    const std::string& user_target = Config::get().get_touchscreen_output();
    const auto& outputs = out_mgr->get_outputs();

    if (outputs.empty()) return;

    // 1. Explicit user target in config (e.g. touch_output = eDP-1)
    if (!user_target.empty()) {
        for (const auto& out : outputs) {
            if (out && out->get_wlr_output() && out->get_wlr_output()->name) {
                if (user_target == out->get_wlr_output()->name) {
                    target_wlr_out = out->get_wlr_output();
                    break;
                }
            }
        }
    }

    // 2. Auto-detect internal laptop display (eDP, LVDS, DSI)
    if (!target_wlr_out) {
        for (const auto& out : outputs) {
            if (out && out->get_wlr_output() && out->get_wlr_output()->name) {
                std::string name = out->get_wlr_output()->name;
                if (name.rfind("eDP", 0) == 0 || name.rfind("LVDS", 0) == 0 || name.rfind("DSI", 0) == 0) {
                    target_wlr_out = out->get_wlr_output();
                    break;
                }
            }
        }
    }

    // 3. Fallback to primary output
    if (!target_wlr_out) {
        auto* primary = out_mgr->get_primary_output();
        if (primary) {
            target_wlr_out = primary->get_wlr_output();
        }
    }

    if (target_wlr_out) {
        wlr_cursor_map_input_to_output(m_cursor, device, target_wlr_out);
        log_info("Touchscreen '" + std::string(device->name ? device->name : "touch") +
                 "' mapped to output: " + std::string(target_wlr_out->name ? target_wlr_out->name : "unnamed"));
    }
}

void InputManager::reapply_device_config() {
    reapply_cursor_config();
    for (auto& kb : m_keyboards) {
        if (kb) kb->reapply_keymap();
    }
    for (auto* device : m_pointers) {
        configure_pointer_device(device);
    }
    for (auto* device : m_touch_devices) {
        map_touch_device_to_output(device);
    }
}

void InputManager::reapply_cursor_config() {
    const std::string& ctheme = Config::get().get_cursor_theme();
    int csize = Config::get().get_cursor_size();
    const char* theme_name = ctheme.empty() ? nullptr : ctheme.c_str();

    log_info("Applying cursor config: theme='" + ctheme + "', size=" + std::to_string(csize));

    if (m_cursor_mgr) {
        wlr_xcursor_manager_destroy(m_cursor_mgr);
        m_cursor_mgr = nullptr;
    }

    m_cursor_mgr = wlr_xcursor_manager_create(theme_name, csize);
    if (!m_cursor_mgr) {
        log_error("Failed to create xcursor manager for theme: " + ctheme);
        return;
    }

    if (!ctheme.empty()) {
        setenv("XCURSOR_THEME", ctheme.c_str(), 1);
    } else {
        unsetenv("XCURSOR_THEME");
    }
    setenv("XCURSOR_SIZE", std::to_string(csize).c_str(), 1);
    spawn_async_command({"systemctl", "--user", "import-environment", "XCURSOR_THEME", "XCURSOR_SIZE"});

    wlr_xcursor_manager_load(m_cursor_mgr, 1.0f);
    if (m_server && m_server->get_output_manager()) {
        for (const auto& out : m_server->get_output_manager()->get_outputs()) {
            if (out && out->get_wlr_output() && out->get_wlr_output()->scale > 0.0f) {
                wlr_xcursor_manager_load(m_cursor_mgr, out->get_wlr_output()->scale);
            }
        }
    }

    if (m_cursor && m_cursor_mgr) {
        if (m_cursor_mode == CursorMode::Move) {
            set_cursor_icon("grab");
        } else if (m_cursor_mode == CursorMode::Resize) {
            set_cursor_icon(edge_to_cursor_name(m_resize_edges));
        } else if (m_border_hover_edges != 0) {
            set_cursor_icon(edge_to_cursor_name(m_border_hover_edges));
        } else {
            const char* cur_icon = m_current_cursor_name.empty() ? "default" : m_current_cursor_name.c_str();
            set_cursor_icon(cur_icon);
        }
    }

    if (m_seat) {
        if (m_seat->pointer_state.focused_surface) {
            wlr_seat_pointer_clear_focus(m_seat);
        }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint32_t time_ms = static_cast<uint32_t>(now.tv_sec * 1000 + now.tv_nsec / 1000000);
        process_cursor_motion(time_ms);
    }
}

void InputManager::load_cursor_scale(float scale) {
    if (m_cursor_mgr && scale > 0.0f) {
        wlr_xcursor_manager_load(m_cursor_mgr, scale);
    }
}

void InputManager::set_cursor_icon(const char* name) {
    m_current_cursor_name = name ? name : "default";
    if (m_cursor && m_cursor_mgr) {
        wlr_cursor_set_xcursor(m_cursor, m_cursor_mgr, m_current_cursor_name.c_str());
    }
}

void InputManager::begin_interactive(View* view, CursorMode mode, uint32_t edges) {
    if (!view || view->is_fullscreen() || view->is_override_redirect()) return;

    m_grabbed_view = view;
    m_grabbed_view_was_tiled = !view->is_floating();
    m_cursor_mode = mode;
    m_grab_x = m_cursor->x;
    m_grab_y = m_cursor->y;
    m_grab_initial_view_box = {
        .x = view->get_x(),
        .y = view->get_y(),
        .width = view->get_width(),
        .height = view->get_height()
    };
    m_resize_edges = edges;

    if (m_grabbed_view_was_tiled && view->get_workspace()) {
        Workspace* ws = view->get_workspace();
        m_grab_initial_split_ratio = ws->get_split_ratio();
        m_grab_initial_secondary_ratio = ws->get_secondary_split_ratio();

        bool primary_is_horizontal = (ws->get_split_mode() == SplitMode::Horizontal);
        if (Config::get().get_layout_mode() == Config::LayoutMode::Stack) {
            primary_is_horizontal = true;
        }

        if (primary_is_horizontal) {
            m_grab_resize_is_secondary = ((edges & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)) != 0);
        } else {
            m_grab_resize_is_secondary = ((edges & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)) != 0);
        }
    }

    view->focus();
    if (view->get_scene_tree()) {
        wlr_scene_node_raise_to_top(&view->get_scene_tree()->node);
    }

    if (mode == CursorMode::Move) {
        set_cursor_icon("grab");
    } else if (mode == CursorMode::Resize) {
        set_cursor_icon(edge_to_cursor_name(edges));
    }
}

void InputManager::end_interactive() {
    if (m_cursor_mode == CursorMode::Move && m_grabbed_view && m_grabbed_view_was_tiled) {
        View* target = nullptr;
        if (m_grabbed_view->get_scene_tree()) {
            wlr_scene_node_set_enabled(&m_grabbed_view->get_scene_tree()->node, false);
            double sx = 0, sy = 0;
            struct wlr_surface* surf = nullptr;
            target = m_server->view_at(m_cursor->x, m_cursor->y, &surf, &sx, &sy);
            wlr_scene_node_set_enabled(&m_grabbed_view->get_scene_tree()->node, true);
        }

        Workspace* ws = m_grabbed_view->get_workspace();
        double dist = std::hypot(m_cursor->x - m_grab_x, m_cursor->y - m_grab_y);

        if (target && target != m_grabbed_view && ws && !target->is_floating() && target->get_workspace() == ws) {
            // Drop onto another tiled window: Swap positions in layout!
            ws->swap_views(m_grabbed_view, target);
        } else if (target == m_grabbed_view || dist < 40.0) {
            // Dropped back in place or slight twitch: snap back to tiled position
            if (ws) {
                auto* out_mgr = m_server->get_output_manager();
                if (out_mgr) {
                    ws->recalculate_layout(out_mgr->get_primary_usable_geometry());
                }
            }
        } else {
            // Dragged far into empty space: pop out to floating
            if (ws) {
                ws->toggle_floating(m_grabbed_view);
            }
        }
    }

    m_cursor_mode = CursorMode::Passthrough;
    m_grabbed_view = nullptr;
    m_grabbed_view_was_tiled = false;
    set_cursor_icon("default");
}

void InputManager::notify_view_destroyed(View* view) {
    if (m_grabbed_view == view) {
        m_grabbed_view = nullptr;
        m_grabbed_view_was_tiled = false;
        m_cursor_mode = CursorMode::Passthrough;
        set_cursor_icon("default");
    }
    if (m_border_hover_view == view) {
        m_border_hover_view = nullptr;
        m_border_hover_edges = 0;
    }
}

void InputManager::recheck_cursor_focus() {
    if (m_server->is_locked()) return;

    m_border_hover_view = nullptr;
    m_border_hover_edges = 0;

    double sx = 0.0, sy = 0.0;
    struct wlr_surface* surface = nullptr;
    View* view = m_server->view_at(m_cursor->x, m_cursor->y, &surface, &sx, &sy);

    View* target_view = view;
    if (target_view && target_view->has_child_dialogs()) {
        View* top_dialog = target_view->get_top_dialog();
        if (top_dialog) {
            target_view = top_dialog;
        }
    }

    if (surface) {
        if (m_seat->pointer_state.focused_surface != surface) {
            wlr_seat_pointer_notify_enter(m_seat, surface, sx, sy);
            set_cursor_icon("default");
        }
        wlr_seat_pointer_notify_motion(m_seat, 0, sx, sy);
    } else {
        wlr_seat_pointer_notify_clear_focus(m_seat);
        set_cursor_icon("default");

        if (view && Config::get().is_resize_on_border_enabled() && !view->is_fullscreen() && !view->is_override_redirect()) {
            int grab = std::max(Config::get().get_window_border_width(), Config::get().get_border_grab_area());
            uint32_t edges = detect_border_edges(view, sx, sy, grab);
            if (edges != 0) {
                m_border_hover_view = view;
                m_border_hover_edges = edges;
                set_cursor_icon(edge_to_cursor_name(edges));
            }
        }
    }

    if (target_view && !target_view->is_override_redirect()) {
        if (target_view != m_server->get_focused_view()) {
            target_view->focus();
        }
    } else {
        Workspace* active_ws = m_server->get_workspace_manager() ? m_server->get_workspace_manager()->get_active_workspace() : nullptr;
        if (active_ws) {
            View* best = m_server->get_workspace_manager()->find_best_focus_view(active_ws);
            if (best && best != m_server->get_focused_view() && !best->is_override_redirect()) {
                best->focus();
            }
        }
    }
}

bool InputManager::handle_keybinding(uint32_t modifiers, xkb_keysym_t keysym) {
    // 0. If session is locked, NEVER process any shortcuts (all keys go directly to locker)
    if (m_server->is_locked()) {
        return false;
    }

    xkb_keysym_t norm_sym = (keysym >= XKB_KEY_A && keysym <= XKB_KEY_Z) ? (keysym - XKB_KEY_A + XKB_KEY_a) : keysym;

    bool mod = (modifiers & WLR_MODIFIER_LOGO) != 0;
    bool shift = (modifiers & WLR_MODIFIER_SHIFT) != 0;
    bool ctrl = (modifiers & WLR_MODIFIER_CTRL) != 0;
    bool alt = (modifiers & WLR_MODIFIER_ALT) != 0;

    // 1. If an exclusive Layer Surface is focused (e.g. rofi, miqulauncher, swaylock), forward keys directly to it
    if (m_server->get_focused_layer_surface()) {
        if (mod && shift && norm_sym == XKB_KEY_q) {
            m_server->terminate();
            return true;
        }
        return false;
    }

    uint32_t active_mods = 0;
    if (mod) active_mods |= WLR_MODIFIER_LOGO;
    if (shift) active_mods |= WLR_MODIFIER_SHIFT;
    if (ctrl) active_mods |= WLR_MODIFIER_CTRL;
    if (alt) active_mods |= WLR_MODIFIER_ALT;

    // Configurable Bindings (from miquland.conf)
    for (const auto& kb : Config::get().get_keybindings()) {
        xkb_keysym_t kb_norm = (kb.keysym >= XKB_KEY_A && kb.keysym <= XKB_KEY_Z) ? (kb.keysym - XKB_KEY_A + XKB_KEY_a) : kb.keysym;

        if (kb.modifiers == active_mods && kb_norm == norm_sym) {
            log_info("Keybinding matched: " + kb.combo_str + " -> " + kb.action);
            return execute_action(kb.action);
        }
    }
    return false;
}

bool InputManager::execute_action(const std::string& action) {
    if (action.empty()) {
        return false;
    }

    if (m_server->get_plugin_manager() && m_server->get_plugin_manager()->execute_dispatcher(action)) {
        return true;
    }

    if (action == "terminal") {
        std::string term = Config::get().get_terminal();
        if (term.empty()) term = "kitty || foot || alacritty || wezterm || weston-terminal || xterm";
        spawn_command(term.c_str());
    } else if (action == "menu" || action == "app_launcher") {
        spawn_command("miqulauncher");
    } else if (action == "close" || action == "close_window") {
        View* focused = m_server->get_focused_view();
        if (focused) focused->close();
    } else if (action == "toggle_fullscreen" || action == "fullscreen") {
        View* focused = m_server->get_focused_view();
        if (focused) {
            focused->set_fullscreen(!focused->is_fullscreen());
        }
    } else if (action == "toggle_floating" || action == "toggle_float" || action == "floating_toggle") {
        m_server->get_workspace_manager()->toggle_floating_active();
    } else if (action == "toggle_layout" || action == "layout_toggle") {
        m_server->get_workspace_manager()->toggle_layout_mode();
    } else if (action == "swap_main" || action == "swap_master" || action == "swap_with_main") {
        m_server->get_workspace_manager()->swap_with_main();
    } else if (action == "toggle_split" || action == "split_toggle") {
        m_server->get_workspace_manager()->toggle_active_split();
    } else if (action == "focus_win_1" || action == "window_1") {
        m_server->get_workspace_manager()->focus_window_index(0);
    } else if (action == "focus_win_2" || action == "window_2") {
        m_server->get_workspace_manager()->focus_window_index(1);
    } else if (action == "toggle_focus" || action == "next_window" || action == "focus_next") {
        m_server->get_workspace_manager()->focus_next_view();
    } else if (action == "prev_window" || action == "focus_prev") {
        m_server->get_workspace_manager()->focus_prev_view();
    } else if (action == "prev_ws" || action == "prev_workspace") {
        m_server->get_workspace_manager()->prev_workspace();
    } else if (action == "next_ws" || action == "next_workspace") {
        m_server->get_workspace_manager()->next_workspace();
    } else if (action.rfind("ws_", 0) == 0) {
        try {
            size_t ws_id = std::stoul(action.substr(3));
            m_server->get_workspace_manager()->switch_to_workspace(ws_id);
        } catch (...) {}
    } else if (action.rfind("move_ws_", 0) == 0) {
        try {
            size_t ws_id = std::stoul(action.substr(8));
            View* focused = m_server->get_focused_view();
            if (focused) m_server->get_workspace_manager()->move_view_to_workspace(focused, ws_id);
        } catch (...) {}
    } else if (action.rfind("move_to_ws_", 0) == 0) {
        try {
            size_t ws_id = std::stoul(action.substr(11));
            View* focused = m_server->get_focused_view();
            if (focused) m_server->get_workspace_manager()->move_view_to_workspace(focused, ws_id);
        } catch (...) {}
    } else if (action.rfind("movetoworkspace_", 0) == 0) {
        try {
            size_t ws_id = std::stoul(action.substr(16));
            View* focused = m_server->get_focused_view();
            if (focused) m_server->get_workspace_manager()->move_view_to_workspace(focused, ws_id);
        } catch (...) {}
    } else if (action == "exit" || action == "quit") {
        m_server->terminate();
    } else {
        spawn_command(action.c_str());
    }
    return true;
}

void InputManager::process_cursor_motion(uint32_t time) {
    if (m_server->is_locked()) {
        double sx = 0, sy = 0;
        SessionLockSurface* lock_surf = m_server->get_session_lock()->surface_at(m_cursor->x, m_cursor->y, &sx, &sy);
        if (lock_surf && lock_surf->get_wlr_surface()) {
            struct wlr_surface* surface = lock_surf->get_wlr_surface();
            if (m_seat->pointer_state.focused_surface != surface) {
                wlr_seat_pointer_notify_enter(m_seat, surface, sx, sy);
            }
            wlr_seat_pointer_notify_motion(m_seat, time, sx, sy);
            lock_surf->focus();
        } else {
            set_cursor_icon("default");
            wlr_seat_pointer_clear_focus(m_seat);
        }
        return;
    }

    if (m_server->get_idle_notifier()) {
        wlr_idle_notifier_v1_notify_activity(m_server->get_idle_notifier(), m_seat);
    }

    if (m_server->get_plugin_manager() && m_server->get_plugin_manager()->dispatch_pointer_motion(m_cursor->x, m_cursor->y)) {
        return;
    }

    // If a workspace transition or interactive swipe is active, do not evaluate hover focus on sliding views
    if (m_server->get_animation_manager() && m_server->get_animation_manager()->is_workspace_animating()) {
        return;
    }

    // Handle interactive Move & Resize
    if (m_cursor_mode == CursorMode::Move || m_cursor_mode == CursorMode::Resize) {
        if (m_grabbed_view && !m_server->is_valid_view(m_grabbed_view)) {
            m_grabbed_view = nullptr;
            m_grabbed_view_was_tiled = false;
            m_cursor_mode = CursorMode::Passthrough;
            set_cursor_icon("default");
            return;
        }
    }

    if (m_cursor_mode == CursorMode::Move) {
        if (m_grabbed_view) {
            struct wlr_box box = calculate_interactive_move(m_grab_initial_view_box, m_cursor->x - m_grab_x, m_cursor->y - m_grab_y);
            m_grabbed_view->set_geometry(box.x, box.y, box.width, box.height);
        }
        return;
    } else if (m_cursor_mode == CursorMode::Resize) {
        if (m_grabbed_view) {
            if (m_grabbed_view_was_tiled && m_grabbed_view->get_workspace()) {
                Workspace* ws = m_grabbed_view->get_workspace();
                auto* out_mgr = m_server->get_output_manager();
                if (out_mgr && ws->get_tiled_views().size() >= 2) {
                    struct wlr_box usable = out_mgr->get_primary_usable_geometry();
                    int pad = Config::get().get_screen_edge_padding();
                    int usable_w = std::max(50, usable.width - 2 * pad);
                    int usable_h = std::max(50, usable.height - 2 * pad);

                    bool primary_is_horizontal = (ws->get_split_mode() == SplitMode::Horizontal);
                    if (Config::get().get_layout_mode() == Config::LayoutMode::Stack) {
                        primary_is_horizontal = true;
                    }

                    double dx = m_cursor->x - m_grab_x;
                    double dy = m_cursor->y - m_grab_y;

                    if (m_grab_resize_is_secondary && ws->get_tiled_views().size() >= 3) {
                        if (primary_is_horizontal) {
                            double delta = dy / static_cast<double>(usable_h);
                            bool is_top_of_stack = (ws->get_tiled_views().size() > 1 && ws->get_tiled_views()[1] == m_grabbed_view);
                            double new_ratio = m_grab_initial_secondary_ratio;
                            if (is_top_of_stack) {
                                new_ratio = (m_resize_edges & WLR_EDGE_BOTTOM)
                                    ? (m_grab_initial_secondary_ratio + delta)
                                    : (m_grab_initial_secondary_ratio - delta);
                            } else {
                                new_ratio = (m_resize_edges & WLR_EDGE_TOP)
                                    ? (m_grab_initial_secondary_ratio + delta)
                                    : (m_grab_initial_secondary_ratio - delta);
                            }
                            ws->set_secondary_split_ratio(new_ratio);
                        } else {
                            double delta = dx / static_cast<double>(usable_w);
                            bool is_left_of_stack = (ws->get_tiled_views().size() > 1 && ws->get_tiled_views()[1] == m_grabbed_view);
                            double new_ratio = m_grab_initial_secondary_ratio;
                            if (is_left_of_stack) {
                                new_ratio = (m_resize_edges & WLR_EDGE_RIGHT)
                                    ? (m_grab_initial_secondary_ratio + delta)
                                    : (m_grab_initial_secondary_ratio - delta);
                            } else {
                                new_ratio = (m_resize_edges & WLR_EDGE_LEFT)
                                    ? (m_grab_initial_secondary_ratio + delta)
                                    : (m_grab_initial_secondary_ratio - delta);
                            }
                            ws->set_secondary_split_ratio(new_ratio);
                        }
                    } else {
                        bool is_first_view = (!ws->get_tiled_views().empty() && ws->get_tiled_views().front() == m_grabbed_view);
                        double new_ratio = m_grab_initial_split_ratio;
                        if (primary_is_horizontal) {
                            double delta = dx / static_cast<double>(usable_w);
                            if (is_first_view) {
                                new_ratio = m_grab_initial_split_ratio + delta;
                            } else {
                                if (m_resize_edges & WLR_EDGE_LEFT) {
                                    new_ratio = m_grab_initial_split_ratio + delta;
                                } else {
                                    new_ratio = m_grab_initial_split_ratio - delta;
                                }
                            }
                        } else {
                            double delta = dy / static_cast<double>(usable_h);
                            if (is_first_view) {
                                new_ratio = m_grab_initial_split_ratio + delta;
                            } else {
                                if (m_resize_edges & WLR_EDGE_TOP) {
                                    new_ratio = m_grab_initial_split_ratio + delta;
                                } else {
                                    new_ratio = m_grab_initial_split_ratio - delta;
                                }
                            }
                        }
                        ws->set_split_ratio(new_ratio);
                    }

                    ws->recalculate_layout(usable);
                }
            } else {
                struct wlr_box box = calculate_interactive_resize(m_grab_initial_view_box, m_resize_edges, m_cursor->x - m_grab_x, m_cursor->y - m_grab_y);
                m_grabbed_view->set_geometry(box.x, box.y, box.width, box.height);
            }
        }
        return;
    }

    double sx, sy;
    struct wlr_surface* surface = nullptr;
    View* view = m_server->view_at(m_cursor->x, m_cursor->y, &surface, &sx, &sy);

    m_border_hover_view = nullptr;
    m_border_hover_edges = 0;

    if (!surface) {
        if (view && Config::get().is_resize_on_border_enabled() && !view->is_fullscreen() && !view->is_override_redirect()) {
            int grab = std::max(Config::get().get_window_border_width(), Config::get().get_border_grab_area());
            uint32_t edges = detect_border_edges(view, sx, sy, grab);

            if (edges != 0) {
                m_border_hover_view = view;
                m_border_hover_edges = edges;
                set_cursor_icon(edge_to_cursor_name(edges));
                wlr_seat_pointer_notify_clear_focus(m_seat);
                return;
            }
        }

        set_cursor_icon("default");
        wlr_seat_pointer_notify_clear_focus(m_seat);
        return;
    }

    if (m_seat->pointer_state.focused_surface != surface) {
        wlr_seat_pointer_notify_enter(m_seat, surface, sx, sy);
        set_cursor_icon("default");
    }
    wlr_seat_pointer_notify_motion(m_seat, time, sx, sy);

    View* target_view = view;
    if (target_view && target_view->has_child_dialogs()) {
        View* top_dialog = target_view->get_top_dialog();
        if (top_dialog) {
            target_view = top_dialog;
        }
    }

    // Hover to focus: if enabled and cursor hovers over a view, focus it!
    // Inhibit focus switching while a popup grab is active or while the focused view has active popups
    bool has_grab = wlr_seat_pointer_has_grab(m_seat) || wlr_seat_keyboard_has_grab(m_seat);
    bool focused_has_popups = (m_server->get_focused_view() && m_server->get_focused_view()->has_popups());

    if (!has_grab && !focused_has_popups) {
        if (target_view && !target_view->is_override_redirect() && target_view != m_server->get_focused_view()) {
            target_view->focus();
        }
    }
}

void InputManager::handle_new_input(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_new_input_listener);
    auto* device = static_cast<struct wlr_input_device*>(data);

    if (device->type == WLR_INPUT_DEVICE_KEYBOARD) {
        manager->m_keyboards.emplace_back(std::make_unique<Keyboard>(manager->m_server, device));
    } else if (device->type == WLR_INPUT_DEVICE_POINTER) {
        manager->configure_pointer_device(device);
        wlr_cursor_attach_input_device(manager->m_cursor, device);
        manager->m_pointers.push_back(device);

        auto node = std::make_unique<InputDeviceNode>();
        node->device = device;
        node->manager = manager;
        node->is_touch = false;
        node->destroy_listener.notify = handle_input_device_destroy;
        wl_signal_add(&device->events.destroy, &node->destroy_listener);
        manager->m_device_nodes.push_back(std::move(node));
    } else if (device->type == WLR_INPUT_DEVICE_TOUCH) {
        wlr_cursor_attach_input_device(manager->m_cursor, device);
        manager->map_touch_device_to_output(device);
        manager->m_touch_devices.push_back(device);

        auto node = std::make_unique<InputDeviceNode>();
        node->device = device;
        node->manager = manager;
        node->is_touch = true;
        node->destroy_listener.notify = handle_input_device_destroy;
        wl_signal_add(&device->events.destroy, &node->destroy_listener);
        manager->m_device_nodes.push_back(std::move(node));

        log_info("Touchscreen device attached: " + std::string(device->name ? device->name : "unnamed"));
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!manager->m_keyboards.empty()) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    if (!manager->m_touch_devices.empty()) {
        caps |= WL_SEAT_CAPABILITY_TOUCH;
    }
    wlr_seat_set_capabilities(manager->m_seat, caps);
}

void InputManager::handle_cursor_motion(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_cursor_motion_listener);
    auto* event = static_cast<struct wlr_pointer_motion_event*>(data);

    wlr_cursor_move(manager->m_cursor, &event->pointer->base, event->delta_x, event->delta_y);
    manager->process_cursor_motion(event->time_msec);

    if (manager->m_server->get_relative_pointer_manager()) {
        wlr_relative_pointer_manager_v1_send_relative_motion(
            manager->m_server->get_relative_pointer_manager(),
            manager->m_seat,
            (uint64_t)event->time_msec * 1000,
            event->delta_x, event->delta_y,
            event->unaccel_dx, event->unaccel_dy
        );
    }
}

void InputManager::handle_cursor_motion_absolute(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_cursor_motion_absolute_listener);
    auto* event = static_cast<struct wlr_pointer_motion_absolute_event*>(data);

    wlr_cursor_warp_absolute(manager->m_cursor, &event->pointer->base, event->x, event->y);
    manager->process_cursor_motion(event->time_msec);
}

void InputManager::handle_cursor_button(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_cursor_button_listener);
    auto* event = static_cast<struct wlr_pointer_button_event*>(data);

    if (manager->m_server->get_idle_notifier()) {
        wlr_idle_notifier_v1_notify_activity(manager->m_server->get_idle_notifier(), manager->m_seat);
    }

    if (manager->m_server->is_locked()) {
        double sx = 0, sy = 0;
        SessionLockSurface* lock_surf = manager->m_server->get_session_lock()->surface_at(manager->m_cursor->x, manager->m_cursor->y, &sx, &sy);
        if (lock_surf) {
            lock_surf->focus();
        }
        wlr_seat_pointer_notify_button(manager->m_seat, event->time_msec, event->button, event->state);
        return;
    }

    bool pressed = (event->state == WL_POINTER_BUTTON_STATE_PRESSED);
    if (manager->m_server->get_plugin_manager() &&
        manager->m_server->get_plugin_manager()->dispatch_pointer_button(manager->m_cursor->x, manager->m_cursor->y, event->button, pressed)) {
        return;
    }

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (manager->m_cursor_mode != CursorMode::Passthrough) {
            manager->end_interactive();
            return;
        }
    }

    double sx, sy;
    struct wlr_surface* surface = nullptr;
    View* view = manager->m_server->view_at(manager->m_cursor->x, manager->m_cursor->y, &surface, &sx, &sy);

    View* target_view = view;
    if (target_view && target_view->has_child_dialogs()) {
        View* top_dialog = target_view->get_top_dialog();
        if (top_dialog) {
            target_view = top_dialog;
        }
    }

    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        // Direct border drag-resize without holding Super
        if (event->button == BTN_LEFT && manager->m_border_hover_view && manager->m_border_hover_edges != 0) {
            manager->begin_interactive(manager->m_border_hover_view, CursorMode::Resize, manager->m_border_hover_edges);
            return;
        }

        struct wlr_keyboard* kb = wlr_seat_get_keyboard(manager->m_seat);
        uint32_t modifiers = kb ? wlr_keyboard_get_modifiers(kb) : 0;
        bool mod_pressed = (modifiers & WLR_MODIFIER_LOGO) != 0;

        if (mod_pressed && target_view && !target_view->is_fullscreen() && !target_view->is_override_redirect()) {
            if (event->button == BTN_LEFT) {
                manager->begin_interactive(target_view, CursorMode::Move, 0);
                return;
            } else if (event->button == BTN_RIGHT) {
                uint32_t edges = 0;
                double center_x = target_view->get_x() + target_view->get_width() / 2.0;
                double center_y = target_view->get_y() + target_view->get_height() / 2.0;
                if (manager->m_cursor->x < center_x) edges |= WLR_EDGE_LEFT;
                else edges |= WLR_EDGE_RIGHT;
                if (manager->m_cursor->y < center_y) edges |= WLR_EDGE_TOP;
                else edges |= WLR_EDGE_BOTTOM;

                manager->begin_interactive(target_view, CursorMode::Resize, edges);
                return;
            }
        }

        if (target_view && !target_view->is_override_redirect() && target_view != manager->m_server->get_focused_view()) {
            target_view->focus();
        }
    }

    wlr_seat_pointer_notify_button(manager->m_seat, event->time_msec, event->button, event->state);
}

void InputManager::handle_cursor_axis(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_cursor_axis_listener);
    auto* event = static_cast<struct wlr_pointer_axis_event*>(data);

    wlr_seat_pointer_notify_axis(manager->m_seat, event->time_msec, event->orientation,
        event->delta, event->delta_discrete, event->source, event->relative_direction);
}

void InputManager::handle_cursor_frame(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_cursor_frame_listener);
    wlr_seat_pointer_notify_frame(manager->m_seat);
}

void InputManager::handle_request_set_cursor(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_request_set_cursor_listener);
    auto* event = static_cast<struct wlr_seat_pointer_request_set_cursor_event*>(data);

    struct wlr_seat_client* focused_client = manager->m_seat->pointer_state.focused_client;
    if (focused_client == event->seat_client) {
        wlr_cursor_set_surface(manager->m_cursor, event->surface, event->hotspot_x, event->hotspot_y);
    }
}

// === Clipboard Event Handlers ===

void InputManager::handle_request_set_selection(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_request_set_selection_listener);
    auto* event = static_cast<struct wlr_seat_request_set_selection_event*>(data);
    wlr_seat_set_selection(manager->m_seat, event->source, event->serial);
}



void InputManager::handle_selection_destroy(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_selection_destroy_listener);
    wl_list_remove(&manager->m_selection_destroy_listener.link);
    wl_list_init(&manager->m_selection_destroy_listener.link);
}

// ================================

void InputManager::handle_cursor_swipe_begin(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_cursor_swipe_begin_listener);
    auto* event = static_cast<struct wlr_pointer_swipe_begin_event*>(data);
    (void)event;

    if (manager->m_server->is_locked()) {
        return;
    }

    manager->m_swipe_dx = 0.0;
    manager->m_swipe_dy = 0.0;
    manager->m_swipe_triggered = false;
    manager->m_touchpad_workspace_swipe_active = false;
}

void InputManager::handle_cursor_swipe_update(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_cursor_swipe_update_listener);
    auto* event = static_cast<struct wlr_pointer_swipe_update_event*>(data);

    if (manager->m_server->is_locked()) {
        return;
    }

    double screen_w = 1920.0;
    if (manager->m_server && manager->m_server->get_output_manager()) {
        auto geom = manager->m_server->get_output_manager()->get_primary_usable_geometry();
        if (geom.width > 0) screen_w = geom.width;
    }
    double touchpad_multiplier = std::max(6.0, screen_w / 140.0) * Config::get().get_touchpad_workspace_swipe_multiplier();

    if (manager->m_touchpad_workspace_swipe_active) {
        manager->m_server->get_animation_manager()->update_workspace_swipe(event->dx * touchpad_multiplier, event->dy);
        return;
    }

    if (manager->m_swipe_triggered) {
        return;
    }

    manager->m_swipe_dx += event->dx;
    manager->m_swipe_dy += event->dy;
    double threshold = Config::get().get_touchpad_swipe_threshold();

    // Check if horizontal 3-finger live workspace swipe should activate
    if (event->fingers == 3 && Config::get().is_workspace_animations_enabled() && manager->m_server->get_animation_manager()) {
        if (std::abs(manager->m_swipe_dx) >= 6.0 && std::abs(manager->m_swipe_dx) > std::abs(manager->m_swipe_dy) * 1.2) {
            manager->m_touchpad_workspace_swipe_active = true;
            manager->m_server->get_animation_manager()->begin_workspace_swipe(3);
            manager->m_server->get_animation_manager()->update_workspace_swipe(event->dx * touchpad_multiplier, event->dy);
            return;
        }
    }

    if (std::abs(manager->m_swipe_dx) >= threshold || std::abs(manager->m_swipe_dy) >= threshold) {
        std::string direction;
        if (std::abs(manager->m_swipe_dx) >= std::abs(manager->m_swipe_dy)) {
            direction = (manager->m_swipe_dx > 0) ? "right" : "left";
        } else {
            direction = (manager->m_swipe_dy > 0) ? "down" : "up";
        }

        std::string pattern = "swipe:" + std::to_string(event->fingers) + ":" + direction;
        std::string action = Config::get().find_gesture_action(pattern);

        if (!action.empty()) {
            log_info("Touchpad gesture triggered: " + pattern + " -> " + action);
            manager->execute_action(action);
        }
        manager->m_swipe_triggered = true;
    }
}

void InputManager::handle_cursor_swipe_end(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_cursor_swipe_end_listener);
    auto* event = static_cast<struct wlr_pointer_swipe_end_event*>(data);

    if (manager->m_server->is_locked()) {
        return;
    }

    if (manager->m_touchpad_workspace_swipe_active) {
        manager->m_server->get_animation_manager()->end_workspace_swipe(event->cancelled);
        manager->m_touchpad_workspace_swipe_active = false;
    }

    manager->m_swipe_dx = 0.0;
    manager->m_swipe_dy = 0.0;
    manager->m_swipe_triggered = false;
}

void InputManager::handle_cursor_pinch_begin(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_cursor_pinch_begin_listener);
    auto* event = static_cast<struct wlr_pointer_pinch_begin_event*>(data);
    if (manager->m_server->get_pointer_gestures()) {
        wlr_pointer_gestures_v1_send_pinch_begin(manager->m_server->get_pointer_gestures(),
            manager->m_seat, event->time_msec, event->fingers);
    }
}

void InputManager::handle_cursor_pinch_update(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_cursor_pinch_update_listener);
    auto* event = static_cast<struct wlr_pointer_pinch_update_event*>(data);
    if (manager->m_server->get_pointer_gestures()) {
        wlr_pointer_gestures_v1_send_pinch_update(manager->m_server->get_pointer_gestures(),
            manager->m_seat, event->time_msec, event->dx, event->dy, event->scale, event->rotation);
    }
}

void InputManager::handle_cursor_pinch_end(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_cursor_pinch_end_listener);
    auto* event = static_cast<struct wlr_pointer_pinch_end_event*>(data);
    if (manager->m_server->get_pointer_gestures()) {
        wlr_pointer_gestures_v1_send_pinch_end(manager->m_server->get_pointer_gestures(),
            manager->m_seat, event->time_msec, event->cancelled);
    }
}

void InputManager::handle_cursor_hold_begin(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_cursor_hold_begin_listener);
    auto* event = static_cast<struct wlr_pointer_hold_begin_event*>(data);
    if (manager->m_server->get_pointer_gestures()) {
        wlr_pointer_gestures_v1_send_hold_begin(manager->m_server->get_pointer_gestures(),
            manager->m_seat, event->time_msec, event->fingers);
    }
}

void InputManager::handle_cursor_hold_end(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_cursor_hold_end_listener);
    auto* event = static_cast<struct wlr_pointer_hold_end_event*>(data);
    if (manager->m_server->get_pointer_gestures()) {
        wlr_pointer_gestures_v1_send_hold_end(manager->m_server->get_pointer_gestures(),
            manager->m_seat, event->time_msec, event->cancelled);
    }
}

void InputManager::handle_cursor_touch_down(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_cursor_touch_down_listener);
    auto* event = static_cast<struct wlr_touch_down_event*>(data);

    if (manager->m_server->get_idle_notifier()) {
        wlr_idle_notifier_v1_notify_activity(manager->m_server->get_idle_notifier(), manager->m_seat);
    }

    double lx = 0.0, ly = 0.0;
    wlr_cursor_absolute_to_layout_coords(manager->m_cursor, &event->touch->base, event->x, event->y, &lx, &ly);

    if (manager->m_server->is_locked()) {
        double sx = 0, sy = 0;
        SessionLockSurface* lock_surf = manager->m_server->get_session_lock()->surface_at(lx, ly, &sx, &sy);
        if (lock_surf && lock_surf->get_wlr_surface()) {
            lock_surf->focus();
            wlr_seat_touch_notify_down(manager->m_seat, lock_surf->get_wlr_surface(),
                event->time_msec, event->touch_id, sx, sy);
        }
        return;
    }

    // Register touch point
    manager->m_touch_points[event->touch_id] = { event->touch_id, lx, ly, lx, ly };
    int finger_count = static_cast<int>(manager->m_touch_points.size());

    // Check if multi-finger gesture should be handled by compositor
    if (finger_count >= 3 || (finger_count >= 2 && Config::get().has_gesture_for_fingers(finger_count))) {
        // Reset origin points for all fingers to current coordinates when multi-touch is grounded
        for (auto& [id, pt] : manager->m_touch_points) {
            pt.start_lx = pt.current_lx;
            pt.start_ly = pt.current_ly;
            // Clear any client focus from previous fingers so app doesn't receive conflicting touch input
            wlr_seat_touch_notify_clear_focus(manager->m_seat, event->time_msec, id);
        }
        return;
    }

    if (manager->m_touch_gesture_active || manager->m_touch_workspace_swipe_active) {
        return;
    }

    double sx = 0.0, sy = 0.0;
    struct wlr_surface* surface = nullptr;
    View* view = manager->m_server->view_at(lx, ly, &surface, &sx, &sy);

    View* target_view = view;
    if (target_view && target_view->has_child_dialogs()) {
        View* top_dialog = target_view->get_top_dialog();
        if (top_dialog) target_view = top_dialog;
    }

    if (target_view && !target_view->is_override_redirect() && target_view != manager->m_server->get_focused_view()) {
        target_view->focus();
    }

    if (surface) {
        wlr_seat_touch_notify_down(manager->m_seat, surface, event->time_msec, event->touch_id, sx, sy);
    }
}

void InputManager::handle_cursor_touch_up(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_cursor_touch_up_listener);
    auto* event = static_cast<struct wlr_touch_up_event*>(data);

    manager->m_touch_points.erase(event->touch_id);

    if (manager->m_touch_workspace_swipe_active) {
        if (manager->m_touch_points.size() < 3) {
            manager->m_server->get_animation_manager()->end_workspace_swipe(false);
            manager->m_touch_workspace_swipe_active = false;
        }
    }

    if (manager->m_touch_points.empty()) {
        manager->m_touch_gesture_active = false;
        manager->m_touch_workspace_swipe_active = false;
    }

    if (!manager->m_touch_gesture_active && !manager->m_touch_workspace_swipe_active) {
        wlr_seat_touch_notify_up(manager->m_seat, event->time_msec, event->touch_id);
    }
}

void InputManager::handle_cursor_touch_motion(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_cursor_touch_motion_listener);
    auto* event = static_cast<struct wlr_touch_motion_event*>(data);

    double lx = 0.0, ly = 0.0;
    wlr_cursor_absolute_to_layout_coords(manager->m_cursor, &event->touch->base, event->x, event->y, &lx, &ly);

    if (manager->m_server->is_locked()) {
        double sx = 0.0, sy = 0.0;
        SessionLockSurface* lock_surf = manager->m_server->get_session_lock()->surface_at(lx, ly, &sx, &sy);
        if (lock_surf && lock_surf->get_wlr_surface()) {
            wlr_seat_touch_notify_motion(manager->m_seat, event->time_msec, event->touch_id, sx, sy);
        }
        return;
    }

    auto it = manager->m_touch_points.find(event->touch_id);
    if (it != manager->m_touch_points.end()) {
        it->second.current_lx = lx;
        it->second.current_ly = ly;
    }

    int finger_count = static_cast<int>(manager->m_touch_points.size());

    if (manager->m_touch_workspace_swipe_active) {
        if (finger_count >= 3) {
            double sum_x = 0.0;
            for (const auto& [id, pt] : manager->m_touch_points) {
                sum_x += pt.current_lx;
            }
            double cur_center_x = sum_x / static_cast<double>(finger_count);
            double dx = cur_center_x - manager->m_last_touch_center_x;
            manager->m_last_touch_center_x = cur_center_x;
            double touch_multiplier = 1.0 * Config::get().get_touchscreen_workspace_swipe_multiplier();
            manager->m_server->get_animation_manager()->update_workspace_swipe(dx * touch_multiplier, 0.0);
        }
        return;
    }

    // Multi-finger gesture recognition on touchscreen
    if (!manager->m_touch_gesture_active &&
        (finger_count >= 3 || (finger_count >= 2 && Config::get().has_gesture_for_fingers(finger_count)))) {

        double total_dx = 0.0;
        double total_dy = 0.0;
        for (const auto& [id, pt] : manager->m_touch_points) {
            total_dx += (pt.current_lx - pt.start_lx);
            total_dy += (pt.current_ly - pt.start_ly);
        }
        double avg_dx = total_dx / finger_count;
        double avg_dy = total_dy / finger_count;
        double threshold = Config::get().get_touchscreen_swipe_threshold();

        // Check if 3-finger horizontal swipe should start interactive workspace swipe
        if (finger_count == 3 && Config::get().is_workspace_animations_enabled() && manager->m_server->get_animation_manager()) {
            if (std::abs(avg_dx) >= 12.0 && std::abs(avg_dx) > std::abs(avg_dy) * 1.2) {
                double sum_x = 0.0;
                for (const auto& [id, pt] : manager->m_touch_points) {
                    sum_x += pt.current_lx;
                }
                manager->m_last_touch_center_x = sum_x / 3.0;
                manager->m_touch_workspace_swipe_active = true;
                manager->m_touch_gesture_active = true;
                manager->m_server->get_animation_manager()->begin_workspace_swipe(3);
                return;
            }
        }

        if (std::abs(avg_dx) >= threshold || std::abs(avg_dy) >= threshold) {
            std::string direction;
            if (std::abs(avg_dx) >= std::abs(avg_dy)) {
                direction = (avg_dx > 0) ? "right" : "left";
            } else {
                direction = (avg_dy > 0) ? "down" : "up";
            }

            std::string pattern = "swipe:" + std::to_string(finger_count) + ":" + direction;
            std::string action = Config::get().find_gesture_action(pattern);

            if (!action.empty()) {
                log_info("Touchscreen gesture triggered: " + pattern + " -> " + action);
                manager->execute_action(action);
            }
            manager->m_touch_gesture_active = true;
        }
        return;
    }

    if (manager->m_touch_gesture_active) {
        return;
    }

    double sx = 0.0, sy = 0.0;
    struct wlr_surface* surface = nullptr;
    manager->m_server->view_at(lx, ly, &surface, &sx, &sy);

    if (surface) {
        wlr_seat_touch_notify_motion(manager->m_seat, event->time_msec, event->touch_id, sx, sy);
    }
}

void InputManager::handle_cursor_touch_cancel(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_cursor_touch_cancel_listener);
    auto* event = static_cast<struct wlr_touch_cancel_event*>(data);

    manager->m_touch_points.erase(event->touch_id);

    if (manager->m_touch_workspace_swipe_active) {
        manager->m_server->get_animation_manager()->end_workspace_swipe(true);
        manager->m_touch_workspace_swipe_active = false;
    }

    if (manager->m_touch_points.empty()) {
        manager->m_touch_gesture_active = false;
        manager->m_touch_workspace_swipe_active = false;
    }

    wlr_seat_touch_notify_clear_focus(manager->m_seat, event->time_msec, event->touch_id);
}

void InputManager::handle_cursor_touch_frame(struct wl_listener* listener, void* data) {
    InputManager* manager = wl_container_of(listener, manager, m_cursor_touch_frame_listener);
    if (!manager->m_touch_gesture_active && !manager->m_touch_workspace_swipe_active) {
        wlr_seat_touch_notify_frame(manager->m_seat);
    }
}

Keyboard::Keyboard(Server* server, struct wlr_input_device* device)
    : m_server(server), m_device(device)
{
    m_keyboard = wlr_keyboard_from_input_device(device);

    reapply_keymap();

    m_modifiers_listener.notify = handle_modifiers;
    wl_signal_add(&m_keyboard->events.modifiers, &m_modifiers_listener);

    m_key_listener.notify = handle_key;
    wl_signal_add(&m_keyboard->events.key, &m_key_listener);

    m_destroy_listener.notify = handle_destroy;
    wl_signal_add(&device->events.destroy, &m_destroy_listener);

    wlr_seat_set_keyboard(server->get_input_manager()->get_seat(), m_keyboard);
}

void Keyboard::reapply_keymap() {
    if (!m_keyboard) return;

    struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    const auto& conf = Config::get();

    struct xkb_rule_names rules = {};
    if (!conf.get_kb_layout().empty()) rules.layout = conf.get_kb_layout().c_str();
    if (!conf.get_kb_variant().empty()) rules.variant = conf.get_kb_variant().c_str();
    if (!conf.get_kb_options().empty()) rules.options = conf.get_kb_options().c_str();
    if (!conf.get_kb_model().empty()) rules.model = conf.get_kb_model().c_str();

    struct xkb_keymap* keymap = xkb_keymap_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap) {
        wlr_keyboard_set_keymap(m_keyboard, keymap);
        xkb_keymap_unref(keymap);
        log_info("Applied XKB keymap (layout: " + conf.get_kb_layout() +
                 ", options: " + conf.get_kb_options() + ")");
    } else {
        log_error("Failed to compile XKB keymap with layout '" + conf.get_kb_layout() +
                  "', options '" + conf.get_kb_options() + "'");
    }
    xkb_context_unref(context);

    wlr_keyboard_set_repeat_info(m_keyboard, conf.get_repeat_rate(), conf.get_repeat_delay());
}

Keyboard::~Keyboard() {
    wl_list_remove(&m_modifiers_listener.link);
    wl_list_remove(&m_key_listener.link);
    wl_list_remove(&m_destroy_listener.link);
}

void Keyboard::handle_modifiers(struct wl_listener* listener, void* data) {
    Keyboard* kb = wl_container_of(listener, kb, m_modifiers_listener);
    struct wlr_seat* seat = kb->m_server->get_input_manager()->get_seat();
    
    wlr_seat_set_keyboard(seat, kb->m_keyboard);
    wlr_seat_keyboard_notify_modifiers(seat, &kb->m_keyboard->modifiers);
}

void Keyboard::handle_key(struct wl_listener* listener, void* data) {
    Keyboard* kb = wl_container_of(listener, kb, m_key_listener);
    auto* event = static_cast<struct wlr_keyboard_key_event*>(data);
    struct wlr_seat* seat = kb->m_server->get_input_manager()->get_seat();

    if (kb->m_server->get_idle_notifier()) {
        wlr_idle_notifier_v1_notify_activity(kb->m_server->get_idle_notifier(), seat);
    }

    uint32_t keycode = event->keycode + 8;
    const xkb_keysym_t* syms;
    int nsyms = xkb_state_key_get_syms(kb->m_keyboard->xkb_state, keycode, &syms);

    bool handled = false;
    uint32_t modifiers = wlr_keyboard_get_modifiers(kb->m_keyboard);
    bool pressed = (event->state == WL_KEYBOARD_KEY_STATE_PRESSED);

    if (kb->m_server->get_plugin_manager()) {
        for (int i = 0; i < nsyms; ++i) {
            handled = kb->m_server->get_plugin_manager()->dispatch_key(syms[i], modifiers, pressed);
            if (handled) break;
        }
    }

    if (!handled && pressed) {
        for (int i = 0; i < nsyms; ++i) {
            handled = kb->m_server->get_input_manager()->handle_keybinding(modifiers, syms[i]);
            if (handled) break;
        }

        // If not handled (e.g. Shift was held down turning '1' into '!'), check unshifted base sym
        if (!handled && kb->m_keyboard->keymap) {
            xkb_layout_index_t layout = xkb_state_key_get_layout(kb->m_keyboard->xkb_state, keycode);
            const xkb_keysym_t* raw_syms;
            int n_raw_syms = xkb_keymap_key_get_syms_by_level(kb->m_keyboard->keymap, keycode, layout, 0, &raw_syms);
            for (int i = 0; i < n_raw_syms; ++i) {
                handled = kb->m_server->get_input_manager()->handle_keybinding(modifiers, raw_syms[i]);
                if (handled) break;
            }
        }
    }

    if (!handled) {
        wlr_seat_set_keyboard(seat, kb->m_keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
    }
}

void Keyboard::handle_destroy(struct wl_listener* listener, void* data) {
    Keyboard* kb = wl_container_of(listener, kb, m_destroy_listener);
    kb->m_server->get_input_manager()->remove_keyboard(kb);
}

} // namespace miquland
