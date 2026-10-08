#pragma once

#include "core/common/util.hpp"
#include "core/animation/easing.hpp"
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

struct wlr_scene_node;
struct wlr_scene_tree;

namespace miquland {

class Server;
class View;
class Workspace;
class LayerSurface;

struct NodeAnimation {
    uint64_t id = 0;
    struct wlr_scene_node* node = nullptr;
    View* bound_view = nullptr;
    size_t bound_workspace_id = 0;
    int start_x = 0;
    int start_y = 0;
    int target_x = 0;
    int target_y = 0;
    double start_time_ms = 0.0;
    double duration_ms = 0.0;
    std::function<float(float)> easing_fn;
    std::function<void()> on_complete;
    bool completed = false;
};

struct ViewAnimation {
    uint64_t id = 0;
    View* view = nullptr;
    double start_scale = 0.85;
    double target_scale = 1.0;
    float start_opacity = 0.0f;
    float target_opacity = 1.0f;
    double start_time_ms = 0.0;
    double scale_duration_ms = 0.0;
    double fade_duration_ms = 0.0;
    std::function<float(float)> scale_easing_fn;
    std::function<float(float)> fade_easing_fn;
    std::function<void()> on_complete;
    bool completed = false;
};

struct ViewGeometryAnimation {
    uint64_t id = 0;
    View* view = nullptr;
    int start_x = 0;
    int start_y = 0;
    int start_w = 0;
    int start_h = 0;
    int target_x = 0;
    int target_y = 0;
    int target_w = 0;
    int target_h = 0;
    double start_time_ms = 0.0;
    double duration_ms = 0.0;
    std::function<float(float)> easing_fn;
    std::function<void()> on_complete;
    bool completed = false;
};

struct LayerAnimation {
    uint64_t id = 0;
    LayerSurface* surface = nullptr;
    int start_offset_x = 0;
    int start_offset_y = 0;
    int target_offset_x = 0;
    int target_offset_y = 0;
    double start_scale_x = 1.0;
    double start_scale_y = 1.0;
    double target_scale_x = 1.0;
    double target_scale_y = 1.0;
    bool center_anchor = true;
    float start_opacity = 0.0f;
    float target_opacity = 1.0f;
    double start_time_ms = 0.0;
    double duration_ms = 0.0;
    double fade_duration_ms = 0.0;
    std::function<float(float)> easing_fn;
    std::function<float(float)> fade_easing_fn;
    bool fade_enabled = true;
    std::function<void()> on_complete;
    bool completed = false;
};

struct ClosingLayerAnimation {
    uint64_t id = 0;
    struct wlr_scene_tree* scene_tree = nullptr;
    struct wlr_scene_blur* blur_node = nullptr;
    int base_geo_x = 0;
    int base_geo_y = 0;
    int base_width = 0;
    int base_height = 0;
    int start_offset_x = 0;
    int start_offset_y = 0;
    int target_offset_x = 0;
    int target_offset_y = 0;
    double start_scale_x = 1.0;
    double start_scale_y = 1.0;
    double target_scale_x = 1.0;
    double target_scale_y = 1.0;
    bool center_anchor = true;
    float start_opacity = 1.0f;
    float target_opacity = 0.0f;
    double start_time_ms = 0.0;
    double duration_ms = 0.0;
    double fade_duration_ms = 0.0;
    std::function<float(float)> easing_fn;
    std::function<float(float)> fade_easing_fn;
    bool fade_enabled = true;
    std::function<void()> on_complete;
    bool completed = false;
};

class AnimationManager {
public:
    explicit AnimationManager(Server* server);
    ~AnimationManager();

    // High-level workspace animations
    void schedule_workspace_transition(Workspace* from_ws, Workspace* to_ws, bool slide_right, int screen_width);

    // High-level window animations
    void schedule_window_open(View* view);
    void schedule_window_close(View* view, std::function<void()> on_complete);
    void schedule_view_geometry(View* view, const struct wlr_box& from_box, const struct wlr_box& to_box);

    // High-level layer surface animations
    void schedule_layer_open(LayerSurface* surface);
    void schedule_layer_close(LayerSurface* surface, std::function<void()> on_complete = nullptr);
    void schedule_layer_unmap_close(LayerSurface* surface);
    bool is_layer_animating(LayerSurface* surface) const;

    // Interactive 1:1 Workspace Gestures
    void begin_workspace_swipe(int fingers);
    void update_workspace_swipe(double dx, double dy);
    void end_workspace_swipe(bool cancelled = false);
    bool is_workspace_swipe_active() const { return m_swipe_state.active; }
    bool is_workspace_animating() const;
    bool is_view_animating(View* view) const;
    bool is_view_geometry_animating(View* view) const;
    struct wlr_box get_view_current_box(View* view, const struct wlr_box& fallback) const;

    // Lifecycle cancellation
    void cancel_for_view(View* view);
    void cancel_for_workspace(Workspace* ws);
    void cancel_for_layer(LayerSurface* surface);
    void cancel_for_node(struct wlr_scene_node* node);
    void clear();

    // VSync Frame Driver
    void tick(double now_ms = 0.0);
    bool has_active_animations() const {
        return !m_animations.empty() || !m_view_animations.empty() ||
               !m_geometry_animations.empty() || !m_layer_animations.empty() ||
               !m_closing_layer_animations.empty() ||
               m_swipe_state.active;
    }
    void schedule_next_frame();

private:
    struct SwipeSample {
        double time_ms = 0.0;
        double delta_x = 0.0;
    };

    struct WorkspaceSwipeState {
        bool active = false;
        Workspace* current_ws = nullptr;
        Workspace* target_ws = nullptr;
        size_t target_ws_id = 0;
        int direction = 0; // -1: left (next), +1: right (prev)
        double delta_x = 0.0;
        double raw_delta_x = 0.0;
        int screen_width = 1920;
        std::vector<SwipeSample> sample_history;
    };

    double get_current_time_ms() const;

    Server* m_server = nullptr;
    std::vector<NodeAnimation> m_animations;
    std::vector<ViewAnimation> m_view_animations;
    std::vector<ViewGeometryAnimation> m_geometry_animations;
    std::vector<LayerAnimation> m_layer_animations;
    std::vector<ClosingLayerAnimation> m_closing_layer_animations;
    WorkspaceSwipeState m_swipe_state;
    uint64_t m_next_id = 1;
};

} // namespace miquland
