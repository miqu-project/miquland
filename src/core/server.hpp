#pragma once

#include "core/common/util.hpp"
#include <vector>
#include <memory>
#include <string>

namespace miquland {

class OutputManager;
class WorkspaceManager;
class InputManager;
class View;
class LayerSurface;
class SessionLock;
class PluginManager;
class AnimationManager;

class Server {
public:
    Server();
    ~Server();

    bool init();
    void run();
    void terminate();

    void set_startup_command(const std::string& cmd) { m_startup_cmd = cmd; }

    struct wl_display* get_display() const { return m_wl_display; }
    struct wl_event_loop* get_event_loop() const { return m_wl_event_loop; }
    struct wlr_backend* get_backend() const { return m_wlr_backend; }
    struct wlr_renderer* get_renderer() const { return m_wlr_renderer; }
    struct wlr_allocator* get_allocator() const { return m_wlr_allocator; }
    struct wlr_scene* get_scene() const { return m_scene; }

    struct wlr_scene_tree* get_workspaces_tree() const { return m_workspaces_tree; }

    struct wlr_scene_tree* get_layer_tree(enum zwlr_layer_shell_v1_layer layer) const;
    struct wlr_scene_tree* get_layer_background_tree() const { return m_layer_background_tree; }
    struct wlr_scene_tree* get_layer_bottom_tree() const { return m_layer_bottom_tree; }
    struct wlr_scene_tree* get_layer_top_tree() const { return m_layer_top_tree; }
    struct wlr_scene_tree* get_layer_overlay_tree() const { return m_layer_overlay_tree; }

    OutputManager* get_output_manager() const { return m_output_manager.get(); }
    WorkspaceManager* get_workspace_manager() const { return m_workspace_manager.get(); }
    InputManager* get_input_manager() const { return m_input_manager.get(); }
    PluginManager* get_plugin_manager() const { return m_plugin_manager.get(); }
    AnimationManager* get_animation_manager() const { return m_animation_manager.get(); }
    struct wlr_tearing_control_manager_v1* get_tearing_manager() const { return m_tearing_manager; }

    void add_view(std::unique_ptr<View> view);
    void remove_view(View* view);
    bool is_valid_view(const View* view) const;
    bool is_valid_view_ptr(const void* ptr) const;
    const std::vector<std::unique_ptr<View>>& get_views() const { return m_views; }
    View* get_view_by_id(uint64_t id) const;
    View* view_at(double lx, double ly, struct wlr_surface** surface, double* sx, double* sy);

    void add_layer_surface(std::unique_ptr<LayerSurface> surface);
    void remove_layer_surface(LayerSurface* surface);
    void arrange_layers(struct wlr_output* output);

    void focus_layer_surface(LayerSurface* surface);
    LayerSurface* get_focused_layer_surface() const { return m_focused_layer_surface; }
    const std::vector<std::unique_ptr<LayerSurface>>& get_layer_surfaces() const { return m_layer_surfaces; }
    struct wlr_foreign_toplevel_manager_v1* get_foreign_toplevel_manager() const { return m_foreign_toplevel_manager; }
    struct wlr_ext_workspace_manager_v1* get_ext_workspace_manager() const { return m_ext_workspace_manager; }
    struct wlr_ext_workspace_group_handle_v1* get_ext_workspace_group() const { return m_ext_workspace_group; }
    struct wlr_pointer_gestures_v1* get_pointer_gestures() const { return m_pointer_gestures; }
    struct wlr_xwayland* get_xwayland() const { return m_xwayland; }
    struct wlr_screencopy_manager_v1* get_screencopy_manager() const { return m_screencopy_manager; }
    struct wlr_gamma_control_manager_v1* get_gamma_control_manager() const { return m_gamma_control_manager; }
    struct wlr_output_power_manager_v1* get_output_power_manager() const { return m_output_power_manager; }
    struct wlr_idle_notifier_v1* get_idle_notifier() const { return m_idle_notifier; }
    struct wlr_idle_inhibit_manager_v1* get_idle_inhibit_manager() const { return m_idle_inhibit_manager; }
    struct wlr_xdg_decoration_manager_v1* get_xdg_decoration_manager() const { return m_xdg_decoration_manager; }
    struct wlr_xdg_activation_v1* get_xdg_activation() const { return m_xdg_activation; }
    struct wlr_cursor_shape_manager_v1* get_cursor_shape_manager() const { return m_cursor_shape_manager; }
    struct wlr_relative_pointer_manager_v1* get_relative_pointer_manager() const { return m_relative_pointer_manager; }
    struct wlr_pointer_constraints_v1* get_pointer_constraints() const { return m_pointer_constraints; }

    void set_focused_view(View* view);
    View* get_focused_view() const { return m_focused_view; }

    void reload_config();

    struct wlr_scene_tree* get_session_lock_tree() const { return m_session_lock_tree; }
    struct wlr_scene_tree* get_popup_tree() const { return m_popup_tree; }
    bool is_locked() const;
    SessionLock* get_session_lock() const;
    void unlock_session();
    void handle_idle_inhibitor_destroy();

private:
    static void handle_new_xdg_toplevel(struct wl_listener* listener, void* data);
    static void handle_new_layer_shell_surface(struct wl_listener* listener, void* data);
    static void handle_ext_workspace_commit(struct wl_listener* listener, void* data);
    static void handle_xwayland_ready(struct wl_listener* listener, void* data);
    static void handle_xwayland_new_surface(struct wl_listener* listener, void* data);
    static int handle_config_inotify(int fd, uint32_t mask, void* data);
    static int handle_config_reload_timer(void* data);
    void setup_config_watcher();

    struct wl_display* m_wl_display = nullptr;
    struct wl_event_loop* m_wl_event_loop = nullptr;
    struct wlr_backend* m_wlr_backend = nullptr;
    struct wlr_renderer* m_wlr_renderer = nullptr;
    struct wlr_allocator* m_wlr_allocator = nullptr;
    struct wlr_compositor* m_wlr_compositor = nullptr;
    struct wlr_subcompositor* m_wlr_subcompositor = nullptr;
    struct wlr_data_device_manager* m_wlr_data_device_manager = nullptr;
    struct wlr_scene* m_scene = nullptr;

    struct wlr_scene_tree* m_layer_background_tree = nullptr;
    struct wlr_scene_tree* m_layer_bottom_tree = nullptr;
    struct wlr_scene_tree* m_workspaces_tree = nullptr;
    struct wlr_scene_tree* m_layer_top_tree = nullptr;
    struct wlr_scene_tree* m_popup_tree = nullptr;
    struct wlr_scene_tree* m_layer_overlay_tree = nullptr;
    struct wlr_scene_tree* m_session_lock_tree = nullptr;

    struct wlr_xdg_output_manager_v1* m_xdg_output_manager = nullptr;
    struct wlr_xdg_shell* m_xdg_shell = nullptr;
    struct wlr_layer_shell_v1* m_layer_shell = nullptr;
    struct wlr_session_lock_manager_v1* m_session_lock_manager = nullptr;
    struct wlr_ext_workspace_manager_v1* m_ext_workspace_manager = nullptr;
    struct wlr_ext_workspace_group_handle_v1* m_ext_workspace_group = nullptr;
    struct wlr_foreign_toplevel_manager_v1* m_foreign_toplevel_manager = nullptr;
    struct wlr_pointer_gestures_v1* m_pointer_gestures = nullptr;
    struct wlr_xwayland* m_xwayland = nullptr;
    struct wlr_screencopy_manager_v1* m_screencopy_manager = nullptr;
    struct wlr_gamma_control_manager_v1* m_gamma_control_manager = nullptr;
    struct wlr_output_power_manager_v1* m_output_power_manager = nullptr;
    struct wlr_idle_notifier_v1* m_idle_notifier = nullptr;
    struct wlr_idle_inhibit_manager_v1* m_idle_inhibit_manager = nullptr;
    struct wlr_xdg_decoration_manager_v1* m_xdg_decoration_manager = nullptr;
    struct wlr_xdg_activation_v1* m_xdg_activation = nullptr;
    struct wlr_cursor_shape_manager_v1* m_cursor_shape_manager = nullptr;
    struct wlr_relative_pointer_manager_v1* m_relative_pointer_manager = nullptr;
    struct wlr_pointer_constraints_v1* m_pointer_constraints = nullptr;
    struct wlr_viewporter* m_viewporter = nullptr;
    struct wlr_fractional_scale_manager_v1* m_fractional_scale_manager = nullptr;
    struct wlr_tearing_control_manager_v1* m_tearing_manager = nullptr;
    size_t m_idle_inhibitor_count = 0;

    const char* m_socket_name = nullptr;
    std::string m_startup_cmd;

    int m_inotify_fd = -1;
    int m_inotify_wd = -1;
    int m_inotify_file_wd = -1;
    struct wl_event_source* m_config_event_source = nullptr;
    struct wl_event_source* m_config_reload_timer = nullptr;

    std::unique_ptr<OutputManager> m_output_manager;
    std::unique_ptr<WorkspaceManager> m_workspace_manager;
    std::unique_ptr<InputManager> m_input_manager;

    std::vector<std::unique_ptr<View>> m_views;
    View* m_focused_view = nullptr;

    std::vector<std::unique_ptr<LayerSurface>> m_layer_surfaces;
    LayerSurface* m_focused_layer_surface = nullptr;

    std::unique_ptr<SessionLock> m_session_lock;
    std::unique_ptr<PluginManager> m_plugin_manager;
    std::unique_ptr<AnimationManager> m_animation_manager;

    static void handle_new_session_lock(struct wl_listener* listener, void* data);
    static void handle_gamma_set_gamma(struct wl_listener* listener, void* data);
    static void handle_output_power_set_mode(struct wl_listener* listener, void* data);
    static void handle_new_idle_inhibitor(struct wl_listener* listener, void* data);
    static void handle_new_xdg_decoration(struct wl_listener* listener, void* data);
    static void handle_xdg_activation_request_activate(struct wl_listener* listener, void* data);
    static void handle_cursor_shape_request_set_shape(struct wl_listener* listener, void* data);

    struct wl_listener m_new_xdg_toplevel_listener;
    struct wl_listener m_new_layer_shell_surface_listener;
    struct wl_listener m_session_lock_new_lock_listener;
    struct wl_listener m_ext_workspace_commit_listener;
    struct wl_listener m_xwayland_ready_listener;
    struct wl_listener m_xwayland_new_surface_listener;
    struct wl_listener m_gamma_set_gamma_listener;
    struct wl_listener m_output_power_set_mode_listener;
    struct wl_listener m_new_idle_inhibitor_listener;
    struct wl_listener m_new_xdg_decoration_listener;
    struct wl_listener m_xdg_activation_request_activate_listener;
    struct wl_listener m_cursor_shape_request_set_shape_listener;
};

} // namespace miquland
