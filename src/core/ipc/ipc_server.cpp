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
    } else if (cmd == "version") {
        send_response(client, ipc::MessageType::Response, handle_version_query());
    } else if (cmd == "dispatch") {
        send_response(client, ipc::MessageType::Response, handle_dispatch(args));
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

    if (sub_action == "close") {
        uint64_t target_id = 0;
        if (iss >> target_id) {
            View* target = m_server->get_view_by_id(target_id);
            if (target) {
                target->close();
                return R"({"status":"ok","action":"close","target_id":)" + std::to_string(target_id) + "}";
            }
            return R"({"status":"error","message":"Target window not found"})";
        }
        View* focused = m_server->get_focused_view();
        if (focused) {
            focused->close();
            return R"({"status":"ok","action":"close","focused":true})";
        }
        return R"({"status":"error","message":"No focused window to close"})";
    }

    if (sub_action == "workspace") {
        size_t ws_id = 0;
        if (iss >> ws_id) {
            if (m_server->get_workspace_manager()) {
                m_server->get_workspace_manager()->switch_to_workspace(ws_id);
                return R"({"status":"ok","action":"workspace","workspace_id":)" + std::to_string(ws_id) + "}";
            }
        }
        return R"({"status":"error","message":"Invalid workspace id"})";
    }

    if (sub_action == "movetoworkspace" || sub_action == "move_to_workspace") {
        size_t ws_id = 0;
        if (iss >> ws_id) {
            uint64_t target_id = 0;
            View* target = nullptr;
            if (iss >> target_id) {
                target = m_server->get_view_by_id(target_id);
            } else {
                target = m_server->get_focused_view();
            }

            if (target && m_server->get_workspace_manager()) {
                m_server->get_workspace_manager()->move_view_to_workspace(target, ws_id);
                return R"({"status":"ok","action":"movetoworkspace","workspace_id":)" + std::to_string(ws_id) + "}";
            }
            return R"({"status":"error","message":"Target window not found"})";
        }
        return R"({"status":"error","message":"Invalid workspace id"})";
    }

    if (m_server->get_input_manager() && m_server->get_input_manager()->execute_action(args)) {
        return R"({"status":"ok","action":")" + ipc::escape_json(args) + "\"}";
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
