cmake_minimum_required(VERSION 3.20)
project(miquland LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_definitions(WLR_USE_UNSTABLE)

find_package(PkgConfig REQUIRED)

# Dependencies
pkg_search_module(WLROOTS REQUIRED IMPORTED_TARGET wlroots-0.20 wlroots-0.19 wlroots-0.18 wlroots)
pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols>=1.24)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(PIXMAN REQUIRED IMPORTED_TARGET pixman-1)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb xcb-icccm xcb-ewmh)

find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")

# Generate xdg-shell protocol
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.h
    COMMAND ${WAYLAND_SCANNER} server-header ${XDG_SHELL_XML} ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.h
    DEPENDS ${XDG_SHELL_XML}
)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c
    COMMAND ${WAYLAND_SCANNER} private-code ${XDG_SHELL_XML} ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c
    DEPENDS ${XDG_SHELL_XML}
)

# Generate wlr-layer-shell protocol
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.h
    COMMAND ${WAYLAND_SCANNER} server-header ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml ${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.c
    COMMAND ${WAYLAND_SCANNER} private-code ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml ${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.c
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml
)

# Generate wlr-output-power-management protocol
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/wlr-output-power-management-unstable-v1-protocol.h
    COMMAND ${WAYLAND_SCANNER} server-header ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-output-power-management-unstable-v1.xml ${CMAKE_CURRENT_BINARY_DIR}/wlr-output-power-management-unstable-v1-protocol.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-output-power-management-unstable-v1.xml
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/wlr-output-power-management-unstable-v1-protocol.c
    COMMAND ${WAYLAND_SCANNER} private-code ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-output-power-management-unstable-v1.xml ${CMAKE_CURRENT_BINARY_DIR}/wlr-output-power-management-unstable-v1-protocol.c
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-output-power-management-unstable-v1.xml
)

add_executable(miquland
    src/main.cpp
    src/core/server.cpp
    src/core/layer_surface.cpp
    src/core/workspace.cpp
    src/core/layout.cpp
    src/core/view.cpp
    src/core/popup.cpp
    src/core/output.cpp
    src/core/input/input.cpp
    src/core/config/config.cpp
    src/core/session_lock.cpp
    src/core/common/cairo_buffer.cpp
    src/core/common/border_paint.cpp
    src/core/plugin_manager.cpp
    src/core/animation/animation_manager.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.h
    ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c
    ${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.h
    ${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.c
    ${CMAKE_CURRENT_BINARY_DIR}/wlr-output-power-management-unstable-v1-protocol.h
    ${CMAKE_CURRENT_BINARY_DIR}/wlr-output-power-management-unstable-v1-protocol.c
)

add_subdirectory(subprojects/scenefx)

set_target_properties(miquland PROPERTIES ENABLE_EXPORTS ON)

target_include_directories(miquland PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_BINARY_DIR}
    ${WLROOTS_INCLUDE_DIRS}
    ${WAYLAND_SERVER_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS}
    ${PIXMAN_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${LIBINPUT_INCLUDE_DIRS}
    ${XCB_INCLUDE_DIRS}
)

target_include_directories(miquland SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/subprojects/scenefx/include
)

target_link_libraries(miquland PRIVATE
    scenefx
    PkgConfig::WLROOTS
    PkgConfig::WAYLAND_SERVER
    PkgConfig::XKBCOMMON
    PkgConfig::PIXMAN
    PkgConfig::CAIRO
    PkgConfig::LIBINPUT
    PkgConfig::XCB
    ${CMAKE_DL_LIBS}
)

target_compile_options(miquland PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

# Installation targets
include(GNUInstallDirs)
install(TARGETS miquland RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Install plugin development headers
install(DIRECTORY include/miquland DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY src/core DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/miquland FILES_MATCHING PATTERN "*.hpp")
install(DIRECTORY DESTINATION ${CMAKE_INSTALL_LIBDIR}/miquland/plugins)

# Generate and install pkg-config file
configure_file(
    miquland.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/miquland.pc
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/miquland.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

# Install assets and desktop entry
install(FILES assets/miquland.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/wayland-sessions)
install(FILES assets/miquland.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/miquland)

