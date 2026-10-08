#pragma once

#include "core/common/wlroots.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <functional>

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
    pid_t pid = fork();
    if (pid == 0) {
        int max_fd = static_cast<int>(sysconf(_SC_OPEN_MAX));
        if (max_fd < 0 || max_fd > 4096) max_fd = 1024;
        for (int fd = 3; fd < max_fd; ++fd) {
            ::close(fd);
        }
        std::vector<char*> c_argv;
        c_argv.reserve(argv.size() + 1);
        for (const auto& arg : argv) {
            c_argv.push_back(const_cast<char*>(arg.c_str()));
        }
        c_argv.push_back(nullptr);
        execvp(c_argv[0], c_argv.data());
        _exit(1);
    }
}

} // namespace miquland
