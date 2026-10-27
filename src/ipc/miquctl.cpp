#include "core/ipc/ipc_protocol.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>
#include <glob.h>
#include <chrono>
#include <thread>

namespace {

using namespace miquland::ipc;

std::string find_socket_path(const std::string& explicit_path) {
    if (!explicit_path.empty()) {
        return explicit_path;
    }

    const char* env_socket = getenv("MIQULAND_SOCKET");
    if (env_socket && env_socket[0] != '\0' && access(env_socket, F_OK) == 0) {
        return env_socket;
    }

    const char* runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char* wayland_display = getenv("WAYLAND_DISPLAY");

    if (runtime_dir && wayland_display) {
        std::string candidate = std::string(runtime_dir) + "/miquland-" + wayland_display + ".sock";
        if (access(candidate.c_str(), F_OK) == 0) {
            return candidate;
        }
    }

    // Search for any active miquland socket in runtime dir or /tmp
    std::vector<std::string> search_patterns;
    if (runtime_dir) {
        search_patterns.push_back(std::string(runtime_dir) + "/miquland-*.sock");
    }
    search_patterns.push_back("/tmp/miquland-" + std::to_string(getuid()) + "-*.sock");

    for (const auto& pattern : search_patterns) {
        glob_t glob_result;
        if (glob(pattern.c_str(), GLOB_NOSORT, nullptr, &glob_result) == 0) {
            if (glob_result.gl_pathc > 0) {
                std::string found = glob_result.gl_pathv[0];
                globfree(&glob_result);
                return found;
            }
            globfree(&glob_result);
        }
    }

    return "";
}

int connect_to_server(const std::string& socket_path) {
    if (socket_path.empty()) {
        std::cerr << "Error: Could not locate miquland IPC socket. Is miquland running?" << std::endl;
        return -1;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "Error: Failed to create socket: " << strerror(errno) << std::endl;
        return -1;
    }

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Error: Failed to connect to miquland socket (" << socket_path << "): "
                  << strerror(errno) << std::endl;
        close(fd);
        return -1;
    }

    return fd;
}

bool send_request(int fd, MessageType type, const std::string& payload) {
    IpcHeader hdr;
    memcpy(hdr.magic, IPC_MAGIC, 4);
    hdr.type = static_cast<uint32_t>(type);
    hdr.payload_len = static_cast<uint32_t>(payload.size());

    if (write(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        return false;
    }
    if (!payload.empty()) {
        if (write(fd, payload.data(), payload.size()) != static_cast<ssize_t>(payload.size())) {
            return false;
        }
    }
    return true;
}

bool read_response(int fd, MessageType& out_type, std::string& out_payload) {
    IpcHeader hdr;
    size_t total = 0;
    while (total < sizeof(hdr)) {
        ssize_t n = read(fd, reinterpret_cast<char*>(&hdr) + total, sizeof(hdr) - total);
        if (n <= 0) return false;
        total += n;
    }

    if (memcmp(hdr.magic, IPC_MAGIC, 4) != 0) {
        return false;
    }

    out_type = static_cast<MessageType>(hdr.type);
    out_payload.resize(hdr.payload_len);
    total = 0;
    while (total < hdr.payload_len) {
        ssize_t n = read(fd, out_payload.data() + total, hdr.payload_len - total);
        if (n <= 0) return false;
        total += n;
    }

    return true;
}

std::string send_command_get_response(const std::string& socket_path, const std::string& cmd) {
    int fd = connect_to_server(socket_path);
    if (fd < 0) return "";

    if (!send_request(fd, MessageType::Command, cmd)) {
        std::cerr << "Error: Failed to send command to miquland" << std::endl;
        close(fd);
        return "";
    }

    MessageType resp_type;
    std::string resp;
    if (!read_response(fd, resp_type, resp)) {
        std::cerr << "Error: Failed to read response from miquland" << std::endl;
        close(fd);
        return "";
    }

    close(fd);
    return resp;
}

std::string pretty_print_json(const std::string& input) {
    std::string out;
    int indent = 0;
    bool in_string = false;
    bool escape_next = false;
    for (char c : input) {
        if (escape_next) {
            out += c;
            escape_next = false;
            continue;
        }
        if (c == '\\' && in_string) {
            out += c;
            escape_next = true;
            continue;
        }
        if (c == '"') {
            in_string = !in_string;
            out += c;
            continue;
        }
        if (in_string) {
            out += c;
            continue;
        }
        if (c == '{' || c == '[') {
            out += c;
            out += '\n';
            indent += 2;
            out.append(indent, ' ');
        } else if (c == '}' || c == ']') {
            out += '\n';
            indent = std::max(0, indent - 2);
            out.append(indent, ' ');
            out += c;
        } else if (c == ',') {
            out += c;
            out += '\n';
            out.append(indent, ' ');
        } else if (c == ':') {
            out += ": ";
        } else if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            out += c;
        }
    }
    return out;
}

void print_help(const char* prog) {
    std::cout << "Usage: " << prog << " [options] <command> [arguments...]\n\n"
              << "First-party IPC controller for the miquland Wayland compositor.\n\n"
              << "Global Options:\n"
              << "  -s, --socket <path>       Specify custom IPC socket path\n"
              << "  -j, --json                Force raw JSON output\n"
              << "  -h, --help                Display this help message\n"
              << "  -v, --version             Display version information\n\n"
              << "State Queries:\n"
              << "  workspaces                List all workspaces and active states\n"
              << "  clients, windows          List all mapped application windows\n"
              << "  monitors, outputs         List all connected outputs\n"
              << "  activewindow              Get currently focused window\n"
              << "  activeworkspace           Get currently active workspace\n"
              << "  version                   Display compositor version and protocol\n\n"
              << "Window & Layout Actions:\n"
              << "  dispatch <action> [args]  Execute a compositor dispatcher action:\n"
              << "                              close [id]                  Politely close window\n"
              << "                              kill [id]                   Forcefully kill window\n"
              << "                              focus <dir|next|prev|id>    Focus directional/by ID\n"
              << "                              move <dx> <dy> [id]         Relative move (floating)\n"
              << "                              moveto <x> <y> [id]         Absolute move (floating)\n"
              << "                              resize <dw> <dh> [id]       Resize window / split\n"
              << "                              resizeto <w> <h> [id]       Set window size\n"
              << "                              workspace <id>              Switch to workspace <id>\n"
              << "                              movetoworkspace <id> [id]   Move window to workspace\n"
              << "                              toggle_floating [id]        Toggle floating mode\n"
              << "                              toggle_fullscreen [id]      Toggle fullscreen mode\n"
              << "                              layout [name]               Get or set layout (dwindle/stack/bstack/centeredmaster/spiral/columns/grid/monocle)\n"
              << "                              toggle_layout [next|prev]   Cycle through available tiling layouts\n"
              << "                              cycle_layout [next|prev]    Cycle through available tiling layouts\n"
              << "                              swap_main [id]              Swap window with master\n"
              << "                              togglesplit, toggle_split   Toggle split orientation (horizontal/vertical)\n"
              << "                              shutdown [timeout_ms]       Trigger graceful shutdown\n\n"
              << "Compositor & Session Control:\n"
              << "  exit                      Terminate miquland session immediately\n"
              << "  reload                    Reload miquland.conf configuration\n"
              << "  exec <command>            Execute a program in the compositor environment\n"
              << "  shutdown [options]        Gracefully close all windows, exit miquland, and\n"
              << "                            optionally trigger power actions:\n"
              << "                              --timeout <ms>   Wait timeout for apps (default: 3000)\n"
              << "                              --force          Immediate exit without waiting for apps\n"
              << "                              --dry-run        List windows that would be closed\n"
              << "                              --no-exit        Close all windows without exiting miquland\n"
              << "                              --post-cmd <cmd> Execute command after exit (e.g. reboot)\n\n"
              << "Event Streaming:\n"
              << "  subscribe <events...>     Subscribe and stream real-time events (workspace, window, monitor)\n";
}

int do_subscribe(const std::string& socket_path, const std::vector<std::string>& events) {
    int fd = connect_to_server(socket_path);
    if (fd < 0) return 1;

    std::string payload;
    for (size_t i = 0; i < events.size(); ++i) {
        if (i > 0) payload += " ";
        payload += events[i];
    }
    if (payload.empty()) payload = "all";

    if (!send_request(fd, MessageType::Subscribe, payload)) {
        std::cerr << "Error: Failed to send subscribe request" << std::endl;
        close(fd);
        return 1;
    }

    MessageType resp_type;
    std::string resp;
    if (!read_response(fd, resp_type, resp)) {
        close(fd);
        return 1;
    }

    // Stream incoming events
    while (true) {
        if (!read_response(fd, resp_type, resp)) {
            break;
        }
        std::cout << resp << std::endl;
    }

    close(fd);
    return 0;
}

// Extract window IDs from JSON array of clients
std::vector<uint64_t> parse_client_ids(const std::string& clients_json) {
    std::vector<uint64_t> ids;
    size_t pos = 0;
    while ((pos = clients_json.find("\"id\":", pos)) != std::string::npos) {
        pos += 5;
        while (pos < clients_json.size() && (clients_json[pos] == ' ' || clients_json[pos] == '\t')) pos++;
        size_t end = clients_json.find_first_of(",}", pos);
        if (end != std::string::npos) {
            try {
                ids.push_back(std::stoull(clients_json.substr(pos, end - pos)));
            } catch (...) {}
            pos = end;
        }
    }
    return ids;
}

int do_shutdown(const std::string& socket_path, int argc, char* argv[], int start_idx) {
    int timeout_ms = 3000;
    bool force = false;
    bool dry_run = false;
    bool no_exit = false;
    std::string post_cmd;

    for (int i = start_idx; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--timeout" && i + 1 < argc) {
            timeout_ms = std::atoi(argv[++i]);
        } else if (arg == "--force") {
            force = true;
            timeout_ms = 0;
        } else if (arg == "--dry-run") {
            dry_run = true;
        } else if (arg == "--no-exit") {
            no_exit = true;
        } else if (arg == "--post-cmd" && i + 1 < argc) {
            post_cmd = argv[++i];
        }
    }

    if (dry_run) {
        std::string clients_json = send_command_get_response(socket_path, "clients");
        std::cout << "[Dry-run] Application windows that would be closed:\n";
        std::cout << pretty_print_json(clients_json) << std::endl;
        return 0;
    }

    if (no_exit) {
        std::string clients_json = send_command_get_response(socket_path, "clients");
        std::vector<uint64_t> client_ids = parse_client_ids(clients_json);
        std::cout << "==> Closing " << client_ids.size() << " application window(s)..." << std::endl;
        for (uint64_t id : client_ids) {
            send_command_get_response(socket_path, "dispatch close " + std::to_string(id));
        }
        std::cout << "==> Sent close requests to all application windows." << std::endl;
        return 0;
    }

    if (force) {
        std::cout << "==> Forcing immediate session exit..." << std::endl;
        send_command_get_response(socket_path, "exit");
        if (!post_cmd.empty()) {
            std::cout << "==> Executing post-command: " << post_cmd << std::endl;
            return system(post_cmd.c_str());
        }
        return 0;
    }

    std::cout << "==> Initiating graceful desktop shutdown (timeout: " << timeout_ms << "ms)..." << std::endl;

    int fd = connect_to_server(socket_path);
    if (fd < 0) {
        std::cerr << "Compositor is not running." << std::endl;
        if (!post_cmd.empty()) {
            return system(post_cmd.c_str());
        }
        return 0;
    }

    std::string req = "shutdown " + std::to_string(timeout_ms);
    if (!send_request(fd, MessageType::Command, req)) {
        std::cerr << "Error: Failed to send shutdown command to miquland" << std::endl;
        close(fd);
        return 1;
    }

    MessageType resp_type;
    std::string resp;
    if (read_response(fd, resp_type, resp)) {
        std::cout << "==> Compositor acknowledged shutdown: " << resp << std::endl;
    }

    std::cout << "==> Waiting for miquland session teardown to complete..." << std::endl;
    char dummy[256];
    while (read(fd, dummy, sizeof(dummy)) > 0) {}
    close(fd);

    std::cout << "==> Desktop session successfully terminated." << std::endl;

    if (!post_cmd.empty()) {
        std::cout << "==> Executing post-command: " << post_cmd << std::endl;
        return system(post_cmd.c_str());
    }

    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }

    std::string explicit_socket;
    bool force_json = false;
    int cmd_idx = 1;

    while (cmd_idx < argc) {
        std::string arg = argv[cmd_idx];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "miquctl version 1.0.0 (miquland IPC client)" << std::endl;
            return 0;
        } else if ((arg == "-s" || arg == "--socket") && cmd_idx + 1 < argc) {
            explicit_socket = argv[++cmd_idx];
            cmd_idx++;
        } else if (arg == "-j" || arg == "--json") {
            force_json = true;
            cmd_idx++;
        } else {
            break;
        }
    }

    if (cmd_idx >= argc) {
        print_help(argv[0]);
        return 1;
    }

    std::string socket_path = find_socket_path(explicit_socket);
    std::string command = argv[cmd_idx];

    if (command == "shutdown") {
        return do_shutdown(socket_path, argc, argv, cmd_idx + 1);
    }

    if (command == "subscribe") {
        std::vector<std::string> events;
        for (int i = cmd_idx + 1; i < argc; ++i) {
            events.emplace_back(argv[i]);
        }
        return do_subscribe(socket_path, events);
    }

    // Build full command payload
    std::string payload = command;
    for (int i = cmd_idx + 1; i < argc; ++i) {
        payload += " ";
        payload += argv[i];
    }

    std::string response = send_command_get_response(socket_path, payload);
    if (response.empty()) {
        return 1;
    }

    if (!force_json && !response.empty() && (response.front() == '{' || response.front() == '[')) {
        std::cout << pretty_print_json(response) << std::endl;
    } else {
        std::cout << response << std::endl;
    }
    return 0;
}
