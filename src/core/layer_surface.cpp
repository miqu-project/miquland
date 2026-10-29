#include "core/layer_surface.hpp"
#include "core/popup.hpp"
#include "core/server.hpp"
#include "core/output.hpp"
#include "core/input/input.hpp"
#include "core/animation/animation_manager.hpp"
#include "core/config/config.hpp"
#include "core/common/border_paint.hpp"
#include <algorithm>
#include <cmath>

namespace miquland {

LayerSurface::LayerSurface(Server* server, struct wlr_layer_surface_v1* layer_surface)
    : m_server(server), m_wlr_layer_surface(layer_surface)
{
    m_current_layer = layer_surface->pending.layer;
    m_namespace = layer_surface->_namespace ? layer_surface->_namespace : "";

    // Default to primary output if not specified by client
    if (!layer_surface->output && server->get_output_manager()) {
        auto* out = server->get_output_manager()->get_primary_output();
        if (out) {
            layer_surface->output = out->get_wlr_output();
        }
    }

    struct wlr_scene_tree* layer_tree = server->get_layer_tree(m_current_layer);
    m_scene_layer_surface = wlr_scene_layer_surface_v1_create(layer_tree, layer_surface);

    m_map_listener.notify = handle_map;
    wl_signal_add(&layer_surface->surface->events.map, &m_map_listener);

    m_unmap_listener.notify = handle_unmap;
    wl_signal_add(&layer_surface->surface->events.unmap, &m_unmap_listener);

    m_destroy_listener.notify = handle_destroy;
    wl_signal_add(&layer_surface->events.destroy, &m_destroy_listener);

    m_surface_commit_listener.notify = handle_surface_commit;
    wl_signal_add(&layer_surface->surface->events.commit, &m_surface_commit_listener);

    m_new_popup_listener.notify = handle_new_popup;
    wl_signal_add(&layer_surface->events.new_popup, &m_new_popup_listener);

    log_info("New layer surface created: namespace=" + m_namespace);
}

LayerSurface::~LayerSurface() {
    if (m_server && m_server->get_animation_manager()) {
        m_server->get_animation_manager()->cancel_for_layer(this);
    }
    if (m_current_buffer) {
        wlr_buffer_unlock(m_current_buffer);
        m_current_buffer = nullptr;
    }
    m_popups.clear();
    wl_list_remove(&m_map_listener.link);
    wl_list_remove(&m_unmap_listener.link);
    wl_list_remove(&m_destroy_listener.link);
    wl_list_remove(&m_surface_commit_listener.link);
    wl_list_remove(&m_new_popup_listener.link);
    m_blur_node = nullptr;
    m_shadow_node = nullptr;
}

void LayerSurface::configure(const struct wlr_box* full_area, struct wlr_box* usable_area) {
    if (!m_scene_layer_surface) return;
    wlr_scene_layer_surface_v1_configure(m_scene_layer_surface, full_area, usable_area);
    if (m_scene_layer_surface && m_scene_layer_surface->tree) {
        m_geo_x = m_scene_layer_surface->tree->node.x;
        m_geo_y = m_scene_layer_surface->tree->node.y;
    }
    if (m_wlr_layer_surface && m_wlr_layer_surface->surface) {
        m_width = m_wlr_layer_surface->surface->current.width;
        m_height = m_wlr_layer_surface->surface->current.height;
    }
    update_shadow();
}

void LayerSurface::update_tree() {
    if (!m_wlr_layer_surface || !m_scene_layer_surface) return;

    if (m_wlr_layer_surface->current.layer != m_current_layer) {
        m_current_layer = m_wlr_layer_surface->current.layer;
        struct wlr_scene_tree* target_tree = m_server->get_layer_tree(m_current_layer);
        if (target_tree) {
            wlr_scene_node_reparent(&m_scene_layer_surface->tree->node, target_tree);
        }
    }
}

void LayerSurface::update_blur() {
    if (!Config::get().is_blur_enabled()) {
        if (m_blur_node) {
            wlr_scene_node_set_enabled(&m_blur_node->node, false);
        }
        return;
    }

    if (!m_scene_layer_surface || !m_wlr_layer_surface || !is_mapped()) {
        if (m_blur_node) {
            wlr_scene_node_set_enabled(&m_blur_node->node, false);
        }
        return;
    }

    // Wallpapers on BACKGROUND layer should not be blurred
    if (m_current_layer == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) {
        if (m_blur_node) {
            wlr_scene_node_set_enabled(&m_blur_node->node, false);
        }
        return;
    }

    // For layer surfaces, blur is opt-in via layer rules (matching namespace)
    const char* ns = m_wlr_layer_surface->_namespace;
    if (!ns || !Config::get().is_layer_blur_enabled(ns)) {
        if (m_blur_node) {
            wlr_scene_node_set_enabled(&m_blur_node->node, false);
        }
        return;
    }

    int w = m_wlr_layer_surface->surface->current.width;
    int h = m_wlr_layer_surface->surface->current.height;
    if (w <= 0 || h <= 0) return;

    if (!m_blur_node) {
        m_blur_node = wlr_scene_blur_create(m_scene_layer_surface->tree, w, h);
        if (m_blur_node) {
            wlr_scene_node_lower_to_bottom(&m_blur_node->node);
        }
    }

    if (m_blur_node) {
        wlr_scene_blur_set_size(m_blur_node, w, h);
        wlr_scene_node_set_enabled(&m_blur_node->node, true);

        Config::LayerRule rule = Config::get().get_layer_rule(ns);
        wlr_scene_blur_set_should_only_blur_bottom_layer(m_blur_node, rule.blur_bottom_only);

        int r = (rule.blur_corner_radius >= 0) ? rule.blur_corner_radius :
                ((rule.shadow_corner_radius >= 0) ? rule.shadow_corner_radius : 0);
        wlr_scene_blur_set_corner_radius(m_blur_node, r);

        float alpha = (rule.blur_alpha >= 0.0f) ? rule.blur_alpha : 1.0f;
        wlr_scene_blur_set_alpha(m_blur_node, alpha);

        float strength = (rule.blur_strength >= 0.0f) ? rule.blur_strength : 1.0f;
        wlr_scene_blur_set_strength(m_blur_node, strength);

        if (!rule.blur_ignore_opacity) {
            // Find the scene buffer for this layer surface and hook transparency mask
            struct wlr_scene_buffer* surf_buf = nullptr;
            wlr_scene_node_for_each_buffer(&m_scene_layer_surface->tree->node,
                [](struct wlr_scene_buffer* buffer, int sx, int sy, void* user_data) {
                    auto** out = static_cast<struct wlr_scene_buffer**>(user_data);
                    if (!*out) *out = buffer;
                }, &surf_buf);

            wlr_scene_blur_set_transparency_mask_source(m_blur_node, surf_buf);
        } else {
            wlr_scene_blur_set_transparency_mask_source(m_blur_node, nullptr);
        }
    }
}

void LayerSurface::sync_shadow_geometry(int w, int h, int r, float sigma, double ox, double oy) {
    if (!m_shadow_node) return;
    int pad = static_cast<int>(std::ceil(std::max(4.0f, sigma * 2.5f)));
    int int_ox = static_cast<int>(std::round(ox));
    int int_oy = static_cast<int>(std::round(oy));

    wlr_scene_node_set_position(&m_shadow_node->node, -pad + int_ox, -pad + int_oy);
    wlr_scene_shadow_set_size(m_shadow_node, w + 2 * pad, h + 2 * pad);
    wlr_scene_shadow_set_blur_sigma(m_shadow_node, sigma);
    wlr_scene_shadow_set_corner_radius(m_shadow_node, r);

    struct clipped_region cr = clipped_region_get_default();
    cr.area.x = pad - int_ox;
    cr.area.y = pad - int_oy;
    cr.area.width = w;
    cr.area.height = h;
    cr.corners = corner_radii_all(r);
    wlr_scene_shadow_set_clipped_region(m_shadow_node, cr);
}

void LayerSurface::update_shadow() {
    if (!m_scene_layer_surface || !m_wlr_layer_surface || !is_mapped()) {
        if (m_shadow_node) {
            wlr_scene_node_set_enabled(&m_shadow_node->node, false);
        }
        return;
    }

    // Wallpapers on BACKGROUND layer should not cast shadows
    if (m_current_layer == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) {
        if (m_shadow_node) {
            wlr_scene_node_set_enabled(&m_shadow_node->node, false);
        }
        return;
    }

    // Shadow is opt-in via layer rules matching namespace
    Config::LayerRule rule = Config::get().get_layer_rule(m_namespace);
    if (!rule.shadow_enabled) {
        if (m_shadow_node) {
            wlr_scene_node_set_enabled(&m_shadow_node->node, false);
        }
        return;
    }

    int w = (m_width > 0) ? m_width : (m_wlr_layer_surface->surface ? m_wlr_layer_surface->surface->current.width : 0);
    int h = (m_height > 0) ? m_height : (m_wlr_layer_surface->surface ? m_wlr_layer_surface->surface->current.height : 0);
    if (w <= 0 || h <= 0) return;

    float sigma = (rule.shadow_blur_sigma >= 0.0f) ? rule.shadow_blur_sigma : Config::get().get_shadow_blur_sigma();
    if (sigma <= 0.0f) {
        if (m_shadow_node) {
            wlr_scene_node_set_enabled(&m_shadow_node->node, false);
        }
        return;
    }

    int radius = (rule.shadow_corner_radius >= 0) ? rule.shadow_corner_radius : Config::get().get_window_border_radius();
    double ox = rule.shadow_offset_specified ? rule.shadow_offset_x : Config::get().get_shadow_offset_x();
    double oy = rule.shadow_offset_specified ? rule.shadow_offset_y : Config::get().get_shadow_offset_y();
    const std::string& col_str = !rule.shadow_color.empty() ? rule.shadow_color : Config::get().get_shadow_color_active();

    if (!m_shadow_node && m_scene_layer_surface && m_scene_layer_surface->tree) {
        float initial_color[4] = {0.0f, 0.0f, 0.0f, 0.4f};
        m_shadow_node = wlr_scene_shadow_create(m_scene_layer_surface->tree, w, h, radius, sigma, initial_color);
        if (m_shadow_node) {
            wlr_scene_node_lower_to_bottom(&m_shadow_node->node);
        }
    }

    if (!m_shadow_node) return;

    sync_shadow_geometry(w, h, radius, sigma, ox, oy);

    float sr = 0.0f, sg = 0.0f, sb = 0.0f, sa = 0.4f;
    BorderPaint::parse_hex_color(col_str, sr, sg, sb, sa);
    float final_col[4] = { sr, sg, sb, sa };
    wlr_scene_shadow_set_color(m_shadow_node, final_col);

    wlr_scene_node_set_enabled(&m_shadow_node->node, true);
}

Config::LayerAnimStyle LayerSurface::deduce_animation_style(bool is_close) const {
    if (!m_wlr_layer_surface) return Config::LayerAnimStyle::None;

    // 1. User layer rule override
    Config::LayerRule rule = Config::get().get_layer_rule(m_namespace);
    if (is_close) {
        if (rule.noanim_close) return Config::LayerAnimStyle::None;
        if (rule.anim_style_close != Config::LayerAnimStyle::DefaultAuto) {
            return rule.anim_style_close;
        }
    } else {
        if (rule.noanim_open) return Config::LayerAnimStyle::None;
        if (rule.anim_style_open != Config::LayerAnimStyle::DefaultAuto) {
            return rule.anim_style_open;
        }
    }

    // 2. Background layer is static by default
    if (m_current_layer == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) {
        return Config::LayerAnimStyle::None;
    }

    // 3. Anchor heuristic
    uint32_t anchor = m_wlr_layer_surface->current.anchor;
    bool top = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) != 0;
    bool bottom = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) != 0;
    bool left = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) != 0;
    bool right = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) != 0;

    if (top && !bottom && !left && !right) return Config::LayerAnimStyle::SlideTop;
    if (bottom && !top && !left && !right) return Config::LayerAnimStyle::SlideBottom;
    if (left && !right && !top && !bottom) return Config::LayerAnimStyle::SlideLeft;
    if (right && !left && !top && !bottom) return Config::LayerAnimStyle::SlideRight;

    // Stretched along single edge
    if (top && !bottom) return Config::LayerAnimStyle::SlideTop;
    if (bottom && !top) return Config::LayerAnimStyle::SlideBottom;
    if (left && !right) return Config::LayerAnimStyle::SlideLeft;
    if (right && !left) return Config::LayerAnimStyle::SlideRight;

    // 4. Centered / Floating dialogs (e.g. miqulauncher, miqulock, miqupolkit, rofi)
    if (m_current_layer == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY || m_current_layer == ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
        if (top && bottom && left && right) {
            return Config::LayerAnimStyle::Fade;
        }
        return Config::LayerAnimStyle::Popin;
    }

    return Config::LayerAnimStyle::Popin;
}

void LayerSurface::apply_animation_transform(int offset_x, int offset_y, double scale_x, double scale_y, float opacity, bool center_anchor) {
    if (!m_scene_layer_surface || !m_scene_layer_surface->tree) return;

    m_animating = true;
    float safe_opacity = std::clamp(opacity, 0.0f, 1.0f);

    int base_w = (m_width > 0) ? m_width : (m_wlr_layer_surface && m_wlr_layer_surface->surface ? m_wlr_layer_surface->surface->current.width : 0);
    int base_h = (m_height > 0) ? m_height : (m_wlr_layer_surface && m_wlr_layer_surface->surface ? m_wlr_layer_surface->surface->current.height : 0);
    if (base_w <= 0) base_w = 400;
    if (base_h <= 0) base_h = 200;

    int cur_x = m_geo_x + offset_x;
    int cur_y = m_geo_y + offset_y;

    if (center_anchor) {
        if (std::abs(scale_x - 1.0) > 0.001) {
            double cur_w = std::max(1.0, static_cast<double>(base_w) * scale_x);
            cur_x += static_cast<int>(std::round((static_cast<double>(base_w) - cur_w) / 2.0));
        }
        if (std::abs(scale_y - 1.0) > 0.001) {
            double cur_h = std::max(1.0, static_cast<double>(base_h) * scale_y);
            cur_y += static_cast<int>(std::round((static_cast<double>(base_h) - cur_h) / 2.0));
        }
    }

    wlr_scene_node_set_position(&m_scene_layer_surface->tree->node, cur_x, cur_y);

    struct BufferAnimData {
        double scale_x;
        double scale_y;
        float opacity;
    } data = { scale_x, scale_y, safe_opacity };

    wlr_scene_node_for_each_buffer(&m_scene_layer_surface->tree->node, [](struct wlr_scene_buffer* buf, int sx, int sy, void* user_data) {
        auto* d = static_cast<BufferAnimData*>(user_data);
        if ((std::abs(d->scale_x - 1.0) > 0.001 || std::abs(d->scale_y - 1.0) > 0.001) && buf->buffer) {
            int sw = std::max(1, static_cast<int>(std::round(static_cast<double>(buf->buffer->width) * d->scale_x)));
            int sh = std::max(1, static_cast<int>(std::round(static_cast<double>(buf->buffer->height) * d->scale_y)));
            wlr_scene_buffer_set_dest_size(buf, sw, sh);
        } else {
            wlr_scene_buffer_set_dest_size(buf, 0, 0);
        }
        wlr_scene_buffer_set_opacity(buf, d->opacity);
    }, &data);

    if (m_blur_node) {
        if (safe_opacity < 0.01f || !Config::get().is_blur_enabled()) {
            wlr_scene_node_set_enabled(&m_blur_node->node, false);
        } else {
            wlr_scene_node_set_enabled(&m_blur_node->node, true);
            int bw = std::max(1, static_cast<int>(std::round(static_cast<double>(base_w) * scale_x)));
            int bh = std::max(1, static_cast<int>(std::round(static_cast<double>(base_h) * scale_y)));
            wlr_scene_blur_set_size(m_blur_node, bw, bh);

            Config::LayerRule rule = Config::get().get_layer_rule(m_namespace);
            float base_alpha = (rule.blur_alpha >= 0.0f) ? rule.blur_alpha : 1.0f;
            wlr_scene_blur_set_alpha(m_blur_node, safe_opacity * base_alpha);

            int r = (rule.blur_corner_radius >= 0) ? rule.blur_corner_radius :
                    ((rule.shadow_corner_radius >= 0) ? rule.shadow_corner_radius : 0);
            int scaled_r = static_cast<int>(std::round(static_cast<double>(r) * std::min(scale_x, scale_y)));
            wlr_scene_blur_set_corner_radius(m_blur_node, scaled_r);
        }
    }

    if (m_shadow_node) {
        Config::LayerRule rule = Config::get().get_layer_rule(m_namespace);
        if (safe_opacity < 0.01f || !rule.shadow_enabled) {
            wlr_scene_node_set_enabled(&m_shadow_node->node, false);
        } else {
            wlr_scene_node_set_enabled(&m_shadow_node->node, true);
            int sw = std::max(1, static_cast<int>(std::round(static_cast<double>(base_w) * scale_x)));
            int sh = std::max(1, static_cast<int>(std::round(static_cast<double>(base_h) * scale_y)));
            float sigma = (rule.shadow_blur_sigma >= 0.0f) ? rule.shadow_blur_sigma : Config::get().get_shadow_blur_sigma();
            int radius = (rule.shadow_corner_radius >= 0) ? rule.shadow_corner_radius : Config::get().get_window_border_radius();
            double ox = rule.shadow_offset_specified ? rule.shadow_offset_x : Config::get().get_shadow_offset_x();
            double oy = rule.shadow_offset_specified ? rule.shadow_offset_y : Config::get().get_shadow_offset_y();
            const std::string& col_str = !rule.shadow_color.empty() ? rule.shadow_color : Config::get().get_shadow_color_active();

            sync_shadow_geometry(sw, sh, radius, sigma, ox, oy);

            float sr = 0.0f, sg = 0.0f, sb = 0.0f, sa = 0.4f;
            BorderPaint::parse_hex_color(col_str, sr, sg, sb, sa);
            float final_col[4] = { sr, sg, sb, sa * safe_opacity };
            wlr_scene_shadow_set_color(m_shadow_node, final_col);
        }
    }
}

void LayerSurface::reset_animation_transform() {
    m_animating = false;
    if (!m_scene_layer_surface || !m_scene_layer_surface->tree) return;
    wlr_scene_node_set_position(&m_scene_layer_surface->tree->node, m_geo_x, m_geo_y);
    wlr_scene_node_for_each_buffer(&m_scene_layer_surface->tree->node, [](struct wlr_scene_buffer* buf, int, int, void*) {
        wlr_scene_buffer_set_dest_size(buf, 0, 0);
        wlr_scene_buffer_set_opacity(buf, 1.0f);
    }, nullptr);
    update_blur();
    update_shadow();

    if (m_wlr_layer_surface && m_wlr_layer_surface->output) {
        wlr_output_schedule_frame(m_wlr_layer_surface->output);
    }
}

void LayerSurface::handle_map(struct wl_listener* listener, void* data) {
    LayerSurface* surface = wl_container_of(listener, surface, m_map_listener);

    // Ensure we have locked the buffer for exit animations
    if (!surface->m_current_buffer && surface->m_wlr_layer_surface && surface->m_wlr_layer_surface->surface) {
        if (surface->m_wlr_layer_surface->surface->buffer) {
            surface->m_current_buffer = &surface->m_wlr_layer_surface->surface->buffer->base;
            wlr_buffer_lock(surface->m_current_buffer);
        }
    }

    surface->m_server->arrange_layers(surface->m_wlr_layer_surface->output);
    surface->update_blur();
    surface->update_shadow();

    if (surface->m_server && surface->m_server->get_animation_manager()) {
        surface->m_server->get_animation_manager()->schedule_layer_open(surface);
    }

    if (surface->m_wlr_layer_surface && surface->m_wlr_layer_surface->output) {
        wlr_output_schedule_frame(surface->m_wlr_layer_surface->output);
    }

    // If layer surface requires keyboard interaction (e.g. rofi, fuzzel, swaylock), focus it
    if (surface->m_wlr_layer_surface->current.keyboard_interactive != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
        surface->m_server->focus_layer_surface(surface);
    }
}

void LayerSurface::handle_unmap(struct wl_listener* listener, void* data) {
    LayerSurface* surface = wl_container_of(listener, surface, m_unmap_listener);

    if (surface->m_server && surface->m_server->get_animation_manager()) {
        if (Config::get().is_layer_close_animation_enabled()) {
            surface->m_server->get_animation_manager()->schedule_layer_unmap_close(surface);
        }
        surface->m_server->get_animation_manager()->cancel_for_layer(surface);
    }

    if (surface->m_server->get_focused_layer_surface() == surface) {
        surface->m_server->focus_layer_surface(nullptr);
    }

    if (surface->m_blur_node) {
        wlr_scene_node_set_enabled(&surface->m_blur_node->node, false);
    }
    if (surface->m_shadow_node) {
        wlr_scene_node_set_enabled(&surface->m_shadow_node->node, false);
    }

    surface->m_server->arrange_layers(surface->m_wlr_layer_surface->output);
}

void LayerSurface::handle_destroy(struct wl_listener* listener, void* data) {
    LayerSurface* surface = wl_container_of(listener, surface, m_destroy_listener);
    if (surface->m_server && surface->m_server->get_animation_manager()) {
        surface->m_server->get_animation_manager()->cancel_for_layer(surface);
    }
    surface->m_server->remove_layer_surface(surface);
}

void LayerSurface::handle_surface_commit(struct wl_listener* listener, void* data) {
    LayerSurface* surface = wl_container_of(listener, surface, m_surface_commit_listener);
    struct wlr_layer_surface_v1* wlr_surface = surface->m_wlr_layer_surface;

    if (!wlr_surface->initialized) return;

    int old_w = surface->m_width;
    int old_h = surface->m_height;
    if (wlr_surface->surface) {
        if (wlr_surface->surface->current.width > 0) surface->m_width = wlr_surface->surface->current.width;
        if (wlr_surface->surface->current.height > 0) surface->m_height = wlr_surface->surface->current.height;
    }
    if (surface->m_scene_layer_surface && surface->m_scene_layer_surface->tree && !surface->m_animating) {
        surface->m_geo_x = surface->m_scene_layer_surface->tree->node.x;
        surface->m_geo_y = surface->m_scene_layer_surface->tree->node.y;
    }

    // Retain a locked reference to the current buffer for unmap close animations
    struct wlr_buffer* committed_buf = nullptr;
    if (wlr_surface->surface && wlr_surface->surface->buffer) {
        committed_buf = &wlr_surface->surface->buffer->base;
    }
    if (!committed_buf && surface->m_scene_layer_surface && surface->m_scene_layer_surface->tree) {
        struct wlr_scene_buffer* surf_buf = nullptr;
        wlr_scene_node_for_each_buffer(&surface->m_scene_layer_surface->tree->node,
            [](struct wlr_scene_buffer* buffer, int sx, int sy, void* user_data) {
                auto** out = static_cast<struct wlr_scene_buffer**>(user_data);
                if (!*out && buffer && buffer->buffer) *out = buffer;
            }, &surf_buf);
        if (surf_buf) {
            committed_buf = surf_buf->buffer;
        }
    }

    if (committed_buf && committed_buf != surface->m_current_buffer) {
        if (surface->m_current_buffer) {
            wlr_buffer_unlock(surface->m_current_buffer);
        }
        surface->m_current_buffer = committed_buf;
        wlr_buffer_lock(surface->m_current_buffer);
    }

    surface->update_tree();
    if (old_w != surface->m_width || old_h != surface->m_height) {
        surface->update_blur();
        surface->update_shadow();
    }

    if (surface->m_current_layer == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND && surface->m_server) {
        surface->m_server->mark_optimized_blur_dirty();
    }

    if (!wlr_surface->output && surface->m_server && surface->m_server->get_output_manager()) {
        auto* out = surface->m_server->get_output_manager()->get_primary_output();
        if (out) {
            wlr_surface->output = out->get_wlr_output();
        }
    }

    uint32_t committed = wlr_surface->current.committed;
    if (wlr_surface->initial_commit ||
        (committed & (WLR_LAYER_SURFACE_V1_STATE_LAYER |
                      WLR_LAYER_SURFACE_V1_STATE_EXCLUSIVE_ZONE |
                      WLR_LAYER_SURFACE_V1_STATE_MARGIN |
                      WLR_LAYER_SURFACE_V1_STATE_DESIRED_SIZE |
                      WLR_LAYER_SURFACE_V1_STATE_ANCHOR))) {
        surface->m_server->arrange_layers(wlr_surface->output);
    }

    if (wlr_surface->surface->mapped) {
        if (wlr_surface->current.keyboard_interactive != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
            if (surface->m_server->get_focused_layer_surface() != surface) {
                surface->m_server->focus_layer_surface(surface);
            }
        } else if (surface->m_server->get_focused_layer_surface() == surface) {
            surface->m_server->focus_layer_surface(nullptr);
        }

        if (wlr_surface->output) {
            wlr_output_schedule_frame(wlr_surface->output);
        }
    }
}

void LayerSurface::handle_new_popup(struct wl_listener* listener, void* data) {
    LayerSurface* surface = wl_container_of(listener, surface, m_new_popup_listener);
    auto* popup = static_cast<struct wlr_xdg_popup*>(data);

    if (!surface->m_scene_layer_surface) return;

    struct wlr_scene_tree* popup_layer = surface->m_server ? surface->m_server->get_popup_tree() : nullptr;
    if (!popup_layer) popup_layer = surface->m_scene_layer_surface->tree;

    auto p = std::make_unique<Popup>(popup, popup_layer, surface, [surface](Popup* target) {
        for (auto it = surface->m_popups.begin(); it != surface->m_popups.end(); ++it) {
            if (it->get() == target) {
                surface->m_popups.erase(it);
                break;
            }
        }
    });
    surface->m_popups.push_back(std::move(p));
}

bool LayerSurface::contains_surface(struct wlr_surface* target) const {
    if (!target || !m_wlr_layer_surface) return false;
    if (m_wlr_layer_surface->surface == target) return true;
    for (const auto& popup : m_popups) {
        if (popup && popup->get_wlr_popup() && popup->get_wlr_popup()->base &&
            popup->get_wlr_popup()->base->surface == target) {
            return true;
        }
    }
    return false;
}

} // namespace miquland
