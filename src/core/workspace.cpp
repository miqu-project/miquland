#include "core/workspace.hpp"
#include "core/server.hpp"
#include "core/view.hpp"
#include "core/output.hpp"
#include "core/config/config.hpp"
#include "core/animation/animation_manager.hpp"
#include "core/input/input.hpp"
#include <algorithm>
#include <iostream>
#include <limits>

namespace miquland {

Workspace::Workspace(Server* server, size_t id)
    : m_server(server), m_id(id)
{
    m_scene_tree = wlr_scene_tree_create(m_server->get_workspaces_tree());
    wlr_scene_node_set_enabled(&m_scene_tree->node, false);
    m_tiled_tree = wlr_scene_tree_create(m_scene_tree);
    m_floating_tree = wlr_scene_tree_create(m_scene_tree);

    m_split_ratio = Config::get().get_default_split_ratio();
    m_secondary_split_ratio = Config::get().get_default_split_ratio();

    if (m_server->get_ext_workspace_manager()) {
        std::string id_str = std::to_string(m_id);
        m_ext_handle = wlr_ext_workspace_handle_v1_create(
            m_server->get_ext_workspace_manager(),
            id_str.c_str(),
            EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE
        );
        if (m_ext_handle) {
            if (m_server->get_ext_workspace_group()) {
                wlr_ext_workspace_handle_v1_set_group(m_ext_handle, m_server->get_ext_workspace_group());
            }
            wlr_ext_workspace_handle_v1_set_name(m_ext_handle, id_str.c_str());
            bool is_act = (m_server->get_workspace_manager()) ? (m_server->get_workspace_manager()->get_active_workspace_id() == m_id) : (m_id == 1);
            wlr_ext_workspace_handle_v1_set_active(m_ext_handle, is_act);
            wlr_ext_workspace_handle_v1_set_hidden(m_ext_handle, true);
            m_ext_handle->data = this;
        }
    }
}

Workspace::~Workspace() {
    if (m_server && m_server->get_animation_manager()) {
        m_server->get_animation_manager()->cancel_for_workspace(this);
    }
    if (m_ext_handle) {
        wlr_ext_workspace_handle_v1_destroy(m_ext_handle);
        m_ext_handle = nullptr;
    }
    if (m_scene_tree) {
        wlr_scene_node_destroy(&m_scene_tree->node);
    }
}

void Workspace::update_ext_state() {
    if (m_ext_handle) {
        wlr_ext_workspace_handle_v1_set_hidden(m_ext_handle, is_empty());
    }
}

bool Workspace::add_view(View* view) {
    if (!view) return false;

    if (view->is_dialog() || view->is_floating()) {
        if (std::find(m_floating_views.begin(), m_floating_views.end(), view) == m_floating_views.end()) {
            m_floating_views.push_back(view);
            view->set_workspace(this);
            if (view->get_scene_tree() && m_floating_tree) {
                wlr_scene_node_reparent(&view->get_scene_tree()->node, m_floating_tree);
                wlr_scene_node_raise_to_top(&view->get_scene_tree()->node);
            }
            update_ext_state();
            return true;
        }
        return false;
    }

    if (std::find(m_tiled_views.begin(), m_tiled_views.end(), view) == m_tiled_views.end()) {
        m_tiled_views.push_back(view);
        view->set_workspace(this);
        if (view->get_scene_tree() && m_tiled_tree) {
            wlr_scene_node_reparent(&view->get_scene_tree()->node, m_tiled_tree);
        }
        update_ext_state();
        return true;
    }
    return false;
}

bool Workspace::remove_view(View* view) {
    auto it_tile = std::find(m_tiled_views.begin(), m_tiled_views.end(), view);
    if (it_tile != m_tiled_views.end()) {
        m_tiled_views.erase(it_tile);
        view->set_workspace(nullptr);
        update_ext_state();
        return true;
    }

    auto it_float = std::find(m_floating_views.begin(), m_floating_views.end(), view);
    if (it_float != m_floating_views.end()) {
        m_floating_views.erase(it_float);
        view->set_workspace(nullptr);
        update_ext_state();
        return true;
    }

    return false;
}

bool Workspace::contains_view(View* view) const {
    return std::find(m_tiled_views.begin(), m_tiled_views.end(), view) != m_tiled_views.end() ||
           std::find(m_floating_views.begin(), m_floating_views.end(), view) != m_floating_views.end();
}

void Workspace::set_visible(bool visible) {
    m_visible = visible;
    if (m_scene_tree) {
        wlr_scene_node_set_enabled(&m_scene_tree->node, visible);
    }
}

View* Workspace::get_view(size_t index) const {
    if (index < m_tiled_views.size()) {
        return m_tiled_views[index];
    }
    size_t float_idx = index - m_tiled_views.size();
    if (float_idx < m_floating_views.size()) {
        return m_floating_views[float_idx];
    }
    return nullptr;
}

void Workspace::swap_with_main(View* view) {
    if (!view || m_tiled_views.size() < 2) return;
    auto it = std::find(m_tiled_views.begin(), m_tiled_views.end(), view);
    if (it != m_tiled_views.end()) {
        size_t idx = std::distance(m_tiled_views.begin(), it);
        if (idx > 0) {
            std::swap(m_tiled_views[0], m_tiled_views[idx]);
        }
    }
}

void Workspace::swap_views(size_t idx1, size_t idx2) {
    if (idx1 < m_tiled_views.size() && idx2 < m_tiled_views.size()) {
        std::swap(m_tiled_views[idx1], m_tiled_views[idx2]);
    }
}

bool Workspace::swap_views(View* view1, View* view2) {
    if (!view1 || !view2 || view1 == view2) return false;
    auto it1 = std::find(m_tiled_views.begin(), m_tiled_views.end(), view1);
    auto it2 = std::find(m_tiled_views.begin(), m_tiled_views.end(), view2);
    if (it1 != m_tiled_views.end() && it2 != m_tiled_views.end()) {
        std::iter_swap(it1, it2);
        auto* out_mgr = m_server->get_output_manager();
        if (out_mgr) {
            recalculate_layout(out_mgr->get_primary_usable_geometry());
        }
        return true;
    }
    return false;
}

void Workspace::toggle_floating(View* view) {
    if (!view || view->is_dialog()) return;

    auto it_tile = std::find(m_tiled_views.begin(), m_tiled_views.end(), view);
    if (it_tile != m_tiled_views.end()) {
        m_tiled_views.erase(it_tile);
        m_floating_views.push_back(view);
        view->set_floating(true);

        if (view->get_scene_tree() && m_floating_tree) {
            wlr_scene_node_reparent(&view->get_scene_tree()->node, m_floating_tree);
            wlr_scene_node_raise_to_top(&view->get_scene_tree()->node);
        }

        auto* out_mgr = m_server->get_output_manager();
        if (out_mgr) {
            recalculate_layout(out_mgr->get_primary_usable_geometry());
        }
    } else {
        auto it_float = std::find(m_floating_views.begin(), m_floating_views.end(), view);
        if (it_float != m_floating_views.end()) {
            m_floating_views.erase(it_float);
            m_tiled_views.push_back(view);
            view->set_floating(false);

            if (view->get_scene_tree() && m_tiled_tree) {
                wlr_scene_node_reparent(&view->get_scene_tree()->node, m_tiled_tree);
            }

            auto* out_mgr = m_server->get_output_manager();
            if (out_mgr) {
                recalculate_layout(out_mgr->get_primary_usable_geometry());
            }
        }
    }
}

void Workspace::recalculate_layout(const struct wlr_box& usable_box) {
    int pad = Config::get().get_screen_edge_padding();
    int gap = Config::get().get_space_between_windows();

    if (Config::get().is_smart_gaps_enabled() && m_tiled_views.size() <= 1) {
        pad = 0;
        gap = 0;
    }

    struct wlr_box inner_box = {
        .x = usable_box.x + pad,
        .y = usable_box.y + pad,
        .width = std::max(50, usable_box.width - 2 * pad),
        .height = std::max(50, usable_box.height - 2 * pad),
    };

    auto boxes = Layout::calculate(
        Config::get().get_layout_mode(),
        m_split_mode,
        inner_box,
        gap,
        m_tiled_views.size(),
        m_split_ratio,
        m_secondary_split_ratio
    );

    auto* anim_mgr = m_server ? m_server->get_animation_manager() : nullptr;
    bool animate_layout = anim_mgr && Config::get().is_window_animations_enabled() && m_visible;

    for (size_t i = 0; i < m_tiled_views.size(); ++i) {
        View* v = m_tiled_views[i];
        if (!v) continue;

        const auto& target_box = boxes[i];
        if (animate_layout && !v->is_mapping() && v->is_mapped() && v->get_width() > 0 && v->get_height() > 0) {
            struct wlr_box from_box = anim_mgr->get_view_current_box(v, { v->get_x(), v->get_y(), v->get_width(), v->get_height() });
            if (from_box.x != target_box.x || from_box.y != target_box.y ||
                from_box.width != target_box.width || from_box.height != target_box.height) {
                anim_mgr->schedule_view_geometry(v, from_box, target_box);
            } else {
                v->set_geometry(target_box.x, target_box.y, target_box.width, target_box.height);
            }
        } else {
            v->set_geometry(target_box.x, target_box.y, target_box.width, target_box.height);
        }
    }

    arrange_floating_views(usable_box);
}

struct wlr_box Workspace::calculate_tiled_geometry_for_new_view(const struct wlr_box& usable_box) const {
    int pad = Config::get().get_screen_edge_padding();
    int gap = Config::get().get_space_between_windows();

    if (Config::get().is_smart_gaps_enabled() && (m_tiled_views.size() + 1) <= 1) {
        pad = 0;
        gap = 0;
    }

    struct wlr_box inner_box = {
        .x = usable_box.x + pad,
        .y = usable_box.y + pad,
        .width = std::max(50, usable_box.width - 2 * pad),
        .height = std::max(50, usable_box.height - 2 * pad),
    };

    auto boxes = Layout::calculate(
        Config::get().get_layout_mode(),
        m_split_mode,
        inner_box,
        gap,
        m_tiled_views.size() + 1,
        m_split_ratio,
        m_secondary_split_ratio
    );

    return boxes.empty() ? inner_box : boxes.back();
}

void Workspace::arrange_floating_views(const struct wlr_box& usable_box) {
    int pad = Config::get().get_screen_edge_padding();
    int base_x = usable_box.x + pad;
    int base_y = usable_box.y + pad;
    int base_w = std::max(50, usable_box.width - 2 * pad);
    int base_h = std::max(50, usable_box.height - 2 * pad);
    int bw = Config::get().get_window_border_width();

    for (auto* fview : m_floating_views) {
        if (!fview || !fview->is_mapped()) continue;

        int req_w = fview->get_width() > 0 ? fview->get_width() : 750;
        int req_h = fview->get_height() > 0 ? fview->get_height() : 500;

        if (fview->get_type() == ViewType::Xdg && fview->get_xdg_toplevel()) {
            auto* xdg_surf = fview->get_xdg_toplevel()->base;
            int gw = xdg_surf->current.geometry.width;
            int gh = xdg_surf->current.geometry.height;
            if (gw <= 0 && xdg_surf->surface) {
                gw = xdg_surf->surface->current.width;
                gh = xdg_surf->surface->current.height;
            }
            if (gw > 0) req_w = gw + 2 * bw;
            if (gh > 0) req_h = gh + 2 * bw;
        }

        View* parent = fview->get_parent_view();
        if (parent && parent->is_mapped() && parent->get_width() > 0 && parent->get_height() > 0) {
            int max_w = std::max(50, parent->get_width() - 20);
            int max_h = std::max(50, parent->get_height() - 20);

            int dw = std::min(req_w, max_w);
            int dh = std::min(req_h, max_h);

            int dx = parent->get_x() + (parent->get_width() - dw) / 2;
            int dy = parent->get_y() + (parent->get_height() - dh) / 2;

            fview->set_geometry(dx, dy, dw, dh);
        } else {
            int dw = std::min(req_w, base_w);
            int dh = std::min(req_h, base_h);
            int dx = fview->get_x();
            int dy = fview->get_y();

            if (dx <= 0 && dy <= 0) {
                dx = base_x + (base_w - dw) / 2;
                dy = base_y + (base_h - dh) / 2;
            } else {
                dx = std::clamp(dx, base_x, std::max(base_x, base_x + base_w - dw));
                dy = std::clamp(dy, base_y, std::max(base_y, base_y + base_h - dh));
            }

            fview->set_geometry(dx, dy, dw, dh);
        }
    }
}

WorkspaceManager::WorkspaceManager(Server* server)
    : m_server(server)
{
    Workspace* ws1 = get_or_create_workspace(1);
    ws1->set_visible(true);
}

WorkspaceManager::~WorkspaceManager() = default;

void WorkspaceManager::prune_workspace(size_t id) {
    if (id == m_active_workspace_id) return;

    auto it = m_workspaces.find(id);
    if (it != m_workspaces.end() && it->second->is_empty()) {
        m_workspaces.erase(it);
    }
}

Workspace* WorkspaceManager::get_workspace(size_t id) {
    auto it = m_workspaces.find(id);
    if (it != m_workspaces.end()) {
        return it->second.get();
    }
    return nullptr;
}

Workspace* WorkspaceManager::get_or_create_workspace(size_t id) {
    auto it = m_workspaces.find(id);
    if (it != m_workspaces.end()) {
        return it->second.get();
    }

    auto ws = std::make_unique<Workspace>(m_server, id);
    Workspace* ptr = ws.get();
    m_workspaces[id] = std::move(ws);
    return ptr;
}

Workspace* WorkspaceManager::get_active_workspace() {
    return get_or_create_workspace(m_active_workspace_id);
}

void WorkspaceManager::switch_to_workspace(size_t id, View* focus_view) {
    if (id == 0 || id == m_active_workspace_id) {
        if (focus_view) {
            focus_view->focus();
        }
        return;
    }

    size_t old_id = m_active_workspace_id;
    Workspace* current = get_workspace(old_id);
    if (current && current->get_ext_handle()) {
        wlr_ext_workspace_handle_v1_set_active(current->get_ext_handle(), false);
    }

    m_active_workspace_id = id;
    Workspace* target = get_or_create_workspace(id);
    if (target->get_ext_handle()) {
        wlr_ext_workspace_handle_v1_set_active(target->get_ext_handle(), true);
    }

    recalculate_layout();

    struct wlr_box geom = m_server->get_output_manager()->get_primary_usable_geometry();
    int screen_w = (geom.width > 0) ? geom.width : 1920;
    bool slide_right = (id > old_id);

    if (m_server->get_animation_manager() && Config::get().is_workspace_animations_enabled()) {
        m_server->get_animation_manager()->schedule_workspace_transition(current, target, slide_right, screen_w);
    } else {
        if (current) current->set_visible(false);
        target->set_visible(true);
        prune_workspace(old_id);
    }

    if (focus_view) {
        focus_view->focus();
    } else {
        View* best = find_best_focus_view(target);
        if (best) {
            best->focus();
        } else {
            m_server->set_focused_view(nullptr);
        }
    }

    if (!m_server->get_animation_manager() || !Config::get().is_workspace_animations_enabled()) {
        if (m_server->get_input_manager()) {
            m_server->get_input_manager()->recheck_cursor_focus();
        }
    }
}

void WorkspaceManager::commit_workspace_switch(size_t id) {
    if (id == 0 || id == m_active_workspace_id) return;

    size_t old_id = m_active_workspace_id;
    Workspace* current = get_workspace(old_id);
    if (current && current->get_ext_handle()) {
        wlr_ext_workspace_handle_v1_set_active(current->get_ext_handle(), false);
    }

    m_active_workspace_id = id;
    Workspace* target = get_or_create_workspace(id);
    if (target->get_ext_handle()) {
        wlr_ext_workspace_handle_v1_set_active(target->get_ext_handle(), true);
    }

    recalculate_layout();
    prune_workspace(old_id);

    View* best = find_best_focus_view(target);
    if (best) {
        best->focus();
    } else {
        m_server->set_focused_view(nullptr);
    }

    if (m_server->get_input_manager()) {
        m_server->get_input_manager()->recheck_cursor_focus();
    }
}

size_t WorkspaceManager::get_prev_workspace_id() const {
    auto it = m_workspaces.find(m_active_workspace_id);
    if (it != m_workspaces.end() && it != m_workspaces.begin()) {
        return std::prev(it)->first;
    } else if (Config::get().is_workspace_cycle_enabled()) {
        if (!m_workspaces.empty() && m_workspaces.rbegin()->first != m_active_workspace_id) {
            return m_workspaces.rbegin()->first;
        } else if (m_active_workspace_id > 1) {
            return m_active_workspace_id - 1;
        } else {
            return 10;
        }
    } else if (m_active_workspace_id > 1) {
        return m_active_workspace_id - 1;
    }
    return m_active_workspace_id;
}

size_t WorkspaceManager::get_next_workspace_id() const {
    auto it = m_workspaces.find(m_active_workspace_id);
    if (it != m_workspaces.end() && std::next(it) != m_workspaces.end()) {
        return std::next(it)->first;
    } else if (Config::get().is_workspace_cycle_enabled()) {
        if (!m_workspaces.empty() && m_workspaces.begin()->first != m_active_workspace_id) {
            return m_workspaces.begin()->first;
        } else if (m_active_workspace_id >= 10) {
            return 1;
        } else {
            return m_active_workspace_id + 1;
        }
    } else {
        return m_active_workspace_id + 1;
    }
}

void WorkspaceManager::prev_workspace() {
    size_t prev_id = get_prev_workspace_id();
    if (prev_id != m_active_workspace_id) {
        switch_to_workspace(prev_id);
    }
}

void WorkspaceManager::next_workspace() {
    size_t next_id = get_next_workspace_id();
    if (next_id != m_active_workspace_id) {
        switch_to_workspace(next_id);
    }
}

void WorkspaceManager::add_view_auto(View* view) {
    if (view->is_dialog()) {
        View* parent = view->get_parent_view();
        Workspace* target_ws = (parent && parent->get_workspace()) ? parent->get_workspace() : get_active_workspace();
        target_ws->add_view(view);
        if (target_ws != get_active_workspace()) {
            switch_to_workspace(target_ws->get_id());
        } else {
            recalculate_layout();
        }
        return;
    }

    Workspace* active_ws = get_active_workspace();
    active_ws->add_view(view);
    recalculate_layout();
}

void WorkspaceManager::remove_view(View* view) {
    Workspace* ws = view->get_workspace();
    View* parent = view->get_parent_view();

    if (ws) {
        size_t ws_id = ws->get_id();
        ws->remove_view(view);
        recalculate_layout();

        if (ws->is_visible()) {
            if (parent && parent->is_mapped() && parent->get_workspace() == ws) {
                parent->focus();
            } else {
                View* best = find_best_focus_view(ws);
                if (best) {
                    best->focus();
                } else {
                    m_server->set_focused_view(nullptr);
                }
            }
        } else if (ws->is_empty()) {
            prune_workspace(ws_id);
        }
    }
}

void WorkspaceManager::move_view_to_workspace(View* view, size_t target_ws_id) {
    if (target_ws_id == 0) return;
    Workspace* current = view->get_workspace();
    if (!current || current->get_id() == target_ws_id) return;

    size_t current_id = current->get_id();
    Workspace* target = get_or_create_workspace(target_ws_id);
    current->remove_view(view);
    target->add_view(view);
    recalculate_layout();

    if (current->is_visible()) {
        View* best = find_best_focus_view(current);
        if (best) {
            best->focus();
        } else {
            m_server->set_focused_view(nullptr);
        }
    } else if (current->is_empty()) {
        prune_workspace(current_id);
    }
}

void WorkspaceManager::focus_next_view() {
    Workspace* ws = get_active_workspace();
    if (!ws || ws->view_count() < 2) return;

    View* cur = m_server->get_focused_view();
    size_t count = ws->view_count();
    size_t cur_idx = 0;

    for (size_t i = 0; i < count; ++i) {
        if (ws->get_view(i) == cur) {
            cur_idx = i;
            break;
        }
    }

    size_t next_idx = (cur_idx + 1) % count;
    ws->get_view(next_idx)->focus();
}

void WorkspaceManager::focus_prev_view() {
    Workspace* ws = get_active_workspace();
    if (!ws || ws->view_count() < 2) return;

    View* cur = m_server->get_focused_view();
    size_t count = ws->view_count();
    size_t cur_idx = 0;

    for (size_t i = 0; i < count; ++i) {
        if (ws->get_view(i) == cur) {
            cur_idx = i;
            break;
        }
    }

    size_t prev_idx = (cur_idx == 0) ? count - 1 : cur_idx - 1;
    ws->get_view(prev_idx)->focus();
}

void WorkspaceManager::focus_window_index(size_t index) {
    Workspace* ws = get_active_workspace();
    if (!ws || index >= ws->view_count()) return;
    ws->get_view(index)->focus();
}

void WorkspaceManager::swap_with_main() {
    Workspace* ws = get_active_workspace();
    if (!ws || ws->view_count() < 2) return;
    View* cur = m_server->get_focused_view();
    if (cur) {
        ws->swap_with_main(cur);
        recalculate_layout();
        cur->focus();
    }
}

void WorkspaceManager::toggle_layout_mode() {
    Config::get().toggle_layout_mode();
    recalculate_layout();
}

void WorkspaceManager::toggle_active_split() {
    Workspace* ws = get_active_workspace();
    if (ws) {
        ws->toggle_split_mode();
        recalculate_layout();
    }
}

void WorkspaceManager::toggle_floating_active() {
    View* cur = m_server->get_focused_view();
    if (cur && cur->get_workspace()) {
        cur->get_workspace()->toggle_floating(cur);
    }
}

void WorkspaceManager::recalculate_layout() {
    struct wlr_box geom = m_server->get_output_manager()->get_primary_usable_geometry();
    if (geom.width <= 0 || geom.height <= 0) {
        geom = { .x = 0, .y = 0, .width = 1920, .height = 1080 };
    }

    for (auto& [id, ws] : m_workspaces) {
        if (ws) {
            ws->recalculate_layout(geom);
        }
    }
}

View* WorkspaceManager::find_best_focus_view(Workspace* ws) const {
    if (!ws || ws->is_empty()) return nullptr;

    double cursor_x = 0.0, cursor_y = 0.0;
    bool has_cursor = false;
    if (m_server && m_server->get_input_manager()) {
        auto* cursor = m_server->get_input_manager()->get_cursor();
        if (cursor) {
            cursor_x = cursor->x;
            cursor_y = cursor->y;
            has_cursor = true;
        }
    }

    if (!has_cursor) {
        return ws->get_view(0);
    }

    // If workspace is on-screen and visible, check for direct surface hit under cursor first
    if (ws->is_visible() && ws->get_scene_tree() && ws->get_scene_tree()->node.x == 0 && ws->get_scene_tree()->node.y == 0) {
        double sx = 0.0, sy = 0.0;
        struct wlr_surface* surface = nullptr;
        View* direct_v = m_server->view_at(cursor_x, cursor_y, &surface, &sx, &sy);
        if (direct_v && direct_v->get_workspace() == ws && !direct_v->is_animating_close() && !direct_v->is_override_redirect()) {
            if (direct_v->has_child_dialogs()) {
                View* top_dialog = direct_v->get_top_dialog();
                if (top_dialog && top_dialog->is_mapped() && !top_dialog->is_animating_close()) {
                    return top_dialog;
                }
            }
            return direct_v;
        }
    }

    int cx = static_cast<int>(std::round(cursor_x));
    int cy = static_cast<int>(std::round(cursor_y));

    // 1. Check floating views (top-most first)
    const auto& floating_views = ws->get_floating_views();
    for (auto it = floating_views.rbegin(); it != floating_views.rend(); ++it) {
        View* fv = *it;
        if (!fv || !fv->is_mapped() || fv->is_animating_close() || fv->is_override_redirect()) {
            continue;
        }
        int fx = fv->get_target_x();
        int fy = fv->get_target_y();
        int fw = fv->get_target_width();
        int fh = fv->get_target_height();
        if (fw <= 0 || fh <= 0) {
            fx = fv->get_x();
            fy = fv->get_y();
            fw = fv->get_width();
            fh = fv->get_height();
        }

        if (cx >= fx && cx < fx + fw && cy >= fy && cy < fy + fh) {
            if (fv->has_child_dialogs()) {
                View* top_dialog = fv->get_top_dialog();
                if (top_dialog && top_dialog->is_mapped() && !top_dialog->is_animating_close()) {
                    return top_dialog;
                }
            }
            return fv;
        }
    }

    // 2. Check tiled views
    const auto& tiled_views = ws->get_tiled_views();
    for (auto* tv : tiled_views) {
        if (!tv || !tv->is_mapped() || tv->is_animating_close() || tv->is_override_redirect()) {
            continue;
        }
        int tx = tv->get_target_x();
        int ty = tv->get_target_y();
        int tw = tv->get_target_width();
        int th = tv->get_target_height();
        if (tw <= 0 || th <= 0) {
            tx = tv->get_x();
            ty = tv->get_y();
            tw = tv->get_width();
            th = tv->get_height();
        }

        if (cx >= tx && cx < tx + tw && cy >= ty && cy < ty + th) {
            if (tv->has_child_dialogs()) {
                View* top_dialog = tv->get_top_dialog();
                if (top_dialog && top_dialog->is_mapped() && !top_dialog->is_animating_close()) {
                    return top_dialog;
                }
            }
            return tv;
        }
    }

    // 3. Fallback: cursor in margins / gaps -> pick closest view by geometry distance
    View* closest_view = nullptr;
    int64_t min_dist_sq = std::numeric_limits<int64_t>::max();

    auto check_dist = [&](View* v) {
        if (!v || !v->is_mapped() || v->is_animating_close() || v->is_override_redirect()) return;
        int vx = v->get_target_x();
        int vy = v->get_target_y();
        int vw = v->get_target_width();
        int vh = v->get_target_height();
        if (vw <= 0 || vh <= 0) {
            vx = v->get_x();
            vy = v->get_y();
            vw = v->get_width();
            vh = v->get_height();
        }
        if (vw <= 0 || vh <= 0) return;

        int64_t dx = 0;
        if (cx < vx) dx = vx - cx;
        else if (cx >= vx + vw) dx = cx - (vx + vw - 1);

        int64_t dy = 0;
        if (cy < vy) dy = vy - cy;
        else if (cy >= vy + vh) dy = cy - (vy + vh - 1);

        int64_t dist_sq = dx * dx + dy * dy;
        if (dist_sq < min_dist_sq) {
            min_dist_sq = dist_sq;
            closest_view = v;
        }
    };

    for (auto it = floating_views.rbegin(); it != floating_views.rend(); ++it) {
        check_dist(*it);
    }
    for (auto* tv : tiled_views) {
        check_dist(tv);
    }

    if (closest_view) {
        if (closest_view->has_child_dialogs()) {
            View* top_dialog = closest_view->get_top_dialog();
            if (top_dialog && top_dialog->is_mapped() && !top_dialog->is_animating_close()) {
                return top_dialog;
            }
        }
        return closest_view;
    }

    return ws->get_view(0);
}

} // namespace miquland
