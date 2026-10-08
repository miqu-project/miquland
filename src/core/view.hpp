#pragma once

#include "core/common/util.hpp"
#include "core/common/cairo_buffer.hpp"
#include <string>
#include <vector>
#include <memory>

struct wlr_xwayland_surface;

namespace miquland {

class Server;
class Workspace;
class Popup;
class Output;

enum class ViewType {
    Xdg,
    XWayland
};

class View {
public:
    View(Server* server, struct wlr_xdg_toplevel* toplevel);
    View(Server* server, struct wlr_xwayland_surface* xsurface);
    ~View();

    Server* get_server() const { return m_server; }
    ViewType get_type() const { return m_type; }
    uint64_t get_id() const { return m_id; }

    void set_workspace(Workspace* ws);
    Workspace* get_workspace() const { return m_workspace; }

    void set_geometry(int x, int y, int width, int height);
    void set_fullscreen(bool fullscreen);
    bool is_fullscreen() const { return m_is_fullscreen; }
    void focus();
    void close();

    void update_frame();
    void update_border();
    void update_shadow();
    void update_opacity();
    void update_corner_radius();
    void apply_animation_transform(double scale, float opacity);
    void apply_geometry_animation(int x, int y, int width, int height);
    void notify_geometry_target(int x, int y, int width, int height);
    void finish_geometry_animation(int x, int y, int width, int height);
    bool is_animating_geometry() const { return m_is_animating_geometry; }
    bool is_focused() const;

    std::string get_title() const;
    std::string get_app_id() const;

    bool is_mapping() const { return m_is_mapping; }
    void set_mapping(bool mapping) { m_is_mapping = mapping; }

    bool is_dialog() const { return m_is_dialog; }
    bool is_floating() const { return m_is_floating; }
    void set_floating(bool floating);
    View* get_parent_view() const { return m_parent_view; }
    void set_parent_view(View* parent);
    void update_parent_relationship();
    bool has_child_dialogs() const;
    View* get_top_dialog() const;
    const std::vector<View*>& get_child_dialogs() const { return m_child_dialogs; }
    bool has_popups() const { return !m_popups.empty(); }

    struct wlr_xdg_toplevel* get_xdg_toplevel() const { return m_xdg_toplevel; }
    struct wlr_xwayland_surface* get_xwayland_surface() const { return m_xwayland_surface; }
    struct wlr_scene_tree* get_scene_tree() const { return m_scene_tree; }
    struct wlr_scene_tree* get_surface_scene_tree() const { return m_surface_scene_tree; }
    struct wlr_scene_buffer* get_border_scene_buffer() const { return m_border_scene_buffer; }
    struct wlr_scene_blur* get_blur_node() const { return m_blur_node; }
    struct wlr_scene_shadow* get_shadow_node() const { return m_shadow_node; }
    struct wlr_foreign_toplevel_handle_v1* get_foreign_toplevel() const { return m_foreign_toplevel; }
    bool is_mapped() const { return m_mapped; }
    bool is_override_redirect() const {
        if (m_type == ViewType::XWayland && m_xwayland_surface) {
            return m_xwayland_surface->override_redirect;
        }
        return m_is_override_redirect;
    }

    int get_x() const { return m_x; }
    int get_y() const { return m_y; }
    int get_width() const { return m_width; }
    int get_height() const { return m_height; }
    int get_target_x() const { return (m_target_width > 0) ? m_target_x : m_x; }
    int get_target_y() const { return (m_target_height > 0) ? m_target_y : m_y; }
    int get_target_width() const { return (m_target_width > 0) ? m_target_width : m_width; }
    int get_target_height() const { return (m_target_height > 0) ? m_target_height : m_height; }
    int get_current_anim_x() const { return (m_current_anim_width > 0) ? m_current_anim_x : m_x; }
    int get_current_anim_y() const { return (m_current_anim_height > 0) ? m_current_anim_y : m_y; }
    int get_current_anim_width() const { return (m_current_anim_width > 0) ? m_current_anim_width : m_width; }
    bool is_animating_close() const { return m_is_animating_close; }
    void set_animating_close(bool animating) { m_is_animating_close = animating; }
    bool is_animating_open() const { return m_is_animating_open; }
    void set_animating_open(bool animating) { m_is_animating_open = animating; }
    float get_output_scale() const;
    Output* get_output() const;
    struct wlr_surface* get_wlr_surface() const;
    bool tearing_allowed() const;

    void set_overview_scaled(bool scaled, double scale = 1.0);
    void reapply_overview_scale();
    bool is_overview_scaled() const { return m_is_overview_scaled; }

private:
    // XDG Handlers
    static void handle_map(struct wl_listener* listener, void* data);
    static void handle_unmap(struct wl_listener* listener, void* data);
    static void handle_destroy(struct wl_listener* listener, void* data);
    static void handle_commit(struct wl_listener* listener, void* data);
    static void handle_request_fullscreen(struct wl_listener* listener, void* data);
    static void handle_request_maximize(struct wl_listener* listener, void* data);
    static void handle_request_move(struct wl_listener* listener, void* data);
    static void handle_request_resize(struct wl_listener* listener, void* data);
    static void handle_set_title(struct wl_listener* listener, void* data);
    static void handle_set_app_id(struct wl_listener* listener, void* data);
    static void handle_set_parent(struct wl_listener* listener, void* data);
    static void handle_foreign_request_activate(struct wl_listener* listener, void* data);
    static void handle_foreign_request_close(struct wl_listener* listener, void* data);
    static void handle_new_popup(struct wl_listener* listener, void* data);
    static void handle_scene_tree_destroy(struct wl_listener* listener, void* data);

    // XWayland Handlers
    static void handle_xwayland_associate(struct wl_listener* listener, void* data);
    static void handle_xwayland_dissociate(struct wl_listener* listener, void* data);
    static void handle_xwayland_request_configure(struct wl_listener* listener, void* data);
    static void handle_xwayland_request_activate(struct wl_listener* listener, void* data);
    static void handle_xwayland_request_move(struct wl_listener* listener, void* data);
    static void handle_xwayland_request_resize(struct wl_listener* listener, void* data);
    static void handle_xwayland_set_geometry(struct wl_listener* listener, void* data);
    static void handle_xwayland_set_class(struct wl_listener* listener, void* data);
    static void handle_xwayland_set_parent(struct wl_listener* listener, void* data);
    static void handle_xwayland_set_override_redirect(struct wl_listener* listener, void* data);

    void setup_foreign_toplevel();
    void update_blur();
    void sync_shadow_geometry(int w, int h, int r, float sigma, int ox, int oy);

    Server* m_server = nullptr;
    Workspace* m_workspace = nullptr;
    ViewType m_type = ViewType::Xdg;

    struct wlr_xdg_toplevel* m_xdg_toplevel = nullptr;
    struct wlr_xwayland_surface* m_xwayland_surface = nullptr;
    struct wlr_foreign_toplevel_handle_v1* m_foreign_toplevel = nullptr;

    struct wlr_scene_tree* m_scene_tree = nullptr;
    struct wlr_scene_buffer* m_border_scene_buffer = nullptr;
    struct wlr_scene_tree* m_surface_scene_tree = nullptr;
    struct wlr_scene_blur* m_blur_node = nullptr;
    struct wlr_scene_shadow* m_shadow_node = nullptr;
    std::unique_ptr<CairoBuffer> m_border_buffer;

    bool m_mapped = false;
    bool m_is_mapping = false;
    bool m_is_animating_close = false;
    bool m_is_animating_open = false;
    bool m_is_animating_geometry = false;
    bool m_is_override_redirect = false;
    bool m_is_fullscreen = false;
    bool m_is_floating = false;
    bool m_is_overview_scaled = false;
    double m_overview_scale = 1.0;
    int m_x = 0;
    int m_y = 0;
    int m_width = 0;
    int m_height = 0;
    int m_target_x = 0;
    int m_target_y = 0;
    int m_target_width = 0;
    int m_target_height = 0;
    int m_current_anim_x = 0;
    int m_current_anim_y = 0;
    int m_current_anim_width = 0;
    int m_current_anim_height = 0;

    // Saved state for fullscreen → tiled restoration
    struct {
        int x = 0, y = 0, w = 0, h = 0;
        struct wlr_scene_tree* workspace_tree = nullptr;
    } m_saved_geometry;

    struct wl_listener m_map_listener;
    struct wl_listener m_unmap_listener;
    struct wl_listener m_destroy_listener;
    struct wl_listener m_commit_listener;
    struct wl_listener m_request_fullscreen_listener;
    struct wl_listener m_request_maximize_listener;
    struct wl_listener m_request_move_listener;
    struct wl_listener m_request_resize_listener;
    struct wl_listener m_set_title_listener;
    struct wl_listener m_set_app_id_listener;
    struct wl_listener m_set_parent_listener;
    struct wl_listener m_foreign_request_activate_listener;
    struct wl_listener m_foreign_request_close_listener;
    struct wl_listener m_new_popup_listener;
    struct wl_listener m_scene_tree_destroy_listener;

    // XWayland specific listeners
    struct wl_listener m_associate_listener;
    struct wl_listener m_dissociate_listener;
    struct wl_listener m_request_configure_listener;
    struct wl_listener m_request_activate_listener;
    struct wl_listener m_xwayland_request_move_listener;
    struct wl_listener m_xwayland_request_resize_listener;
    struct wl_listener m_set_geometry_listener;
    struct wl_listener m_set_class_listener;
    struct wl_listener m_set_override_redirect_listener;

    View* m_parent_view = nullptr;
    bool m_is_dialog = false;
    std::vector<View*> m_child_dialogs;
    std::vector<std::unique_ptr<Popup>> m_popups;
    uint64_t m_id = 0;
};

} // namespace miquland
