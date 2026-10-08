#pragma once

#include "core/common/util.hpp"
#include "core/layout.hpp"
#include <map>
#include <vector>

namespace miquland {

class Server;
class View;

class Workspace {
public:
    Workspace(Server* server, size_t id);
    ~Workspace();

    size_t get_id() const { return m_id; }
    struct wlr_scene_tree* get_scene_tree() const { return m_scene_tree; }
    struct wlr_scene_tree* get_tiled_tree() const { return m_tiled_tree; }
    struct wlr_scene_tree* get_floating_tree() const { return m_floating_tree; }

    bool can_accept_view() const { return true; }
    size_t view_count() const { return m_tiled_views.size(); }
    size_t total_view_count() const { return m_tiled_views.size() + m_floating_views.size(); }
    bool is_empty() const { return m_tiled_views.empty() && m_floating_views.empty(); }
    const std::vector<View*>& get_views() const { return m_tiled_views; }
    const std::vector<View*>& get_tiled_views() const { return m_tiled_views; }
    const std::vector<View*>& get_floating_views() const { return m_floating_views; }

    void update_ext_state();

    bool add_view(View* view);
    bool remove_view(View* view);
    bool contains_view(View* view) const;

    void set_visible(bool visible);
    bool is_visible() const { return m_visible; }

    SplitMode get_split_mode() const { return m_split_mode; }
    void set_split_mode(SplitMode mode) { m_split_mode = mode; }
    void toggle_split_mode() {
        m_split_mode = (m_split_mode == SplitMode::Horizontal) ? SplitMode::Vertical : SplitMode::Horizontal;
    }

    double get_split_ratio() const { return m_split_ratio; }
    void set_split_ratio(double ratio) { m_split_ratio = std::clamp(ratio, 0.1, 0.9); }

    double get_secondary_split_ratio() const { return m_secondary_split_ratio; }
    void set_secondary_split_ratio(double ratio) { m_secondary_split_ratio = std::clamp(ratio, 0.1, 0.9); }

    void swap_with_main(View* view);
    void swap_views(size_t idx1, size_t idx2);
    bool swap_views(View* view1, View* view2);
    void toggle_floating(View* view);

    void recalculate_layout(const struct wlr_box& usable_box);
    struct wlr_box calculate_tiled_geometry_for_new_view(const struct wlr_box& usable_box) const;
    void arrange_floating_views(const struct wlr_box& usable_box);

    View* get_view(size_t index) const;
    struct wlr_ext_workspace_handle_v1* get_ext_handle() const { return m_ext_handle; }

private:
    Server* m_server = nullptr;
    size_t m_id = 1;
    struct wlr_scene_tree* m_scene_tree = nullptr;
    struct wlr_scene_tree* m_tiled_tree = nullptr;
    struct wlr_scene_tree* m_floating_tree = nullptr;
    struct wlr_ext_workspace_handle_v1* m_ext_handle = nullptr;
    std::vector<View*> m_tiled_views;
    std::vector<View*> m_floating_views;
    bool m_visible = false;
    SplitMode m_split_mode = SplitMode::Horizontal;
    double m_split_ratio = 0.5;
    double m_secondary_split_ratio = 0.5;
};

class WorkspaceManager {
public:
    explicit WorkspaceManager(Server* server);
    ~WorkspaceManager();

    Workspace* get_workspace(size_t id);
    Workspace* get_or_create_workspace(size_t id);
    Workspace* get_active_workspace();

    void switch_to_workspace(size_t id, View* focus_view = nullptr);
    void commit_workspace_switch(size_t id);
    void prev_workspace();
    void next_workspace();
    size_t get_prev_workspace_id() const;
    size_t get_next_workspace_id() const;

    void add_view_auto(View* view);
    void remove_view(View* view);
    void move_view_to_workspace(View* view, size_t target_ws_id);

    void focus_next_view();
    void focus_prev_view();
    void focus_window_index(size_t index);
    void swap_with_main();
    void toggle_layout_mode();
    void toggle_active_split();
    void toggle_floating_active();
    void recalculate_layout();

    size_t get_active_workspace_id() const { return m_active_workspace_id; }
    void prune_workspace(size_t id);
    View* find_best_focus_view(Workspace* ws) const;
    const std::map<size_t, std::unique_ptr<Workspace>>& get_workspaces_map() const { return m_workspaces; }

private:
    Server* m_server = nullptr;
    std::map<size_t, std::unique_ptr<Workspace>> m_workspaces;
    size_t m_active_workspace_id = 1;
};

} // namespace miquland
