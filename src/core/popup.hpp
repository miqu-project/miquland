#pragma once

#include "core/common/util.hpp"
#include <vector>
#include <memory>
#include <functional>

namespace miquland {

class View;
class LayerSurface;

class Popup {
public:
    Popup(struct wlr_xdg_popup* popup, struct wlr_scene_tree* parent_tree, View* view, std::function<void(Popup*)> on_destroy = nullptr);
    Popup(struct wlr_xdg_popup* popup, struct wlr_scene_tree* parent_tree, LayerSurface* layer_surface, std::function<void(Popup*)> on_destroy = nullptr);
    ~Popup();

    void unconstrain();
    void update_position();
    struct wlr_xdg_popup* get_wlr_popup() const { return m_popup; }
    struct wlr_scene_tree* get_scene_tree() const { return m_scene_tree; }
    struct wlr_scene_tree* get_xdg_surface_tree() const { return m_xdg_surface_tree; }
    bool is_root() const { return m_is_root; }

private:
    static void handle_commit(struct wl_listener* listener, void* data);
    static void handle_reposition(struct wl_listener* listener, void* data);
    static void handle_destroy(struct wl_listener* listener, void* data);
    static void handle_new_popup(struct wl_listener* listener, void* data);

    struct wlr_xdg_popup* m_popup = nullptr;
    struct wlr_scene_tree* m_scene_tree = nullptr;
    struct wlr_scene_tree* m_xdg_surface_tree = nullptr;
    bool m_is_root = false;
    View* m_view = nullptr;
    LayerSurface* m_layer_surface = nullptr;
    std::function<void(Popup*)> m_on_destroy;

    struct wl_listener m_commit_listener;
    struct wl_listener m_reposition_listener;
    struct wl_listener m_destroy_listener;
    struct wl_listener m_new_popup_listener;

    std::vector<std::unique_ptr<Popup>> m_child_popups;
};

} // namespace miquland
