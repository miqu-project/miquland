#include "core/view.hpp"
#include "core/popup.hpp"
#include "core/server.hpp"
#include "core/output.hpp"
#include "core/workspace.hpp"
#include "core/input/input.hpp"
#include "core/config/config.hpp"
#include "core/plugin_manager.hpp"
#include "core/animation/animation_manager.hpp"
#include <atomic>
#include <cmath>
#include <algorithm>

namespace miquland {

static void draw_rounded_rectangle(cairo_t* cr, double x, double y, double w, double h, double r) {
    if (r <= 0.0) {
        cairo_rectangle(cr, x, y, w, h);
        return;
    }
    r = std::min(r, std::min(w / 2.0, h / 2.0));
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, M_PI / 2.0);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2.0, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
    cairo_close_path(cr);
}

namespace {
static std::atomic<uint64_t> s_next_view_id{1};
}

View::View(Server* server, struct wlr_xdg_toplevel* toplevel)
    : m_server(server), m_type(ViewType::Xdg), m_xdg_toplevel(toplevel)
{
    m_id = s_next_view_id.fetch_add(1, std::memory_order_relaxed);

    wl_list_init(&m_map_listener.link);
    wl_list_init(&m_unmap_listener.link);
    wl_list_init(&m_destroy_listener.link);
    wl_list_init(&m_commit_listener.link);
    wl_list_init(&m_request_fullscreen_listener.link);
    wl_list_init(&m_request_maximize_listener.link);
    wl_list_init(&m_request_move_listener.link);
    wl_list_init(&m_request_resize_listener.link);
    wl_list_init(&m_set_title_listener.link);
    wl_list_init(&m_set_app_id_listener.link);
    wl_list_init(&m_set_parent_listener.link);
    wl_list_init(&m_foreign_request_activate_listener.link);
    wl_list_init(&m_foreign_request_close_listener.link);
    wl_list_init(&m_new_popup_listener.link);
    wl_list_init(&m_scene_tree_destroy_listener.link);
    wl_list_init(&m_associate_listener.link);
    wl_list_init(&m_dissociate_listener.link);
    wl_list_init(&m_request_configure_listener.link);
    wl_list_init(&m_request_activate_listener.link);
    wl_list_init(&m_xwayland_request_move_listener.link);
    wl_list_init(&m_xwayland_request_resize_listener.link);
    wl_list_init(&m_set_geometry_listener.link);
    wl_list_init(&m_set_class_listener.link);
    wl_list_init(&m_set_override_redirect_listener.link);

    // Create view container scene tree under workspaces tree (reparented to specific workspace later)
    struct wlr_scene_tree* root_parent = server->get_workspaces_tree() ? server->get_workspaces_tree() : &server->get_scene()->tree;
    m_scene_tree = wlr_scene_tree_create(root_parent);
    m_scene_tree->node.data = this;
    wlr_scene_node_set_enabled(&m_scene_tree->node, false);

    m_scene_tree_destroy_listener.notify = handle_scene_tree_destroy;
    wl_signal_add(&m_scene_tree->node.events.destroy, &m_scene_tree_destroy_listener);

    // Create xdg surface scene tree under view container tree (Child 1 - bottom layer)
    m_surface_scene_tree = wlr_scene_xdg_surface_create(m_scene_tree, toplevel->base);
    m_surface_scene_tree->node.data = this;
    toplevel->base->data = m_surface_scene_tree;

    // Create border scene buffer on top of xdg surface (Child 2 - top overlay layer)
    m_border_scene_buffer = wlr_scene_buffer_create(m_scene_tree, nullptr);
    m_border_scene_buffer->point_accepts_input = [](struct wlr_scene_buffer* buffer, double* sx, double* sy) -> bool {
        if (!Config::get().is_resize_on_border_enabled()) {
            return false;
        }
        if (!buffer || !buffer->node.parent || !buffer->node.parent->node.data) {
            return false;
        }
        auto* view = static_cast<View*>(buffer->node.parent->node.data);
        if (!view || view->is_fullscreen()) {
            return false;
        }
        int grab = std::max(Config::get().get_window_border_width(), Config::get().get_border_grab_area());
        int w = view->get_width();
        int h = view->get_height();
        if (w <= 0 || h <= 0) return false;
        if (*sx < grab || *sx >= w - grab || *sy < grab || *sy >= h - grab) {
            return true;
        }
        return false;
    };

    // Connect XDG event listeners
    m_map_listener.notify = handle_map;
    wl_signal_add(&toplevel->base->surface->events.map, &m_map_listener);

    m_unmap_listener.notify = handle_unmap;
    wl_signal_add(&toplevel->base->surface->events.unmap, &m_unmap_listener);

    m_destroy_listener.notify = handle_destroy;
    wl_signal_add(&toplevel->events.destroy, &m_destroy_listener);

    m_commit_listener.notify = handle_commit;
    wl_signal_add(&toplevel->base->surface->events.commit, &m_commit_listener);

    m_request_fullscreen_listener.notify = handle_request_fullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen, &m_request_fullscreen_listener);

    m_request_maximize_listener.notify = handle_request_maximize;
    wl_signal_add(&toplevel->events.request_maximize, &m_request_maximize_listener);

    m_request_move_listener.notify = handle_request_move;
    wl_signal_add(&toplevel->events.request_move, &m_request_move_listener);

    m_request_resize_listener.notify = handle_request_resize;
    wl_signal_add(&toplevel->events.request_resize, &m_request_resize_listener);

    m_set_title_listener.notify = handle_set_title;
    wl_signal_add(&toplevel->events.set_title, &m_set_title_listener);

    m_set_app_id_listener.notify = handle_set_app_id;
    wl_signal_add(&toplevel->events.set_app_id, &m_set_app_id_listener);

    m_set_parent_listener.notify = handle_set_parent;
    wl_signal_add(&toplevel->events.set_parent, &m_set_parent_listener);

    // Wire up XDG popup listener for context menus and dropdowns
    m_new_popup_listener.notify = handle_new_popup;
    wl_signal_add(&toplevel->base->events.new_popup, &m_new_popup_listener);

    update_parent_relationship();
    setup_foreign_toplevel();
}

View::View(Server* server, struct wlr_xwayland_surface* xsurface)
    : m_server(server), m_type(ViewType::XWayland), m_xwayland_surface(xsurface),
      m_is_override_redirect(xsurface->override_redirect)
{
    m_id = s_next_view_id.fetch_add(1, std::memory_order_relaxed);

    wl_list_init(&m_map_listener.link);
    wl_list_init(&m_unmap_listener.link);
    wl_list_init(&m_destroy_listener.link);
    wl_list_init(&m_commit_listener.link);
    wl_list_init(&m_request_fullscreen_listener.link);
    wl_list_init(&m_request_maximize_listener.link);
    wl_list_init(&m_request_move_listener.link);
    wl_list_init(&m_request_resize_listener.link);
    wl_list_init(&m_set_title_listener.link);
    wl_list_init(&m_set_app_id_listener.link);
    wl_list_init(&m_set_parent_listener.link);
    wl_list_init(&m_foreign_request_activate_listener.link);
    wl_list_init(&m_foreign_request_close_listener.link);
    wl_list_init(&m_new_popup_listener.link);
    wl_list_init(&m_scene_tree_destroy_listener.link);
    wl_list_init(&m_associate_listener.link);
    wl_list_init(&m_dissociate_listener.link);
    wl_list_init(&m_request_configure_listener.link);
    wl_list_init(&m_request_activate_listener.link);
    wl_list_init(&m_xwayland_request_move_listener.link);
    wl_list_init(&m_xwayland_request_resize_listener.link);
    wl_list_init(&m_set_geometry_listener.link);
    wl_list_init(&m_set_class_listener.link);
    wl_list_init(&m_set_override_redirect_listener.link);

    // Create view container scene tree: override-redirect windows (popups/menus) belong in popup_tree,
    // while regular application windows belong under workspaces_tree
    struct wlr_scene_tree* root_parent = m_is_override_redirect
        ? (server->get_popup_tree() ? server->get_popup_tree() : &server->get_scene()->tree)
        : (server->get_workspaces_tree() ? server->get_workspaces_tree() : &server->get_scene()->tree);
    m_scene_tree = wlr_scene_tree_create(root_parent);
    m_scene_tree->node.data = this;

    m_scene_tree_destroy_listener.notify = handle_scene_tree_destroy;
    wl_signal_add(&m_scene_tree->node.events.destroy, &m_scene_tree_destroy_listener);

    if (m_is_override_redirect) {
        m_x = xsurface->x;
        m_y = xsurface->y;
        m_width = xsurface->width;
        m_height = xsurface->height;
        wlr_scene_node_set_position(&m_scene_tree->node, m_x, m_y);
    } else {
        wlr_scene_node_set_enabled(&m_scene_tree->node, false);
        // Create border scene buffer on top of surface
        m_border_scene_buffer = wlr_scene_buffer_create(m_scene_tree, nullptr);
        m_border_scene_buffer->point_accepts_input = [](struct wlr_scene_buffer* buffer, double* sx, double* sy) -> bool {
            if (!Config::get().is_resize_on_border_enabled()) {
                return false;
            }
            if (!buffer || !buffer->node.parent || !buffer->node.parent->node.data) {
                return false;
            }
            auto* view = static_cast<View*>(buffer->node.parent->node.data);
            if (!view || view->is_fullscreen()) {
                return false;
            }
            int grab = std::max(Config::get().get_window_border_width(), Config::get().get_border_grab_area());
            int w = view->get_width();
            int h = view->get_height();
            if (w <= 0 || h <= 0) return false;
            if (*sx < grab || *sx >= w - grab || *sy < grab || *sy >= h - grab) {
                return true;
            }
            return false;
        };
    }

    // Connect XWayland event listeners
    m_associate_listener.notify = handle_xwayland_associate;
    wl_signal_add(&xsurface->events.associate, &m_associate_listener);

    m_dissociate_listener.notify = handle_xwayland_dissociate;
    wl_signal_add(&xsurface->events.dissociate, &m_dissociate_listener);

    m_destroy_listener.notify = handle_destroy;
    wl_signal_add(&xsurface->events.destroy, &m_destroy_listener);

    m_request_configure_listener.notify = handle_xwayland_request_configure;
    wl_signal_add(&xsurface->events.request_configure, &m_request_configure_listener);

    m_request_activate_listener.notify = handle_xwayland_request_activate;
    wl_signal_add(&xsurface->events.request_activate, &m_request_activate_listener);

    m_request_fullscreen_listener.notify = handle_request_fullscreen;
    wl_signal_add(&xsurface->events.request_fullscreen, &m_request_fullscreen_listener);

    m_request_maximize_listener.notify = handle_request_maximize;
    wl_signal_add(&xsurface->events.request_maximize, &m_request_maximize_listener);

    m_xwayland_request_move_listener.notify = handle_xwayland_request_move;
    wl_signal_add(&xsurface->events.request_move, &m_xwayland_request_move_listener);

    m_xwayland_request_resize_listener.notify = handle_xwayland_request_resize;
    wl_signal_add(&xsurface->events.request_resize, &m_xwayland_request_resize_listener);

    m_set_title_listener.notify = handle_set_title;
    wl_signal_add(&xsurface->events.set_title, &m_set_title_listener);

    m_set_class_listener.notify = handle_xwayland_set_class;
    wl_signal_add(&xsurface->events.set_class, &m_set_class_listener);

    m_set_parent_listener.notify = handle_xwayland_set_parent;
    wl_signal_add(&xsurface->events.set_parent, &m_set_parent_listener);

    m_set_geometry_listener.notify = handle_xwayland_set_geometry;
    wl_signal_add(&xsurface->events.set_geometry, &m_set_geometry_listener);

    m_set_override_redirect_listener.notify = handle_xwayland_set_override_redirect;
    wl_signal_add(&xsurface->events.set_override_redirect, &m_set_override_redirect_listener);

    // If surface was already associated at view creation time
    if (xsurface->surface) {
        handle_xwayland_associate(&m_associate_listener, nullptr);
    }

    setup_foreign_toplevel();
}

View::~View() {
    if (m_server && m_server->get_plugin_manager()) {
        m_server->get_plugin_manager()->dispatch_view_destroy(this);
    }

    if (m_server && m_server->get_input_manager()) {
        m_server->get_input_manager()->notify_view_destroyed(this);
    }

    if (m_server && m_server->get_animation_manager()) {
        m_server->get_animation_manager()->cancel_for_view(this);
    }

    if (m_workspace) {
        m_workspace->remove_view(this);
        m_workspace = nullptr;
    }


    safe_remove_listener(m_map_listener);
    safe_remove_listener(m_unmap_listener);
    safe_remove_listener(m_destroy_listener);
    safe_remove_listener(m_commit_listener);
    safe_remove_listener(m_request_fullscreen_listener);
    safe_remove_listener(m_request_maximize_listener);
    safe_remove_listener(m_request_move_listener);
    safe_remove_listener(m_request_resize_listener);
    safe_remove_listener(m_set_title_listener);
    safe_remove_listener(m_set_app_id_listener);
    safe_remove_listener(m_set_parent_listener);
    safe_remove_listener(m_new_popup_listener);

    safe_remove_listener(m_associate_listener);
    safe_remove_listener(m_dissociate_listener);
    safe_remove_listener(m_request_configure_listener);
    safe_remove_listener(m_request_activate_listener);
    safe_remove_listener(m_xwayland_request_move_listener);
    safe_remove_listener(m_xwayland_request_resize_listener);
    safe_remove_listener(m_set_class_listener);
    safe_remove_listener(m_set_geometry_listener);
    safe_remove_listener(m_set_override_redirect_listener);

    safe_remove_listener(m_foreign_request_activate_listener);
    safe_remove_listener(m_foreign_request_close_listener);
    safe_remove_listener(m_scene_tree_destroy_listener);

    if (m_server && m_server->get_animation_manager()) {
        m_server->get_animation_manager()->cancel_for_view(this);
    }

    if (m_parent_view) {
        auto& children = m_parent_view->m_child_dialogs;
        children.erase(std::remove(children.begin(), children.end(), this), children.end());
        m_parent_view = nullptr;
    }
    for (auto* child : m_child_dialogs) {
        if (child) child->m_parent_view = nullptr;
    }
    m_child_dialogs.clear();

    if (m_foreign_toplevel) {
        wlr_foreign_toplevel_handle_v1_destroy(m_foreign_toplevel);
        m_foreign_toplevel = nullptr;
    }

    m_popups.clear();

    if (m_scene_tree) {
        wlr_scene_node_destroy(&m_scene_tree->node);
        m_scene_tree = nullptr;
    }
    m_surface_scene_tree = nullptr;
    m_border_scene_buffer = nullptr;
    m_blur_node = nullptr;
    m_shadow_node = nullptr;
}

void View::setup_foreign_toplevel() {
    if (m_is_override_redirect) return;

    if (m_server->get_foreign_toplevel_manager()) {
        m_foreign_toplevel = wlr_foreign_toplevel_handle_v1_create(m_server->get_foreign_toplevel_manager());
        if (m_foreign_toplevel) {
            std::string title = get_title();
            std::string app_id = get_app_id();
            if (!title.empty()) wlr_foreign_toplevel_handle_v1_set_title(m_foreign_toplevel, title.c_str());
            if (!app_id.empty()) wlr_foreign_toplevel_handle_v1_set_app_id(m_foreign_toplevel, app_id.c_str());

            if (m_server->get_output_manager()) {
                auto* out = m_server->get_output_manager()->get_primary_output();
                if (out && out->get_wlr_output()) {
                    wlr_foreign_toplevel_handle_v1_output_enter(m_foreign_toplevel, out->get_wlr_output());
                }
            }

            m_foreign_request_activate_listener.notify = handle_foreign_request_activate;
            wl_signal_add(&m_foreign_toplevel->events.request_activate, &m_foreign_request_activate_listener);

            m_foreign_request_close_listener.notify = handle_foreign_request_close;
            wl_signal_add(&m_foreign_toplevel->events.request_close, &m_foreign_request_close_listener);
        }
    }
}

std::string View::get_title() const {
    if (m_type == ViewType::Xdg) {
        if (m_xdg_toplevel && m_xdg_toplevel->title) return m_xdg_toplevel->title;
    } else {
        if (m_xwayland_surface && m_xwayland_surface->title) return m_xwayland_surface->title;
    }
    return "";
}

std::string View::get_app_id() const {
    if (m_type == ViewType::Xdg) {
        if (m_xdg_toplevel && m_xdg_toplevel->app_id) return m_xdg_toplevel->app_id;
    } else {
        if (m_xwayland_surface) {
            if (m_xwayland_surface->_class) return m_xwayland_surface->_class;
            if (m_xwayland_surface->instance) return m_xwayland_surface->instance;
        }
    }
    return "";
}

void View::set_workspace(Workspace* ws) {
    if (m_workspace == ws) return;
    m_workspace = ws;
    if (m_workspace && m_scene_tree) {
        struct wlr_scene_tree* target_parent = (m_is_floating || m_is_dialog)
            ? m_workspace->get_floating_tree()
            : m_workspace->get_tiled_tree();
        if (target_parent) {
            wlr_scene_node_reparent(&m_scene_tree->node, target_parent);
        } else {
            wlr_scene_node_reparent(&m_scene_tree->node, m_workspace->get_scene_tree());
        }
    }
}

void View::set_floating(bool floating) {
    m_is_floating = floating;
    if (m_workspace && m_scene_tree) {
        struct wlr_scene_tree* target_parent = (m_is_floating || m_is_dialog)
            ? m_workspace->get_floating_tree()
            : m_workspace->get_tiled_tree();
        if (target_parent) {
            wlr_scene_node_reparent(&m_scene_tree->node, target_parent);
            if (m_is_floating) {
                wlr_scene_node_raise_to_top(&m_scene_tree->node);
            }
        }
    }
}

bool View::is_focused() const {
    return m_server && m_server->get_focused_view() == this;
}

float View::get_output_scale() const {
    if (m_server && m_server->get_output_manager()) {
        auto* out = m_server->get_output_manager()->get_primary_output();
        if (out && out->get_wlr_output() && out->get_wlr_output()->scale > 0.0f) {
            return out->get_wlr_output()->scale;
        }
    }
    return 1.0f;
}

Output* View::get_output() const {
    if (!m_server || !m_server->get_output_manager()) return nullptr;
    auto* layout = m_server->get_output_manager()->get_layout();
    if (!layout) return m_server->get_output_manager()->get_primary_output();

    double cx = m_x + (m_width > 0 ? m_width : 1) / 2.0;
    double cy = m_y + (m_height > 0 ? m_height : 1) / 2.0;
    struct wlr_output* wlr_out = wlr_output_layout_output_at(layout, cx, cy);
    if (wlr_out) {
        return m_server->get_output_manager()->find_output(wlr_out);
    }
    return m_server->get_output_manager()->get_primary_output();
}

struct wlr_surface* View::get_wlr_surface() const {
    if (m_type == ViewType::Xdg && m_xdg_toplevel && m_xdg_toplevel->base) {
        return m_xdg_toplevel->base->surface;
    }
    if (m_type == ViewType::XWayland && m_xwayland_surface) {
        return m_xwayland_surface->surface;
    }
    return nullptr;
}

bool View::tearing_allowed() const {
    if (!Config::get().is_tearing_allowed()) {
        return false;
    }

    if (!m_is_fullscreen || m_is_overview_scaled || has_child_dialogs()) {
        return false;
    }

    std::string app_id = get_app_id();
    std::string title = get_title();
    if (Config::get().should_tear(app_id, title)) {
        return true;
    }

    struct wlr_surface* surface = get_wlr_surface();
    if (surface && m_server && m_server->get_tearing_manager()) {
        enum wp_tearing_control_v1_presentation_hint hint =
            wlr_tearing_control_manager_v1_surface_hint_from_surface(m_server->get_tearing_manager(), surface);
        if (hint == WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC) {
            return true;
        }
    }

    return false;
}

void View::set_geometry(int x, int y, int width, int height) {
    if (m_x == x && m_y == y && m_width == width && m_height == height && m_mapped) {
        return;
    }

    bool size_changed = (m_width != width || m_height != height);
    bool pos_changed = (m_x != x || m_y != y);
    m_x = x;
    m_y = y;
    m_width = width;
    m_height = height;
    m_target_x = x;
    m_target_y = y;
    m_target_width = width;
    m_target_height = height;
    m_current_anim_x = x;
    m_current_anim_y = y;
    m_current_anim_width = width;
    m_current_anim_height = height;
    m_is_animating_geometry = false;

    if (m_scene_tree) {
        wlr_scene_node_set_position(&m_scene_tree->node, x, y);
    }

    int bw = Config::get().get_window_border_width();
    int client_w = std::max(1, width - 2 * bw);
    int client_h = std::max(1, height - 2 * bw);

    if (m_type == ViewType::Xdg) {
        if (m_surface_scene_tree) {
            wlr_scene_node_set_position(&m_surface_scene_tree->node, bw, bw);
        }
        if (m_xdg_toplevel) {
            if (!is_dialog() && !is_floating()) {
                wlr_xdg_toplevel_set_maximized(m_xdg_toplevel, false);
                wlr_xdg_toplevel_set_tiled(m_xdg_toplevel, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
            } else {
                wlr_xdg_toplevel_set_tiled(m_xdg_toplevel, 0);
            }
            if (m_xdg_toplevel->scheduled.width != client_w || m_xdg_toplevel->scheduled.height != client_h) {
                wlr_xdg_toplevel_set_size(m_xdg_toplevel, client_w, client_h);
            }
        }
    } else {
        if (m_surface_scene_tree) {
            wlr_scene_node_set_position(&m_surface_scene_tree->node, bw, bw);
        }
        if (m_xwayland_surface && (size_changed || pos_changed)) {
            int conf_x = x + bw;
            int conf_y = y + bw;
            int conf_w = client_w;
            int conf_h = client_h;
            if (Config::get().is_xwayland_force_zero_scaling_enabled()) {
                float scale = get_output_scale();
                if (scale > 1.001f) {
                    conf_w = std::max(1, (int)std::round(client_w * scale));
                    conf_h = std::max(1, (int)std::round(client_h * scale));
                    conf_x = (int)std::round(conf_x * scale);
                    conf_y = (int)std::round(conf_y * scale);
                }
            }
            wlr_xwayland_surface_configure(m_xwayland_surface, conf_x, conf_y, conf_w, conf_h);
        }
    }

    if (m_surface_scene_tree && !is_floating() && !is_dialog() && !m_is_fullscreen) {
        struct BufferFitData { int w; int h; } bfd = { client_w, client_h };
        wlr_scene_node_for_each_buffer(&m_surface_scene_tree->node, [](struct wlr_scene_buffer* buf, int sx, int sy, void* data) {
            auto* d = static_cast<BufferFitData*>(data);
            if (buf && buf->buffer) {
                if (buf->buffer->width != d->w || buf->buffer->height != d->h) {
                    wlr_scene_buffer_set_dest_size(buf, d->w, d->h);
                } else {
                    wlr_scene_buffer_set_dest_size(buf, 0, 0);
                }
            }
        }, &bfd);
    }

    update_frame();

    for (auto& popup : m_popups) {
        if (popup) popup->update_position();
    }

    if (m_workspace && !m_child_dialogs.empty()) {
        auto* out_mgr = m_server->get_output_manager();
        if (out_mgr) {
            m_workspace->arrange_floating_views(out_mgr->get_primary_usable_geometry());
        }
    }
}

void View::set_fullscreen(bool fullscreen) {
    if (m_is_fullscreen == fullscreen) return;

    auto* output_mgr = m_server->get_output_manager();
    if (!output_mgr) return;
    auto* primary_out = output_mgr->get_primary_output();
    if (!primary_out || !primary_out->get_wlr_output()) return;

    if (fullscreen) {
        // --- Enter fullscreen ---
        m_saved_geometry.x = m_x;
        m_saved_geometry.y = m_y;
        m_saved_geometry.w = m_width;
        m_saved_geometry.h = m_height;
        m_saved_geometry.workspace_tree = m_workspace ?
            ((m_is_floating || m_is_dialog) ? m_workspace->get_floating_tree() : m_workspace->get_tiled_tree())
            : nullptr;

        // Reparent to the overlay scene tree so it sits above everything
        if (m_scene_tree) {
            wlr_scene_node_reparent(&m_scene_tree->node, m_server->get_layer_overlay_tree());
            wlr_scene_node_raise_to_top(&m_scene_tree->node);
        }

        // Cover the entire output (no border offset)
        struct wlr_box output_box = {};
        wlr_output_effective_resolution(primary_out->get_wlr_output(), &output_box.width, &output_box.height);
        // Account for output layout position
        struct wlr_box layout_box = output_mgr->get_primary_geometry();
        output_box.x = layout_box.x;
        output_box.y = layout_box.y;

        m_x = output_box.x;
        m_y = output_box.y;
        m_width = output_box.width;
        m_height = output_box.height;

        if (m_scene_tree) {
            wlr_scene_node_set_position(&m_scene_tree->node, m_x, m_y);
        }

        if (m_type == ViewType::Xdg && m_xdg_toplevel) {
            if (m_surface_scene_tree) {
                wlr_scene_node_set_position(&m_surface_scene_tree->node, 0, 0);
            }
            wlr_xdg_toplevel_set_fullscreen(m_xdg_toplevel, true);
            wlr_xdg_toplevel_set_size(m_xdg_toplevel, m_width, m_height);
        } else if (m_type == ViewType::XWayland && m_xwayland_surface) {
            if (m_surface_scene_tree) {
                wlr_scene_node_set_position(&m_surface_scene_tree->node, 0, 0);
            }
            wlr_xwayland_surface_set_fullscreen(m_xwayland_surface, true);
            int conf_x = m_x;
            int conf_y = m_y;
            int conf_w = m_width;
            int conf_h = m_height;
            if (Config::get().is_xwayland_force_zero_scaling_enabled()) {
                float scale = get_output_scale();
                if (scale > 1.001f) {
                    conf_w = std::max(1, (int)std::round(m_width * scale));
                    conf_h = std::max(1, (int)std::round(m_height * scale));
                    conf_x = (int)std::round(m_x * scale);
                    conf_y = (int)std::round(m_y * scale);
                }
            }
            wlr_xwayland_surface_configure(m_xwayland_surface, conf_x, conf_y, conf_w, conf_h);
        }

        // Disable border and shadow in fullscreen
        if (m_border_scene_buffer) {
            wlr_scene_node_set_enabled(&m_border_scene_buffer->node, false);
        }
        if (m_shadow_node) {
            wlr_scene_node_set_enabled(&m_shadow_node->node, false);
        }

        m_is_fullscreen = true;
        update_corner_radius();
        focus();

    } else {
        // --- Exit fullscreen ---
        m_is_fullscreen = false;

        // Reparent back to workspace scene tree
        struct wlr_scene_tree* parent_tree = m_saved_geometry.workspace_tree;
        if (!parent_tree && m_workspace) {
            parent_tree = (m_is_floating || m_is_dialog)
                ? m_workspace->get_floating_tree()
                : m_workspace->get_tiled_tree();
        }
        if (parent_tree && m_scene_tree) {
            wlr_scene_node_reparent(&m_scene_tree->node, parent_tree);
        }

        if (m_type == ViewType::Xdg && m_xdg_toplevel) {
            if (m_surface_scene_tree) {
                int bw = Config::get().get_window_border_width();
                wlr_scene_node_set_position(&m_surface_scene_tree->node, bw, bw);
            }
            wlr_xdg_toplevel_set_fullscreen(m_xdg_toplevel, false);
        } else if (m_type == ViewType::XWayland && m_xwayland_surface) {
            if (m_surface_scene_tree) {
                int bw = Config::get().get_window_border_width();
                wlr_scene_node_set_position(&m_surface_scene_tree->node, bw, bw);
            }
            wlr_xwayland_surface_set_fullscreen(m_xwayland_surface, false);
        }

        // Re-enable border and shadow
        if (m_border_scene_buffer) {
            wlr_scene_node_set_enabled(&m_border_scene_buffer->node, true);
        }
        if (m_shadow_node) {
            wlr_scene_node_set_enabled(&m_shadow_node->node, true);
        }

        update_corner_radius();

        if (m_is_floating) {
            set_geometry(m_saved_geometry.x, m_saved_geometry.y, m_saved_geometry.w, m_saved_geometry.h);
        } else {
            // Trigger workspace relayout to reassign geometry
            m_server->get_workspace_manager()->recalculate_layout();
        }
    }
}


void View::update_frame() {
    if (m_is_animating_close || m_is_animating_open) return;
    update_border();
    update_corner_radius();
    update_opacity();
    update_blur();
    update_shadow();
}

void View::update_blur() {
    if (!Config::get().is_blur_enabled()) {
        if (m_blur_node) {
            wlr_scene_node_set_enabled(&m_blur_node->node, false);
        }
        return;
    }

    if (!m_mapped || m_width <= 0 || m_height <= 0 || m_is_fullscreen) {
        if (m_blur_node) {
            wlr_scene_node_set_enabled(&m_blur_node->node, false);
        }
        return;
    }

    if (!m_blur_node && m_scene_tree) {
        m_blur_node = wlr_scene_blur_create(m_scene_tree, m_width, m_height);
        if (m_blur_node) {
            wlr_scene_node_lower_to_bottom(&m_blur_node->node);
        }
    }

    if (m_blur_node) {
        wlr_scene_blur_set_size(m_blur_node, m_width, m_height);
        int r = Config::get().get_window_border_radius();
        wlr_scene_blur_set_corner_radius(m_blur_node, r);
        wlr_scene_node_set_enabled(&m_blur_node->node, true);
    }
}

using CairoPatternPtr = std::unique_ptr<cairo_pattern_t, decltype(&cairo_pattern_destroy)>;

static void render_border_stroke(cairo_t* cr, const BorderPaint& paint, int bw, int width, int height) {
    cairo_set_line_width(cr, bw);
    if (paint.is_gradient()) {
        CairoPatternPtr pat(paint.create_cairo_pattern(width, height), &cairo_pattern_destroy);
        if (pat) {
            cairo_set_source(cr, pat.get());
            cairo_stroke_preserve(cr);
            return;
        }
    }
    float r = 0.0f, g = 0.8f, b = 1.0f, a = 1.0f;
    paint.get_single_color(r, g, b, a);
    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_stroke_preserve(cr);
}

static void render_border_shading(cairo_t* cr, int height, float strength) {
    CairoPatternPtr shade_pat(cairo_pattern_create_linear(0, 0, 0, height), &cairo_pattern_destroy);
    if (shade_pat) {
        cairo_pattern_add_color_stop_rgba(shade_pat.get(), 0.0, 1.0, 1.0, 1.0, 0.5f * strength);
        cairo_pattern_add_color_stop_rgba(shade_pat.get(), 0.3, 1.0, 1.0, 1.0, 0.1f * strength);
        cairo_pattern_add_color_stop_rgba(shade_pat.get(), 0.7, 0.0, 0.0, 0.0, 0.1f * strength);
        cairo_pattern_add_color_stop_rgba(shade_pat.get(), 1.0, 0.0, 0.0, 0.0, 0.6f * strength);
        cairo_set_source(cr, shade_pat.get());
        cairo_stroke(cr);
    } else {
        cairo_new_path(cr);
    }
}

void View::sync_shadow_geometry(int w, int h, int r, float sigma, int ox, int oy) {
    if (!m_shadow_node) return;
    int pad = static_cast<int>(std::ceil(std::max(4.0f, sigma * 2.5f)));

    wlr_scene_node_set_position(&m_shadow_node->node, -pad + ox, -pad + oy);
    wlr_scene_shadow_set_size(m_shadow_node, w + 2 * pad, h + 2 * pad);
    wlr_scene_shadow_set_blur_sigma(m_shadow_node, sigma);
    wlr_scene_shadow_set_corner_radius(m_shadow_node, r);

    struct clipped_region cr = clipped_region_get_default();
    cr.area.x = pad - ox;
    cr.area.y = pad - oy;
    cr.area.width = w;
    cr.area.height = h;
    cr.corners = corner_radii_all(r);
    wlr_scene_shadow_set_clipped_region(m_shadow_node, cr);
}

void View::update_shadow() {
    if (!Config::get().is_shadow_enabled() || !m_mapped || m_width <= 0 || m_height <= 0 ||
        m_is_fullscreen || m_is_override_redirect) {
        if (m_shadow_node) {
            wlr_scene_node_set_enabled(&m_shadow_node->node, false);
        }
        return;
    }

    float sigma = Config::get().get_shadow_blur_sigma();
    if (sigma <= 0.0f) {
        if (m_shadow_node) {
            wlr_scene_node_set_enabled(&m_shadow_node->node, false);
        }
        return;
    }

    if (!m_shadow_node && m_scene_tree) {
        float initial_color[4] = {0.0f, 0.0f, 0.0f, 0.4f};
        m_shadow_node = wlr_scene_shadow_create(m_scene_tree, m_width, m_height, 0, sigma, initial_color);
        if (m_shadow_node) {
            wlr_scene_node_lower_to_bottom(&m_shadow_node->node);
        }
    }

    if (!m_shadow_node) return;

    int ox = Config::get().get_shadow_offset_x();
    int oy = Config::get().get_shadow_offset_y();
    int radius = Config::get().get_window_border_radius();

    sync_shadow_geometry(m_width, m_height, radius, sigma, ox, oy);

    const std::string& col_str = is_focused()
        ? Config::get().get_shadow_color_active()
        : Config::get().get_shadow_color_inactive();
    float sr = 0.0f, sg = 0.0f, sb = 0.0f, sa = 0.4f;
    BorderPaint::parse_hex_color(col_str, sr, sg, sb, sa);

    float opacity = is_focused()
        ? Config::get().get_window_opacity_active()
        : Config::get().get_window_opacity_inactive();
    float final_col[4] = { sr, sg, sb, sa * opacity };
    wlr_scene_shadow_set_color(m_shadow_node, final_col);

    wlr_scene_node_set_enabled(&m_shadow_node->node, true);
}

void View::update_border() {
    if (m_is_override_redirect || !m_mapped || m_width <= 0 || m_height <= 0) {
        if (m_border_scene_buffer) {
            wlr_scene_node_set_enabled(&m_border_scene_buffer->node, false);
        }
        return;
    }

    int bw = Config::get().get_window_border_width();
    int radius = Config::get().get_window_border_radius();

    if (bw <= 0 && radius <= 0) {
        if (m_border_scene_buffer) {
            wlr_scene_node_set_enabled(&m_border_scene_buffer->node, false);
        }
        return;
    }

    wlr_scene_node_set_enabled(&m_border_scene_buffer->node, true);

    if (!m_border_buffer) {
        m_border_buffer = std::make_unique<CairoBuffer>(m_width, m_height);
    } else {
        m_border_buffer->resize(m_width, m_height);
    }

    cairo_t* cr = m_border_buffer->get_cairo();
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    if (bw > 0) {
        double half_bw = bw / 2.0;
        double draw_x = half_bw;
        double draw_y = half_bw;
        double draw_w = std::max(1.0, (double)m_width - bw);
        double draw_h = std::max(1.0, (double)m_height - bw);
        double draw_r = std::max(0.0, (double)radius - half_bw);

        draw_rounded_rectangle(cr, draw_x, draw_y, draw_w, draw_h, draw_r);

        const BorderPaint& paint = is_focused()
            ? Config::get().get_window_border_paint_active()
            : Config::get().get_window_border_paint_inactive();

        render_border_stroke(cr, paint, bw, m_width, m_height);

        if (Config::get().is_window_border_shading_enabled()) {
            render_border_shading(cr, m_height, Config::get().get_window_border_shading_strength());
        } else {
            cairo_new_path(cr);
        }
    }

    wlr_scene_buffer_set_buffer(m_border_scene_buffer, m_border_buffer->get_wlr_buffer());
}

void View::update_opacity() {
    if (!m_mapped || !m_surface_scene_tree || m_is_animating_close || m_is_animating_open) return;

    float opacity = is_focused()
        ? Config::get().get_window_opacity_active()
        : Config::get().get_window_opacity_inactive();

    opacity = Config::get().get_rule_opacity(get_app_id(), get_title(), opacity);
    opacity = std::clamp(opacity, 0.0f, 1.0f);

    wlr_scene_node_for_each_buffer(&m_surface_scene_tree->node, [](struct wlr_scene_buffer* buffer, int sx, int sy, void* data) {
        float val = *static_cast<float*>(data);
        wlr_scene_buffer_set_opacity(buffer, val);
    }, &opacity);
}

void View::update_corner_radius() {
    if (!m_mapped || !m_surface_scene_tree) return;

    int bw = Config::get().get_window_border_width();
    int client_w = std::max(1, m_width - 2 * bw);
    int client_h = std::max(1, m_height - 2 * bw);

    int geom_x = 0, geom_y = 0;
    int geom_w = client_w, geom_h = client_h;
    if (m_type == ViewType::Xdg && m_xdg_toplevel && m_xdg_toplevel->base) {
        auto* xdg_surf = m_xdg_toplevel->base;
        geom_x = xdg_surf->current.geometry.x;
        geom_y = xdg_surf->current.geometry.y;
        if ((is_floating() || is_dialog()) && xdg_surf->current.geometry.width > 0 && xdg_surf->current.geometry.height > 0) {
            geom_w = xdg_surf->current.geometry.width;
            geom_h = xdg_surf->current.geometry.height;
        }
    }

    struct wlr_box clip = {
        .x = geom_x,
        .y = geom_y,
        .width = geom_w,
        .height = geom_h,
    };

    wlr_scene_subsurface_tree_set_clip(&m_surface_scene_tree->node, &clip);

    if (!m_is_fullscreen && !m_is_overview_scaled && !m_is_animating_geometry) {
        wlr_scene_node_set_position(&m_surface_scene_tree->node, bw, bw);
    } else if (m_is_fullscreen) {
        wlr_scene_node_set_position(&m_surface_scene_tree->node, 0, 0);
    }

    int radius = 0;
    if (!m_is_fullscreen && !m_is_override_redirect) {
        int r = Config::get().get_window_border_radius();
        radius = (bw > 0) ? std::max(0, r - bw) : std::max(0, r);
    }

    wlr_scene_node_for_each_buffer(&m_surface_scene_tree->node, [](struct wlr_scene_buffer* buffer, int sx, int sy, void* user_data) {
        int r = *static_cast<int*>(user_data);
        wlr_scene_buffer_set_corner_radius(buffer, r);
    }, &radius);

    if (m_blur_node) {
        wlr_scene_blur_set_corner_radius(m_blur_node, radius);
    }
}

void View::apply_animation_transform(double scale, float opacity) {
    if (!m_mapped || !m_scene_tree || m_width <= 0 || m_height <= 0) {
        return;
    }

    int bw = Config::get().get_window_border_width();
    int base_w = (m_current_anim_width > 0) ? m_current_anim_width : m_width;
    int base_h = (m_current_anim_height > 0) ? m_current_anim_height : m_height;
    int base_x = (m_current_anim_width > 0) ? m_current_anim_x : m_x;
    int base_y = (m_current_anim_height > 0) ? m_current_anim_y : m_y;

    int geom_x = 0, geom_y = 0;
    int geom_w = std::max(1, base_w - 2 * bw);
    int geom_h = std::max(1, base_h - 2 * bw);
    if (m_type == ViewType::Xdg && m_xdg_toplevel && m_xdg_toplevel->base) {
        auto* xdg_surf = m_xdg_toplevel->base;
        geom_x = xdg_surf->current.geometry.x;
        geom_y = xdg_surf->current.geometry.y;
        if ((is_floating() || is_dialog()) && xdg_surf->current.geometry.width > 0 && xdg_surf->current.geometry.height > 0) {
            geom_w = xdg_surf->current.geometry.width;
            geom_h = xdg_surf->current.geometry.height;
        }
    }

    float safe_opacity = std::clamp(opacity, 0.0f, 1.0f);

    if (scale >= 0.999 && !m_is_animating_geometry && !m_is_animating_close && !m_is_animating_open) {
        wlr_scene_node_set_position(&m_scene_tree->node, m_x, m_y);
        if (m_surface_scene_tree) {
            wlr_scene_node_set_position(&m_surface_scene_tree->node, bw, bw);
            wlr_scene_node_for_each_buffer(&m_surface_scene_tree->node, [](struct wlr_scene_buffer* buf, int sx, int sy, void* data) {
                float op = *static_cast<float*>(data);
                wlr_scene_buffer_set_dest_size(buf, 0, 0);
                wlr_scene_buffer_set_opacity(buf, op);
            }, &safe_opacity);

            struct wlr_box clip = {
                .x = geom_x,
                .y = geom_y,
                .width = geom_w,
                .height = geom_h,
            };
            wlr_scene_subsurface_tree_set_clip(&m_surface_scene_tree->node, &clip);
        }
        if (m_border_scene_buffer) {
            wlr_scene_buffer_set_dest_size(m_border_scene_buffer, 0, 0);
            wlr_scene_buffer_set_opacity(m_border_scene_buffer, safe_opacity);
        }
        if (m_blur_node) {
            if (safe_opacity < 0.05f || !Config::get().is_blur_enabled()) {
                wlr_scene_node_set_enabled(&m_blur_node->node, false);
            } else {
                wlr_scene_node_set_enabled(&m_blur_node->node, true);
                wlr_scene_blur_set_size(m_blur_node, m_width, m_height);
            }
        }
        if (m_shadow_node) {
            if (safe_opacity < 0.05f || !Config::get().is_shadow_enabled() || m_is_fullscreen || m_is_override_redirect) {
                wlr_scene_node_set_enabled(&m_shadow_node->node, false);
            } else {
                wlr_scene_node_set_enabled(&m_shadow_node->node, true);
            }
        }
        update_frame();
        return;
    }

    // Center-anchored coordinate transformation relative to current base geometry
    double cur_w = std::max(1.0, static_cast<double>(base_w) * scale);
    double cur_h = std::max(1.0, static_cast<double>(base_h) * scale);
    int cur_x = static_cast<int>(std::round(static_cast<double>(base_x) + (static_cast<double>(base_w) - cur_w) / 2.0));
    int cur_y = static_cast<int>(std::round(static_cast<double>(base_y) + (static_cast<double>(base_h) - cur_h) / 2.0));

    wlr_scene_node_set_position(&m_scene_tree->node, cur_x, cur_y);

    int scaled_bw = static_cast<int>(std::round(static_cast<double>(bw) * scale));

    if (m_surface_scene_tree) {
        wlr_scene_node_set_position(&m_surface_scene_tree->node, scaled_bw, scaled_bw);

        int r = (!m_is_fullscreen && !m_is_override_redirect) ? Config::get().get_window_border_radius() : 0;
        int base_r = (bw > 0) ? std::max(0, r - bw) : std::max(0, r);
        int scaled_r = static_cast<int>(std::round(static_cast<double>(base_r) * scale));

        struct BufferTransformData {
            double scale;
            float opacity;
            int corner_radius;
        } data = { scale, safe_opacity, scaled_r };

        wlr_scene_node_for_each_buffer(&m_surface_scene_tree->node, [](struct wlr_scene_buffer* buf, int sx, int sy, void* user_data) {
            auto* d = static_cast<BufferTransformData*>(user_data);
            if (buf->buffer) {
                int sw = std::max(1, static_cast<int>(std::round(static_cast<double>(buf->buffer->width) * d->scale)));
                int sh = std::max(1, static_cast<int>(std::round(static_cast<double>(buf->buffer->height) * d->scale)));
                wlr_scene_buffer_set_dest_size(buf, sw, sh);
            }
            wlr_scene_buffer_set_opacity(buf, d->opacity);
            wlr_scene_buffer_set_corner_radius(buf, d->corner_radius);
        }, &data);
    }

    if (m_border_scene_buffer) {
        int border_w = std::max(1, static_cast<int>(std::round(cur_w)));
        int border_h = std::max(1, static_cast<int>(std::round(cur_h)));
        wlr_scene_buffer_set_dest_size(m_border_scene_buffer, border_w, border_h);
        wlr_scene_buffer_set_opacity(m_border_scene_buffer, safe_opacity);
    }

    if (m_blur_node) {
        if (safe_opacity < 0.01f || !Config::get().is_blur_enabled()) {
            wlr_scene_node_set_enabled(&m_blur_node->node, false);
        } else {
            wlr_scene_node_set_enabled(&m_blur_node->node, true);
            int blur_w = std::max(1, static_cast<int>(std::round(cur_w)));
            int blur_h = std::max(1, static_cast<int>(std::round(cur_h)));
            wlr_scene_blur_set_size(m_blur_node, blur_w, blur_h);
            wlr_scene_blur_set_alpha(m_blur_node, safe_opacity);
            int r = (!m_is_fullscreen && !m_is_override_redirect) ? Config::get().get_window_border_radius() : 0;
            int base_r = (bw > 0) ? std::max(0, r - bw) : std::max(0, r);
            int scaled_r = static_cast<int>(std::round(static_cast<double>(base_r) * scale));
            wlr_scene_blur_set_corner_radius(m_blur_node, scaled_r);
        }
    }

    if (m_shadow_node) {
        if (safe_opacity < 0.01f || !Config::get().is_shadow_enabled() || m_is_fullscreen || m_is_override_redirect) {
            wlr_scene_node_set_enabled(&m_shadow_node->node, false);
        } else {
            wlr_scene_node_set_enabled(&m_shadow_node->node, true);
            float sigma = Config::get().get_shadow_blur_sigma() * static_cast<float>(scale);
            int ox = static_cast<int>(std::round(static_cast<double>(Config::get().get_shadow_offset_x()) * scale));
            int oy = static_cast<int>(std::round(static_cast<double>(Config::get().get_shadow_offset_y()) * scale));
            int r = (!m_is_fullscreen && !m_is_override_redirect) ? Config::get().get_window_border_radius() : 0;
            int scaled_r = static_cast<int>(std::round(static_cast<double>(r) * scale));
            int anim_w = std::max(1, static_cast<int>(std::round(cur_w)));
            int anim_h = std::max(1, static_cast<int>(std::round(cur_h)));

            sync_shadow_geometry(anim_w, anim_h, scaled_r, sigma, ox, oy);

            const std::string& col_str = is_focused()
                ? Config::get().get_shadow_color_active()
                : Config::get().get_shadow_color_inactive();
            float sr = 0.0f, sg = 0.0f, sb = 0.0f, sa = 0.4f;
            BorderPaint::parse_hex_color(col_str, sr, sg, sb, sa);
            float final_col[4] = { sr, sg, sb, sa * safe_opacity };
            wlr_scene_shadow_set_color(m_shadow_node, final_col);
        }
    }
}

void View::notify_geometry_target(int x, int y, int width, int height) {
    m_is_animating_geometry = true;
    m_target_x = x;
    m_target_y = y;
    m_target_width = width;
    m_target_height = height;

    int bw = Config::get().get_window_border_width();
    int client_w = std::max(1, width - 2 * bw);
    int client_h = std::max(1, height - 2 * bw);

    if (m_type == ViewType::Xdg && m_xdg_toplevel) {
        if (!is_dialog() && !is_floating()) {
            wlr_xdg_toplevel_set_tiled(m_xdg_toplevel, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
        }
        if (m_xdg_toplevel->resource && wl_resource_get_version(m_xdg_toplevel->resource) >= 4) {
            wlr_xdg_toplevel_set_bounds(m_xdg_toplevel, client_w, client_h);
        }
        wlr_xdg_toplevel_set_size(m_xdg_toplevel, client_w, client_h);
    } else if (m_type == ViewType::XWayland && m_xwayland_surface) {
        int conf_x = x + bw;
        int conf_y = y + bw;
        int conf_w = client_w;
        int conf_h = client_h;
        if (Config::get().is_xwayland_force_zero_scaling_enabled()) {
            float scale = get_output_scale();
            if (scale > 1.001f) {
                conf_w = std::max(1, (int)std::round(client_w * scale));
                conf_h = std::max(1, (int)std::round(client_h * scale));
                conf_x = (int)std::round(conf_x * scale);
                conf_y = (int)std::round(conf_y * scale);
            }
        }
        wlr_xwayland_surface_configure(m_xwayland_surface, conf_x, conf_y, conf_w, conf_h);
    }
}

void View::apply_geometry_animation(int cur_x, int cur_y, int cur_w, int cur_h) {
    if (!m_mapped || !m_scene_tree || cur_w <= 0 || cur_h <= 0) {
        return;
    }

    m_current_anim_x = cur_x;
    m_current_anim_y = cur_y;
    m_current_anim_width = cur_w;
    m_current_anim_height = cur_h;

    wlr_scene_node_set_position(&m_scene_tree->node, cur_x, cur_y);

    int bw = Config::get().get_window_border_width();
    int client_w = std::max(1, cur_w - 2 * bw);
    int client_h = std::max(1, cur_h - 2 * bw);

    int geom_x = 0, geom_y = 0;
    int geom_w = client_w, geom_h = client_h;
    if (m_type == ViewType::Xdg && m_xdg_toplevel && m_xdg_toplevel->base) {
        auto* xdg_surf = m_xdg_toplevel->base;
        geom_x = xdg_surf->current.geometry.x;
        geom_y = xdg_surf->current.geometry.y;
        if ((is_floating() || is_dialog()) && xdg_surf->current.geometry.width > 0 && xdg_surf->current.geometry.height > 0) {
            geom_w = xdg_surf->current.geometry.width;
            geom_h = xdg_surf->current.geometry.height;
        }
    }

    if (m_surface_scene_tree) {
        wlr_scene_node_set_position(&m_surface_scene_tree->node, bw, bw);

        struct BufferScaleData {
            int target_w;
            int target_h;
        } data = { client_w, client_h };

        wlr_scene_node_for_each_buffer(&m_surface_scene_tree->node, [](struct wlr_scene_buffer* buf, int sx, int sy, void* user_data) {
            auto* d = static_cast<BufferScaleData*>(user_data);
            if (buf->buffer) {
                wlr_scene_buffer_set_dest_size(buf, d->target_w, d->target_h);
            }
        }, &data);

        struct wlr_box clip = {
            .x = geom_x,
            .y = geom_y,
            .width = geom_w,
            .height = geom_h,
        };
        wlr_scene_subsurface_tree_set_clip(&m_surface_scene_tree->node, &clip);
    }

    if (m_border_scene_buffer) {
        wlr_scene_buffer_set_dest_size(m_border_scene_buffer, cur_w, cur_h);
    }

    if (m_blur_node) {
        wlr_scene_blur_set_size(m_blur_node, cur_w, cur_h);
    }

    if (m_shadow_node && Config::get().is_shadow_enabled() && !m_is_fullscreen && !m_is_override_redirect) {
        float sigma = Config::get().get_shadow_blur_sigma();
        int ox = Config::get().get_shadow_offset_x();
        int oy = Config::get().get_shadow_offset_y();
        int r = Config::get().get_window_border_radius();
        sync_shadow_geometry(cur_w, cur_h, r, sigma, ox, oy);
    }
}

void View::finish_geometry_animation(int target_x, int target_y, int target_w, int target_h) {
    m_is_animating_geometry = false;
    m_x = target_x;
    m_y = target_y;
    m_width = target_w;
    m_height = target_h;
    m_target_x = target_x;
    m_target_y = target_y;
    m_target_width = target_w;
    m_target_height = target_h;
    m_current_anim_x = target_x;
    m_current_anim_y = target_y;
    m_current_anim_width = target_w;
    m_current_anim_height = target_h;

    if (m_scene_tree) {
        wlr_scene_node_set_position(&m_scene_tree->node, target_x, target_y);
    }

    int bw = Config::get().get_window_border_width();
    int client_w = std::max(1, target_w - 2 * bw);
    int client_h = std::max(1, target_h - 2 * bw);

    if (m_surface_scene_tree) {
        wlr_scene_node_set_position(&m_surface_scene_tree->node, bw, bw);

        struct FinishBufferData {
            int target_w;
            int target_h;
        } data = { client_w, client_h };

        wlr_scene_node_for_each_buffer(&m_surface_scene_tree->node, [](struct wlr_scene_buffer* buf, int sx, int sy, void* user_data) {
            auto* d = static_cast<FinishBufferData*>(user_data);
            if (buf->buffer) {
                if (buf->buffer->width == d->target_w && buf->buffer->height == d->target_h) {
                    wlr_scene_buffer_set_dest_size(buf, 0, 0);
                } else {
                    wlr_scene_buffer_set_dest_size(buf, d->target_w, d->target_h);
                }
            }
        }, &data);
    }

    if (m_border_scene_buffer) {
        wlr_scene_buffer_set_dest_size(m_border_scene_buffer, 0, 0);
    }

    update_frame();

    for (auto& popup : m_popups) {
        if (popup) popup->update_position();
    }
}

void View::set_overview_scaled(bool scaled, double scale) {
    if (m_is_overview_scaled == scaled && std::abs(m_overview_scale - scale) < 0.0001) {
        return;
    }

    m_is_overview_scaled = scaled;
    m_overview_scale = scale;

    if (scaled) {
        if (m_border_scene_buffer) {
            wlr_scene_node_set_enabled(&m_border_scene_buffer->node, false);
        }
        if (m_blur_node) {
            wlr_scene_node_set_enabled(&m_blur_node->node, false);
        }
        if (m_shadow_node) {
            wlr_scene_node_set_enabled(&m_shadow_node->node, false);
        }
        reapply_overview_scale();
    } else {
        int bw = Config::get().get_window_border_width();
        if (m_surface_scene_tree) {
            wlr_scene_node_set_position(&m_surface_scene_tree->node, bw, bw);
            wlr_scene_node_for_each_buffer(&m_surface_scene_tree->node, [](struct wlr_scene_buffer* buf, int sx, int sy, void* data) {
                wlr_scene_buffer_set_dest_size(buf, 0, 0);
            }, nullptr);
        }
        update_frame();
    }
}

void View::reapply_overview_scale() {
    if (!m_is_overview_scaled || m_overview_scale <= 0.0 || !m_surface_scene_tree) {
        return;
    }

    double scale = m_overview_scale;
    int bw = Config::get().get_window_border_width();
    int client_w = std::max(1, m_width - 2 * bw);
    int client_h = std::max(1, m_height - 2 * bw);

    if (m_border_scene_buffer) {
        wlr_scene_node_set_enabled(&m_border_scene_buffer->node, false);
    }
    if (m_blur_node) {
        wlr_scene_node_set_enabled(&m_blur_node->node, false);
    }

    int geom_x = 0, geom_y = 0;
    int geom_w = client_w, geom_h = client_h;

    if (m_type == ViewType::Xdg && m_xdg_toplevel && m_xdg_toplevel->base) {
        auto* xdg_surf = m_xdg_toplevel->base;
        geom_x = xdg_surf->current.geometry.x;
        geom_y = xdg_surf->current.geometry.y;
        if (xdg_surf->current.geometry.width > 0 && xdg_surf->current.geometry.height > 0) {
            geom_w = xdg_surf->current.geometry.width;
            geom_h = xdg_surf->current.geometry.height;
        }
    }

    wlr_scene_node_set_position(&m_surface_scene_tree->node, 0, 0);

    // Clip in root surface unscaled coordinate space to crop CSD drop-shadows
    struct wlr_box clip = {
        .x = geom_x,
        .y = geom_y,
        .width = geom_w,
        .height = geom_h,
    };
    wlr_scene_subsurface_tree_set_clip(&m_surface_scene_tree->node, &clip);

    // Scale all buffers in surface scene tree
    wlr_scene_node_for_each_buffer(&m_surface_scene_tree->node, [](struct wlr_scene_buffer* buf, int sx, int sy, void* data) {
        double s = *static_cast<double*>(data);
        if (buf->buffer) {
            int sw = std::max(1, static_cast<int>(std::round(buf->buffer->width * s)));
            int sh = std::max(1, static_cast<int>(std::round(buf->buffer->height * s)));
            wlr_scene_buffer_set_dest_size(buf, sw, sh);
        }
    }, &scale);
}

void View::focus() {
    if (!m_mapped) return;

    if (m_workspace && m_server->get_workspace_manager()) {
        if (m_workspace->get_id() != m_server->get_workspace_manager()->get_active_workspace_id()) {
            m_server->get_workspace_manager()->switch_to_workspace(m_workspace->get_id(), this);
            return;
        }
    }

    if (is_override_redirect()) {
        if (m_scene_tree) {
            wlr_scene_node_raise_to_top(&m_scene_tree->node);
        }
        if (m_xwayland_surface && m_xwayland_surface->surface) {
            struct wlr_seat* seat = m_server->get_input_manager()->get_seat();
            struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
            if (keyboard) {
                wlr_seat_keyboard_notify_enter(seat, m_xwayland_surface->surface,
                    keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
            }
        }
        return;
    }

    View* prev = m_server->get_focused_view();
    if (prev == this) {
        for (auto& popup : m_popups) {
            if (popup && popup->get_scene_tree()) {
                wlr_scene_node_raise_to_top(&popup->get_scene_tree()->node);
            }
        }
        for (const auto& v : m_server->get_views()) {
            if (v && v->is_mapped() && v->is_override_redirect() && v->get_scene_tree()) {
                wlr_scene_node_raise_to_top(&v->get_scene_tree()->node);
            }
        }
        return;
    }

    if (prev) {
        if (prev->m_type == ViewType::Xdg && prev->m_xdg_toplevel) {
            wlr_xdg_toplevel_set_activated(prev->m_xdg_toplevel, false);
        } else if (prev->m_type == ViewType::XWayland && prev->m_xwayland_surface) {
            if (!prev->is_override_redirect()) {
                wlr_xwayland_surface_activate(prev->m_xwayland_surface, false);
            }
        }
        if (prev->m_foreign_toplevel) {
            wlr_foreign_toplevel_handle_v1_set_activated(prev->m_foreign_toplevel, false);
        }
    }

    if (m_scene_tree) {
        wlr_scene_node_raise_to_top(&m_scene_tree->node);
        for (auto* dialog : m_child_dialogs) {
            if (dialog && dialog->is_mapped() && dialog->get_scene_tree()) {
                wlr_scene_node_raise_to_top(&dialog->get_scene_tree()->node);
            }
        }
        for (auto& popup : m_popups) {
            if (popup && popup->get_scene_tree()) {
                wlr_scene_node_raise_to_top(&popup->get_scene_tree()->node);
            }
        }
        for (const auto& v : m_server->get_views()) {
            if (v && v->is_mapped() && v->is_override_redirect() && v->get_scene_tree()) {
                wlr_scene_node_raise_to_top(&v->get_scene_tree()->node);
            }
        }
    }

    struct wlr_surface* target_surface = nullptr;
    if (m_type == ViewType::Xdg && m_xdg_toplevel) {
        wlr_xdg_toplevel_set_activated(m_xdg_toplevel, true);
        target_surface = m_xdg_toplevel->base->surface;
    } else if (m_type == ViewType::XWayland && m_xwayland_surface) {
        if (!is_override_redirect()) {
            wlr_xwayland_surface_activate(m_xwayland_surface, true);
        }
        target_surface = m_xwayland_surface->surface;
    }

    if (m_foreign_toplevel) {
        wlr_foreign_toplevel_handle_v1_set_activated(m_foreign_toplevel, true);
    }
    m_server->set_focused_view(this);

    if (prev && prev != this) {
        prev->update_frame();
    }
    update_frame();

    if (target_surface) {
        struct wlr_seat* seat = m_server->get_input_manager()->get_seat();
        struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
        if (keyboard) {
            wlr_seat_keyboard_notify_enter(seat, target_surface,
                keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
        }

        if (seat && m_server->get_input_manager()) {
            auto* cursor = m_server->get_input_manager()->get_cursor();
            if (cursor) {
                double sx = 0.0, sy = 0.0;
                struct wlr_surface* surf_at_cursor = nullptr;
                View* v = m_server->view_at(cursor->x, cursor->y, &surf_at_cursor, &sx, &sy);
                if (v == this && surf_at_cursor) {
                    if (seat->pointer_state.focused_surface != surf_at_cursor) {
                        wlr_seat_pointer_notify_enter(seat, surf_at_cursor, sx, sy);
                    }
                    wlr_seat_pointer_notify_motion(seat, 0, sx, sy);
                }
            }
        }
    }
}

void View::close() {
    if (m_is_animating_close) return;

    auto do_send_close = [this]() {
        if (m_type == ViewType::Xdg && m_xdg_toplevel) {
            wlr_xdg_toplevel_send_close(m_xdg_toplevel);
        } else if (m_type == ViewType::XWayland && m_xwayland_surface) {
            if (!is_override_redirect()) {
                wlr_xwayland_surface_close(m_xwayland_surface);
            }
        }
    };

    if (!Config::get().is_window_animations_enabled() || !m_mapped || m_is_fullscreen || is_override_redirect()) {
        do_send_close();
        return;
    }

    // Windows on inactive/hidden workspaces must not visually animate onto the active screen
    if (m_workspace && !m_workspace->is_visible()) {
        do_send_close();
        return;
    }

    m_is_animating_close = true;
    if (m_server && m_server->get_animation_manager()) {
        // Reparent closing view to workspaces root tree so it renders above all workspaces,
        // below top/overlay layer surfaces, and is immune to workspace destruction/pruning
        if (m_scene_tree && m_server->get_workspaces_tree()) {
            wlr_scene_node_reparent(&m_scene_tree->node, m_server->get_workspaces_tree());
            wlr_scene_node_raise_to_top(&m_scene_tree->node);
        }
        if (m_workspace && !is_floating() && !is_dialog()) {
            m_server->get_workspace_manager()->remove_view(this);
        }
        m_server->get_animation_manager()->schedule_window_close(this, do_send_close);
    } else {
        do_send_close();
    }
}

// Returns true when an xdg_toplevel has declared fixed, non-resizable dimensions by
// setting min_width == max_width (and min_height == max_height) to the same positive
// value.  This is the canonical Wayland signal that the client cannot be freely
// resized, so we should float-and-center it rather than force a tile geometry onto it.
static bool is_fixed_size_xdg_toplevel(struct wlr_xdg_toplevel* toplevel) {
    if (!toplevel) return false;
    const auto& s = toplevel->current;

    const bool w_fixed = (s.min_width > 0 && s.max_width > 0 && s.min_width == s.max_width);
    const bool h_fixed = (s.min_height > 0 && s.max_height > 0 && s.min_height == s.max_height);
    return w_fixed && h_fixed;
}

void View::handle_map(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_map_listener);
    view->m_mapped = true;
    view->m_is_animating_close = false;
    view->update_parent_relationship();

    if (view->m_foreign_toplevel) {
        std::string title = view->get_title();
        std::string app_id = view->get_app_id();
        if (!title.empty()) wlr_foreign_toplevel_handle_v1_set_title(view->m_foreign_toplevel, title.c_str());
        if (!app_id.empty()) wlr_foreign_toplevel_handle_v1_set_app_id(view->m_foreign_toplevel, app_id.c_str());
        if (view->m_server->get_output_manager()) {
            auto* out = view->m_server->get_output_manager()->get_primary_output();
            if (out && out->get_wlr_output()) {
                wlr_foreign_toplevel_handle_v1_output_enter(view->m_foreign_toplevel, out->get_wlr_output());
            }
        }
    }

    if (view->m_type == ViewType::XWayland) {
        if (view->m_surface_scene_tree) {
            wlr_scene_node_destroy(&view->m_surface_scene_tree->node);
            view->m_surface_scene_tree = nullptr;
        }
        if (view->m_xwayland_surface && view->m_xwayland_surface->surface) {
            view->m_surface_scene_tree = wlr_scene_subsurface_tree_create(view->m_scene_tree, view->m_xwayland_surface->surface);
            view->m_surface_scene_tree->node.data = view;
            view->m_xwayland_surface->surface->data = view->m_surface_scene_tree;
        }

        if (view->is_override_redirect()) {
            if (view->m_server && view->m_server->get_popup_tree() && view->m_scene_tree &&
                view->m_scene_tree->node.parent != view->m_server->get_popup_tree()) {
                wlr_scene_node_reparent(&view->m_scene_tree->node, view->m_server->get_popup_tree());
            }

            int lx = view->m_xwayland_surface->x;
            int ly = view->m_xwayland_surface->y;
            view->m_x = lx;
            view->m_y = ly;
            view->m_width = view->m_xwayland_surface->width;
            view->m_height = view->m_xwayland_surface->height;

            if (Config::get().is_xwayland_force_zero_scaling_enabled()) {
                float scale = view->get_output_scale();
                if (scale > 1.001f) {
                    lx = (int)std::round(lx / scale);
                    ly = (int)std::round(ly / scale);
                }
            }
            wlr_scene_node_set_position(&view->m_scene_tree->node, lx, ly);
            wlr_scene_node_raise_to_top(&view->m_scene_tree->node);
            wlr_scene_node_set_enabled(&view->m_scene_tree->node, true);
            if (wlr_xwayland_surface_override_redirect_wants_focus(view->m_xwayland_surface)) {
                struct wlr_seat* seat = view->m_server->get_input_manager()->get_seat();
                struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
                if (keyboard && view->m_xwayland_surface->surface) {
                    wlr_seat_keyboard_notify_enter(seat, view->m_xwayland_surface->surface,
                        keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
                }
            }
            return;
        }
    }

    std::string app_id = view->get_app_id();
    std::string title = view->get_title();

    bool is_pip = (title.find("Picture-in-Picture") != std::string::npos ||
                   title.find("Picture in picture") != std::string::npos ||
                   app_id == "pip" || app_id == "Picture-in-Picture");

    if (Config::get().should_float(app_id, title) || is_pip) {
        view->set_floating(true);
    } else if (view->m_type == ViewType::Xdg &&
               Config::get().is_auto_float_fixed_size_enabled() &&
               is_fixed_size_xdg_toplevel(view->m_xdg_toplevel)) {
        // The client declared fixed, non-resizable size constraints (min==max).
        // Tiling it would force a geometry it cannot honour; float and center it instead.
        view->set_floating(true);
    }

    view->m_is_mapping = true;
    int target_ws_id = Config::get().get_target_workspace(app_id, title);
    if (target_ws_id > 0) {
        Workspace* target_ws = view->m_server->get_workspace_manager()->get_or_create_workspace(target_ws_id);
        target_ws->add_view(view);
        view->m_server->get_workspace_manager()->switch_to_workspace(target_ws_id, view);
    } else {
        view->m_server->get_workspace_manager()->add_view_auto(view);
    }
    view->m_is_mapping = false;

    bool will_animate = (!view->is_override_redirect() && view->m_server && view->m_server->get_animation_manager() &&
        Config::get().is_window_animations_enabled() && !view->m_is_fullscreen);

    if (will_animate) {
        view->m_is_animating_open = true;
        view->update_border();
        view->update_corner_radius();
        // Pre-transform the scene node to initial animation scale and initial opacity BEFORE enabling node
        double open_scale = Config::get().get_window_animation_open_scale();
        float target_op = view->is_focused()
            ? Config::get().get_window_opacity_active()
            : Config::get().get_window_opacity_inactive();
        target_op = Config::get().get_rule_opacity(view->get_app_id(), view->get_title(), target_op);
        float init_op = Config::get().is_window_animation_fade_enabled() ? 0.0f : target_op;
        view->apply_animation_transform(open_scale, init_op);
    } else {
        view->update_opacity();
        view->update_corner_radius();
    }

    if (view->m_workspace && view->m_workspace->is_visible()) {
        wlr_scene_node_set_enabled(&view->m_scene_tree->node, true);
    }
    view->focus();

    if (will_animate) {
        view->m_server->get_animation_manager()->schedule_window_open(view);
    }
}

void View::handle_unmap(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_unmap_listener);

    // If this view is fullscreen, restore scene tree parent before normal teardown
    if (view->m_is_fullscreen) {
        view->m_is_fullscreen = false;
        struct wlr_scene_tree* parent_tree = view->m_saved_geometry.workspace_tree;
        if (!parent_tree && view->m_workspace) {
            parent_tree = (view->is_floating() || view->is_dialog())
                ? view->m_workspace->get_floating_tree()
                : view->m_workspace->get_tiled_tree();
        }
        if (parent_tree && view->m_scene_tree) {
            wlr_scene_node_reparent(&view->m_scene_tree->node, parent_tree);
        }
        if (view->m_type == ViewType::Xdg && view->m_xdg_toplevel) {
            wlr_xdg_toplevel_set_fullscreen(view->m_xdg_toplevel, false);
        } else if (view->m_type == ViewType::XWayland && view->m_xwayland_surface) {
            wlr_xwayland_surface_set_fullscreen(view->m_xwayland_surface, false);
        }
    }

    view->m_mapped = false;
    view->m_is_mapping = false;
    view->m_is_animating_close = false;
    view->m_is_animating_open = false;
    view->m_is_animating_geometry = false;
    view->m_x = 0;
    view->m_y = 0;
    view->m_width = 0;
    view->m_height = 0;
    view->m_target_x = 0;
    view->m_target_y = 0;
    view->m_target_width = 0;
    view->m_target_height = 0;
    view->m_current_anim_x = 0;
    view->m_current_anim_y = 0;
    view->m_current_anim_width = 0;
    view->m_current_anim_height = 0;
    view->m_saved_geometry = {};

    if (view->m_server && view->m_server->get_animation_manager()) {
        view->m_server->get_animation_manager()->cancel_for_view(view);
    }

    if (view->m_scene_tree) {
        wlr_scene_node_set_enabled(&view->m_scene_tree->node, false);
    }

    if (view->m_blur_node) {
        wlr_scene_node_set_enabled(&view->m_blur_node->node, false);
    }

    if (view->m_shadow_node) {
        wlr_scene_node_set_enabled(&view->m_shadow_node->node, false);
    }

    // Reparent scene tree back to root scene and disable it so that if the workspace is pruned
    // while the window is hidden/minimized to tray, view->m_scene_tree is not destroyed!
    if (!view->is_override_redirect() && view->m_scene_tree && view->m_server && view->m_server->get_scene()) {
        wlr_scene_node_reparent(&view->m_scene_tree->node, &view->m_server->get_scene()->tree);
        wlr_scene_node_set_enabled(&view->m_scene_tree->node, false);
    }

    if (view->m_server && view->m_server->get_input_manager()) {
        view->m_server->get_input_manager()->notify_view_destroyed(view);
    }

    if (!view->is_override_redirect()) {
        view->m_server->get_workspace_manager()->remove_view(view);
    }

    if (view->m_type == ViewType::XWayland && view->m_surface_scene_tree) {
        wlr_scene_node_destroy(&view->m_surface_scene_tree->node);
        view->m_surface_scene_tree = nullptr;
    }

    if (view->is_override_redirect()) {
        View* focused = view->m_server->get_focused_view();
        if (focused && focused->is_mapped()) {
            struct wlr_surface* target_surface = nullptr;
            if (focused->get_type() == ViewType::Xdg && focused->get_xdg_toplevel()) {
                target_surface = focused->get_xdg_toplevel()->base->surface;
            } else if (focused->get_type() == ViewType::XWayland && focused->get_xwayland_surface()) {
                target_surface = focused->get_xwayland_surface()->surface;
            }
            if (target_surface) {
                struct wlr_seat* seat = view->m_server->get_input_manager()->get_seat();
                struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
                if (keyboard) {
                    wlr_seat_keyboard_notify_enter(seat, target_surface,
                        keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
                }
            }
        }
    }
}

void View::handle_destroy(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_destroy_listener);
    view->m_server->remove_view(view);
}

void View::handle_commit(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_commit_listener);
    if (view->m_type == ViewType::Xdg) {
        if (view->m_xdg_toplevel->base->initial_commit) {
            view->update_parent_relationship();

            std::string app_id = view->get_app_id();
            std::string title = view->get_title();
            bool is_pip = (title.find("Picture-in-Picture") != std::string::npos ||
                           title.find("Picture in picture") != std::string::npos ||
                           app_id == "pip" || app_id == "Picture-in-Picture");

            if (Config::get().should_float(app_id, title) || is_pip) {
                view->set_floating(true);
            }

            if (view->is_dialog() || view->is_floating()) {
                wlr_xdg_toplevel_set_size(view->m_xdg_toplevel, 0, 0);
            } else {
                auto* ws_mgr = view->m_server->get_workspace_manager();
                int target_ws_id = Config::get().get_target_workspace(app_id, title);
                Workspace* ws = nullptr;
                if (target_ws_id > 0 && ws_mgr) {
                    ws = ws_mgr->get_or_create_workspace(target_ws_id);
                } else if (ws_mgr) {
                    ws = ws_mgr->get_active_workspace();
                }

                if (ws && view->m_server->get_output_manager()) {
                    struct wlr_box usable = view->m_server->get_output_manager()->get_primary_usable_geometry();
                    if (usable.width <= 0 || usable.height <= 0) {
                        usable = { .x = 0, .y = 0, .width = 1920, .height = 1080 };
                    }
                    struct wlr_box next_box = ws->calculate_tiled_geometry_for_new_view(usable);
                    int bw = Config::get().get_window_border_width();
                    int client_w = std::max(1, next_box.width - 2 * bw);
                    int client_h = std::max(1, next_box.height - 2 * bw);

                    view->m_width = next_box.width;
                    view->m_height = next_box.height;
                    view->m_target_width = next_box.width;
                    view->m_target_height = next_box.height;
                    view->m_current_anim_width = next_box.width;
                    view->m_current_anim_height = next_box.height;

                    wlr_xdg_toplevel_set_maximized(view->m_xdg_toplevel, false);
                    wlr_xdg_toplevel_set_tiled(view->m_xdg_toplevel, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
                    if (view->m_xdg_toplevel->resource && wl_resource_get_version(view->m_xdg_toplevel->resource) >= 4) {
                        wlr_xdg_toplevel_set_bounds(view->m_xdg_toplevel, client_w, client_h);
                    }
                    wlr_xdg_toplevel_set_size(view->m_xdg_toplevel, client_w, client_h);
                } else {
                    wlr_xdg_toplevel_set_size(view->m_xdg_toplevel, 0, 0);
                }
            }
            return;
        }

        if (view->m_mapped && view->is_dialog()) {
            auto* xdg_surf = view->m_xdg_toplevel->base;
            int gw = xdg_surf->current.geometry.width;
            int gh = xdg_surf->current.geometry.height;
            if (gw <= 0 && xdg_surf->surface) {
                gw = xdg_surf->surface->current.width;
                gh = xdg_surf->surface->current.height;
            }
            int bw = Config::get().get_window_border_width();
            if (gw > 0 && gh > 0 && (gw + 2 * bw != view->m_width || gh + 2 * bw != view->m_height)) {
                if (view->m_workspace) {
                    view->m_server->get_workspace_manager()->recalculate_layout();
                }
            }
        }
    } else if (view->m_type == ViewType::XWayland && Config::get().is_xwayland_force_zero_scaling_enabled()) {
        float scale = view->get_output_scale();
        if (scale > 1.001f && view->m_surface_scene_tree) {
            wlr_scene_node_for_each_buffer(&view->m_surface_scene_tree->node,
                [](struct wlr_scene_buffer* buffer, int sx, int sy, void* data) {
                    float s = *static_cast<float*>(data);
                    if (buffer && buffer->buffer_width > 0 && buffer->buffer_height > 0 && s > 0.0f) {
                        int dst_w = std::max(1, (int)std::round(buffer->buffer_width / s));
                        int dst_h = std::max(1, (int)std::round(buffer->buffer_height / s));
                        wlr_scene_buffer_set_dest_size(buffer, dst_w, dst_h);
                    }
                }, &scale);
        }
    }

    if (view->m_mapped && !view->m_is_animating_close) {
        if (!view->is_animating_geometry() && !view->is_overview_scaled() && view->m_surface_scene_tree) {
            int bw = Config::get().get_window_border_width();
            int cw = std::max(1, view->get_width() - 2 * bw);
            int ch = std::max(1, view->get_height() - 2 * bw);
            struct CheckBufferData { int w; int h; } cbd = { cw, ch };
            wlr_scene_node_for_each_buffer(&view->m_surface_scene_tree->node, [](struct wlr_scene_buffer* buf, int sx, int sy, void* data) {
                auto* d = static_cast<CheckBufferData*>(data);
                if (buf && buf->buffer && buf->buffer->width == d->w && buf->buffer->height == d->h) {
                    wlr_scene_buffer_set_dest_size(buf, 0, 0);
                }
            }, &cbd);
        }

        if (view->m_is_overview_scaled) {
            view->reapply_overview_scale();
        } else {
            view->update_frame();
        }

        if (view->tearing_allowed()) {
            Output* out = view->get_output();
            if (out) {
                out->commit_tearing();
            }
        }
    }
}

void View::handle_request_fullscreen(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_request_fullscreen_listener);
    if (view->m_type == ViewType::Xdg) {
        if (view->m_xdg_toplevel && view->m_xdg_toplevel->base->surface->mapped) {
            bool requested = view->m_xdg_toplevel->requested.fullscreen;
            view->set_fullscreen(requested);
        }
    } else if (view->m_type == ViewType::XWayland && view->m_xwayland_surface) {
        if (!view->is_override_redirect()) {
            bool requested = view->m_xwayland_surface->fullscreen;
            view->set_fullscreen(requested);
        }
    }
}

void View::handle_request_maximize(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_request_maximize_listener);
    if (view->m_type == ViewType::Xdg && view->m_xdg_toplevel) {
        if (view->m_xdg_toplevel->base && view->m_xdg_toplevel->base->initialized) {
            wlr_xdg_toplevel_set_maximized(view->m_xdg_toplevel, false);
        }
    } else if (view->m_type == ViewType::XWayland && view->m_xwayland_surface) {
        if (!view->is_override_redirect()) {
            wlr_xwayland_surface_set_maximized(view->m_xwayland_surface, false, false);
        }
    }
}

void View::handle_request_move(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_request_move_listener);
    if (!view || !view->m_mapped || view->m_is_fullscreen) return;
    if (view->m_server && view->m_server->get_input_manager()) {
        view->m_server->get_input_manager()->begin_interactive(view, CursorMode::Move, 0);
    }
}

void View::handle_request_resize(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_request_resize_listener);
    auto* event = static_cast<struct wlr_xdg_toplevel_resize_event*>(data);
    if (!view || !view->m_mapped || view->m_is_fullscreen) return;
    if (view->m_server && view->m_server->get_input_manager()) {
        view->m_server->get_input_manager()->begin_interactive(view, CursorMode::Resize, event ? event->edges : 0);
    }
}

void View::handle_xwayland_request_move(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_xwayland_request_move_listener);
    if (!view || !view->m_mapped || view->m_is_fullscreen) return;
    if (view->m_server && view->m_server->get_input_manager()) {
        view->m_server->get_input_manager()->begin_interactive(view, CursorMode::Move, 0);
    }
}

void View::handle_xwayland_request_resize(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_xwayland_request_resize_listener);
    auto* event = static_cast<struct wlr_xwayland_resize_event*>(data);
    if (!view || !view->m_mapped || view->m_is_fullscreen) return;
    if (view->m_server && view->m_server->get_input_manager()) {
        view->m_server->get_input_manager()->begin_interactive(view, CursorMode::Resize, event ? event->edges : 0);
    }
}

void View::handle_set_title(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_set_title_listener);
    std::string title = view->get_title();
    if (view->m_foreign_toplevel && !title.empty()) {
        wlr_foreign_toplevel_handle_v1_set_title(view->m_foreign_toplevel, title.c_str());
    }
}

void View::handle_set_app_id(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_set_app_id_listener);
    std::string app_id = view->get_app_id();
    if (view->m_foreign_toplevel && !app_id.empty()) {
        wlr_foreign_toplevel_handle_v1_set_app_id(view->m_foreign_toplevel, app_id.c_str());
    }
}

void View::handle_foreign_request_activate(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_foreign_request_activate_listener);
    if (!view->m_server) return;
    auto* wm = view->m_server->get_workspace_manager();
    if (!wm) return;

    if (view->m_workspace) {
        wm->switch_to_workspace(view->m_workspace->get_id(), view);
    } else {
        // Unminimizing/restoring from tray: add to the currently active workspace cleanly as a fresh window
        view->set_mapping(true);
        wm->add_view_auto(view);
        view->set_mapping(false);
        if (view->m_scene_tree) {
            wlr_scene_node_set_enabled(&view->m_scene_tree->node, true);
        }
    }
    view->focus();
}

void View::handle_foreign_request_close(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_foreign_request_close_listener);
    view->close();
}

void View::handle_scene_tree_destroy(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_scene_tree_destroy_listener);
    wl_list_remove(&view->m_scene_tree_destroy_listener.link);
    wl_list_init(&view->m_scene_tree_destroy_listener.link);

    if (view->m_server && view->m_server->get_animation_manager()) {
        view->m_server->get_animation_manager()->cancel_for_view(view);
    }

    view->m_scene_tree = nullptr;
    view->m_surface_scene_tree = nullptr;
    view->m_border_scene_buffer = nullptr;
    view->m_blur_node = nullptr;
    view->m_shadow_node = nullptr;
}

void View::handle_new_popup(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_new_popup_listener);
    auto* popup = static_cast<struct wlr_xdg_popup*>(data);

    Server* server = view->m_server;
    struct wlr_scene_tree* popup_layer = server ? server->get_popup_tree() : nullptr;
    if (!popup_layer) {
        popup_layer = view->m_surface_scene_tree ? view->m_surface_scene_tree : view->get_scene_tree();
    }
    if (!popup_layer) return;

    auto p = std::make_unique<Popup>(popup, popup_layer, view, [view](Popup* target) {
        for (auto it = view->m_popups.begin(); it != view->m_popups.end(); ++it) {
            if (it->get() == target) {
                view->m_popups.erase(it);
                break;
            }
        }
    });
    view->m_popups.push_back(std::move(p));
}

// XWayland Event Handlers
void View::handle_xwayland_associate(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_associate_listener);
    if (!view->m_xwayland_surface || !view->m_xwayland_surface->surface) return;

    safe_remove_listener(view->m_map_listener);
    safe_remove_listener(view->m_unmap_listener);
    safe_remove_listener(view->m_commit_listener);

    view->m_map_listener.notify = handle_map;
    wl_signal_add(&view->m_xwayland_surface->surface->events.map, &view->m_map_listener);

    view->m_unmap_listener.notify = handle_unmap;
    wl_signal_add(&view->m_xwayland_surface->surface->events.unmap, &view->m_unmap_listener);

    view->m_commit_listener.notify = handle_commit;
    wl_signal_add(&view->m_xwayland_surface->surface->events.commit, &view->m_commit_listener);
}

void View::handle_xwayland_dissociate(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_dissociate_listener);
    safe_remove_listener(view->m_map_listener);
    safe_remove_listener(view->m_unmap_listener);
    safe_remove_listener(view->m_commit_listener);
}

void View::handle_xwayland_request_configure(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_request_configure_listener);
    auto* ev = static_cast<struct wlr_xwayland_surface_configure_event*>(data);

    if (view->m_mapped && !view->is_override_redirect()) {
        int bw = Config::get().get_window_border_width();
        int client_w = std::max(1, view->m_width - 2 * bw);
        int client_h = std::max(1, view->m_height - 2 * bw);
        int client_x = view->m_x + bw;
        int client_y = view->m_y + bw;
        if (Config::get().is_xwayland_force_zero_scaling_enabled()) {
            float scale = view->get_output_scale();
            if (scale > 1.001f) {
                client_w = std::max(1, (int)std::round(client_w * scale));
                client_h = std::max(1, (int)std::round(client_h * scale));
                client_x = (int)std::round(client_x * scale);
                client_y = (int)std::round(client_y * scale);
            }
        }
        wlr_xwayland_surface_configure(view->m_xwayland_surface, client_x, client_y, client_w, client_h);
    } else {
        view->m_x = ev->x;
        view->m_y = ev->y;
        view->m_width = ev->width;
        view->m_height = ev->height;
        if (view->m_scene_tree) {
            int lx = ev->x;
            int ly = ev->y;
            if (Config::get().is_xwayland_force_zero_scaling_enabled()) {
                float scale = view->get_output_scale();
                if (scale > 1.001f) {
                    lx = (int)std::round(lx / scale);
                    ly = (int)std::round(ly / scale);
                }
            }
            wlr_scene_node_set_position(&view->m_scene_tree->node, lx, ly);
        }
        wlr_xwayland_surface_configure(view->m_xwayland_surface, ev->x, ev->y, ev->width, ev->height);
    }
}

void View::handle_xwayland_request_activate(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_request_activate_listener);
    if (view->is_override_redirect()) return;

    if (!view->m_server) return;
    auto* wm = view->m_server->get_workspace_manager();
    if (!wm) return;

    if (view->m_workspace) {
        wm->switch_to_workspace(view->m_workspace->get_id(), view);
    } else {
        // Unminimizing/restoring from tray / X11 netwm: add to current active workspace cleanly as a fresh window
        view->set_mapping(true);
        wm->add_view_auto(view);
        view->set_mapping(false);
        if (view->m_scene_tree) {
            wlr_scene_node_set_enabled(&view->m_scene_tree->node, true);
        }
    }
    view->focus();
}

void View::handle_xwayland_set_geometry(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_set_geometry_listener);
    if (view->is_override_redirect() && view->m_scene_tree && view->m_xwayland_surface) {
        view->m_x = view->m_xwayland_surface->x;
        view->m_y = view->m_xwayland_surface->y;
        view->m_width = view->m_xwayland_surface->width;
        view->m_height = view->m_xwayland_surface->height;
        int lx = view->m_x;
        int ly = view->m_y;
        if (Config::get().is_xwayland_force_zero_scaling_enabled()) {
            float scale = view->get_output_scale();
            if (scale > 1.001f) {
                lx = (int)std::round(lx / scale);
                ly = (int)std::round(ly / scale);
            }
        }
        wlr_scene_node_set_position(&view->m_scene_tree->node, lx, ly);
    }
}

void View::handle_xwayland_set_class(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_set_class_listener);
    std::string app_id = view->get_app_id();
    if (view->m_foreign_toplevel && !app_id.empty()) {
        wlr_foreign_toplevel_handle_v1_set_app_id(view->m_foreign_toplevel, app_id.c_str());
    }
}

void View::handle_set_parent(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_set_parent_listener);
    view->update_parent_relationship();
}

void View::handle_xwayland_set_parent(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_set_parent_listener);
    view->update_parent_relationship();
    // If needed, restack relative to parent
    if (view->m_xwayland_surface && view->m_xwayland_surface->parent) {
        if (!view->is_override_redirect() && !view->m_xwayland_surface->parent->override_redirect) {
            wlr_xwayland_surface_restack(view->m_xwayland_surface, view->m_xwayland_surface->parent, XCB_STACK_MODE_ABOVE);
        }
    }
}

void View::handle_xwayland_set_override_redirect(struct wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, m_set_override_redirect_listener);
    if (!view->m_xwayland_surface) return;
    bool old_val = view->m_is_override_redirect;
    view->m_is_override_redirect = view->m_xwayland_surface->override_redirect;

    if (view->m_mapped && old_val != view->m_is_override_redirect) {
        if (view->m_is_override_redirect) {
            view->m_server->get_workspace_manager()->remove_view(view);
            if (view->m_border_scene_buffer) {
                wlr_scene_node_set_enabled(&view->m_border_scene_buffer->node, false);
            }
            if (view->m_scene_tree && view->m_server->get_popup_tree()) {
                wlr_scene_node_reparent(&view->m_scene_tree->node, view->m_server->get_popup_tree());
                wlr_scene_node_raise_to_top(&view->m_scene_tree->node);
            }
        } else {
            view->m_server->get_workspace_manager()->add_view_auto(view);
            if (view->m_border_scene_buffer) {
                wlr_scene_node_set_enabled(&view->m_border_scene_buffer->node, true);
            }
        }
    }
}

void View::set_parent_view(View* parent) {
    if (m_parent_view == parent) return;
    if (m_parent_view) {
        auto& children = m_parent_view->m_child_dialogs;
        children.erase(std::remove(children.begin(), children.end(), this), children.end());
    }
    m_parent_view = parent;
    m_is_dialog = (parent != nullptr);
    if (m_parent_view) {
        m_parent_view->m_child_dialogs.push_back(this);
        Workspace* target_ws = m_parent_view->get_workspace();
        if (target_ws && m_workspace != target_ws) {
            if (m_workspace) {
                m_workspace->remove_view(this);
            }
            target_ws->add_view(this);
        } else if (m_workspace) {
            // View was already on this workspace (likely added to m_tiled_views).
            // Now that it has a parent and is a dialog, move it to m_floating_views!
            m_workspace->remove_view(this);
            m_workspace->add_view(this);
            auto* out_mgr = m_server->get_output_manager();
            if (out_mgr) {
                m_workspace->recalculate_layout(out_mgr->get_primary_usable_geometry());
            }
        }
    }
}

void View::update_parent_relationship() {
    if (m_type == ViewType::Xdg && m_xdg_toplevel) {
        if (m_xdg_toplevel->parent) {
            for (const auto& v : m_server->get_views()) {
                if (v.get() != this && v->get_type() == ViewType::Xdg && v->get_xdg_toplevel() == m_xdg_toplevel->parent) {
                    set_parent_view(v.get());
                    return;
                }
            }
        }
        // Fallback for system file-picker / portal dialogs only
        std::string app = get_app_id();
        if (app == "xdg-desktop-portal-gtk" || app == "org.freedesktop.impl.portal.desktop.gtk" || app == "zenity") {
            if (!m_parent_view && m_server->get_focused_view() && m_server->get_focused_view() != this) {
                set_parent_view(m_server->get_focused_view());
                return;
            }
        }
    } else if (m_type == ViewType::XWayland && m_xwayland_surface) {
        if (m_xwayland_surface->parent) {
            for (const auto& v : m_server->get_views()) {
                if (v.get() != this && v->get_type() == ViewType::XWayland && v->get_xwayland_surface() == m_xwayland_surface->parent) {
                    set_parent_view(v.get());
                    return;
                }
            }
        }
    }
}

bool View::has_child_dialogs() const {
    for (auto* d : m_child_dialogs) {
        if (d && d->is_mapped()) return true;
    }
    return false;
}

View* View::get_top_dialog() const {
    for (auto it = m_child_dialogs.rbegin(); it != m_child_dialogs.rend(); ++it) {
        if (*it && (*it)->is_mapped()) {
            return *it;
        }
    }
    return nullptr;
}

} // namespace miquland
