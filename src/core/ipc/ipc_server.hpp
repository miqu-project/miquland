#pragma once

#include "core/ipc/ipc_protocol.hpp"
#include <memory>
#include <string>
#include <vector>
#include <unordered_set>

struct wl_event_source;

namespace miquland {

class Server;

class IpcServer {
public:
    explicit IpcServer(Server* server);
    ~IpcServer();

    bool init(const std::string& display_name);
    void broadcast_event(const std::string& event_name, const std::string& payload_json);

    const std::string& get_socket_path() const { return m_socket_path; }

private:
    struct ClientConnection {
        int fd = -1;
        struct wl_event_source* event_source = nullptr;
        std::vector<uint8_t> rx_buffer;
        std::unordered_set<std::string> subscriptions;
    };

    static int handle_listen_readable(int fd, uint32_t mask, void* data);
    static int handle_client_readable(int fd, uint32_t mask, void* data);

    void accept_client();
    void process_client_data(ClientConnection& client);
    void handle_client_message(ClientConnection& client, ipc::MessageType type, const std::string& payload);

    void send_response(ClientConnection& client, ipc::MessageType type, const std::string& payload);
    void close_client(int fd);

    // Command handlers
    std::string handle_workspaces_query();
    std::string handle_clients_query();
    std::string handle_monitors_query();
    std::string handle_activewindow_query();
    std::string handle_activeworkspace_query();
    std::string handle_version_query();
    std::string handle_dispatch(const std::string& args);

    Server* m_server = nullptr;
    int m_listen_fd = -1;
    struct wl_event_source* m_listen_event_source = nullptr;
    std::string m_socket_path;
    std::vector<std::unique_ptr<ClientConnection>> m_clients;
};

} // namespace miquland
