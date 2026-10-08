#include "core/animation/animation_manager.hpp"
#include "core/server.hpp"
#include "core/view.hpp"
#include "core/layer_surface.hpp"
#include "core/workspace.hpp"
#include "core/output.hpp"
#include "core/input/input.hpp"
#include "core/config/config.hpp"
#include <ctime>
#include <cmath>
#include <algorithm>

namespace miquland {

AnimationManager::AnimationManager(Server* server)
    : m_server(server)
{
}

AnimationManager::~AnimationManager() {
    clear();
}

double AnimationManager::get_current_time_ms() const {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1000000.0;
}

void AnimationManager::schedule_workspace_transition(
    Workspace* from_ws,
    Workspace* to_ws,
    bool slide_right,
    int screen_width
) {
    if (!to_ws) return;

    if (!Config::get().is_workspace_animations_enabled() || !from_ws || screen_width <= 0) {
        if (from_ws) from_ws->set_visible(false);
        to_ws->set_visible(true);
        return;
    }

    cancel_for_workspace(from_ws);
    cancel_for_workspace(to_ws);

    // Cancel any in-flight workspace node animations to prevent stale lingering animations
    for (auto it = m_animations.begin(); it != m_animations.end(); ) {
        if (it->bound_workspace_id != 0) {
            it = m_animations.erase(it);
        } else {
            ++it;
        }
    }

    int duration = Config::get().get_workspace_animation_duration_ms();
    auto easing = Easing::from_name(Config::get().get_workspace_animation_curve());
    int offset = slide_right ? screen_width : -screen_width;
    double now = get_current_time_ms();

    // Position target workspace off-screen and make visible
    if (to_ws->get_scene_tree()) {
        wlr_scene_node_set_position(&to_ws->get_scene_tree()->node, offset, 0);
    }
    to_ws->set_visible(true);

    // Animation 1: Move old workspace off-screen
    if (from_ws->get_scene_tree()) {
        Server* srv = m_server;
        size_t from_id = from_ws ? from_ws->get_id() : 0;
        NodeAnimation anim_from;
        anim_from.id = m_next_id++;
        anim_from.node = &from_ws->get_scene_tree()->node;
        anim_from.bound_workspace_id = from_id;
        anim_from.start_x = 0;
        anim_from.start_y = 0;
        anim_from.target_x = -offset;
        anim_from.target_y = 0;
        anim_from.start_time_ms = now;
        anim_from.duration_ms = duration;
        anim_from.easing_fn = easing;
        anim_from.on_complete = [srv, from_id]() {
            if (srv && srv->get_workspace_manager()) {
                Workspace* ws = srv->get_workspace_manager()->get_workspace(from_id);
                if (ws) {
                    ws->set_visible(false);
                    if (ws->get_scene_tree()) {
                        wlr_scene_node_set_position(&ws->get_scene_tree()->node, 0, 0);
                    }
                }
                if (from_id > 0) {
                    srv->get_workspace_manager()->prune_workspace(from_id);
                }
            }
        };
        m_animations.push_back(std::move(anim_from));
    }

    // Animation 2: Move new workspace on-screen to 0,0
    if (to_ws->get_scene_tree()) {
        Server* srv = m_server;
        size_t to_id = to_ws ? to_ws->get_id() : 0;
        NodeAnimation anim_to;
        anim_to.id = m_next_id++;
        anim_to.node = &to_ws->get_scene_tree()->node;
        anim_to.bound_workspace_id = to_id;
        anim_to.start_x = offset;
        anim_to.start_y = 0;
        anim_to.target_x = 0;
        anim_to.target_y = 0;
        anim_to.start_time_ms = now;
        anim_to.duration_ms = duration;
        anim_to.easing_fn = easing;
        anim_to.on_complete = [srv, to_id]() {
            if (srv && srv->get_workspace_manager()) {
                Workspace* ws = srv->get_workspace_manager()->get_workspace(to_id);
                if (ws && ws->get_scene_tree()) {
                    wlr_scene_node_set_position(&ws->get_scene_tree()->node, 0, 0);
                }
            }
            if (srv && srv->get_input_manager()) {
                srv->get_input_manager()->recheck_cursor_focus();
            }
        };
        m_animations.push_back(std::move(anim_to));
    }

    schedule_next_frame();
}

bool AnimationManager::is_workspace_animating() const {
    if (m_swipe_state.active) return true;
    for (const auto& anim : m_animations) {
        if (anim.bound_workspace_id != 0) return true;
    }
    return false;
}

void AnimationManager::begin_workspace_swipe(int fingers) {
    if (!Config::get().is_workspace_animations_enabled() || !m_server || !m_server->get_workspace_manager()) {
        return;
    }

    if (m_swipe_state.active) {
        end_workspace_swipe(true);
    }

    Workspace* current = m_server->get_workspace_manager()->get_active_workspace();
    if (!current) return;

    cancel_for_workspace(current);

    int screen_w = 1920;
    if (m_server->get_output_manager()) {
        struct wlr_box geom = m_server->get_output_manager()->get_primary_usable_geometry();
        if (geom.width > 0) screen_w = geom.width;
    }

    m_swipe_state.active = true;
    m_swipe_state.current_ws = current;
    m_swipe_state.target_ws = nullptr;
    m_swipe_state.target_ws_id = 0;
    m_swipe_state.direction = 0;
    m_swipe_state.delta_x = 0.0;
    m_swipe_state.raw_delta_x = 0.0;
    m_swipe_state.screen_width = screen_w;
    m_swipe_state.sample_history.clear();
    m_swipe_state.sample_history.push_back({ get_current_time_ms(), 0.0 });
}

void AnimationManager::update_workspace_swipe(double dx, double dy) {
    if (!m_swipe_state.active || !m_swipe_state.current_ws) return;

    m_swipe_state.raw_delta_x += dx;

    double now = get_current_time_ms();
    m_swipe_state.sample_history.push_back({ now, m_swipe_state.raw_delta_x });
    while (m_swipe_state.sample_history.size() > 2 && (now - m_swipe_state.sample_history.front().time_ms) > 120) {
        m_swipe_state.sample_history.erase(m_swipe_state.sample_history.begin());
    }

    // Determine target direction:
    // raw_delta_x < 0 means dragged left -> revealing next workspace from right (+screen_width)
    // raw_delta_x > 0 means dragged right -> revealing prev workspace from left (-screen_width)
    int target_dir = 0;
    if (m_swipe_state.raw_delta_x < -2.0) {
        target_dir = -1;
    } else if (m_swipe_state.raw_delta_x > 2.0) {
        target_dir = 1;
    }

    if (target_dir != 0 && (target_dir != m_swipe_state.direction || !m_swipe_state.target_ws)) {
        size_t next_target_id = 0;
        if (target_dir == -1) {
            next_target_id = m_server->get_workspace_manager()->get_next_workspace_id();
        } else {
            next_target_id = m_server->get_workspace_manager()->get_prev_workspace_id();
        }

        if (next_target_id == m_swipe_state.current_ws->get_id()) {
            // Cannot swipe past boundary: apply asymptotic exponential elastic resistance
            double res_factor = Config::get().get_workspace_swipe_edge_resistance();
            double limit = static_cast<double>(m_swipe_state.screen_width) * res_factor;
            if (limit > 0.0) {
                double abs_raw = std::abs(m_swipe_state.raw_delta_x);
                double resisted = limit * (1.0 - std::exp(-abs_raw / limit));
                m_swipe_state.delta_x = (m_swipe_state.raw_delta_x >= 0 ? resisted : -resisted);
            } else {
                m_swipe_state.delta_x = 0.0;
            }
        } else {
            if (m_swipe_state.target_ws && m_swipe_state.target_ws_id != next_target_id) {
                m_swipe_state.target_ws->set_visible(false);
                if (m_swipe_state.target_ws->get_scene_tree()) {
                    wlr_scene_node_set_position(&m_swipe_state.target_ws->get_scene_tree()->node, 0, 0);
                }
            }

            m_swipe_state.target_ws_id = next_target_id;
            m_swipe_state.target_ws = m_server->get_workspace_manager()->get_or_create_workspace(next_target_id);
            m_swipe_state.direction = target_dir;

            if (m_swipe_state.target_ws) {
                if (m_server->get_output_manager()) {
                    m_swipe_state.target_ws->recalculate_layout(m_server->get_output_manager()->get_primary_usable_geometry());
                }
                m_swipe_state.target_ws->set_visible(true);
            }
            m_swipe_state.delta_x = std::clamp(m_swipe_state.raw_delta_x,
                -static_cast<double>(m_swipe_state.screen_width),
                static_cast<double>(m_swipe_state.screen_width));
        }
    } else if (m_swipe_state.target_ws) {
        m_swipe_state.delta_x = std::clamp(m_swipe_state.raw_delta_x,
            -static_cast<double>(m_swipe_state.screen_width),
            static_cast<double>(m_swipe_state.screen_width));
    } else {
        // Boundary resistance when no target workspace
        double res_factor = Config::get().get_workspace_swipe_edge_resistance();
        double limit = static_cast<double>(m_swipe_state.screen_width) * res_factor;
        if (limit > 0.0) {
            double abs_raw = std::abs(m_swipe_state.raw_delta_x);
            double resisted = limit * (1.0 - std::exp(-abs_raw / limit));
            m_swipe_state.delta_x = (m_swipe_state.raw_delta_x >= 0 ? resisted : -resisted);
        } else {
            m_swipe_state.delta_x = 0.0;
        }
    }

    int cur_x = static_cast<int>(std::round(m_swipe_state.delta_x));

    if (m_swipe_state.current_ws && m_swipe_state.current_ws->get_scene_tree()) {
        wlr_scene_node_set_position(&m_swipe_state.current_ws->get_scene_tree()->node, cur_x, 0);
    }

    if (m_swipe_state.target_ws && m_swipe_state.target_ws->get_scene_tree()) {
        int target_x = 0;
        if (m_swipe_state.direction == -1) {
            target_x = m_swipe_state.screen_width + cur_x;
        } else if (m_swipe_state.direction == 1) {
            target_x = -m_swipe_state.screen_width + cur_x;
        }
        wlr_scene_node_set_position(&m_swipe_state.target_ws->get_scene_tree()->node, target_x, 0);
    }

    schedule_next_frame();
}

void AnimationManager::end_workspace_swipe(bool cancelled) {
    if (!m_swipe_state.active || !m_swipe_state.current_ws) {
        m_swipe_state.active = false;
        return;
    }

    // Calculate release velocity (px/ms) from recent sample window
    double release_velocity = 0.0;
    if (m_swipe_state.sample_history.size() >= 2) {
        const auto& oldest = m_swipe_state.sample_history.front();
        const auto& newest = m_swipe_state.sample_history.back();
        double dt = newest.time_ms - oldest.time_ms;
        if (dt >= 10.0) {
            release_velocity = (newest.delta_x - oldest.delta_x) / dt;
        }
    }

    Workspace* current = m_swipe_state.current_ws;
    Workspace* target = m_swipe_state.target_ws;
    size_t target_id = m_swipe_state.target_ws_id;
    int direction = m_swipe_state.direction;
    double delta_x = m_swipe_state.delta_x;
    int screen_w = m_swipe_state.screen_width;

    m_swipe_state.active = false;
    m_swipe_state.current_ws = nullptr;
    m_swipe_state.target_ws = nullptr;
    m_swipe_state.target_ws_id = 0;
    m_swipe_state.direction = 0;
    m_swipe_state.delta_x = 0.0;
    m_swipe_state.raw_delta_x = 0.0;
    m_swipe_state.sample_history.clear();

    int cur_start_x = static_cast<int>(std::round(delta_x));
    double ratio = (screen_w > 0) ? (std::abs(delta_x) / static_cast<double>(screen_w)) : 0.0;

    double cancel_ratio = Config::get().get_workspace_swipe_cancel_ratio();
    double min_speed = Config::get().get_workspace_swipe_min_speed_to_force();
    bool commit = false;

    if (!cancelled && (target != nullptr)) {
        if (direction == -1) {
            // Dragged left (revealing next workspace on right)
            if (release_velocity <= -min_speed) {
                commit = true; // Fast flick in direction of motion
            } else if (release_velocity >= min_speed) {
                commit = false; // Fast flick back home
            } else {
                commit = (ratio >= cancel_ratio);
            }
        } else if (direction == 1) {
            // Dragged right (revealing previous workspace on left)
            if (release_velocity >= min_speed) {
                commit = true; // Fast flick in direction of motion
            } else if (release_velocity <= -min_speed) {
                commit = false; // Fast flick back home
            } else {
                commit = (ratio >= cancel_ratio);
            }
        }
    }

    auto easing = Easing::from_name(Config::get().get_workspace_animation_curve());
    double now = get_current_time_ms();
    int base_dur = Config::get().get_workspace_animation_duration_ms();
    double v_abs = std::abs(release_velocity);

    if (commit && target) {
        int cur_target_x = (direction == -1) ? -screen_w : screen_w;
        int target_start_x = (direction == -1) ? (screen_w + cur_start_x) : (-screen_w + cur_start_x);
        double remaining_px = static_cast<double>(screen_w) * (1.0 - ratio);
        int duration = base_dur;
        if (v_abs >= min_speed) {
            int momentum_dur = static_cast<int>(remaining_px / v_abs);
            duration = std::clamp(momentum_dur, 80, base_dur);
        } else {
            duration = std::max(60, static_cast<int>(base_dur * (1.0 - ratio)));
        }

        // Animate old workspace to offscreen
        Server* srv = m_server;
        size_t cur_id = current ? current->get_id() : 0;
        if (current->get_scene_tree()) {
            NodeAnimation anim_from;
            anim_from.id = m_next_id++;
            anim_from.node = &current->get_scene_tree()->node;
            anim_from.bound_workspace_id = cur_id;
            anim_from.start_x = cur_start_x;
            anim_from.start_y = 0;
            anim_from.target_x = cur_target_x;
            anim_from.target_y = 0;
            anim_from.start_time_ms = now;
            anim_from.duration_ms = duration;
            anim_from.easing_fn = easing;
            anim_from.on_complete = [srv, cur_id]() {
                if (srv && srv->get_workspace_manager()) {
                    Workspace* ws = srv->get_workspace_manager()->get_workspace(cur_id);
                    if (ws) {
                        ws->set_visible(false);
                        if (ws->get_scene_tree()) {
                            wlr_scene_node_set_position(&ws->get_scene_tree()->node, 0, 0);
                        }
                    }
                }
            };
            m_animations.push_back(std::move(anim_from));
        }

        // Animate target workspace to 0,0 and commit switch
        if (target->get_scene_tree()) {
            NodeAnimation anim_to;
            anim_to.id = m_next_id++;
            anim_to.node = &target->get_scene_tree()->node;
            anim_to.bound_workspace_id = target_id;
            anim_to.start_x = target_start_x;
            anim_to.start_y = 0;
            anim_to.target_x = 0;
            anim_to.target_y = 0;
            anim_to.start_time_ms = now;
            anim_to.duration_ms = duration;
            anim_to.easing_fn = easing;
            anim_to.on_complete = [srv, target_id]() {
                if (srv && srv->get_workspace_manager()) {
                    Workspace* ws = srv->get_workspace_manager()->get_workspace(target_id);
                    if (ws && ws->get_scene_tree()) {
                        wlr_scene_node_set_position(&ws->get_scene_tree()->node, 0, 0);
                    }
                    srv->get_workspace_manager()->commit_workspace_switch(target_id);
                }
            };
            m_animations.push_back(std::move(anim_to));
        } else {
            if (srv && srv->get_workspace_manager()) {
                srv->get_workspace_manager()->commit_workspace_switch(target_id);
            }
        }
    } else {
        // Snap back to current workspace
        double snap_back_px = static_cast<double>(screen_w) * ratio;
        int duration = base_dur;
        if (v_abs >= min_speed) {
            int momentum_dur = static_cast<int>(snap_back_px / v_abs);
            duration = std::clamp(momentum_dur, 80, base_dur);
        } else {
            duration = std::max(60, static_cast<int>(base_dur * std::max(0.1, ratio)));
        }
        Server* srv = m_server;
        size_t cur_id = current ? current->get_id() : 0;

        if (current->get_scene_tree()) {
            NodeAnimation anim_cur;
            anim_cur.id = m_next_id++;
            anim_cur.node = &current->get_scene_tree()->node;
            anim_cur.bound_workspace_id = cur_id;
            anim_cur.start_x = cur_start_x;
            anim_cur.start_y = 0;
            anim_cur.target_x = 0;
            anim_cur.target_y = 0;
            anim_cur.start_time_ms = now;
            anim_cur.duration_ms = duration;
            anim_cur.easing_fn = easing;
            anim_cur.on_complete = [srv, cur_id]() {
                if (srv && srv->get_workspace_manager()) {
                    Workspace* ws = srv->get_workspace_manager()->get_workspace(cur_id);
                    if (ws && ws->get_scene_tree()) {
                        wlr_scene_node_set_position(&ws->get_scene_tree()->node, 0, 0);
                    }
                }
                if (srv && srv->get_input_manager()) {
                    srv->get_input_manager()->recheck_cursor_focus();
                }
            };
            m_animations.push_back(std::move(anim_cur));
        }


        if (target && target->get_scene_tree()) {
            int target_start_x = (direction == -1) ? (screen_w + cur_start_x) : (-screen_w + cur_start_x);
            int target_end_x = (direction == -1) ? screen_w : -screen_w;
            size_t target_id_val = target->get_id();

            NodeAnimation anim_target;
            anim_target.id = m_next_id++;
            anim_target.node = &target->get_scene_tree()->node;
            anim_target.bound_workspace_id = target_id_val;
            anim_target.start_x = target_start_x;
            anim_target.start_y = 0;
            anim_target.target_x = target_end_x;
            anim_target.target_y = 0;
            anim_target.start_time_ms = now;
            anim_target.duration_ms = duration;
            anim_target.easing_fn = easing;
            anim_target.on_complete = [srv, target_id_val]() {
                if (srv && srv->get_workspace_manager()) {
                    Workspace* ws = srv->get_workspace_manager()->get_workspace(target_id_val);
                    if (ws) {
                        ws->set_visible(false);
                        if (ws->get_scene_tree()) {
                            wlr_scene_node_set_position(&ws->get_scene_tree()->node, 0, 0);
                        }
                    }
                }
            };
            m_animations.push_back(std::move(anim_target));
        }
    }

    schedule_next_frame();
}

void AnimationManager::schedule_window_open(View* view) {
    if (!view || !view->is_mapped()) return;

    std::string app_id = view->get_app_id();
    std::string title = view->get_title();

    if (!Config::get().is_window_open_animation_enabled(app_id, title)) {
        view->set_animating_open(false);
        view->apply_animation_transform(1.0, 1.0f);
        view->update_frame();
        return;
    }

    cancel_for_view(view);
    view->set_animating_open(true);

    int scale_duration = Config::get().get_window_open_duration_ms(app_id, title);
    int fade_duration = Config::get().get_window_open_fade_duration_ms(app_id, title);
    auto scale_easing = Easing::from_name(Config::get().get_window_open_curve(app_id, title));
    auto fade_easing = Easing::from_name(Config::get().get_window_open_fade_curve(app_id, title));
    double open_scale = Config::get().get_window_open_scale(app_id, title);
    double now = get_current_time_ms();

    float target_opacity = view->is_focused()
        ? Config::get().get_window_opacity_active()
        : Config::get().get_window_opacity_inactive();
    target_opacity = Config::get().get_rule_opacity(app_id, title, target_opacity);
    target_opacity = std::clamp(target_opacity, 0.0f, 1.0f);

    bool fade_enabled = Config::get().is_window_open_fade_enabled(app_id, title);
    float start_opacity = fade_enabled ? 0.0f : target_opacity;

    // Initialize with center-scaled start position and start opacity
    view->apply_animation_transform(open_scale, start_opacity);

    ViewAnimation anim;
    anim.id = m_next_id++;
    anim.view = view;
    anim.start_scale = open_scale;
    anim.target_scale = 1.0;
    anim.start_opacity = start_opacity;
    anim.target_opacity = target_opacity;
    anim.start_time_ms = now;
    anim.scale_duration_ms = scale_duration;
    anim.fade_duration_ms = fade_duration;
    anim.scale_easing_fn = scale_easing;
    anim.fade_easing_fn = fade_easing;
    anim.on_complete = [view, target_opacity]() {
        if (view && view->is_mapped()) {
            view->set_animating_open(false);
            view->apply_animation_transform(1.0, target_opacity);
            view->update_frame();
        }
    };

    m_view_animations.push_back(std::move(anim));
    schedule_next_frame();
}

void AnimationManager::schedule_window_close(View* view, std::function<void()> on_complete) {
    if (!view) {
        if (on_complete) on_complete();
        return;
    }

    std::string app_id = view->get_app_id();
    std::string title = view->get_title();

    if (!Config::get().is_window_close_animation_enabled(app_id, title) || !view->is_mapped() || view->is_fullscreen()) {
        view->set_animating_close(false);
        if (on_complete) on_complete();
        return;
    }

    cancel_for_view(view);
    view->set_animating_close(true);

    int scale_duration = Config::get().get_window_close_duration_ms(app_id, title);
    int fade_duration = Config::get().get_window_close_fade_duration_ms(app_id, title);
    auto scale_easing = Easing::from_name(Config::get().get_window_close_curve(app_id, title));
    auto fade_easing = Easing::from_name(Config::get().get_window_close_fade_curve(app_id, title));
    double close_scale = Config::get().get_window_close_scale(app_id, title);
    double now = get_current_time_ms();

    float cur_opacity = view->is_focused()
        ? Config::get().get_window_opacity_active()
        : Config::get().get_window_opacity_inactive();
    cur_opacity = Config::get().get_rule_opacity(app_id, title, cur_opacity);
    cur_opacity = std::clamp(cur_opacity, 0.0f, 1.0f);

    bool fade_enabled = Config::get().is_window_close_fade_enabled(app_id, title);
    float target_close_opacity = fade_enabled ? 0.0f : cur_opacity;

    ViewAnimation anim;
    anim.id = m_next_id++;
    anim.view = view;
    anim.start_scale = 1.0;
    anim.target_scale = close_scale;
    anim.start_opacity = cur_opacity;
    anim.target_opacity = target_close_opacity;
    anim.start_time_ms = now;
    anim.scale_duration_ms = scale_duration;
    anim.fade_duration_ms = fade_duration;
    anim.scale_easing_fn = scale_easing;
    anim.fade_easing_fn = fade_easing;
    anim.on_complete = [view, on_complete = std::move(on_complete)]() {
        if (view) {
            if (view->get_scene_tree()) {
                wlr_scene_node_set_enabled(&view->get_scene_tree()->node, false);
            }
            view->set_animating_close(false);
        }
        if (on_complete) on_complete();
    };

    m_view_animations.push_back(std::move(anim));
    schedule_next_frame();
}

void AnimationManager::schedule_layer_open(LayerSurface* surface) {
    if (!surface || !surface->is_mapped()) return;

    if (!Config::get().is_layer_open_animation_enabled()) {
        surface->reset_animation_transform();
        return;
    }

    cancel_for_layer(surface);

    Config::LayerAnimStyle style = surface->deduce_animation_style(false);
    if (style == Config::LayerAnimStyle::None) {
        surface->reset_animation_transform();
        return;
    }

    Config::LayerRule rule = Config::get().get_layer_rule(surface->get_namespace());
    int duration = (rule.duration_open_ms > 0) ? rule.duration_open_ms : Config::get().get_layer_open_duration_ms();
    if (duration <= 0) {
        surface->reset_animation_transform();
        return;
    }
    std::string curve_name = (!rule.curve_open.empty()) ? rule.curve_open : Config::get().get_layer_open_curve();
    auto easing = Easing::from_name(curve_name);
    double now = get_current_time_ms();

    int fade_dur = (rule.fade_open_duration_ms >= 0) ? rule.fade_open_duration_ms : Config::get().get_layer_animation_fade_in_duration_ms();
    if (fade_dur <= 0) fade_dur = duration;
    std::string fade_curve_name = (!rule.fade_open_curve.empty()) ? rule.fade_open_curve : Config::get().get_layer_animation_fade_in_curve();
    auto fade_easing = Easing::from_name(fade_curve_name);
    bool fade_enabled = rule.fade_open_enabled && Config::get().is_layer_animation_fade_enabled();

    int start_ox = 0, start_oy = 0;
    int target_ox = 0, target_oy = 0;
    double start_scale_x = 1.0, start_scale_y = 1.0;
    double target_scale_x = 1.0, target_scale_y = 1.0;
    bool center_anchor = true;
    float start_opacity = fade_enabled ? 0.0f : 1.0f, target_opacity = 1.0f;

    int surf_w = surface->get_width();
    int surf_h = surface->get_height();
    if (surf_w <= 0 && surface->get_wlr_layer_surface() && surface->get_wlr_layer_surface()->surface) {
        surf_w = surface->get_wlr_layer_surface()->surface->current.width;
    }
    if (surf_h <= 0 && surface->get_wlr_layer_surface() && surface->get_wlr_layer_surface()->surface) {
        surf_h = surface->get_wlr_layer_surface()->surface->current.height;
    }
    if (surf_w <= 0) surf_w = 400;
    if (surf_h <= 0) surf_h = 200;

    if (style == Config::LayerAnimStyle::SlideTop) {
        start_oy = -surf_h;
    } else if (style == Config::LayerAnimStyle::SlideBottom) {
        start_oy = +surf_h;
    } else if (style == Config::LayerAnimStyle::SlideLeft) {
        start_ox = -surf_w;
    } else if (style == Config::LayerAnimStyle::SlideRight) {
        start_ox = +surf_w;
    } else if (style == Config::LayerAnimStyle::Slide) {
        start_oy = -surf_h;
    } else if (style == Config::LayerAnimStyle::Popin) {
        double pop_scale = (rule.popin_scale > 0.0) ? rule.popin_scale : Config::get().get_layer_animation_popin_scale();
        start_scale_x = std::clamp(pop_scale, 0.1, 1.0);
        start_scale_y = std::clamp(pop_scale, 0.1, 1.0);
        center_anchor = true;
    } else if (style == Config::LayerAnimStyle::Unroll) {
        start_scale_x = 1.0;
        start_scale_y = 0.0;
        center_anchor = false;
    } else if (style == Config::LayerAnimStyle::Unfold) {
        start_scale_x = 1.0;
        start_scale_y = 0.0;
        center_anchor = true;
    }

    surface->apply_animation_transform(start_ox, start_oy, start_scale_x, start_scale_y, start_opacity, center_anchor);

    LayerAnimation anim;
    anim.id = m_next_id++;
    anim.surface = surface;
    anim.start_offset_x = start_ox;
    anim.start_offset_y = start_oy;
    anim.target_offset_x = target_ox;
    anim.target_offset_y = target_oy;
    anim.start_scale_x = start_scale_x;
    anim.start_scale_y = start_scale_y;
    anim.target_scale_x = target_scale_x;
    anim.target_scale_y = target_scale_y;
    anim.center_anchor = center_anchor;
    anim.start_opacity = start_opacity;
    anim.target_opacity = target_opacity;
    anim.start_time_ms = now;
    anim.duration_ms = duration;
    anim.fade_duration_ms = fade_dur;
    anim.easing_fn = easing;
    anim.fade_easing_fn = fade_easing;
    anim.fade_enabled = fade_enabled;
    anim.on_complete = [surface]() {
        if (surface && surface->is_mapped()) {
            surface->reset_animation_transform();
        }
    };

    m_layer_animations.push_back(std::move(anim));
    schedule_next_frame();
}

void AnimationManager::schedule_layer_close(LayerSurface* surface, std::function<void()> on_complete) {
    if (!surface || !surface->is_mapped()) {
        if (on_complete) on_complete();
        return;
    }

    if (!Config::get().is_layer_close_animation_enabled()) {
        if (on_complete) on_complete();
        return;
    }

    Config::LayerAnimStyle style = surface->deduce_animation_style(true);
    if (style == Config::LayerAnimStyle::None) {
        if (on_complete) on_complete();
        return;
    }

    cancel_for_layer(surface);

    Config::LayerRule rule = Config::get().get_layer_rule(surface->get_namespace());
    int duration = (rule.duration_close_ms > 0) ? rule.duration_close_ms : Config::get().get_layer_close_duration_ms();
    if (duration <= 0) {
        if (on_complete) on_complete();
        return;
    }
    std::string curve_name = (!rule.curve_close.empty()) ? rule.curve_close : Config::get().get_layer_close_curve();
    auto easing = Easing::from_name(curve_name);
    double now = get_current_time_ms();

    int fade_dur = (rule.fade_close_duration_ms >= 0) ? rule.fade_close_duration_ms : Config::get().get_layer_animation_fade_out_duration_ms();
    if (fade_dur <= 0) fade_dur = duration;
    std::string fade_curve_name = (!rule.fade_close_curve.empty()) ? rule.fade_close_curve : Config::get().get_layer_animation_fade_out_curve();
    auto fade_easing = Easing::from_name(fade_curve_name);
    bool fade_enabled = rule.fade_close_enabled && Config::get().is_layer_animation_fade_enabled();

    int start_ox = 0, start_oy = 0;
    int target_ox = 0, target_oy = 0;
    double start_scale_x = 1.0, start_scale_y = 1.0;
    double target_scale_x = 1.0, target_scale_y = 1.0;
    bool center_anchor = true;
    float start_opacity = 1.0f, target_opacity = fade_enabled ? 0.0f : 1.0f;

    int surf_w = surface->get_width();
    int surf_h = surface->get_height();
    if (surf_w <= 0) surf_w = 400;
    if (surf_h <= 0) surf_h = 200;

    if (style == Config::LayerAnimStyle::SlideTop) {
        target_oy = -surf_h;
    } else if (style == Config::LayerAnimStyle::SlideBottom) {
        target_oy = +surf_h;
    } else if (style == Config::LayerAnimStyle::SlideLeft) {
        target_ox = -surf_w;
    } else if (style == Config::LayerAnimStyle::SlideRight) {
        target_ox = +surf_w;
    } else if (style == Config::LayerAnimStyle::Slide) {
        target_oy = -surf_h;
    } else if (style == Config::LayerAnimStyle::Popin) {
        double pop_scale = (rule.popin_scale > 0.0) ? rule.popin_scale : Config::get().get_layer_animation_popin_scale();
        target_scale_x = std::clamp(pop_scale, 0.1, 1.0);
        target_scale_y = std::clamp(pop_scale, 0.1, 1.0);
        center_anchor = true;
    } else if (style == Config::LayerAnimStyle::Unroll) {
        target_scale_x = 1.0;
        target_scale_y = 0.0;
        center_anchor = false;
    } else if (style == Config::LayerAnimStyle::Unfold) {
        target_scale_x = 1.0;
        target_scale_y = 0.0;
        center_anchor = true;
    }

    LayerAnimation anim;
    anim.id = m_next_id++;
    anim.surface = surface;
    anim.start_offset_x = start_ox;
    anim.start_offset_y = start_oy;
    anim.target_offset_x = target_ox;
    anim.target_offset_y = target_oy;
    anim.start_scale_x = start_scale_x;
    anim.start_scale_y = start_scale_y;
    anim.target_scale_x = target_scale_x;
    anim.target_scale_y = target_scale_y;
    anim.center_anchor = center_anchor;
    anim.start_opacity = start_opacity;
    anim.target_opacity = target_opacity;
    anim.start_time_ms = now;
    anim.duration_ms = duration;
    anim.fade_duration_ms = fade_dur;
    anim.easing_fn = easing;
    anim.fade_easing_fn = fade_easing;
    anim.fade_enabled = fade_enabled;
    anim.on_complete = on_complete;

    m_layer_animations.push_back(std::move(anim));
    schedule_next_frame();
}

void AnimationManager::schedule_layer_unmap_close(LayerSurface* surface) {
    if (!surface || !m_server) return;

    if (!Config::get().is_layer_close_animation_enabled()) return;

    Config::LayerAnimStyle style = surface->deduce_animation_style(true);
    if (style == Config::LayerAnimStyle::None) return;

    struct wlr_buffer* buf = surface->get_current_buffer();
    if (!buf && surface->get_wlr_layer_surface() && surface->get_wlr_layer_surface()->surface) {
        if (surface->get_wlr_layer_surface()->surface->buffer) {
            buf = &surface->get_wlr_layer_surface()->surface->buffer->base;
        }
    }
    if (!buf) return;

    struct wlr_scene_tree* layer_tree = m_server->get_layer_tree(surface->get_layer());
    if (!layer_tree) return;

    struct wlr_scene_tree* closing_tree = wlr_scene_tree_create(layer_tree);
    if (!closing_tree) return;

    struct wlr_scene_buffer* scene_buf = wlr_scene_buffer_create(closing_tree, buf);
    if (!scene_buf) {
        wlr_scene_node_destroy(&closing_tree->node);
        return;
    }

    int base_w = (surface->get_width() > 0) ? surface->get_width() : (buf->width > 0 ? buf->width : 400);
    int base_h = (surface->get_height() > 0) ? surface->get_height() : (buf->height > 0 ? buf->height : 200);

    struct wlr_scene_blur* blur = nullptr;
    if (Config::get().is_blur_enabled() && Config::get().is_layer_blur_enabled(surface->get_namespace())) {
        blur = wlr_scene_blur_create(closing_tree, base_w, base_h);
        if (blur) {
            wlr_scene_blur_set_transparency_mask_source(blur, scene_buf);
        }
    }

    wlr_scene_node_set_position(&closing_tree->node, surface->get_geo_x(), surface->get_geo_y());

    Config::LayerRule rule = Config::get().get_layer_rule(surface->get_namespace());
    int duration = (rule.duration_close_ms > 0) ? rule.duration_close_ms : Config::get().get_layer_close_duration_ms();
    if (duration <= 0) {
        wlr_scene_node_destroy(&closing_tree->node);
        return;
    }
    std::string curve_name = (!rule.curve_close.empty()) ? rule.curve_close : Config::get().get_layer_close_curve();
    auto easing = Easing::from_name(curve_name);
    double now = get_current_time_ms();

    int fade_dur = (rule.fade_close_duration_ms >= 0) ? rule.fade_close_duration_ms : Config::get().get_layer_animation_fade_out_duration_ms();
    if (fade_dur <= 0) fade_dur = duration;
    std::string fade_curve_name = (!rule.fade_close_curve.empty()) ? rule.fade_close_curve : Config::get().get_layer_animation_fade_out_curve();
    auto fade_easing = Easing::from_name(fade_curve_name);
    bool fade_enabled = rule.fade_close_enabled && Config::get().is_layer_animation_fade_enabled();

    int start_ox = 0, start_oy = 0;
    int target_ox = 0, target_oy = 0;
    double start_scale_x = 1.0, start_scale_y = 1.0;
    double target_scale_x = 1.0, target_scale_y = 1.0;
    bool center_anchor = true;
    float start_opacity = 1.0f, target_opacity = fade_enabled ? 0.0f : 1.0f;

    if (style == Config::LayerAnimStyle::SlideTop) {
        target_oy = -base_h;
    } else if (style == Config::LayerAnimStyle::SlideBottom) {
        target_oy = +base_h;
    } else if (style == Config::LayerAnimStyle::SlideLeft) {
        target_ox = -base_w;
    } else if (style == Config::LayerAnimStyle::SlideRight) {
        target_ox = +base_w;
    } else if (style == Config::LayerAnimStyle::Slide) {
        target_oy = -base_h;
    } else if (style == Config::LayerAnimStyle::Popin) {
        double pop_scale = (rule.popin_scale > 0.0) ? rule.popin_scale : Config::get().get_layer_animation_popin_scale();
        target_scale_x = std::clamp(pop_scale, 0.1, 1.0);
        target_scale_y = std::clamp(pop_scale, 0.1, 1.0);
        center_anchor = true;
    } else if (style == Config::LayerAnimStyle::Unroll) {
        target_scale_x = 1.0;
        target_scale_y = 0.0;
        center_anchor = false;
    } else if (style == Config::LayerAnimStyle::Unfold) {
        target_scale_x = 1.0;
        target_scale_y = 0.0;
        center_anchor = true;
    }

    ClosingLayerAnimation anim;
    anim.id = m_next_id++;
    anim.scene_tree = closing_tree;
    anim.blur_node = blur;
    anim.base_geo_x = surface->get_geo_x();
    anim.base_geo_y = surface->get_geo_y();
    anim.base_width = base_w;
    anim.base_height = base_h;
    anim.start_offset_x = start_ox;
    anim.start_offset_y = start_oy;
    anim.target_offset_x = target_ox;
    anim.target_offset_y = target_oy;
    anim.start_scale_x = start_scale_x;
    anim.start_scale_y = start_scale_y;
    anim.target_scale_x = target_scale_x;
    anim.target_scale_y = target_scale_y;
    anim.center_anchor = center_anchor;
    anim.start_opacity = start_opacity;
    anim.target_opacity = target_opacity;
    anim.start_time_ms = now;
    anim.duration_ms = duration;
    anim.fade_duration_ms = fade_dur;
    anim.easing_fn = easing;
    anim.fade_easing_fn = fade_easing;
    anim.fade_enabled = fade_enabled;
    anim.on_complete = [closing_tree]() {
        if (closing_tree) {
            wlr_scene_node_destroy(&closing_tree->node);
        }
    };

    m_closing_layer_animations.push_back(std::move(anim));
    schedule_next_frame();
}

bool AnimationManager::is_layer_animating(LayerSurface* surface) const {
    if (!surface) return !m_layer_animations.empty();
    for (const auto& anim : m_layer_animations) {
        if (anim.surface == surface) return true;
    }
    return false;
}

void AnimationManager::cancel_for_layer(LayerSurface* surface) {
    if (!surface) return;
    auto it = std::remove_if(m_layer_animations.begin(), m_layer_animations.end(), [surface](const LayerAnimation& a) {
        return a.surface == surface;
    });
    m_layer_animations.erase(it, m_layer_animations.end());
}

bool AnimationManager::is_view_animating(View* view) const {
    if (!view) return !m_view_animations.empty() || !m_geometry_animations.empty();
    for (const auto& anim : m_view_animations) {
        if (anim.view == view) return true;
    }
    for (const auto& anim : m_geometry_animations) {
        if (anim.view == view) return true;
    }
    return false;
}

bool AnimationManager::is_view_geometry_animating(View* view) const {
    if (!view) return !m_geometry_animations.empty();
    for (const auto& anim : m_geometry_animations) {
        if (anim.view == view) return true;
    }
    return false;
}

struct wlr_box AnimationManager::get_view_current_box(View* view, const struct wlr_box& fallback) const {
    if (!view) return fallback;
    double now = get_current_time_ms();
    for (const auto& anim : m_geometry_animations) {
        if (anim.view == view) {
            float progress = 1.0f;
            if (anim.duration_ms > 0.0) {
                double elapsed = (now >= anim.start_time_ms) ? (now - anim.start_time_ms) : 0.0;
                progress = static_cast<float>(elapsed / anim.duration_ms);
            }
            progress = std::clamp(progress, 0.0f, 1.0f);
            float eased = anim.easing_fn ? anim.easing_fn(progress) : progress;
            struct wlr_box cur_box;
            cur_box.x = anim.start_x + static_cast<int>(std::round(static_cast<float>(anim.target_x - anim.start_x) * eased));
            cur_box.y = anim.start_y + static_cast<int>(std::round(static_cast<float>(anim.target_y - anim.start_y) * eased));
            cur_box.width = std::max(1, anim.start_w + static_cast<int>(std::round(static_cast<float>(anim.target_w - anim.start_w) * eased)));
            cur_box.height = std::max(1, anim.start_h + static_cast<int>(std::round(static_cast<float>(anim.target_h - anim.start_h) * eased)));
            return cur_box;
        }
    }
    return fallback;
}

void AnimationManager::schedule_view_geometry(View* view, const struct wlr_box& from_box, const struct wlr_box& to_box) {
    if (!view || !view->is_mapped() || to_box.width <= 0 || to_box.height <= 0) {
        if (view) {
            view->set_geometry(to_box.x, to_box.y, to_box.width, to_box.height);
        }
        return;
    }

    if (!Config::get().is_window_animations_enabled() || view->is_fullscreen() || view->is_override_redirect()) {
        view->set_geometry(to_box.x, to_box.y, to_box.width, to_box.height);
        return;
    }

    struct wlr_box actual_from = from_box;
    double now = get_current_time_ms();

    auto it_existing = std::find_if(m_geometry_animations.begin(), m_geometry_animations.end(), [view](const ViewGeometryAnimation& a) {
        return a.view == view;
    });

    if (it_existing != m_geometry_animations.end()) {
        float progress = 1.0f;
        if (it_existing->duration_ms > 0.0) {
            double elapsed = (now >= it_existing->start_time_ms) ? (now - it_existing->start_time_ms) : 0.0;
            progress = static_cast<float>(elapsed / it_existing->duration_ms);
        }
        progress = std::clamp(progress, 0.0f, 1.0f);
        float eased = it_existing->easing_fn ? it_existing->easing_fn(progress) : progress;
        actual_from.x = it_existing->start_x + static_cast<int>(std::round(static_cast<float>(it_existing->target_x - it_existing->start_x) * eased));
        actual_from.y = it_existing->start_y + static_cast<int>(std::round(static_cast<float>(it_existing->target_y - it_existing->start_y) * eased));
        actual_from.width = std::max(1, it_existing->start_w + static_cast<int>(std::round(static_cast<float>(it_existing->target_w - it_existing->start_w) * eased)));
        actual_from.height = std::max(1, it_existing->start_h + static_cast<int>(std::round(static_cast<float>(it_existing->target_h - it_existing->start_h) * eased)));

        m_geometry_animations.erase(it_existing);
    }

    if (actual_from.x == to_box.x && actual_from.y == to_box.y &&
        actual_from.width == to_box.width && actual_from.height == to_box.height) {
        view->set_geometry(to_box.x, to_box.y, to_box.width, to_box.height);
        return;
    }

    int duration = Config::get().get_window_animation_duration_ms();
    auto easing = Easing::from_name(Config::get().get_window_animation_curve());

    // Notify client of target size immediately so it starts preparing target buffer
    view->notify_geometry_target(to_box.x, to_box.y, to_box.width, to_box.height);

    // Apply starting frame transform
    view->apply_geometry_animation(actual_from.x, actual_from.y, actual_from.width, actual_from.height);

    ViewGeometryAnimation anim;
    anim.id = m_next_id++;
    anim.view = view;
    anim.start_x = actual_from.x;
    anim.start_y = actual_from.y;
    anim.start_w = actual_from.width;
    anim.start_h = actual_from.height;
    anim.target_x = to_box.x;
    anim.target_y = to_box.y;
    anim.target_w = to_box.width;
    anim.target_h = to_box.height;
    anim.start_time_ms = now;
    anim.duration_ms = duration;
    anim.easing_fn = easing;
    anim.on_complete = [view, to_box]() {
        if (view && view->is_mapped()) {
            view->finish_geometry_animation(to_box.x, to_box.y, to_box.width, to_box.height);
        }
    };

    m_geometry_animations.push_back(std::move(anim));
    schedule_next_frame();
}

void AnimationManager::cancel_for_view(View* view) {
    if (!view) return;
    view->set_animating_open(false);
    view->set_animating_close(false);
    auto it_node = std::remove_if(m_animations.begin(), m_animations.end(), [view](const NodeAnimation& a) {
        return a.bound_view == view;
    });
    m_animations.erase(it_node, m_animations.end());

    auto it_view = std::remove_if(m_view_animations.begin(), m_view_animations.end(), [view](const ViewAnimation& a) {
        return a.view == view;
    });
    m_view_animations.erase(it_view, m_view_animations.end());

    for (auto it = m_geometry_animations.begin(); it != m_geometry_animations.end(); ) {
        if (it->view == view) {
            if (view->is_mapped()) {
                view->finish_geometry_animation(it->target_x, it->target_y, it->target_w, it->target_h);
            }
            it = m_geometry_animations.erase(it);
        } else {
            ++it;
        }
    }
}

void AnimationManager::cancel_for_workspace(Workspace* ws) {
    if (!ws) return;
    if (m_swipe_state.current_ws == ws || m_swipe_state.target_ws == ws) {
        if (m_swipe_state.current_ws && m_swipe_state.current_ws->get_scene_tree()) {
            wlr_scene_node_set_position(&m_swipe_state.current_ws->get_scene_tree()->node, 0, 0);
        }
        if (m_swipe_state.target_ws && m_swipe_state.target_ws != m_swipe_state.current_ws) {
            m_swipe_state.target_ws->set_visible(false);
            if (m_swipe_state.target_ws->get_scene_tree()) {
                wlr_scene_node_set_position(&m_swipe_state.target_ws->get_scene_tree()->node, 0, 0);
            }
        }
        m_swipe_state.active = false;
        m_swipe_state.current_ws = nullptr;
        m_swipe_state.target_ws = nullptr;
        m_swipe_state.target_ws_id = 0;
        m_swipe_state.direction = 0;
        m_swipe_state.delta_x = 0.0;
    }
    size_t ws_id = ws->get_id();
    auto it = std::remove_if(m_animations.begin(), m_animations.end(), [ws_id](const NodeAnimation& a) {
        return a.bound_workspace_id == ws_id;
    });
    m_animations.erase(it, m_animations.end());
}

void AnimationManager::cancel_for_node(struct wlr_scene_node* node) {
    if (!node) return;
    auto it = std::remove_if(m_animations.begin(), m_animations.end(), [node](const NodeAnimation& a) {
        return a.node == node;
    });
    m_animations.erase(it, m_animations.end());
}

void AnimationManager::clear() {
    if (m_swipe_state.active) {
        if (m_swipe_state.current_ws && m_swipe_state.current_ws->get_scene_tree()) {
            wlr_scene_node_set_position(&m_swipe_state.current_ws->get_scene_tree()->node, 0, 0);
        }
        if (m_swipe_state.target_ws && m_swipe_state.target_ws != m_swipe_state.current_ws) {
            m_swipe_state.target_ws->set_visible(false);
            if (m_swipe_state.target_ws->get_scene_tree()) {
                wlr_scene_node_set_position(&m_swipe_state.target_ws->get_scene_tree()->node, 0, 0);
            }
        }
        m_swipe_state.active = false;
        m_swipe_state.current_ws = nullptr;
        m_swipe_state.target_ws = nullptr;
    }
    m_animations.clear();
    m_view_animations.clear();
    m_geometry_animations.clear();
    m_layer_animations.clear();
    for (auto& a : m_closing_layer_animations) {
        if (a.scene_tree) {
            wlr_scene_node_destroy(&a.scene_tree->node);
            a.scene_tree = nullptr;
        }
    }
    m_closing_layer_animations.clear();
}

void AnimationManager::tick(double now_ms) {
    if (!has_active_animations()) return;

    if (now_ms <= 0.0) {
        now_ms = get_current_time_ms();
    }

    std::vector<std::function<void()>> completions;

    // 1. Tick node animations (workspaces)
    for (auto& anim : m_animations) {
        if (!anim.node) {
            anim.completed = true;
            continue;
        }

        double elapsed = (now_ms >= anim.start_time_ms) ? (now_ms - anim.start_time_ms) : 0.0;
        float progress = 1.0f;
        if (anim.duration_ms > 0.0) {
            progress = static_cast<float>(elapsed / anim.duration_ms);
        }

        if (progress >= 1.0f) {
            progress = 1.0f;
            anim.completed = true;
        }

        float eased = anim.easing_fn ? anim.easing_fn(progress) : progress;
        int cur_x = anim.start_x + static_cast<int>(std::round(static_cast<float>(anim.target_x - anim.start_x) * eased));
        int cur_y = anim.start_y + static_cast<int>(std::round(static_cast<float>(anim.target_y - anim.start_y) * eased));

        wlr_scene_node_set_position(anim.node, cur_x, cur_y);

        if (anim.completed && anim.on_complete) {
            completions.push_back(anim.on_complete);
        }
    }

    auto it_node = std::remove_if(m_animations.begin(), m_animations.end(), [](const NodeAnimation& a) {
        return a.completed;
    });
    m_animations.erase(it_node, m_animations.end());

    // 2. Tick view geometry animations (smooth tiling transitions)
    for (auto& anim : m_geometry_animations) {
        if (!anim.view || !m_server || !m_server->is_valid_view(anim.view) || !anim.view->is_mapped() || !anim.view->get_scene_tree()) {
            anim.completed = true;
            continue;
        }

        double elapsed = (now_ms >= anim.start_time_ms) ? (now_ms - anim.start_time_ms) : 0.0;
        float progress = 1.0f;
        if (anim.duration_ms > 0.0) {
            progress = static_cast<float>(elapsed / anim.duration_ms);
        }

        if (progress >= 1.0f) {
            progress = 1.0f;
            anim.completed = true;
        }

        float eased = anim.easing_fn ? anim.easing_fn(progress) : progress;
        int cur_x = anim.start_x + static_cast<int>(std::round(static_cast<float>(anim.target_x - anim.start_x) * eased));
        int cur_y = anim.start_y + static_cast<int>(std::round(static_cast<float>(anim.target_y - anim.start_y) * eased));
        int cur_w = std::max(1, anim.start_w + static_cast<int>(std::round(static_cast<float>(anim.target_w - anim.start_w) * eased)));
        int cur_h = std::max(1, anim.start_h + static_cast<int>(std::round(static_cast<float>(anim.target_h - anim.start_h) * eased)));

        anim.view->apply_geometry_animation(cur_x, cur_y, cur_w, cur_h);

        if (anim.completed && anim.on_complete) {
            completions.push_back(anim.on_complete);
        }
    }

    auto it_geom = std::remove_if(m_geometry_animations.begin(), m_geometry_animations.end(), [](const ViewGeometryAnimation& a) {
        return a.completed;
    });
    m_geometry_animations.erase(it_geom, m_geometry_animations.end());

    // 3. Tick view pop-in / fade animations (open & close transforms)
    for (auto& anim : m_view_animations) {
        if (!anim.view || !m_server || !m_server->is_valid_view(anim.view) || !anim.view->is_mapped() || !anim.view->get_scene_tree()) {
            anim.completed = true;
            continue;
        }

        double elapsed = (now_ms >= anim.start_time_ms) ? (now_ms - anim.start_time_ms) : 0.0;

        // 1. Scale interpolation
        float scale_progress = 1.0f;
        if (anim.scale_duration_ms > 0.0) {
            scale_progress = static_cast<float>(elapsed / anim.scale_duration_ms);
        }
        if (scale_progress >= 1.0f) {
            scale_progress = 1.0f;
        }
        float scale_eased = anim.scale_easing_fn ? anim.scale_easing_fn(scale_progress) : scale_progress;
        double cur_scale = anim.start_scale + (anim.target_scale - anim.start_scale) * static_cast<double>(scale_eased);

        // 2. Fade interpolation
        float fade_progress = 1.0f;
        if (anim.fade_duration_ms > 0.0) {
            fade_progress = static_cast<float>(elapsed / anim.fade_duration_ms);
        }
        if (fade_progress >= 1.0f) {
            fade_progress = 1.0f;
        }
        float fade_eased = anim.fade_easing_fn ? anim.fade_easing_fn(fade_progress) : fade_progress;
        float cur_opacity = anim.start_opacity + (anim.target_opacity - anim.start_opacity) * fade_eased;

        anim.view->apply_animation_transform(cur_scale, cur_opacity);

        if (scale_progress >= 1.0f && fade_progress >= 1.0f) {
            anim.completed = true;
            if (anim.on_complete) {
                completions.push_back(anim.on_complete);
            }
        }
    }

    auto it_view = std::remove_if(m_view_animations.begin(), m_view_animations.end(), [](const ViewAnimation& a) {
        return a.completed;
    });
    m_view_animations.erase(it_view, m_view_animations.end());

    // 4. Tick layer surface animations (slide, popin, fade)
    for (auto& anim : m_layer_animations) {
        if (!anim.surface || !anim.surface->is_mapped()) {
            anim.completed = true;
            continue;
        }

        double elapsed = (now_ms >= anim.start_time_ms) ? (now_ms - anim.start_time_ms) : 0.0;
        float progress = 1.0f;
        if (anim.duration_ms > 0.0) {
            progress = static_cast<float>(elapsed / anim.duration_ms);
        }
        if (progress >= 1.0f) {
            progress = 1.0f;
        }
        float eased = anim.easing_fn ? anim.easing_fn(progress) : progress;

        float fade_progress = 1.0f;
        if (anim.fade_duration_ms > 0.0) {
            fade_progress = static_cast<float>(elapsed / anim.fade_duration_ms);
        }
        if (fade_progress >= 1.0f) {
            fade_progress = 1.0f;
        }
        float fade_eased = anim.fade_easing_fn ? anim.fade_easing_fn(fade_progress) : fade_progress;

        int cur_ox = anim.start_offset_x + static_cast<int>(std::round(static_cast<float>(anim.target_offset_x - anim.start_offset_x) * eased));
        int cur_oy = anim.start_offset_y + static_cast<int>(std::round(static_cast<float>(anim.target_offset_y - anim.start_offset_y) * eased));
        double cur_scale_x = anim.start_scale_x + (anim.target_scale_x - anim.start_scale_x) * static_cast<double>(eased);
        double cur_scale_y = anim.start_scale_y + (anim.target_scale_y - anim.start_scale_y) * static_cast<double>(eased);
        float cur_opacity = anim.fade_enabled ? (anim.start_opacity + (anim.target_opacity - anim.start_opacity) * fade_eased) : anim.target_opacity;

        anim.surface->apply_animation_transform(cur_ox, cur_oy, cur_scale_x, cur_scale_y, cur_opacity, anim.center_anchor);

        if (progress >= 1.0f && fade_progress >= 1.0f) {
            anim.completed = true;
            if (anim.on_complete) {
                completions.push_back(anim.on_complete);
            }
        }
    }

    auto it_layer = std::remove_if(m_layer_animations.begin(), m_layer_animations.end(), [](const LayerAnimation& a) {
        return a.completed;
    });
    m_layer_animations.erase(it_layer, m_layer_animations.end());

    // 5. Tick closing unmap layer animations
    for (auto& anim : m_closing_layer_animations) {
        if (!anim.scene_tree) {
            anim.completed = true;
            continue;
        }

        double elapsed = (now_ms >= anim.start_time_ms) ? (now_ms - anim.start_time_ms) : 0.0;
        float progress = (anim.duration_ms > 0.0) ? static_cast<float>(elapsed / anim.duration_ms) : 1.0f;
        if (progress >= 1.0f) progress = 1.0f;
        float eased = anim.easing_fn ? anim.easing_fn(progress) : progress;

        float fade_progress = (anim.fade_duration_ms > 0.0) ? static_cast<float>(elapsed / anim.fade_duration_ms) : 1.0f;
        if (fade_progress >= 1.0f) fade_progress = 1.0f;
        float fade_eased = anim.fade_easing_fn ? anim.fade_easing_fn(fade_progress) : fade_progress;

        int cur_ox = anim.start_offset_x + static_cast<int>(std::round(static_cast<float>(anim.target_offset_x - anim.start_offset_x) * eased));
        int cur_oy = anim.start_offset_y + static_cast<int>(std::round(static_cast<float>(anim.target_offset_y - anim.start_offset_y) * eased));
        double cur_scale_x = anim.start_scale_x + (anim.target_scale_x - anim.start_scale_x) * static_cast<double>(eased);
        double cur_scale_y = anim.start_scale_y + (anim.target_scale_y - anim.start_scale_y) * static_cast<double>(eased);
        float cur_opacity = anim.fade_enabled ? std::clamp(anim.start_opacity + (anim.target_opacity - anim.start_opacity) * fade_eased, 0.0f, 1.0f) : anim.target_opacity;

        int cur_x = anim.base_geo_x + cur_ox;
        int cur_y = anim.base_geo_y + cur_oy;
        if (anim.center_anchor) {
            if (cur_scale_x < 0.999) {
                double cur_w = std::max(1.0, static_cast<double>(anim.base_width) * cur_scale_x);
                cur_x += static_cast<int>(std::round((static_cast<double>(anim.base_width) - cur_w) / 2.0));
            }
            if (cur_scale_y < 0.999) {
                double cur_h = std::max(1.0, static_cast<double>(anim.base_height) * cur_scale_y);
                cur_y += static_cast<int>(std::round((static_cast<double>(anim.base_height) - cur_h) / 2.0));
            }
        }
        wlr_scene_node_set_position(&anim.scene_tree->node, cur_x, cur_y);

        struct BufferAnimData {
            double scale_x;
            double scale_y;
            float opacity;
        } bdata = { cur_scale_x, cur_scale_y, cur_opacity };

        wlr_scene_node_for_each_buffer(&anim.scene_tree->node, [](struct wlr_scene_buffer* buf, int sx, int sy, void* user_data) {
            auto* d = static_cast<BufferAnimData*>(user_data);
            if ((d->scale_x < 0.999 || d->scale_y < 0.999) && buf->buffer) {
                int sw = std::max(1, static_cast<int>(std::round(static_cast<double>(buf->buffer->width) * d->scale_x)));
                int sh = std::max(1, static_cast<int>(std::round(static_cast<double>(buf->buffer->height) * d->scale_y)));
                wlr_scene_buffer_set_dest_size(buf, sw, sh);
            } else {
                wlr_scene_buffer_set_dest_size(buf, 0, 0);
            }
            wlr_scene_buffer_set_opacity(buf, d->opacity);
        }, &bdata);

        if (anim.blur_node) {
            if (cur_opacity < 0.01f || !Config::get().is_blur_enabled()) {
                wlr_scene_node_set_enabled(&anim.blur_node->node, false);
            } else {
                wlr_scene_node_set_enabled(&anim.blur_node->node, true);
                int bw = std::max(1, static_cast<int>(std::round(static_cast<double>(anim.base_width) * cur_scale_x)));
                int bh = std::max(1, static_cast<int>(std::round(static_cast<double>(anim.base_height) * cur_scale_y)));
                wlr_scene_blur_set_size(anim.blur_node, bw, bh);
                wlr_scene_blur_set_alpha(anim.blur_node, cur_opacity);
            }
        }

        if (progress >= 1.0f && fade_progress >= 1.0f) {
            anim.completed = true;
            if (anim.on_complete) {
                completions.push_back(anim.on_complete);
            }
        }
    }

    auto it_closing = std::remove_if(m_closing_layer_animations.begin(), m_closing_layer_animations.end(), [](const ClosingLayerAnimation& a) {
        return a.completed;
    });
    m_closing_layer_animations.erase(it_closing, m_closing_layer_animations.end());

    // 6. Invoke completions
    for (auto& cb : completions) {
        if (cb) cb();
    }

    if (has_active_animations()) {
        schedule_next_frame();
    }
}

void AnimationManager::schedule_next_frame() {
    if (!m_server || !m_server->get_output_manager()) return;

    const auto& outputs = m_server->get_output_manager()->get_outputs();
    for (const auto& out : outputs) {
        if (out && out->get_wlr_output()) {
            wlr_output_schedule_frame(out->get_wlr_output());
        }
    }
}

} // namespace miquland
