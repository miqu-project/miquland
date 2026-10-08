#include "core/server.hpp"
#include "core/output.hpp"
#include "core/workspace.hpp"
#include "core/input/input.hpp"
#include "core/view.hpp"
#include "core/layer_surface.hpp"
#include "core/popup.hpp"
#include "core/session_lock.hpp"
#include "core/plugin_manager.hpp"
#include "core/animation/animation_manager.hpp"
#include "core/config/config.hpp"
extern "C" {
#include <scenefx/render/fx_renderer/fx_renderer.h>
}
#include <sys/inotify.h>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>
#include <iostream>

namespace miquland {

Server::Server() {
    wl_list_init(&m_new_xdg_toplevel_listener.link);
    wl_list_init(&m_new_layer_shell_surface_listener.link);
    wl_list_init(&m_session_lock_new_lock_listener.link);
    wl_list_init(&m_ext_workspace_commit_listener.link);
    wl_list_init(&m_xwayland_ready_listener.link);
    wl_list_init(&m_xwayland_new_surface_listener.link);
    wl_list_init(&m_gamma_set_gamma_listener.link);
    wl_list_init(&m_output_power_set_mode_listener.link);
    wl_list_init(&m_new_idle_inhibitor_listener.link);
    wl_list_init(&m_new_xdg_decoration_listener.link);
    wl_list_init(&m_xdg_activation_request_activate_listener.link);
    wl_list_init(&m_cursor_shape_request_set_shape_listener.link);
}

Server::~Server() {
    if (m_config_reload_timer) wl_event_source_remove(m_config_reload_timer);
    if (m_config_event_source) wl_event_source_remove(m_config_event_source);
    if (m_inotify_file_wd >= 0 && m_inotify_fd >= 0) inotify_rm_watch(m_inotify_fd, m_inotify_file_wd);
    if (m_inotify_wd >= 0 && m_inotify_fd >= 0) inotify_rm_watch(m_inotify_fd, m_inotify_wd);
    if (m_inotify_fd >= 0) ::close(m_inotify_fd);

    if (m_plugin_manager) {
        m_plugin_manager->unload_all();
        m_plugin_manager.reset();
    }

    m_layer_surfaces.clear();
    m_views.clear();

    m_animation_manager.reset();
    m_input_manager.reset();
    m_workspace_manager.reset();
    m_output_manager.reset();

    if (m_foreign_toplevel_manager) {
        // Destroyed with display
    }
    if (!wl_list_empty(&m_xwayland_ready_listener.link)) wl_list_remove(&m_xwayland_ready_listener.link);
    if (!wl_list_empty(&m_xwayland_new_surface_listener.link)) wl_list_remove(&m_xwayland_new_surface_listener.link);
    if (m_xwayland) {
        wlr_xwayland_destroy(m_xwayland);
        m_xwayland = nullptr;
    }

    if (!wl_list_empty(&m_new_xdg_toplevel_listener.link)) wl_list_remove(&m_new_xdg_toplevel_listener.link);
    if (!wl_list_empty(&m_new_layer_shell_surface_listener.link)) wl_list_remove(&m_new_layer_shell_surface_listener.link);
    if (!wl_list_empty(&m_session_lock_new_lock_listener.link)) wl_list_remove(&m_session_lock_new_lock_listener.link);
    if (!wl_list_empty(&m_ext_workspace_commit_listener.link)) wl_list_remove(&m_ext_workspace_commit_listener.link);
    if (!wl_list_empty(&m_gamma_set_gamma_listener.link)) wl_list_remove(&m_gamma_set_gamma_listener.link);
    if (!wl_list_empty(&m_output_power_set_mode_listener.link)) wl_list_remove(&m_output_power_set_mode_listener.link);
    if (!wl_list_empty(&m_new_idle_inhibitor_listener.link)) wl_list_remove(&m_new_idle_inhibitor_listener.link);
    if (!wl_list_empty(&m_new_xdg_decoration_listener.link)) wl_list_remove(&m_new_xdg_decoration_listener.link);
    if (!wl_list_empty(&m_xdg_activation_request_activate_listener.link)) wl_list_remove(&m_xdg_activation_request_activate_listener.link);
    if (!wl_list_empty(&m_cursor_shape_request_set_shape_listener.link)) wl_list_remove(&m_cursor_shape_request_set_shape_listener.link);

    if (m_wlr_allocator) wlr_allocator_destroy(m_wlr_allocator);
    if (m_wlr_renderer) wlr_renderer_destroy(m_wlr_renderer);
    if (m_wlr_backend) wlr_backend_destroy(m_wlr_backend);
    if (m_wl_display) wl_display_destroy(m_wl_display);
}

bool Server::init() {
    wlr_log_init(WLR_DEBUG, nullptr);

    m_wl_display = wl_display_create();
    if (!m_wl_display) {
        log_error("Failed to create Wayland display");
        return false;
    }

    m_wl_event_loop = wl_display_get_event_loop(m_wl_display);

    m_wlr_backend = wlr_backend_autocreate(m_wl_event_loop, nullptr);
    if (!m_wlr_backend) {
        log_error("Failed to create wlr_backend");
        return false;
    }

    m_wlr_renderer = fx_renderer_create(m_wlr_backend);
    if (!m_wlr_renderer) {
        log_warn("fx_renderer_create failed, falling back to wlr_renderer_autocreate");
        m_wlr_renderer = wlr_renderer_autocreate(m_wlr_backend);
    }
    if (!m_wlr_renderer) {
        log_error("Failed to create wlr_renderer");
        return false;
    }

    wlr_renderer_init_wl_display(m_wlr_renderer, m_wl_display);

    m_wlr_allocator = wlr_allocator_autocreate(m_wlr_backend, m_wlr_renderer);
    if (!m_wlr_allocator) {
        log_error("Failed to create wlr_allocator");
        return false;
    }

    m_wlr_compositor = wlr_compositor_create(m_wl_display, 6, m_wlr_renderer);
    if (!m_wlr_compositor) {
        log_error("Failed to create wlr_compositor");
        return false;
    }
    m_wlr_subcompositor = wlr_subcompositor_create(m_wl_display);
    if (!m_wlr_subcompositor) {
        log_error("Failed to create wlr_subcompositor");
        return false;
    }
    m_wlr_data_device_manager = wlr_data_device_manager_create(m_wl_display);
    if (!m_wlr_data_device_manager) {
        log_error("Failed to create wlr_data_device_manager");
        return false;
    }

    wlr_data_control_manager_v1_create(m_wl_display);
    wlr_primary_selection_v1_device_manager_create(m_wl_display);

    m_scene = wlr_scene_create();
    if (!m_scene) {
        log_error("Failed to create wlr_scene");
        return false;
    }

    if (Config::get().is_blur_enabled()) {
        wlr_scene_set_blur_data(m_scene,
            Config::get().get_blur_num_passes(),
            Config::get().get_blur_radius(),
            Config::get().get_blur_noise(),
            Config::get().get_blur_brightness(),
            Config::get().get_blur_contrast(),
            Config::get().get_blur_saturation());
        log_info("SceneFX Dual Kawase background blur initialized");
    }

    // Standard Layer Scene Tree Hierarchy:
    // Background -> Bottom -> Workspaces (Tiled Apps) -> Top -> Overlay -> Session Lock
    m_layer_background_tree = wlr_scene_tree_create(&m_scene->tree);
    m_layer_bottom_tree = wlr_scene_tree_create(&m_scene->tree);
    m_workspaces_tree = wlr_scene_tree_create(&m_scene->tree);
    m_layer_top_tree = wlr_scene_tree_create(&m_scene->tree);
    m_layer_overlay_tree = wlr_scene_tree_create(&m_scene->tree);
    m_popup_tree = wlr_scene_tree_create(&m_scene->tree);
    m_session_lock_tree = wlr_scene_tree_create(&m_scene->tree);

    m_session_lock_manager = wlr_session_lock_manager_v1_create(m_wl_display);
    if (m_session_lock_manager) {
        m_session_lock_new_lock_listener.notify = handle_new_session_lock;
        wl_signal_add(&m_session_lock_manager->events.new_lock, &m_session_lock_new_lock_listener);
    }

    m_ext_workspace_manager = wlr_ext_workspace_manager_v1_create(m_wl_display, 1);
    if (m_ext_workspace_manager) {
        m_ext_workspace_group = wlr_ext_workspace_group_handle_v1_create(m_ext_workspace_manager, 0);
        m_ext_workspace_commit_listener.notify = handle_ext_workspace_commit;
        wl_signal_add(&m_ext_workspace_manager->events.commit, &m_ext_workspace_commit_listener);
    }

    m_output_manager = std::make_unique<OutputManager>(this);
    m_workspace_manager = std::make_unique<WorkspaceManager>(this);
    m_input_manager = std::make_unique<InputManager>(this);
    m_animation_manager = std::make_unique<AnimationManager>(this);

    m_xdg_output_manager = wlr_xdg_output_manager_v1_create(m_wl_display, m_output_manager->get_layout());

    m_xdg_shell = wlr_xdg_shell_create(m_wl_display, 6);
    m_new_xdg_toplevel_listener.notify = handle_new_xdg_toplevel;
    wl_signal_add(&m_xdg_shell->events.new_toplevel, &m_new_xdg_toplevel_listener);

    m_layer_shell = wlr_layer_shell_v1_create(m_wl_display, 5);
    m_new_layer_shell_surface_listener.notify = handle_new_layer_shell_surface;
    wl_signal_add(&m_layer_shell->events.new_surface, &m_new_layer_shell_surface_listener);

    m_foreign_toplevel_manager = wlr_foreign_toplevel_manager_v1_create(m_wl_display);
    m_pointer_gestures = wlr_pointer_gestures_v1_create(m_wl_display);

    // Protocols
    m_screencopy_manager = wlr_screencopy_manager_v1_create(m_wl_display);

    m_gamma_control_manager = wlr_gamma_control_manager_v1_create(m_wl_display);
    if (m_gamma_control_manager) {
        m_gamma_set_gamma_listener.notify = handle_gamma_set_gamma;
        wl_signal_add(&m_gamma_control_manager->events.set_gamma, &m_gamma_set_gamma_listener);
    }

    m_output_power_manager = wlr_output_power_manager_v1_create(m_wl_display);
    if (m_output_power_manager) {
        m_output_power_set_mode_listener.notify = handle_output_power_set_mode;
        wl_signal_add(&m_output_power_manager->events.set_mode, &m_output_power_set_mode_listener);
    }

    m_idle_notifier = wlr_idle_notifier_v1_create(m_wl_display);
    m_idle_inhibit_manager = wlr_idle_inhibit_v1_create(m_wl_display);
    if (m_idle_inhibit_manager) {
        m_new_idle_inhibitor_listener.notify = handle_new_idle_inhibitor;
        wl_signal_add(&m_idle_inhibit_manager->events.new_inhibitor, &m_new_idle_inhibitor_listener);
    }

    m_xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(m_wl_display);
    if (m_xdg_decoration_manager) {
        m_new_xdg_decoration_listener.notify = handle_new_xdg_decoration;
        wl_signal_add(&m_xdg_decoration_manager->events.new_toplevel_decoration, &m_new_xdg_decoration_listener);
    }

    m_xdg_activation = wlr_xdg_activation_v1_create(m_wl_display);
    if (m_xdg_activation) {
        m_xdg_activation_request_activate_listener.notify = handle_xdg_activation_request_activate;
        wl_signal_add(&m_xdg_activation->events.request_activate, &m_xdg_activation_request_activate_listener);
    }

    m_cursor_shape_manager = wlr_cursor_shape_manager_v1_create(m_wl_display, 1);
    if (m_cursor_shape_manager) {
        m_cursor_shape_request_set_shape_listener.notify = handle_cursor_shape_request_set_shape;
        wl_signal_add(&m_cursor_shape_manager->events.request_set_shape, &m_cursor_shape_request_set_shape_listener);
    }

    m_relative_pointer_manager = wlr_relative_pointer_manager_v1_create(m_wl_display);
    m_pointer_constraints = wlr_pointer_constraints_v1_create(m_wl_display);

    m_viewporter = wlr_viewporter_create(m_wl_display);
    m_fractional_scale_manager = wlr_fractional_scale_manager_v1_create(m_wl_display, 1);
    m_tearing_manager = wlr_tearing_control_manager_v1_create(m_wl_display, 1);
    if (m_tearing_manager) {
        log_info("Initialized tearing control protocol manager v1");
    }

    m_xwayland = wlr_xwayland_create(m_wl_display, m_wlr_compositor, true);
    if (m_xwayland) {
        wlr_xwayland_set_seat(m_xwayland, m_input_manager->get_seat());

        m_xwayland_ready_listener.notify = handle_xwayland_ready;
        wl_signal_add(&m_xwayland->events.ready, &m_xwayland_ready_listener);

        m_xwayland_new_surface_listener.notify = handle_xwayland_new_surface;
        wl_signal_add(&m_xwayland->events.new_surface, &m_xwayland_new_surface_listener);

        if (m_xwayland->display_name) {
            setenv("DISPLAY", m_xwayland->display_name, 1);
            log_info("Xwayland support initialized on DISPLAY=" + std::string(m_xwayland->display_name));
        } else {
            log_info("Xwayland support initialized");
        }
    }

    m_socket_name = wl_display_add_socket_auto(m_wl_display);
    if (!m_socket_name) {
        log_error("Failed to add Wayland socket");
        return false;
    }

    log_info("Wayland compositor running on WAYLAND_DISPLAY=" + std::string(m_socket_name));
    setenv("WAYLAND_DISPLAY", m_socket_name, 1);
    setenv("XDG_CURRENT_DESKTOP", "miquland", 1);
    spawn_async_command({"systemctl", "--user", "import-environment", "WAYLAND_DISPLAY", "DISPLAY", "XDG_CURRENT_DESKTOP", "XCURSOR_THEME", "XCURSOR_SIZE"});

    setup_config_watcher();

    m_plugin_manager = std::make_unique<PluginManager>(this);
    m_plugin_manager->load_configured_plugins();

    // Autostart commands from configuration
    for (const auto& cmd : Config::get().get_exec_once_commands()) {
        m_input_manager->spawn_command(cmd.c_str());
    }
    for (const auto& cmd : Config::get().get_exec_commands()) {
        m_input_manager->spawn_command(cmd.c_str());
    }

    return true;
}

void Server::setup_config_watcher() {
    m_inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_inotify_fd < 0) return;

    std::string config_dir = Config::get_config_dir_path();
    m_inotify_wd = inotify_add_watch(m_inotify_fd, config_dir.c_str(), IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
    if (m_inotify_wd < 0) {
        log_warn("Failed to watch config directory: " + config_dir);
    } else {
        log_info("Watching config directory for live reload: " + config_dir);
    }

    std::string config_file = Config::get_config_file_path();
    m_inotify_file_wd = inotify_add_watch(m_inotify_fd, config_file.c_str(), IN_MODIFY | IN_CLOSE_WRITE);

    m_config_event_source = wl_event_loop_add_fd(m_wl_event_loop, m_inotify_fd, WL_EVENT_READABLE, handle_config_inotify, this);
    m_config_reload_timer = wl_event_loop_add_timer(m_wl_event_loop, handle_config_reload_timer, this);
}

int Server::handle_config_inotify(int fd, uint32_t mask, void* data) {
    auto* server = static_cast<Server*>(data);
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t len;

    bool should_reload = false;
    while ((len = read(fd, buf, sizeof(buf))) > 0) {
        const struct inotify_event* event;
        for (char* ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
            event = reinterpret_cast<const struct inotify_event*>(ptr);
            if (server->m_inotify_file_wd >= 0 && event->wd == server->m_inotify_file_wd) {
                should_reload = true;
                if (event->mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF)) {
                    server->m_inotify_file_wd = -1;
                }
            } else if (event->len > 0) {
                std::string filename(event->name);
                if ((filename == "miquland.conf" || filename.ends_with(".conf")) &&
                    !filename.starts_with(".") && !filename.ends_with("~")) {
                    should_reload = true;
                }
            }
        }
    }

    // Re-arm file watch if it was removed by atomic editor rewrite
    if (server->m_inotify_file_wd < 0 && server->m_inotify_fd >= 0) {
        std::string config_file = Config::get_config_file_path();
        server->m_inotify_file_wd = inotify_add_watch(server->m_inotify_fd, config_file.c_str(), IN_MODIFY | IN_CLOSE_WRITE);
    }

    if (should_reload) {
        // Debounce: wait 250ms for rapid inotify events / editor write flushes to settle
        if (server->m_config_reload_timer) {
            wl_event_source_timer_update(server->m_config_reload_timer, 250);
        } else {
            server->reload_config();
        }
    }
    return 0;
}

int Server::handle_config_reload_timer(void* data) {
    auto* server = static_cast<Server*>(data);
    log_info("Detected config file change, reloading...");
    server->reload_config();
    return 0;
}

void Server::reload_config() {
    Config::get().load();

    if (m_output_manager) {
        m_output_manager->reapply_all_configs();
    }

    if (m_input_manager) {
        m_input_manager->reapply_device_config();
    }

    for (const auto& v : m_views) {
        if (v && v->is_mapped()) {
            v->update_frame();
        }
    }

    if (m_output_manager) {
        for (const auto& out : m_output_manager->get_outputs()) {
            if (out && out->get_wlr_output()) {
                arrange_layers(out->get_wlr_output());
            }
        }
    }

    if (m_workspace_manager) {
        m_workspace_manager->recalculate_layout();
    }

    if (m_plugin_manager) {
        m_plugin_manager->unload_all();
        m_plugin_manager->load_configured_plugins();
    }

    for (const auto& cmd : Config::get().get_exec_commands()) {
        if (m_input_manager) {
            m_input_manager->spawn_command(cmd.c_str());
        }
    }
}

void Server::handle_new_xdg_toplevel(struct wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, m_new_xdg_toplevel_listener);
    auto* toplevel = static_cast<struct wlr_xdg_toplevel*>(data);

    auto view = std::make_unique<View>(server, toplevel);
    server->add_view(std::move(view));
}

void Server::run() {
    if (!wlr_backend_start(m_wlr_backend)) {
        log_error("Failed to start backend");
        return;
    }

    if (!m_startup_cmd.empty() && m_input_manager) {
        m_input_manager->spawn_command(m_startup_cmd.c_str());
    }

    log_info("Running Wayland event loop");
    wl_display_run(m_wl_display);
}

void Server::terminate() {
    log_info("Terminating miquland compositor");
    if (m_wl_display) {
        wl_display_terminate(m_wl_display);
    }
}

void Server::add_view(std::unique_ptr<View> view) {
    m_views.push_back(std::move(view));
}

void Server::remove_view(View* view) {
    bool was_focused = (m_focused_view == view);
    if (was_focused) {
        m_focused_view = nullptr;
    }

    for (auto it = m_views.begin(); it != m_views.end(); ++it) {
        if (it->get() == view) {
            m_views.erase(it);
            break;
        }
    }

    if (was_focused && m_workspace_manager) {
        Workspace* ws = m_workspace_manager->get_active_workspace();
        if (ws) {
            View* best = m_workspace_manager->find_best_focus_view(ws);
            if (best) {
                best->focus();
            }
        }
    }
}

bool Server::is_valid_view(const View* view) const {
    if (!view) return false;
    for (const auto& v : m_views) {
        if (v.get() == view) return true;
    }
    return false;
}

bool Server::is_valid_view_ptr(const void* ptr) const {
    if (!ptr) return false;
    for (const auto& v : m_views) {
        if (static_cast<const void*>(v.get()) == ptr) return true;
    }
    return false;
}

View* Server::get_view_by_id(uint64_t id) const {
    if (id == 0) return nullptr;
    for (const auto& v : m_views) {
        if (v && v->get_id() == id) return v.get();
    }
    return nullptr;
}

void Server::set_focused_view(View* view) {
    if (m_focused_view == view) return;
    View* prev = m_focused_view;
    m_focused_view = view;
    if (prev) {
        prev->update_frame();
        if (view == nullptr && prev->get_foreign_toplevel()) {
            wlr_foreign_toplevel_handle_v1_set_activated(prev->get_foreign_toplevel(), false);
        }
    }
    if (m_focused_view) m_focused_view->update_frame();
}

View* Server::view_at(double lx, double ly, struct wlr_surface** surface, double* sx, double* sy) {
    struct wlr_scene_node* node = wlr_scene_node_at(&m_scene->tree.node, lx, ly, sx, sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
        return nullptr;
    }

    auto* scene_buffer = wlr_scene_buffer_from_node(node);
    auto* scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
    if (scene_surface) {
        *surface = scene_surface->surface;
    } else {
        *surface = nullptr;
    }

    // Find the View associated with this scene node hierarchy
    struct wlr_scene_tree* tree = node->parent;
    while (tree != nullptr) {
        if (tree->node.data != nullptr) {
            if (is_valid_view_ptr(tree->node.data)) {
                auto* possible_view = static_cast<View*>(tree->node.data);
                if (!possible_view->is_animating_close()) {
                    return possible_view;
                }
            }
        }
        tree = tree->node.parent;
    }

    return nullptr;
}

struct wlr_scene_tree* Server::get_layer_tree(enum zwlr_layer_shell_v1_layer layer) const {
    switch (layer) {
        case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
            return m_layer_background_tree;
        case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
            return m_layer_bottom_tree;
        case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
            return m_layer_top_tree;
        case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
        default:
            return m_layer_overlay_tree;
    }
}

void Server::add_layer_surface(std::unique_ptr<LayerSurface> surface) {
    m_layer_surfaces.push_back(std::move(surface));
}

void Server::remove_layer_surface(LayerSurface* surface) {
    if (m_focused_layer_surface == surface) {
        m_focused_layer_surface = nullptr;
        if (m_focused_view) {
            m_focused_view->focus();
        }
    }

    for (auto it = m_layer_surfaces.begin(); it != m_layer_surfaces.end(); ++it) {
        if (it->get() == surface) {
            m_layer_surfaces.erase(it);
            break;
        }
    }
}

void Server::focus_layer_surface(LayerSurface* surface) {
    if (surface && surface->get_wlr_layer_surface()) {
        m_focused_layer_surface = surface;
        struct wlr_surface* wlr_surf = surface->get_wlr_layer_surface()->surface;
        struct wlr_seat* seat = m_input_manager->get_seat();
        struct wlr_keyboard* kb = wlr_seat_get_keyboard(seat);
        if (kb && wlr_surf) {
            wlr_seat_keyboard_notify_enter(seat, wlr_surf, kb->keycodes, kb->num_keycodes, &kb->modifiers);
        }
    } else {
        m_focused_layer_surface = nullptr;
        if (m_focused_view) {
            m_focused_view->focus();
        }
    }
}

void Server::arrange_layers(struct wlr_output* output) {
    if (!output) return;

    int eff_w = 0, eff_h = 0;
    wlr_output_effective_resolution(output, &eff_w, &eff_h);

    struct wlr_box full_area = {
        .x = 0,
        .y = 0,
        .width = (eff_w > 0) ? eff_w : output->width,
        .height = (eff_h > 0) ? eff_h : output->height
    };
    struct wlr_box usable_area = full_area;

    const enum zwlr_layer_shell_v1_layer layers_order[] = {
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND
    };

    struct wlr_output* primary_output = nullptr;
    if (m_output_manager) {
        auto* primary = m_output_manager->get_primary_output();
        if (primary) primary_output = primary->get_wlr_output();
    }

    for (auto layer : layers_order) {
        for (auto& l_surf : m_layer_surfaces) {
            if (!l_surf || !l_surf->get_wlr_layer_surface()) continue;
            struct wlr_output* surf_out = l_surf->get_wlr_layer_surface()->output;
            if (!surf_out) {
                surf_out = primary_output;
            }
            if (l_surf->get_layer() == layer && surf_out == output) {
                if (layer == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) {
                    struct wlr_box bg_usable = full_area;
                    l_surf->configure(&full_area, &bg_usable);
                } else {
                    l_surf->configure(&full_area, &usable_area);
                }
            }
        }
    }

    bool area_changed = false;
    if (m_output_manager) {
        auto* out = m_output_manager->find_output(output);
        if (out) {
            const auto& prev = out->get_usable_area();
            area_changed = (prev.x != usable_area.x || prev.y != usable_area.y ||
                            prev.width != usable_area.width || prev.height != usable_area.height);
            out->set_usable_area(usable_area);
        }
    }

    if (area_changed && m_workspace_manager) {
        m_workspace_manager->recalculate_layout();
    }
}

void Server::handle_new_layer_shell_surface(struct wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, m_new_layer_shell_surface_listener);
    auto* wlr_layer_surface = static_cast<struct wlr_layer_surface_v1*>(data);

    auto layer_surface = std::make_unique<LayerSurface>(server, wlr_layer_surface);
    server->add_layer_surface(std::move(layer_surface));
}

void Server::handle_ext_workspace_commit(struct wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, m_ext_workspace_commit_listener);
    auto* event = static_cast<struct wlr_ext_workspace_v1_commit_event*>(data);

    struct wlr_ext_workspace_v1_request* req;
    wl_list_for_each(req, event->requests, link) {
        if (req->type == WLR_EXT_WORKSPACE_V1_REQUEST_ACTIVATE && req->activate.workspace) {
            auto* ws = static_cast<Workspace*>(req->activate.workspace->data);
            if (ws && server->m_workspace_manager) {
                server->m_workspace_manager->switch_to_workspace(ws->get_id());
            }
        }
    }
}

void Server::handle_xwayland_ready(struct wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, m_xwayland_ready_listener);
    if (server->m_xwayland && server->m_xwayland->display_name) {
        setenv("DISPLAY", server->m_xwayland->display_name, 1);
        log_info("Xwayland server is ready on DISPLAY=" + std::string(server->m_xwayland->display_name));
        spawn_async_command({"systemctl", "--user", "import-environment", "DISPLAY"});
    }
}

void Server::handle_xwayland_new_surface(struct wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, m_xwayland_new_surface_listener);
    auto* xsurface = static_cast<struct wlr_xwayland_surface*>(data);

    auto view = std::make_unique<View>(server, xsurface);
    server->add_view(std::move(view));
}

bool Server::is_locked() const {
    return m_session_lock != nullptr;
}

SessionLock* Server::get_session_lock() const {
    return m_session_lock.get();
}

void Server::handle_new_session_lock(struct wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, m_session_lock_new_lock_listener);
    auto* lock = static_cast<struct wlr_session_lock_v1*>(data);
    if (server->m_session_lock) {
        log_warn("Session lock requested while already locked; rejecting duplicate request");
        wlr_session_lock_v1_destroy(lock);
        return;
    }

    log_info("Activating new session lock");
    server->m_session_lock = std::make_unique<SessionLock>(server, lock);
}

void Server::unlock_session() {
    if (!m_session_lock) return;
    log_info("Session unlocked successfully");
    m_session_lock.reset();

    if (m_workspace_manager) {
        Workspace* ws = m_workspace_manager->get_active_workspace();
        if (ws) {
            View* best = m_workspace_manager->find_best_focus_view(ws);
            if (best) best->focus();
        }
    }
}

void Server::handle_gamma_set_gamma(struct wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, m_gamma_set_gamma_listener);
    auto* event = static_cast<struct wlr_gamma_control_manager_v1_set_gamma_event*>(data);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    if (!wlr_gamma_control_v1_apply(event->control, &state)) {
        wlr_output_state_finish(&state);
        return;
    }
    if (!wlr_output_commit_state(event->output, &state)) {
        wlr_gamma_control_v1_send_failed_and_destroy(event->control);
    }
    wlr_output_state_finish(&state);
}

void Server::handle_output_power_set_mode(struct wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, m_output_power_set_mode_listener);
    auto* event = static_cast<struct wlr_output_power_v1_set_mode_event*>(data);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON);
    wlr_output_commit_state(event->output, &state);
    wlr_output_state_finish(&state);
}

struct IdleInhibitorWrapper {
    Server* server = nullptr;
    struct wl_listener destroy;

    static void handle_destroy(struct wl_listener* listener, void* data) {
        IdleInhibitorWrapper* wrapper = wl_container_of(listener, wrapper, destroy);
        if (wrapper->server) {
            wrapper->server->handle_idle_inhibitor_destroy();
        }
        wl_list_remove(&wrapper->destroy.link);
        delete wrapper;
    }
};

void Server::handle_idle_inhibitor_destroy() {
    if (m_idle_inhibitor_count > 0) {
        m_idle_inhibitor_count--;
    }
    if (m_idle_notifier) {
        wlr_idle_notifier_v1_set_inhibited(m_idle_notifier, m_idle_inhibitor_count > 0);
    }
}

void Server::handle_new_idle_inhibitor(struct wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, m_new_idle_inhibitor_listener);
    auto* inhibitor = static_cast<struct wlr_idle_inhibitor_v1*>(data);

    server->m_idle_inhibitor_count++;
    if (server->m_idle_notifier) {
        wlr_idle_notifier_v1_set_inhibited(server->m_idle_notifier, true);
    }

    auto* wrapper = new IdleInhibitorWrapper();
    wrapper->server = server;
    wrapper->destroy.notify = IdleInhibitorWrapper::handle_destroy;
    wl_signal_add(&inhibitor->events.destroy, &wrapper->destroy);
}

struct DecorationWrapper {
    struct wlr_xdg_toplevel_decoration_v1* decoration = nullptr;
    struct wl_listener request_mode;
    struct wl_listener surface_commit;
    struct wl_listener destroy;

    static void handle_request_mode(struct wl_listener* listener, void* data) {
        DecorationWrapper* wrapper = wl_container_of(listener, wrapper, request_mode);
        if (wrapper->decoration && wrapper->decoration->toplevel &&
            wrapper->decoration->toplevel->base &&
            wrapper->decoration->toplevel->base->initialized) {
            wlr_xdg_toplevel_decoration_v1_set_mode(wrapper->decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        }
    }

    static void handle_commit(struct wl_listener* listener, void* data) {
        DecorationWrapper* wrapper = wl_container_of(listener, wrapper, surface_commit);
        if (wrapper->decoration && wrapper->decoration->toplevel &&
            wrapper->decoration->toplevel->base &&
            wrapper->decoration->toplevel->base->initialized) {
            if (wrapper->decoration->current.mode != WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE &&
                wrapper->decoration->scheduled_mode != WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE) {
                wlr_xdg_toplevel_decoration_v1_set_mode(wrapper->decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
            }
            wl_list_remove(&wrapper->surface_commit.link);
            wl_list_init(&wrapper->surface_commit.link);
        }
    }

    static void handle_destroy(struct wl_listener* listener, void* data) {
        DecorationWrapper* wrapper = wl_container_of(listener, wrapper, destroy);
        wl_list_remove(&wrapper->request_mode.link);
        wl_list_remove(&wrapper->surface_commit.link);
        wl_list_remove(&wrapper->destroy.link);
        delete wrapper;
    }
};

void Server::handle_new_xdg_decoration(struct wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, m_new_xdg_decoration_listener);
    auto* decoration = static_cast<struct wlr_xdg_toplevel_decoration_v1*>(data);

    auto* wrapper = new DecorationWrapper();
    wrapper->decoration = decoration;

    wrapper->request_mode.notify = DecorationWrapper::handle_request_mode;
    wl_signal_add(&decoration->events.request_mode, &wrapper->request_mode);

    wrapper->surface_commit.notify = DecorationWrapper::handle_commit;
    if (decoration->toplevel && decoration->toplevel->base && decoration->toplevel->base->surface) {
        wl_signal_add(&decoration->toplevel->base->surface->events.commit, &wrapper->surface_commit);
    } else {
        wl_list_init(&wrapper->surface_commit.link);
    }

    wrapper->destroy.notify = DecorationWrapper::handle_destroy;
    wl_signal_add(&decoration->events.destroy, &wrapper->destroy);

    if (decoration->toplevel && decoration->toplevel->base && decoration->toplevel->base->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
}

void Server::handle_xdg_activation_request_activate(struct wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, m_xdg_activation_request_activate_listener);
    auto* event = static_cast<struct wlr_xdg_activation_v1_request_activate_event*>(data);

    if (!event || !event->surface) return;

    for (const auto& v : server->m_views) {
        if (!v || !v->is_mapped()) continue;

        struct wlr_surface* view_surf = nullptr;
        if (v->get_type() == ViewType::Xdg && v->get_xdg_toplevel() && v->get_xdg_toplevel()->base) {
            view_surf = v->get_xdg_toplevel()->base->surface;
        } else if (v->get_type() == ViewType::XWayland && v->get_xwayland_surface()) {
            view_surf = v->get_xwayland_surface()->surface;
        }

        if (view_surf == event->surface) {
            if (v->get_workspace() && server->get_workspace_manager()) {
                server->get_workspace_manager()->switch_to_workspace(v->get_workspace()->get_id(), v.get());
            } else {
                v->focus();
            }
            break;
        }
    }
}

void Server::handle_cursor_shape_request_set_shape(struct wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, m_cursor_shape_request_set_shape_listener);
    auto* event = static_cast<struct wlr_cursor_shape_manager_v1_request_set_shape_event*>(data);

    if (!server->m_input_manager) return;

    struct wlr_seat_client* focused_client = server->m_input_manager->get_seat()->pointer_state.focused_client;
    if (focused_client == event->seat_client) {
        const char* name = wlr_cursor_shape_v1_name(event->shape);
        server->m_input_manager->set_cursor_icon(name);
    }
}

} // namespace miquland
