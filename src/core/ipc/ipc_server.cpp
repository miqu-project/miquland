#include "core/ipc/ipc_server.hpp"
#include "core/server.hpp"
#include "core/view.hpp"
#include "core/workspace.hpp"
#include "core/output.hpp"
#include "core/input/input.hpp"
#include "core/config/config.hpp"
#include "core/common/util.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <sstream>
#include <algorithm>

namespace miquland {

IpcServer::IpcServer(Server* server)
    : m_server(server) {}

IpcServer::~IpcServer() {
    for (auto& client : m_clients) {
        if (client->event_source) {
            wl_event_source_remove(client->event_source);
            client->event_source = nullptr;
        }
        if (client->fd >= 0) {
            ::close(client->fd);
            client->fd = -1;
        }
    }
    m_clients.clear();

    if (m_listen_event_source) {
        wl_event_source_remove(m_listen_event_source);
        m_listen_event_source = nullptr;
    }

    if (m_listen_fd >= 0) {
        ::close(m_listen_fd);
        m_listen_fd = -1;
    }

    if (!m_socket_path.empty()) {
        unlink(m_socket_path.c_str());
        m_socket_path.clear();
    }
}

bool IpcServer::init(const std::string& display_name) {
    const char* runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (runtime_dir && runtime_dir[0] != '\0') {
        m_socket_path = std::string(runtime_dir) + "/miquland-" + display_name + ".sock";
    } else {
        m_socket_path = "/tmp/miquland-" + std::to_string(getuid()) + "-" + display_name + ".sock";
    }

    unlink(m_socket_path.c_str());

    m_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_listen_fd < 0) {
        log_error("Failed to create IPC socket: " + std::string(strerror(errno)));
        return false;
    }

    int flags = fcntl(m_listen_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(m_listen_fd, F_SETFL, flags | O_NONBLOCK | O_CLOEXEC);
    }

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, m_socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(m_listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        log_error("Failed to bind IPC socket to " + m_socket_path + ": " + std::string(strerror(errno)));
        ::close(m_listen_fd);
        m_listen_fd = -1;
        return false;
    }

    chmod(m_socket_path.c_str(), 0700);

    if (listen(m_listen_fd, 16) < 0) {
        log_error("Failed to listen on IPC socket: " + std::string(strerror(errno)));
        ::close(m_listen_fd);
        m_listen_fd = -1;
        return false;
    }

    m_listen_event_source = wl_event_loop_add_fd(
        m_server->get_event_loop(),
        m_listen_fd,
        WL_EVENT_READABLE,
        handle_listen_readable,
        this
    );

    if (!m_listen_event_source) {
        log_error("Failed to register IPC socket with Wayland event loop");
        ::close(m_listen_fd);
        m_listen_fd = -1;
        return false;
    }

    setenv("MIQULAND_SOCKET", m_socket_path.c_str(), 1);
    log_info("IPC server listening on " + m_socket_path);
    return true;
}

int IpcServer::handle_listen_readable(int /*fd*/, uint32_t mask, void* data) {
    auto* self = static_cast<IpcServer*>(data);
    if (mask & WL_EVENT_READABLE) {
        self->accept_client();
    }
    return 0;
}

void IpcServer::accept_client() {
    struct sockaddr_un client_addr = {};
    socklen_t client_len = sizeof(client_addr);

    int client_fd = accept4(m_listen_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_error("Failed to accept IPC client: " + std::string(strerror(errno)));
        }
        return;
    }

    auto client = std::make_unique<ClientConnection>();
    client->fd = client_fd;
    client->event_source = wl_event_loop_add_fd(
        m_server->get_event_loop(),
        client_fd,
        WL_EVENT_READABLE | WL_EVENT_HANGUP | WL_EVENT_ERROR,
        handle_client_readable,
        this
    );

    if (!client->event_source) {
        ::close(client_fd);
        return;
    }

    m_clients.push_back(std::move(client));
}

int IpcServer::handle_client_readable(int fd, uint32_t mask, void* data) {
    auto* self = static_cast<IpcServer*>(data);
    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
        self->close_client(fd);
        return 0;
    }

    if (mask & WL_EVENT_READABLE) {
        auto it = std::find_if(self->m_clients.begin(), self->m_clients.end(), [fd](const auto& c) {
            return c->fd == fd;
        });
        if (it != self->m_clients.end()) {
            self->process_client_data(**it);
        }
    }
    return 0;
}

void IpcServer::close_client(int fd) {
    auto it = std::find_if(m_clients.begin(), m_clients.end(), [fd](const auto& c) {
        return c->fd == fd;
    });
    if (it != m_clients.end()) {
        if ((*it)->event_source) {
            wl_event_source_remove((*it)->event_source);
            (*it)->event_source = nullptr;
        }
        if ((*it)->fd >= 0) {
            ::close((*it)->fd);
            (*it)->fd = -1;
        }
        m_clients.erase(it);
    }
}

void IpcServer::process_client_data(ClientConnection& client) {
    uint8_t buffer[4096];
    while (true) {
        ssize_t n = read(client.fd, buffer, sizeof(buffer));
        if (n > 0) {
            client.rx_buffer.insert(client.rx_buffer.end(), buffer, buffer + n);
        } else if (n == 0) {
            close_client(client.fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            close_client(client.fd);
            return;
        }
    }

    while (!client.rx_buffer.empty()) {
        if (client.rx_buffer.size() >= 4 && memcmp(client.rx_buffer.data(), ipc::IPC_MAGIC, 4) == 0) {
            if (client.rx_buffer.size() < sizeof(ipc::IpcHeader)) {
                break;
            }
            ipc::IpcHeader hdr;
            memcpy(&hdr, client.rx_buffer.data(), sizeof(hdr));

            if (client.rx_buffer.size() < sizeof(ipc::IpcHeader) + hdr.payload_len) {
                break;
            }

            std::string payload(
                reinterpret_cast<const char*>(client.rx_buffer.data() + sizeof(ipc::IpcHeader)),
                hdr.payload_len
            );
            client.rx_buffer.erase(client.rx_buffer.begin(), client.rx_buffer.begin() + sizeof(ipc::IpcHeader) + hdr.payload_len);

            handle_client_message(client, static_cast<ipc::MessageType>(hdr.type), payload);
        } else {
            auto newline_it = std::find(client.rx_buffer.begin(), client.rx_buffer.end(), '\n');
            if (newline_it == client.rx_buffer.end()) {
                break;
            }

            std::string line(reinterpret_cast<const char*>(client.rx_buffer.data()), newline_it - client.rx_buffer.begin());
            client.rx_buffer.erase(client.rx_buffer.begin(), newline_it + 1);

            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            if (!line.empty()) {
                handle_client_message(client, ipc::MessageType::Command, line);
            }
        }
    }
}

void IpcServer::send_response(ClientConnection& client, ipc::MessageType type, const std::string& payload) {
    ipc::IpcHeader hdr;
    memcpy(hdr.magic, ipc::IPC_MAGIC, 4);
    hdr.type = static_cast<uint32_t>(type);
    hdr.payload_len = static_cast<uint32_t>(payload.size());

    write(client.fd, &hdr, sizeof(hdr));
    if (!payload.empty()) {
        write(client.fd, payload.data(), payload.size());
    }
}

void IpcServer::handle_client_message(ClientConnection& client, ipc::MessageType type, const std::string& payload) {
    if (type == ipc::MessageType::Subscribe) {
        std::istringstream iss(payload);
        std::string event_name;
        while (iss >> event_name) {
            client.subscriptions.insert(event_name);
        }
        send_response(client, ipc::MessageType::Response, R"({"status":"subscribed"})");
        return;
    }

    std::string cmd = payload;
    std::string args;
    size_t space_pos = payload.find(' ');
    if (space_pos != std::string::npos) {
        cmd = payload.substr(0, space_pos);
        args = payload.substr(space_pos + 1);
        while (!args.empty() && args.front() == ' ') args.erase(0, 1);
    }

    if (cmd == "workspaces") {
        send_response(client, ipc::MessageType::Response, handle_workspaces_query());
    } else if (cmd == "clients" || cmd == "windows") {
        send_response(client, ipc::MessageType::Response, handle_clients_query());
    } else if (cmd == "monitors" || cmd == "outputs") {
        send_response(client, ipc::MessageType::Response, handle_monitors_query());
    } else if (cmd == "activewindow") {
        send_response(client, ipc::MessageType::Response, handle_activewindow_query());
    } else if (cmd == "activeworkspace") {
        send_response(client, ipc::MessageType::Response, handle_activeworkspace_query());
    } else if (cmd == "version") {
        send_response(client, ipc::MessageType::Response, handle_version_query());
    } else if (cmd == "dispatch") {
        send_response(client, ipc::MessageType::Response, handle_dispatch(args));
    } else if (cmd == "shutdown") {
        uint32_t timeout_ms = 2500;
        if (!args.empty()) {
            try { timeout_ms = std::stoul(args); } catch (...) {}
        }
        send_response(client, ipc::MessageType::Response, R"({"status":"ok","action":"shutdown","timeout_ms":)" + std::to_string(timeout_ms) + "}");
        m_server->graceful_shutdown(timeout_ms);
    } else if (cmd == "exit" || cmd == "quit") {
        send_response(client, ipc::MessageType::Response, R"({"status":"ok","action":"exit"})");
        m_server->terminate();
    } else if (cmd == "reload") {
        m_server->reload_config();
        send_response(client, ipc::MessageType::Response, R"({"status":"ok","action":"reload"})");
    } else if (cmd == "exec") {
        if (m_server->get_input_manager() && !args.empty()) {
            m_server->get_input_manager()->spawn_command(args.c_str());
            send_response(client, ipc::MessageType::Response, R"({"status":"ok","action":"exec"})");
        } else {
            send_response(client, ipc::MessageType::Error, R"({"status":"error","message":"No command specified"})");
        }
    } else {
        std::string res = handle_dispatch(payload);
        send_response(client, ipc::MessageType::Response, res);
    }
}

std::string IpcServer::handle_workspaces_query() {
    WorkspaceManager* wm = m_server->get_workspace_manager();
    if (!wm) return "[]";

    std::ostringstream ss;
    ss << "[";
    bool first = true;
    for (const auto& [id, ws] : wm->get_workspaces_map()) {
        if (!ws) continue;
        if (!first) ss << ",";
        first = false;

        ss << "{"
           << "\"id\":" << id << ","
           << "\"active\":" << (id == wm->get_active_workspace_id() ? "true" : "false") << ","
           << "\"visible\":" << (ws->is_visible() ? "true" : "false") << ","
           << "\"view_count\":" << ws->total_view_count() << ","
           << "\"tiled_view_count\":" << ws->get_tiled_views().size() << ","
           << "\"floating_view_count\":" << ws->get_floating_views().size()
           << "}";
    }
    ss << "]";
    return ss.str();
}

std::string IpcServer::handle_clients_query() {
    std::ostringstream ss;
    ss << "[";
    bool first = true;
    for (const auto& v_ptr : m_server->get_views()) {
        View* v = v_ptr.get();
        if (!v || !v->is_mapped()) continue;
        if (!first) ss << ",";
        first = false;

        size_t ws_id = v->get_workspace() ? v->get_workspace()->get_id() : 0;
        ss << "{"
           << "\"id\":" << v->get_id() << ","
           << "\"title\":\"" << ipc::escape_json(v->get_title()) << "\","
           << "\"app_id\":\"" << ipc::escape_json(v->get_app_id()) << "\","
           << "\"workspace_id\":" << ws_id << ","
           << "\"focused\":" << (v->is_focused() ? "true" : "false") << ","
           << "\"floating\":" << (v->is_floating() ? "true" : "false") << ","
           << "\"fullscreen\":" << (v->is_fullscreen() ? "true" : "false") << ","
           << "\"geometry\":{"
           << "\"x\":" << v->get_x() << ","
           << "\"y\":" << v->get_y() << ","
           << "\"width\":" << v->get_width() << ","
           << "\"height\":" << v->get_height()
           << "}}";
    }
    ss << "]";
    return ss.str();
}

std::string IpcServer::handle_monitors_query() {
    OutputManager* om = m_server->get_output_manager();
    if (!om) return "[]";

    std::ostringstream ss;
    ss << "[";
    bool first = true;
    for (const auto& out : om->get_outputs()) {
        if (!out) continue;
        struct wlr_output* wlr_out = out->get_wlr_output();
        if (!wlr_out) continue;
        if (!first) ss << ",";
        first = false;

        const struct wlr_box& usable = out->get_usable_area();
        ss << "{"
           << "\"name\":\"" << ipc::escape_json(wlr_out->name ? wlr_out->name : "unknown") << "\","
           << "\"width\":" << wlr_out->width << ","
           << "\"height\":" << wlr_out->height << ","
           << "\"refresh_rate_mhz\":" << wlr_out->refresh << ","
           << "\"scale\":" << wlr_out->scale << ","
           << "\"usable_area\":{"
           << "\"x\":" << usable.x << ","
           << "\"y\":" << usable.y << ","
           << "\"width\":" << usable.width << ","
           << "\"height\":" << usable.height
           << "}}";
    }
    ss << "]";
    return ss.str();
}

std::string IpcServer::handle_activewindow_query() {
    View* v = m_server->get_focused_view();
    if (!v || !v->is_mapped()) return "null";

    size_t ws_id = v->get_workspace() ? v->get_workspace()->get_id() : 0;
    std::ostringstream ss;
    ss << "{"
       << "\"id\":" << v->get_id() << ","
       << "\"title\":\"" << ipc::escape_json(v->get_title()) << "\","
       << "\"app_id\":\"" << ipc::escape_json(v->get_app_id()) << "\","
       << "\"workspace_id\":" << ws_id << ","
       << "\"focused\":true,"
       << "\"floating\":" << (v->is_floating() ? "true" : "false") << ","
       << "\"fullscreen\":" << (v->is_fullscreen() ? "true" : "false") << ","
       << "\"geometry\":{"
       << "\"x\":" << v->get_x() << ","
       << "\"y\":" << v->get_y() << ","
       << "\"width\":" << v->get_width() << ","
       << "\"height\":" << v->get_height()
       << "}}";
    return ss.str();
}

std::string IpcServer::handle_activeworkspace_query() {
    WorkspaceManager* wm = m_server->get_workspace_manager();
    if (!wm) return "null";
    Workspace* ws = wm->get_active_workspace();
    if (!ws) return "null";

    std::ostringstream ss;
    ss << "{"
       << "\"id\":" << ws->get_id() << ","
       << "\"active\":true,"
       << "\"visible\":" << (ws->is_visible() ? "true" : "false") << ","
       << "\"view_count\":" << ws->total_view_count() << ","
       << "\"tiled_view_count\":" << ws->get_tiled_views().size() << ","
       << "\"floating_view_count\":" << ws->get_floating_views().size()
       << "}";
    return ss.str();
}

std::string IpcServer::handle_version_query() {
    return R"({"name":"miquland","version":"1.0.0","protocol":"miqu-ipc-v1"})";
}

std::string IpcServer::handle_dispatch(const std::string& args) {
    if (args.empty()) {
        return R"({"status":"error","message":"Empty dispatch command"})";
    }

    std::istringstream iss(args);
    std::string sub_action;
    iss >> sub_action;

    WorkspaceManager* wm = m_server->get_workspace_manager();

    if (sub_action == "close") {
        uint64_t target_id = 0;
        View* target = nullptr;
        if (iss >> target_id) {
            target = m_server->get_view_by_id(target_id);
            if (!target) {
                return R"({"status":"error","message":"Target window not found"})";
            }
        } else {
            target = m_server->get_focused_view();
            if (!target) {
                return R"({"status":"error","message":"No focused window to close"})";
            }
        }
        target->close();
        return R"({"status":"ok","action":"close","target_id":)" + std::to_string(target->get_id()) + "}";
    }

    if (sub_action == "kill") {
        uint64_t target_id = 0;
        View* target = nullptr;
        if (iss >> target_id) {
            target = m_server->get_view_by_id(target_id);
            if (!target) {
                return R"({"status":"error","message":"Target window not found"})";
            }
        } else {
            target = m_server->get_focused_view();
            if (!target) {
                return R"({"status":"error","message":"No focused window to kill"})";
            }
        }

        uint64_t killed_id = target->get_id();
        if (target->get_type() == ViewType::Xdg && target->get_xdg_toplevel() && target->get_xdg_toplevel()->base) {
            struct wl_client* client = wl_resource_get_client(target->get_xdg_toplevel()->base->resource);
            if (client) {
                wl_client_destroy(client);
            }
        } else if (target->get_type() == ViewType::XWayland && target->get_xwayland_surface() && target->get_xwayland_surface()->surface) {
            struct wl_client* client = wl_resource_get_client(target->get_xwayland_surface()->surface->resource);
            if (client) {
                wl_client_destroy(client);
            }
        } else {
            target->close();
        }
        return R"({"status":"ok","action":"kill","target_id":)" + std::to_string(killed_id) + "}";
    }

    if (sub_action == "focus" || sub_action == "focus_next" || sub_action == "focus_prev" ||
        sub_action == "next_window" || sub_action == "prev_window") {
        if (!wm) return R"({"status":"error","message":"Workspace manager unavailable"})";

        std::string target_spec;
        if (sub_action == "focus") {
            iss >> target_spec;
        } else if (sub_action == "focus_next" || sub_action == "next_window") {
            target_spec = "next";
        } else {
            target_spec = "prev";
        }

        if (target_spec.empty() || target_spec == "next") {
            wm->focus_next_view();
            return R"({"status":"ok","action":"focus","direction":"next"})";
        } else if (target_spec == "prev") {
            wm->focus_prev_view();
            return R"({"status":"ok","action":"focus","direction":"prev"})";
        } else if (target_spec == "left" || target_spec == "right" || target_spec == "up" || target_spec == "down") {
            if (wm->focus_direction(target_spec)) {
                return R"({"status":"ok","action":"focus","direction":")" + target_spec + "\"}";
            }
            return R"({"status":"error","message":"No window in direction: )" + target_spec + "\"}";
        } else {
            try {
                uint64_t target_id = std::stoull(target_spec);
                View* v = m_server->get_view_by_id(target_id);
                if (v && v->is_mapped()) {
                    v->focus();
                    return R"({"status":"ok","action":"focus","target_id":)" + std::to_string(target_id) + "}";
                }
                return R"({"status":"error","message":"Window ID not found"})";
            } catch (...) {
                return R"({"status":"error","message":"Invalid focus argument: )" + ipc::escape_json(target_spec) + "\"}";
            }
        }
    }

    if (sub_action == "workspace" || sub_action.rfind("ws_", 0) == 0) {
        if (!wm) return R"({"status":"error","message":"Workspace manager unavailable"})";
        size_t ws_id = 0;
        if (sub_action.rfind("ws_", 0) == 0) {
            try { ws_id = std::stoul(sub_action.substr(3)); } catch (...) {}
        } else {
            iss >> ws_id;
        }
        if (ws_id > 0) {
            wm->switch_to_workspace(ws_id);
            return R"({"status":"ok","action":"workspace","workspace_id":)" + std::to_string(ws_id) + "}";
        }
        return R"({"status":"error","message":"Invalid workspace id"})";
    }

    if (sub_action == "movetoworkspace" || sub_action == "move_to_workspace" ||
        sub_action.rfind("move_ws_", 0) == 0 || sub_action.rfind("move_to_ws_", 0) == 0) {
        if (!wm) return R"({"status":"error","message":"Workspace manager unavailable"})";
        size_t ws_id = 0;
        if (sub_action.rfind("move_to_ws_", 0) == 0) {
            try { ws_id = std::stoul(sub_action.substr(11)); } catch (...) {}
        } else if (sub_action.rfind("move_ws_", 0) == 0) {
            try { ws_id = std::stoul(sub_action.substr(8)); } catch (...) {}
        } else {
            iss >> ws_id;
        }

        if (ws_id > 0) {
            uint64_t target_id = 0;
            View* target = nullptr;
            if (iss >> target_id) {
                target = m_server->get_view_by_id(target_id);
            } else {
                target = m_server->get_focused_view();
            }

            if (target) {
                wm->move_view_to_workspace(target, ws_id);
                return R"({"status":"ok","action":"movetoworkspace","workspace_id":)" + std::to_string(ws_id) +
                    ",\"target_id\":" + std::to_string(target->get_id()) + "}";
            }
            return R"({"status":"error","message":"Target window not found"})";
        }
        return R"({"status":"error","message":"Invalid workspace id"})";
    }

    if (sub_action == "move" || sub_action == "moveto") {
        int x = 0, y = 0;
        if (!(iss >> x >> y)) {
            return R"({"status":"error","message":"Usage: dispatch move/moveto <x> <y> [target_id]"})";
        }
        uint64_t target_id = 0;
        View* target = (iss >> target_id) ? m_server->get_view_by_id(target_id) : m_server->get_focused_view();
        if (!target) return R"({"status":"error","message":"Target window not found"})";
        if (!target->is_floating()) return R"({"status":"error","message":"Window is not floating"})";

        int new_x = (sub_action == "move") ? (target->get_x() + x) : x;
        int new_y = (sub_action == "move") ? (target->get_y() + y) : y;
        target->set_geometry(new_x, new_y, target->get_width(), target->get_height());
        target->set_user_moved(true);
        target->update_frame();
        return R"({"status":"ok","action":")" + sub_action + R"(","x":)" + std::to_string(new_x) + R"(,"y":)" + std::to_string(new_y) + "}";
    }

    if (sub_action == "resize" || sub_action == "resizeto") {
        int w = 0, h = 0;
        if (!(iss >> w >> h)) {
            return R"({"status":"error","message":"Usage: dispatch resize/resizeto <w> <h> [target_id]"})";
        }
        uint64_t target_id = 0;
        View* target = (iss >> target_id) ? m_server->get_view_by_id(target_id) : m_server->get_focused_view();
        if (!target) return R"({"status":"error","message":"Target window not found"})";

        if (target->is_floating()) {
            int new_w = (sub_action == "resize") ? std::max(100, target->get_width() + w) : std::max(100, w);
            int new_h = (sub_action == "resize") ? std::max(80, target->get_height() + h) : std::max(80, h);
            target->set_geometry(target->get_x(), target->get_y(), new_w, new_h);
            target->update_frame();
            return R"({"status":"ok","action":")" + sub_action + R"(","width":)" + std::to_string(new_w) + R"(,"height":)" + std::to_string(new_h) + "}";
        } else {
            if (wm && wm->get_active_workspace()) {
                Workspace* ws = wm->get_active_workspace();
                double delta = static_cast<double>(w) / 1000.0;
                ws->set_split_ratio(ws->get_split_ratio() + delta);
                wm->recalculate_layout();
                return R"({"status":"ok","action":"resize_split","split_ratio":)" + std::to_string(ws->get_split_ratio()) + "}";
            }
        }
    }

    if (sub_action == "toggle_floating" || sub_action == "toggle_float" || sub_action == "set_floating") {
        uint64_t target_id = 0;
        int explicit_val = -1;
        if (sub_action == "set_floating") {
            iss >> explicit_val;
        }
        View* target = (iss >> target_id) ? m_server->get_view_by_id(target_id) : m_server->get_focused_view();
        if (!target) return R"({"status":"error","message":"Target window not found"})";

        if (explicit_val != -1) {
            bool target_float = (explicit_val != 0);
            if (target->is_floating() != target_float && target->get_workspace()) {
                target->get_workspace()->toggle_floating(target);
                if (wm) wm->recalculate_layout();
            }
        } else {
            if (target->get_workspace()) {
                target->get_workspace()->toggle_floating(target);
                if (wm) wm->recalculate_layout();
            }
        }
        return R"({"status":"ok","action":"toggle_floating","floating":)" + std::string(target->is_floating() ? "true" : "false") + "}";
    }

    if (sub_action == "toggle_fullscreen" || sub_action == "fullscreen" || sub_action == "set_fullscreen") {
        uint64_t target_id = 0;
        int explicit_val = -1;
        if (sub_action == "set_fullscreen") {
            iss >> explicit_val;
        }
        View* target = (iss >> target_id) ? m_server->get_view_by_id(target_id) : m_server->get_focused_view();
        if (!target) return R"({"status":"error","message":"Target window not found"})";

        bool new_fs = (explicit_val != -1) ? (explicit_val != 0) : !target->is_fullscreen();
        target->set_fullscreen(new_fs);
        return R"({"status":"ok","action":"fullscreen","fullscreen":)" + std::string(target->is_fullscreen() ? "true" : "false") + "}";
    }

    if (sub_action == "toggle_layout" || sub_action == "layout_toggle" || sub_action == "cycle_layout") {
        if (wm) {
            std::string dir;
            iss >> dir;
            bool reverse = (dir == "prev" || dir == "previous" || dir == "reverse" || dir == "back");
            wm->cycle_layout_mode(reverse);
            std::string mode_str = Config::layout_mode_to_string(Config::get().get_layout_mode());
            return R"({"status":"ok","action":"cycle_layout","layout":")" + mode_str + "\"}";
        }
        return R"({"status":"error","message":"Workspace manager unavailable"})";
    }

    if (sub_action == "layout" || sub_action == "set_layout") {
        std::string layout_name;
        if (iss >> layout_name) {
            if (wm) {
                auto mode = Config::string_to_layout_mode(layout_name);
                wm->set_layout_mode(mode);
                std::string mode_str = Config::layout_mode_to_string(Config::get().get_layout_mode());
                return R"({"status":"ok","action":"set_layout","layout":")" + mode_str + "\"}";
            }
            return R"({"status":"error","message":"Workspace manager unavailable"})";
        }
        std::string mode_str = Config::layout_mode_to_string(Config::get().get_layout_mode());
        return R"({"status":"ok","action":"get_layout","layout":")" + mode_str + "\"}";
    }

    if (sub_action == "swap_main" || sub_action == "swap_master") {
        if (wm) {
            uint64_t target_id = 0;
            View* target = (iss >> target_id) ? m_server->get_view_by_id(target_id) : m_server->get_focused_view();
            if (target && target->get_workspace()) {
                target->get_workspace()->swap_with_main(target);
                wm->recalculate_layout();
                return R"({"status":"ok","action":"swap_main"})";
            }
            wm->swap_with_main();
            return R"({"status":"ok","action":"swap_main"})";
        }
        return R"({"status":"error","message":"Workspace manager unavailable"})";
    }

    if (sub_action == "toggle_split" || sub_action == "split_toggle" || sub_action == "togglesplit") {
        if (wm) {
            wm->toggle_active_split();
            return R"({"status":"ok","action":"toggle_split"})";
        }
        return R"({"status":"error","message":"Workspace manager unavailable"})";
    }

    if (sub_action == "shutdown") {
        uint32_t timeout_ms = 2500;
        iss >> timeout_ms;
        m_server->graceful_shutdown(timeout_ms);
        return R"({"status":"ok","action":"shutdown","timeout_ms":)" + std::to_string(timeout_ms) + "}";
    }

    if (sub_action == "terminal") {
        std::string term = Config::get().get_terminal();
        if (term.empty()) term = "kitty || foot || alacritty || wezterm || weston-terminal || xterm";
        if (m_server->get_input_manager()) m_server->get_input_manager()->spawn_command(term.c_str());
        return R"({"status":"ok","action":"terminal"})";
    }

    if (sub_action == "menu" || sub_action == "app_launcher") {
        if (m_server->get_input_manager()) m_server->get_input_manager()->spawn_command("miqulauncher");
        return R"({"status":"ok","action":"menu"})";
    }

    return R"({"status":"error","message":"Unknown dispatch action: )" + ipc::escape_json(args) + "\"}";
}

void IpcServer::broadcast_event(const std::string& event_name, const std::string& payload_json) {
    if (m_clients.empty()) return;

    std::string full_event = "{\"event\":\"" + ipc::escape_json(event_name) + "\",\"data\":" + payload_json + "}";

    for (auto& client : m_clients) {
        if (client->subscriptions.contains(event_name) || client->subscriptions.contains("all")) {
            send_response(*client, ipc::MessageType::Event, full_event);
        }
    }
}

} // namespace miquland
