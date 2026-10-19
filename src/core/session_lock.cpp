#include "core/session_lock.hpp"
#include "core/server.hpp"
#include "core/output.hpp"
#include "core/input/input.hpp"
#include "core/config/config.hpp"

namespace miquland {

SessionLockSurface::SessionLockSurface(SessionLock* lock, struct wlr_session_lock_surface_v1* lock_surface)
    : m_lock(lock), m_server(lock->get_server()), m_lock_surface(lock_surface)
{
    m_destroy_listener.notify = handle_destroy;
    wl_signal_add(&lock_surface->events.destroy, &m_destroy_listener);

    m_surface_commit_listener.notify = handle_surface_commit;
    wl_signal_add(&lock_surface->surface->events.commit, &m_surface_commit_listener);

    // Create scene tree for subsurface under the session lock tree
    m_scene_tree = wlr_scene_subsurface_tree_create(m_server->get_session_lock_tree(), lock_surface->surface);
    m_scene_tree->node.data = this;

    struct wlr_output_layout* layout = m_server->get_output_manager()->get_layout();
    struct wlr_output_layout_output* layout_output = wlr_output_layout_get(layout, lock_surface->output);
    if (layout_output) {
        wlr_scene_node_set_position(&m_scene_tree->node, layout_output->x, layout_output->y);
    }

    int width = 0, height = 0;
    wlr_output_effective_resolution(lock_surface->output, &width, &height);
    wlr_session_lock_surface_v1_configure(lock_surface, width, height);
}

SessionLockSurface::~SessionLockSurface() {
    if (m_destroy_listener.link.next) {
        wl_list_remove(&m_destroy_listener.link);
        m_destroy_listener.link.next = nullptr;
    }
    if (m_surface_commit_listener.link.next) {
        wl_list_remove(&m_surface_commit_listener.link);
        m_surface_commit_listener.link.next = nullptr;
    }
    if (m_scene_tree) {
        wlr_scene_node_destroy(&m_scene_tree->node);
        m_scene_tree = nullptr;
    }
    m_blur_node = nullptr;
}

void SessionLockSurface::handle_destroy(struct wl_listener* listener, void* data) {
    SessionLockSurface* surface = wl_container_of(listener, surface, m_destroy_listener);
    if (surface->m_destroy_listener.link.next) {
        wl_list_remove(&surface->m_destroy_listener.link);
        surface->m_destroy_listener.link.next = nullptr;
    }
    surface->m_lock->remove_surface(surface);
}

void SessionLockSurface::handle_surface_commit(struct wl_listener* listener, void* data) {
    SessionLockSurface* surface = wl_container_of(listener, surface, m_surface_commit_listener);
    if (!surface->m_configured) {
        surface->m_configured = true;
        surface->m_lock->check_and_send_locked();
    }
    surface->update_blur();
}

void SessionLockSurface::update_blur() {
    if (!Config::get().is_blur_enabled()) {
        if (m_blur_node) {
            wlr_scene_node_set_enabled(&m_blur_node->node, false);
        }
        return;
    }

    if (!m_scene_tree || !m_lock_surface || !m_lock_surface->surface) {
        if (m_blur_node) {
            wlr_scene_node_set_enabled(&m_blur_node->node, false);
        }
        return;
    }

    // Map layerrule "miqulock" (and "sessionlock") to session lock surfaces
    Config::LayerRule rule = Config::get().get_layer_rule("miqulock");
    bool blur_enabled = Config::get().is_layer_blur_enabled("miqulock");
    if (!rule.blur_enabled_specified && !blur_enabled) {
        Config::LayerRule lock_rule = Config::get().get_layer_rule("sessionlock");
        bool lock_blur_enabled = Config::get().is_layer_blur_enabled("sessionlock");
        if (lock_rule.blur_enabled_specified || lock_blur_enabled) {
            rule = lock_rule;
            blur_enabled = lock_blur_enabled;
        }
    }

    if (!blur_enabled) {
        if (m_blur_node) {
            wlr_scene_node_set_enabled(&m_blur_node->node, false);
        }
        return;
    }

    int w = m_lock_surface->surface->current.width;
    int h = m_lock_surface->surface->current.height;
    if (w <= 0 || h <= 0) {
        if (m_lock_surface->output) {
            wlr_output_effective_resolution(m_lock_surface->output, &w, &h);
        }
    }
    if (w <= 0 || h <= 0) return;

    if (!m_blur_node) {
        m_blur_node = wlr_scene_blur_create(m_scene_tree, w, h);
        if (m_blur_node) {
            wlr_scene_node_lower_to_bottom(&m_blur_node->node);
        }
    }

    if (m_blur_node) {
        wlr_scene_blur_set_size(m_blur_node, w, h);
        wlr_scene_node_set_enabled(&m_blur_node->node, true);
        wlr_scene_node_lower_to_bottom(&m_blur_node->node);
        wlr_scene_blur_set_should_only_blur_bottom_layer(m_blur_node, rule.blur_bottom_only);

        struct wlr_scene_buffer* surf_buf = nullptr;
        wlr_scene_node_for_each_buffer(&m_scene_tree->node,
            [](struct wlr_scene_buffer* buffer, int sx, int sy, void* user_data) {
                auto** out = static_cast<struct wlr_scene_buffer**>(user_data);
                if (!*out) *out = buffer;
            }, &surf_buf);

        if (surf_buf) {
            wlr_scene_blur_set_transparency_mask_source(m_blur_node, surf_buf);
        }
    }
}

void SessionLockSurface::focus() {
    if (!m_lock_surface || !m_lock_surface->surface) return;
    struct wlr_seat* seat = m_server->get_input_manager()->get_seat();
    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, m_lock_surface->surface,
            keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }
}

struct wlr_buffer* SessionLockSurface::get_current_buffer() const {
    if (m_lock_surface && m_lock_surface->surface && m_lock_surface->surface->buffer) {
        return &m_lock_surface->surface->buffer->base;
    }
    struct wlr_scene_buffer* scene_buf = nullptr;
    if (m_scene_tree) {
        wlr_scene_node_for_each_buffer(&m_scene_tree->node,
            [](struct wlr_scene_buffer* buffer, int sx, int sy, void* user_data) {
                auto** out = static_cast<struct wlr_scene_buffer**>(user_data);
                if (!*out && buffer && buffer->buffer) *out = buffer;
            }, &scene_buf);
    }
    return scene_buf ? scene_buf->buffer : nullptr;
}

int SessionLockSurface::get_geo_x() const {
    if (!m_server || !m_lock_surface || !m_lock_surface->output) return 0;
    struct wlr_output_layout* layout = m_server->get_output_manager()->get_layout();
    struct wlr_output_layout_output* layout_output = wlr_output_layout_get(layout, m_lock_surface->output);
    return layout_output ? layout_output->x : 0;
}

int SessionLockSurface::get_geo_y() const {
    if (!m_server || !m_lock_surface || !m_lock_surface->output) return 0;
    struct wlr_output_layout* layout = m_server->get_output_manager()->get_layout();
    struct wlr_output_layout_output* layout_output = wlr_output_layout_get(layout, m_lock_surface->output);
    return layout_output ? layout_output->y : 0;
}

int SessionLockSurface::get_width() const {
    if (m_lock_surface && m_lock_surface->surface && m_lock_surface->surface->current.width > 0) {
        return m_lock_surface->surface->current.width;
    }
    if (m_lock_surface && m_lock_surface->output) {
        int w = 0, h = 0;
        wlr_output_effective_resolution(m_lock_surface->output, &w, &h);
        return w;
    }
    return 0;
}

int SessionLockSurface::get_height() const {
    if (m_lock_surface && m_lock_surface->surface && m_lock_surface->surface->current.height > 0) {
        return m_lock_surface->surface->current.height;
    }
    if (m_lock_surface && m_lock_surface->output) {
        int w = 0, h = 0;
        wlr_output_effective_resolution(m_lock_surface->output, &w, &h);
        return h;
    }
    return 0;
}

SessionLock::SessionLock(Server* server, struct wlr_session_lock_v1* lock)
    : m_server(server), m_wlr_lock(lock)
{
    m_new_surface_listener.notify = handle_new_surface;
    wl_signal_add(&lock->events.new_surface, &m_new_surface_listener);

    m_unlock_listener.notify = handle_unlock;
    wl_signal_add(&lock->events.unlock, &m_unlock_listener);

    m_destroy_listener.notify = handle_destroy;
    wl_signal_add(&lock->events.destroy, &m_destroy_listener);
}

SessionLock::~SessionLock() {
    if (m_new_surface_listener.link.next) {
        wl_list_remove(&m_new_surface_listener.link);
        m_new_surface_listener.link.next = nullptr;
    }
    if (m_unlock_listener.link.next) {
        wl_list_remove(&m_unlock_listener.link);
        m_unlock_listener.link.next = nullptr;
    }
    if (m_destroy_listener.link.next) {
        wl_list_remove(&m_destroy_listener.link);
        m_destroy_listener.link.next = nullptr;
    }
    m_surfaces.clear();
}

void SessionLock::handle_new_surface(struct wl_listener* listener, void* data) {
    SessionLock* lock = wl_container_of(listener, lock, m_new_surface_listener);
    auto* lock_surface = static_cast<struct wlr_session_lock_surface_v1*>(data);
    lock->m_surfaces.push_back(std::make_unique<SessionLockSurface>(lock, lock_surface));
}

void SessionLock::handle_unlock(struct wl_listener* listener, void* data) {
    SessionLock* lock = wl_container_of(listener, lock, m_unlock_listener);
    if (lock->m_unlocking) return;
    lock->m_unlocking = true;
    lock->m_server->unlock_session();
}

void SessionLock::handle_destroy(struct wl_listener* listener, void* data) {
    SessionLock* lock = wl_container_of(listener, lock, m_destroy_listener);
    if (lock->m_unlocking) return;
    lock->m_unlocking = true;
    lock->m_server->unlock_session();
}

void SessionLock::remove_surface(SessionLockSurface* surface) {
    for (auto it = m_surfaces.begin(); it != m_surfaces.end(); ++it) {
        if (it->get() == surface) {
            m_surfaces.erase(it);
            break;
        }
    }
}

void SessionLock::check_and_send_locked() {
    if (!m_wlr_lock || m_locked_sent) return;

    if (m_surfaces.empty()) return;

    // Verify all active (enabled) outputs have a configured lock surface
    if (m_server && m_server->get_output_manager()) {
        const auto& outputs = m_server->get_output_manager()->get_outputs();
        for (const auto& out : outputs) {
            if (!out || !out->get_wlr_output() || !out->get_wlr_output()->enabled) {
                continue;
            }
            bool found = false;
            for (const auto& s : m_surfaces) {
                if (s && s->get_wlr_lock_surface() && s->get_wlr_lock_surface()->output == out->get_wlr_output()) {
                    if (s->is_configured()) {
                        found = true;
                    }
                    break;
                }
            }
            if (!found) {
                return;
            }
        }
    }

    for (const auto& s : m_surfaces) {
        if (!s || !s->is_configured()) return;
    }

    m_locked_sent = true;
    wlr_session_lock_v1_send_locked(m_wlr_lock);

    if (!m_surfaces.empty()) {
        m_surfaces.front()->focus();
    }
}

void SessionLock::update_blur() {
    for (const auto& surface : m_surfaces) {
        if (surface) {
            surface->update_blur();
        }
    }
}

struct wlr_surface* SessionLock::get_active_surface() const {
    if (m_surfaces.empty()) return nullptr;
    return m_surfaces.front()->get_wlr_surface();
}

SessionLockSurface* SessionLock::surface_at(double lx, double ly, double* sx, double* sy) {
    for (const auto& s : m_surfaces) {
        if (!s || !s->get_wlr_lock_surface() || !s->get_wlr_surface()) continue;
        struct wlr_output* output = s->get_wlr_lock_surface()->output;
        if (!output || !output->enabled) continue;
        struct wlr_output_layout* layout = m_server->get_output_manager()->get_layout();
        struct wlr_output_layout_output* layout_output = wlr_output_layout_get(layout, output);
        if (layout_output) {
            int ox = layout_output->x;
            int oy = layout_output->y;
            int ow = output->width;
            int oh = output->height;
            wlr_output_effective_resolution(output, &ow, &oh);
            if (lx >= ox && lx < ox + ow && ly >= oy && ly < oy + oh) {
                if (sx) *sx = lx - ox;
                if (sy) *sy = ly - oy;
                return s.get();
            }
        }
    }
    return nullptr;
}

} // namespace miquland
