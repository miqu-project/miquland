#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

struct wlr_scene;
struct wlr_scene_tree;
struct wlr_output;

namespace miquland {

class Server;
class WorkspaceManager;
class OutputManager;
class View;

constexpr uint32_t MIQU_PLUGIN_ABI_VERSION = 1;

struct PluginInfo {
    const char* name = nullptr;
    const char* author = nullptr;
    const char* description = nullptr;
    const char* version = nullptr;
    uint32_t abi_version = MIQU_PLUGIN_ABI_VERSION;
};

class PluginAPI {
public:
    virtual ~PluginAPI() = default;

    virtual Server* get_server() = 0;
    virtual WorkspaceManager* get_workspace_manager() = 0;
    virtual OutputManager* get_output_manager() = 0;
    virtual struct wlr_scene* get_scene() = 0;
    virtual struct wlr_scene_tree* get_workspaces_tree() = 0;
    virtual struct wlr_scene_tree* get_layer_overlay_tree() = 0;

    // Register a custom command/dispatcher that can be bound in miquland.conf:
    // e.g. bind = SUPER, Tab, overview_toggle
    virtual void register_dispatcher(const std::string& name, std::function<void()> handler) = 0;

    // Input hooks: return true if the event was consumed and should NOT be processed further
    virtual void register_pointer_button_hook(std::function<bool(double lx, double ly, uint32_t button, bool pressed)> hook) = 0;
    virtual void register_pointer_motion_hook(std::function<bool(double lx, double ly)> hook) = 0;
    virtual void register_key_hook(std::function<bool(uint32_t keysym, uint32_t modifiers, bool pressed)> hook) = 0;

    // Lifecycle hooks: called when a View is being destroyed before scene tree teardown
    virtual void register_view_destroy_hook(std::function<void(View*)> hook) = 0;
};

typedef PluginInfo* (*PluginInitFunc)(PluginAPI* api);
typedef void (*PluginExitFunc)();

} // namespace miquland

#define MIQU_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
