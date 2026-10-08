#include "core/server.hpp"
#include "core/config/config.hpp"
#include <csignal>
#include <iostream>
#include <string>

static miquland::Server* g_server = nullptr;

static void handle_signal(int sig) {
    if (g_server) {
        g_server->terminate();
    }
}

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  -s, --startup <cmd>    Execute startup command after compositor initializes\n"
              << "  -h, --help             Show this help message\n\n"
              << "Configuration File:\n"
              << "  ~/.config/miquland/miquland.conf\n\n"
              << "Keybindings:\n"
              << "  Super + Space          Spawn application menu launcher\n"
              << "  Super + T              Spawn terminal\n"
              << "  Super + L              Toggle tiling layout (Spiral / Stack)\n"
              << "  Super + Return         Swap active window with main window\n"
              << "  Super + Q              Close active window\n"
              << "  Super + [1..9]         Switch to workspace 1..9\n"
              << "  Super + Shift + Q      Exit compositor\n\n";
}

int main(int argc, char* argv[]) {
    std::string startup_cmd;

    // Load configuration file
    miquland::Config::get().load();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if ((arg == "-s" || arg == "--startup") && i + 1 < argc) {
            startup_cmd = argv[++i];
        }
    }

    miquland::Server server;
    g_server = &server;

    struct sigaction sa = {};
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    // Automatically reap child processes without creating zombies
    struct sigaction sa_chld = {};
    sa_chld.sa_handler = SIG_DFL;
    sa_chld.sa_flags = SA_NOCLDWAIT | SA_RESTART;
    sigaction(SIGCHLD, &sa_chld, nullptr);

    if (!startup_cmd.empty()) {
        server.set_startup_command(startup_cmd);
    }

    if (!server.init()) {
        std::cerr << "Failed to initialize Wayland compositor" << std::endl;
        return 1;
    }

    server.run();
    return 0;
}
