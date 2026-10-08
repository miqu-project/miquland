#pragma once

#include "miquland/plugin.hpp"
#include <vector>
#include <map>
#include <string>
#include <functional>
#include <memory>

struct wl_event_source;

namespace miquland {

class Server;

struct LoadedPlugin {
    void* handle = nullptr;
    std::string name;
    std::string author;
    std::string description;
    std::string version;
    uint32_t abi_version = 0;
    PluginExitFunc exit_func = nullptr;
    std::string path;
};

class PluginManager : public PluginAPI {
public:
    explicit PluginManager(Server* server);
    ~PluginManager() override;

    // PluginAPI implementation
    Server* get_server() override { return m_server; }
    WorkspaceManager* get_workspace_manager() override;
    OutputManager* get_output_manager() override;
    struct wlr_scene* get_scene() override;
    struct wlr_scene_tree* get_workspaces_tree() override;
    struct wlr_scene_tree* get_layer_overlay_tree() override;

    void register_dispatcher(const std::string& name, std::function<void()> handler) override;
    void register_pointer_button_hook(std::function<bool(double lx, double ly, uint32_t button, bool pressed)> hook) override;
    void register_pointer_motion_hook(std::function<bool(double lx, double ly)> hook) override;
    void register_key_hook(std::function<bool(uint32_t keysym, uint32_t modifiers, bool pressed)> hook) override;
    void register_view_destroy_hook(std::function<void(View*)> hook) override;

    // Loader & Lifecycle
    bool load_plugin(const std::string& path);
    void unload_all();
    void load_configured_plugins();

    // Event & Dispatcher execution
    bool execute_dispatcher(const std::string& name);
    bool has_dispatcher(const std::string& name) const;
    bool dispatch_pointer_button(double lx, double ly, uint32_t button, bool pressed);
    bool dispatch_pointer_motion(double lx, double ly);
    bool dispatch_key(uint32_t keysym, uint32_t modifiers, bool pressed);
    void dispatch_view_destroy(View* view);

    const std::vector<LoadedPlugin>& get_loaded_plugins() const { return m_loaded_plugins; }

private:
    Server* m_server = nullptr;
    std::vector<LoadedPlugin> m_loaded_plugins;
    std::map<std::string, std::function<void()>> m_dispatchers;
    std::vector<std::function<bool(double, double, uint32_t, bool)>> m_pointer_button_hooks;
    std::vector<std::function<bool(double, double)>> m_pointer_motion_hooks;
    std::vector<std::function<bool(uint32_t, uint32_t, bool)>> m_key_hooks;
    std::vector<std::function<void(View*)>> m_view_destroy_hooks;

    int m_in_plugin_dispatch = 0;
    bool m_pending_reload = false;
    struct wl_event_source* m_idle_reload_source = nullptr;
    static void handle_idle_reload(void* data);
};

} // namespace miquland
