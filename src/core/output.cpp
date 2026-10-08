#include "core/output.hpp"
#include "core/server.hpp"
#include "core/workspace.hpp"
#include "core/input/input.hpp"
#include "core/config/config.hpp"
#include "core/animation/animation_manager.hpp"
#include <ctime>

namespace miquland {

Output::Output(Server* server, struct wlr_output* wlr_output)
    : m_server(server), m_wlr_output(wlr_output)
{
    wlr_output_init_render(wlr_output, server->get_allocator(), server->get_renderer());

    apply_config();

    m_frame_listener.notify = handle_frame;
    wl_signal_add(&wlr_output->events.frame, &m_frame_listener);

    m_request_state_listener.notify = handle_request_state;
    wl_signal_add(&wlr_output->events.request_state, &m_request_state_listener);

    m_destroy_listener.notify = handle_destroy;
    wl_signal_add(&wlr_output->events.destroy, &m_destroy_listener);
}

void Output::apply_config() {
    const MonitorRule* rule = Config::get().find_monitor_rule(m_wlr_output->name ? m_wlr_output->name : "");

    struct wlr_output_state state;
    wlr_output_state_init(&state);

    if (rule && rule->disabled) {
        wlr_output_state_set_enabled(&state, false);
        wlr_output_commit_state(m_wlr_output, &state);
        wlr_output_state_finish(&state);

        wlr_output_layout_remove(m_server->get_output_manager()->get_layout(), m_wlr_output);
        return;
    }

    wlr_output_state_set_enabled(&state, true);

    // Mode resolution and refresh rate
    struct wlr_output_mode* best_mode = nullptr;
    if (rule && rule->width > 0 && rule->height > 0) {
        int32_t target_refresh_mHz = (int32_t)(rule->refresh_rate * 1000.0);
        struct wlr_output_mode* m;
        wl_list_for_each(m, &m_wlr_output->modes, link) {
            if (m->width == rule->width && m->height == rule->height) {
                if (target_refresh_mHz > 0) {
                    if (std::abs(m->refresh - target_refresh_mHz) < 1000) {
                        best_mode = m;
                        break;
                    }
                } else {
                    if (!best_mode || m->refresh > best_mode->refresh) {
                        best_mode = m;
                    }
                }
            }
        }
        if (best_mode) {
            wlr_output_state_set_mode(&state, best_mode);
        } else {
            wlr_output_state_set_custom_mode(&state, rule->width, rule->height, target_refresh_mHz);
        }
    } else {
        struct wlr_output_mode* mode = wlr_output_preferred_mode(m_wlr_output);
        if (mode != nullptr) {
            wlr_output_state_set_mode(&state, mode);
        }
    }

    // Scale
    float scale = (rule && rule->scale > 0.0) ? (float)rule->scale : 1.0f;
    wlr_output_state_set_scale(&state, scale);
    if (m_server && m_server->get_input_manager()) {
        m_server->get_input_manager()->load_cursor_scale(scale);
    }

    // Transform
    enum wl_output_transform transform = rule ? rule->transform : WL_OUTPUT_TRANSFORM_NORMAL;
    wlr_output_state_set_transform(&state, transform);

    wlr_output_commit_state(m_wlr_output, &state);
    wlr_output_state_finish(&state);

    m_usable_area = {
        .x = 0,
        .y = 0,
        .width = m_wlr_output->width,
        .height = m_wlr_output->height
    };

    if (!m_scene_output) {
        m_scene_output = wlr_scene_output_create(m_server->get_scene(), m_wlr_output);
    }

    struct wlr_output_layout* layout = m_server->get_output_manager()->get_layout();
    if (layout) {
        if (rule && rule->x >= 0 && rule->y >= 0) {
            wlr_output_layout_add(layout, m_wlr_output, rule->x, rule->y);
        } else {
            wlr_output_layout_add_auto(layout, m_wlr_output);
        }
    }

    if (m_server) {
        m_server->arrange_layers(m_wlr_output);
    }
}

Output::~Output() {
    wl_list_remove(&m_frame_listener.link);
    wl_list_remove(&m_request_state_listener.link);
    wl_list_remove(&m_destroy_listener.link);
}

void Output::handle_frame(struct wl_listener* listener, void* data) {
    Output* output = wl_container_of(listener, output, m_frame_listener);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double now_ms = static_cast<double>(now.tv_sec) * 1000.0 + static_cast<double>(now.tv_nsec) / 1000000.0;

    if (output->m_server && output->m_server->get_animation_manager()) {
        output->m_server->get_animation_manager()->tick(now_ms);
    }

    wlr_scene_output_commit(output->m_scene_output, nullptr);

    wlr_scene_output_send_frame_done(output->m_scene_output, &now);
}

bool Output::commit_tearing() {
    if (!m_scene_output || !m_wlr_output) return false;

    struct wlr_output_state state;
    wlr_output_state_init(&state);

    if (!wlr_scene_output_build_state(m_scene_output, &state, nullptr)) {
        wlr_output_state_finish(&state);
        return false;
    }

    state.tearing_page_flip = true;

    bool ok = wlr_output_commit_state(m_wlr_output, &state);
    wlr_output_state_finish(&state);

    if (ok) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        wlr_scene_output_send_frame_done(m_scene_output, &now);
        return true;
    }

    wlr_output_schedule_frame(m_wlr_output);
    return false;
}

void Output::handle_request_state(struct wl_listener* listener, void* data) {
    Output* output = wl_container_of(listener, output, m_request_state_listener);
    auto* event = static_cast<struct wlr_output_event_request_state*>(data);
    wlr_output_commit_state(output->m_wlr_output, event->state);
    if (output->m_server->get_output_manager()) {
        output->m_server->get_output_manager()->update_manager_config();
    }
}

void Output::handle_destroy(struct wl_listener* listener, void* data) {
    Output* output = wl_container_of(listener, output, m_destroy_listener);
    output->m_server->get_output_manager()->remove_output(output);
}

OutputManager::OutputManager(Server* server)
    : m_server(server)
{
    m_output_layout = wlr_output_layout_create(server->get_display());
    wlr_scene_attach_output_layout(server->get_scene(), m_output_layout);

    m_new_output_listener.notify = handle_new_output;
    wl_signal_add(&server->get_backend()->events.new_output, &m_new_output_listener);

    m_output_manager_v1 = wlr_output_manager_v1_create(server->get_display());
    if (m_output_manager_v1) {
        m_output_manager_apply_listener.notify = handle_manager_apply;
        wl_signal_add(&m_output_manager_v1->events.apply, &m_output_manager_apply_listener);

        m_output_manager_test_listener.notify = handle_manager_test;
        wl_signal_add(&m_output_manager_v1->events.test, &m_output_manager_test_listener);
    }
}

OutputManager::~OutputManager() {
    wl_list_remove(&m_new_output_listener.link);
    if (m_output_manager_v1) {
        wl_list_remove(&m_output_manager_apply_listener.link);
        wl_list_remove(&m_output_manager_test_listener.link);
    }
}

struct wlr_box OutputManager::get_primary_geometry() const {
    struct wlr_box box = { 0, 0, 0, 0 };
    if (m_output_layout) {
        wlr_output_layout_get_box(m_output_layout, nullptr, &box);
    }
    return box;
}

struct wlr_box OutputManager::get_primary_usable_geometry() const {
    auto* primary = get_primary_output();
    if (primary) {
        const auto& area = primary->get_usable_area();
        if (area.width > 0 && area.height > 0) {
            return area;
        }
    }
    return get_primary_geometry();
}

Output* OutputManager::find_output(struct wlr_output* wlr_out) const {
    for (const auto& out : m_outputs) {
        if (out && out->get_wlr_output() == wlr_out) {
            return out.get();
        }
    }
    return nullptr;
}

void OutputManager::add_output(Output* output) {
    m_outputs.emplace_back(output);
    if (m_server->get_ext_workspace_group() && output && output->get_wlr_output()) {
        wlr_ext_workspace_group_handle_v1_output_enter(m_server->get_ext_workspace_group(), output->get_wlr_output());
    }
    if (m_server->get_workspace_manager()) {
        m_server->get_workspace_manager()->recalculate_layout();
    }
    if (m_server->get_input_manager()) {
        m_server->get_input_manager()->reapply_device_config();
    }
    update_manager_config();
}

void OutputManager::remove_output(Output* output) {
    if (m_server->get_ext_workspace_group() && output && output->get_wlr_output()) {
        wlr_ext_workspace_group_handle_v1_output_leave(m_server->get_ext_workspace_group(), output->get_wlr_output());
    }
    for (auto it = m_outputs.begin(); it != m_outputs.end(); ++it) {
        if (it->get() == output) {
            m_outputs.erase(it);
            break;
        }
    }
    if (m_server->get_workspace_manager()) {
        m_server->get_workspace_manager()->recalculate_layout();
    }
    if (m_server->get_input_manager()) {
        m_server->get_input_manager()->reapply_device_config();
    }
    update_manager_config();
}

void OutputManager::handle_new_output(struct wl_listener* listener, void* data) {
    OutputManager* manager = wl_container_of(listener, manager, m_new_output_listener);
    auto* wlr_output = static_cast<struct wlr_output*>(data);

    Output* output = new Output(manager->m_server, wlr_output);
    manager->add_output(output);
}

void OutputManager::reapply_all_configs() {
    for (const auto& out : m_outputs) {
        if (out) {
            out->apply_config();
        }
    }
    update_manager_config();
    if (m_server->get_workspace_manager()) {
        m_server->get_workspace_manager()->recalculate_layout();
    }
    if (m_server->get_input_manager()) {
        m_server->get_input_manager()->reapply_device_config();
    }
}

void OutputManager::apply_all_configs() {
    reapply_all_configs();
}

void OutputManager::update_manager_config() {
    if (!m_output_manager_v1) return;

    struct wlr_output_configuration_v1* config = wlr_output_configuration_v1_create();
    for (const auto& out : m_outputs) {
        if (!out || !out->get_wlr_output()) continue;
        struct wlr_output* wlr_out = out->get_wlr_output();
        struct wlr_output_configuration_head_v1* config_head =
            wlr_output_configuration_head_v1_create(config, wlr_out);

        struct wlr_box box = { 0, 0, 0, 0 };
        wlr_output_layout_get_box(m_output_layout, wlr_out, &box);
        config_head->state.x = box.x;
        config_head->state.y = box.y;
    }
    wlr_output_manager_v1_set_configuration(m_output_manager_v1, config);
}

void OutputManager::handle_manager_apply(struct wl_listener* listener, void* data) {
    OutputManager* manager = wl_container_of(listener, manager, m_output_manager_apply_listener);
    auto* config = static_cast<struct wlr_output_configuration_v1*>(data);
    manager->apply_config(config, false);
}

void OutputManager::handle_manager_test(struct wl_listener* listener, void* data) {
    OutputManager* manager = wl_container_of(listener, manager, m_output_manager_test_listener);
    auto* config = static_cast<struct wlr_output_configuration_v1*>(data);
    manager->apply_config(config, true);
}

bool OutputManager::apply_config(struct wlr_output_configuration_v1* config, bool test_only) {
    bool ok = true;
    struct wlr_output_configuration_head_v1* config_head;
    wl_list_for_each(config_head, &config->heads, link) {
        struct wlr_output* wlr_out = config_head->state.output;
        struct wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_head_v1_state_apply(&config_head->state, &state);
        if (!wlr_output_test_state(wlr_out, &state)) {
            ok = false;
        }
        wlr_output_state_finish(&state);
        if (!ok) break;
    }

    if (!ok) {
        wlr_output_configuration_v1_send_failed(config);
        wlr_output_configuration_v1_destroy(config);
        return false;
    }

    if (test_only) {
        wlr_output_configuration_v1_send_succeeded(config);
        wlr_output_configuration_v1_destroy(config);
        return true;
    }

    wl_list_for_each(config_head, &config->heads, link) {
        struct wlr_output* wlr_out = config_head->state.output;
        struct wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_head_v1_state_apply(&config_head->state, &state);
        wlr_output_commit_state(wlr_out, &state);
        wlr_output_state_finish(&state);

        if (config_head->state.enabled) {
            wlr_output_layout_add(m_output_layout, wlr_out, config_head->state.x, config_head->state.y);
        } else {
            wlr_output_layout_remove(m_output_layout, wlr_out);
        }

        Output* out = find_output(wlr_out);
        if (out && m_server) {
            m_server->arrange_layers(wlr_out);
        }
    }

    wlr_output_configuration_v1_send_succeeded(config);
    wlr_output_configuration_v1_destroy(config);

    update_manager_config();
    if (m_server->get_workspace_manager()) {
        m_server->get_workspace_manager()->recalculate_layout();
    }
    return true;
}

} // namespace miquland
