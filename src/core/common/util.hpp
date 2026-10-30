#pragma once

#include "core/common/wlroots.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <spawn.h>
#include <unistd.h>

extern "C" char** environ;

namespace miquland {

inline void log_info(const std::string& msg) {
    wlr_log(WLR_INFO, "%s", msg.c_str());
}

inline void log_warn(const std::string& msg) {
    wlr_log(WLR_INFO, "[WARN] %s", msg.c_str());
}

inline void log_error(const std::string& msg) {
    wlr_log(WLR_ERROR, "%s", msg.c_str());
}

inline void log_debug(const std::string& msg) {
    wlr_log(WLR_DEBUG, "%s", msg.c_str());
}

inline void safe_remove_listener(struct wl_listener& listener) {
    if (listener.link.prev != nullptr && listener.link.next != nullptr && !wl_list_empty(&listener.link)) {
        wl_list_remove(&listener.link);
        wl_list_init(&listener.link);
    }
}

inline void spawn_async_command(const std::vector<std::string>& argv) {
    if (argv.empty()) return;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclosefrom_np(&actions, 3);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);

    std::vector<char*> c_argv;
    c_argv.reserve(argv.size() + 1);
    for (const auto& arg : argv) {
        c_argv.push_back(const_cast<char*>(arg.c_str()));
    }
    c_argv.push_back(nullptr);

    pid_t pid = -1;
    posix_spawnp(&pid, c_argv[0], &actions, &attr, c_argv.data(), environ);

    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
}

} // namespace miquland
