#pragma once

#include "core/common/util.hpp"
#include <vector>
#include <memory>

namespace miquland {

class Server;
class SessionLock;

class SessionLockSurface {
public:
    SessionLockSurface(SessionLock* lock, struct wlr_session_lock_surface_v1* lock_surface);
    ~SessionLockSurface();

    struct wlr_session_lock_surface_v1* get_wlr_lock_surface() const { return m_lock_surface; }
    struct wlr_surface* get_wlr_surface() const { return m_lock_surface ? m_lock_surface->surface : nullptr; }
    struct wlr_scene_tree* get_scene_tree() const { return m_scene_tree; }
    bool is_configured() const { return m_configured; }

    void focus();

private:
    static void handle_destroy(struct wl_listener* listener, void* data);
    static void handle_surface_commit(struct wl_listener* listener, void* data);

    SessionLock* m_lock = nullptr;
    Server* m_server = nullptr;
    struct wlr_session_lock_surface_v1* m_lock_surface = nullptr;
    struct wlr_scene_tree* m_scene_tree = nullptr;
    bool m_configured = false;

    struct wl_listener m_destroy_listener;
    struct wl_listener m_surface_commit_listener;
};

class SessionLock {
public:
    SessionLock(Server* server, struct wlr_session_lock_v1* lock);
    ~SessionLock();

    struct wlr_session_lock_v1* get_wlr_lock() const { return m_wlr_lock; }
    Server* get_server() const { return m_server; }

    void remove_surface(SessionLockSurface* surface);
    void check_and_send_locked();
    bool is_locked_sent() const { return m_locked_sent; }

    struct wlr_surface* get_active_surface() const;
    SessionLockSurface* surface_at(double lx, double ly, double* sx, double* sy);

private:
    static void handle_new_surface(struct wl_listener* listener, void* data);
    static void handle_unlock(struct wl_listener* listener, void* data);
    static void handle_destroy(struct wl_listener* listener, void* data);

    Server* m_server = nullptr;
    struct wlr_session_lock_v1* m_wlr_lock = nullptr;
    std::vector<std::unique_ptr<SessionLockSurface>> m_surfaces;

    struct wl_listener m_new_surface_listener;
    struct wl_listener m_unlock_listener;
    struct wl_listener m_destroy_listener;
    bool m_locked_sent = false;
    bool m_unlocking = false;
};

} // namespace miquland
