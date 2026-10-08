#pragma once

#include "core/common/util.hpp"
#include <vector>
#include <memory>

namespace miquland {

class Server;

class Output {
public:
    Output(Server* server, struct wlr_output* wlr_output);
    ~Output();

    struct wlr_output* get_wlr_output() const { return m_wlr_output; }
    struct wlr_scene_output* get_scene_output() const { return m_scene_output; }

    void apply_config();

    const struct wlr_box& get_usable_area() const { return m_usable_area; }
    void set_usable_area(const struct wlr_box& area) { m_usable_area = area; }

    bool commit_tearing();

private:
    static void handle_frame(struct wl_listener* listener, void* data);
    static void handle_request_state(struct wl_listener* listener, void* data);
    static void handle_destroy(struct wl_listener* listener, void* data);

    Server* m_server = nullptr;
    struct wlr_output* m_wlr_output = nullptr;
    struct wlr_scene_output* m_scene_output = nullptr;
    struct wlr_box m_usable_area = { 0, 0, 0, 0 };

    struct wl_listener m_frame_listener;
    struct wl_listener m_request_state_listener;
    struct wl_listener m_destroy_listener;
};

class OutputManager {
public:
    explicit OutputManager(Server* server);
    ~OutputManager();

    struct wlr_output_layout* get_layout() const { return m_output_layout; }
    struct wlr_box get_primary_geometry() const;
    struct wlr_box get_primary_usable_geometry() const;

    Output* get_primary_output() const {
        return m_outputs.empty() ? nullptr : m_outputs.front().get();
    }
    Output* find_output(struct wlr_output* wlr_out) const;
    const std::vector<std::unique_ptr<Output>>& get_outputs() const { return m_outputs; }

    void add_output(Output* output);
    void remove_output(Output* output);

    void apply_all_configs();
    void reapply_all_configs();
    void update_manager_config();

private:
    static void handle_new_output(struct wl_listener* listener, void* data);
    static void handle_manager_apply(struct wl_listener* listener, void* data);
    static void handle_manager_test(struct wl_listener* listener, void* data);

    bool apply_config(struct wlr_output_configuration_v1* config, bool test_only);

    Server* m_server = nullptr;
    struct wlr_output_layout* m_output_layout = nullptr;
    struct wlr_output_manager_v1* m_output_manager_v1 = nullptr;
    std::vector<std::unique_ptr<Output>> m_outputs;

    struct wl_listener m_new_output_listener;
    struct wl_listener m_output_manager_apply_listener;
    struct wl_listener m_output_manager_test_listener;
};

} // namespace miquland
