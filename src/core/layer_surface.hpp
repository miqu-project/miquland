#pragma once

#include "core/common/util.hpp"
#include "core/config/config.hpp"
#include <vector>
#include <memory>
#include <string>

namespace miquland {

class Server;
class Popup;

class LayerSurface {
public:
    LayerSurface(Server* server, struct wlr_layer_surface_v1* layer_surface);
    ~LayerSurface();

    struct wlr_layer_surface_v1* get_wlr_layer_surface() const { return m_wlr_layer_surface; }
    struct wlr_scene_layer_surface_v1* get_scene_layer_surface() const { return m_scene_layer_surface; }
    enum zwlr_layer_shell_v1_layer get_layer() const { return m_current_layer; }
    bool is_mapped() const { return m_wlr_layer_surface && m_wlr_layer_surface->surface->mapped; }

    int get_geo_x() const { return m_geo_x; }
    int get_geo_y() const { return m_geo_y; }
    int get_width() const { return m_width; }
    int get_height() const { return m_height; }
    const std::string& get_namespace() const { return m_namespace; }

    void configure(const struct wlr_box* full_area, struct wlr_box* usable_area);
    void update_tree();
    void update_blur();

    struct wlr_buffer* get_current_buffer() const { return m_current_buffer; }
    Server* get_server() const { return m_server; }
    Config::LayerAnimStyle deduce_animation_style(bool is_close = false) const;
    void apply_animation_transform(int offset_x, int offset_y, double scale_x, double scale_y, float opacity, bool center_anchor = true);
    void reset_animation_transform();

private:
    static void handle_map(struct wl_listener* listener, void* data);
    static void handle_unmap(struct wl_listener* listener, void* data);
    static void handle_destroy(struct wl_listener* listener, void* data);
    static void handle_surface_commit(struct wl_listener* listener, void* data);
    static void handle_new_popup(struct wl_listener* listener, void* data);

    Server* m_server = nullptr;
    struct wlr_layer_surface_v1* m_wlr_layer_surface = nullptr;
    struct wlr_scene_layer_surface_v1* m_scene_layer_surface = nullptr;
    struct wlr_buffer* m_current_buffer = nullptr;
    struct wlr_scene_blur* m_blur_node = nullptr;
    struct wlr_scene_tree* m_popups_tree = nullptr;
    enum zwlr_layer_shell_v1_layer m_current_layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;

    int m_geo_x = 0;
    int m_geo_y = 0;
    int m_width = 0;
    int m_height = 0;
    std::string m_namespace;
    bool m_animating = false;

    struct wl_listener m_map_listener;
    struct wl_listener m_unmap_listener;
    struct wl_listener m_destroy_listener;
    struct wl_listener m_surface_commit_listener;
    struct wl_listener m_new_popup_listener;

    std::vector<std::unique_ptr<Popup>> m_popups;
};

} // namespace miquland
