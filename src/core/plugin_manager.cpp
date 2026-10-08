#include "core/plugin_manager.hpp"
#include "core/server.hpp"
#include "core/workspace.hpp"
#include "core/output.hpp"
#include "core/config/config.hpp"
#include "core/common/util.hpp"

#include <dlfcn.h>
#include <iostream>

namespace {
struct PluginDispatchGuard {
    int& counter;
    explicit PluginDispatchGuard(int& c) : counter(c) { counter++; }
    ~PluginDispatchGuard() { counter--; }
};
}

namespace miquland {

PluginManager::PluginManager(Server* server)
    : m_server(server) {}

PluginManager::~PluginManager() {
    if (m_idle_reload_source) {
        wl_event_source_remove(m_idle_reload_source);
        m_idle_reload_source = nullptr;
    }
    unload_all();
}

WorkspaceManager* PluginManager::get_workspace_manager() {
    return m_server ? m_server->get_workspace_manager() : nullptr;
}

OutputManager* PluginManager::get_output_manager() {
    return m_server ? m_server->get_output_manager() : nullptr;
}

struct wlr_scene* PluginManager::get_scene() {
    return m_server ? m_server->get_scene() : nullptr;
}

struct wlr_scene_tree* PluginManager::get_workspaces_tree() {
    return m_server ? m_server->get_workspaces_tree() : nullptr;
}

struct wlr_scene_tree* PluginManager::get_layer_overlay_tree() {
    return m_server ? m_server->get_layer_overlay_tree() : nullptr;
}

void PluginManager::register_dispatcher(const std::string& name, std::function<void()> handler) {
    m_dispatchers[name] = std::move(handler);
    log_info("Plugin registered dispatcher: " + name);
}

void PluginManager::register_pointer_button_hook(std::function<bool(double lx, double ly, uint32_t button, bool pressed)> hook) {
    m_pointer_button_hooks.push_back(std::move(hook));
}

void PluginManager::register_pointer_motion_hook(std::function<bool(double lx, double ly)> hook) {
    m_pointer_motion_hooks.push_back(std::move(hook));
}

void PluginManager::register_key_hook(std::function<bool(uint32_t keysym, uint32_t modifiers, bool pressed)> hook) {
    m_key_hooks.push_back(std::move(hook));
}

void PluginManager::register_view_destroy_hook(std::function<void(View*)> hook) {
    m_view_destroy_hooks.push_back(std::move(hook));
}

bool PluginManager::load_plugin(const std::string& path) {
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        log_error("Failed to load plugin " + path + ": " + dlerror());
        return false;
    }

    auto init_func = reinterpret_cast<PluginInitFunc>(dlsym(handle, "miqu_plugin_init"));
    if (!init_func) {
        log_error("Plugin " + path + " missing miqu_plugin_init entry point: " + dlerror());
        dlclose(handle);
        return false;
    }

    PluginInfo* info = init_func(this);
    if (!info) {
        log_error("Plugin " + path + " init returned null info");
        dlclose(handle);
        return false;
    }

    if (info->abi_version != MIQU_PLUGIN_ABI_VERSION) {
        log_error("Plugin " + path + " ABI mismatch (plugin: " + std::to_string(info->abi_version) +
                  ", miquland: " + std::to_string(MIQU_PLUGIN_ABI_VERSION) + ")");
        dlclose(handle);
        return false;
    }

    auto exit_func = reinterpret_cast<PluginExitFunc>(dlsym(handle, "miqu_plugin_exit"));

    LoadedPlugin loaded;
    loaded.handle = handle;
    loaded.name = (info && info->name) ? info->name : path;
    loaded.author = (info && info->author) ? info->author : "Unknown";
    loaded.description = (info && info->description) ? info->description : "";
    loaded.version = (info && info->version) ? info->version : "1.0.0";
    loaded.abi_version = info ? info->abi_version : 0;
    loaded.exit_func = exit_func;
    loaded.path = path;

    log_info("Loaded plugin: " + loaded.name +
             " v" + loaded.version +
             " by " + loaded.author);

    m_loaded_plugins.push_back(std::move(loaded));
    return true;
}

void PluginManager::unload_all() {
    if (m_in_plugin_dispatch > 0) {
        log_info("Plugin unload requested during plugin dispatch; deferring unload to next idle iteration");
        m_pending_reload = true;
        if (!m_idle_reload_source && m_server && m_server->get_display()) {
            struct wl_event_loop* loop = wl_display_get_event_loop(m_server->get_display());
            m_idle_reload_source = wl_event_loop_add_idle(loop, handle_idle_reload, this);
        }
        return;
    }

    for (auto it = m_loaded_plugins.rbegin(); it != m_loaded_plugins.rend(); ++it) {
        if (it->exit_func) {
            it->exit_func();
        }
        if (it->handle) {
            dlclose(it->handle);
        }
    }
    m_loaded_plugins.clear();

    m_dispatchers.clear();
    m_pointer_button_hooks.clear();
    m_pointer_motion_hooks.clear();
    m_key_hooks.clear();
    m_view_destroy_hooks.clear();
}

void PluginManager::handle_idle_reload(void* data) {
    auto* self = static_cast<PluginManager*>(data);
    if (self->m_idle_reload_source) {
        wl_event_source_remove(self->m_idle_reload_source);
        self->m_idle_reload_source = nullptr;
    }
    if (self->m_pending_reload) {
        self->m_pending_reload = false;
        self->unload_all();
        self->load_configured_plugins();
    }
}

void PluginManager::load_configured_plugins() {
    for (const auto& path : Config::get().get_plugins()) {
        load_plugin(path);
    }
}

bool PluginManager::execute_dispatcher(const std::string& name) {
    PluginDispatchGuard guard(m_in_plugin_dispatch);
    auto it = m_dispatchers.find(name);
    if (it != m_dispatchers.end()) {
        it->second();
        return true;
    }
    return false;
}

bool PluginManager::has_dispatcher(const std::string& name) const {
    return m_dispatchers.find(name) != m_dispatchers.end();
}

bool PluginManager::dispatch_pointer_button(double lx, double ly, uint32_t button, bool pressed) {
    PluginDispatchGuard guard(m_in_plugin_dispatch);
    for (auto it = m_pointer_button_hooks.rbegin(); it != m_pointer_button_hooks.rend(); ++it) {
        if ((*it)(lx, ly, button, pressed)) {
            return true;
        }
    }
    return false;
}

bool PluginManager::dispatch_pointer_motion(double lx, double ly) {
    PluginDispatchGuard guard(m_in_plugin_dispatch);
    for (auto it = m_pointer_motion_hooks.rbegin(); it != m_pointer_motion_hooks.rend(); ++it) {
        if ((*it)(lx, ly)) {
            return true;
        }
    }
    return false;
}

bool PluginManager::dispatch_key(uint32_t keysym, uint32_t modifiers, bool pressed) {
    PluginDispatchGuard guard(m_in_plugin_dispatch);
    for (auto it = m_key_hooks.rbegin(); it != m_key_hooks.rend(); ++it) {
        if ((*it)(keysym, modifiers, pressed)) {
            return true;
        }
    }
    return false;
}

void PluginManager::dispatch_view_destroy(View* view) {
    PluginDispatchGuard guard(m_in_plugin_dispatch);
    for (auto it = m_view_destroy_hooks.rbegin(); it != m_view_destroy_hooks.rend(); ++it) {
        if (*it) {
            (*it)(view);
        }
    }
}

} // namespace miquland
