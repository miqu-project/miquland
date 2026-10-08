cmake_minimum_required(VERSION 3.20)
project(scenefx LANGUAGES C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WLROOTS REQUIRED IMPORTED_TARGET wlroots-0.20)
pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols>=1.24)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(PIXMAN REQUIRED IMPORTED_TARGET pixman-1)
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(GBM REQUIRED IMPORTED_TARGET gbm)
pkg_check_modules(GLESV2 REQUIRED IMPORTED_TARGET glesv2)

find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")

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

set(SHADER_NAMES
    common.vert
    gradient.frag
    corner_alpha.frag
    quad.frag
    quad_grad.frag
    quad_round.frag
    quad_grad_round.frag
    tex.frag
    box_shadow.frag
    blur1.frag
    blur2.frag
    blur_effects.frag
)

set(SHADER_HEADERS "")
foreach(SNAME ${SHADER_NAMES})
    string(REPLACE "." "_" SNAME_CLEAN ${SNAME})
    set(HEADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/${SNAME_CLEAN}_src.h")
    add_custom_command(
        OUTPUT ${HEADER_OUT}
        COMMAND /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/render/fx_renderer/shaders/embed.sh ${SNAME_CLEAN}_src < ${CMAKE_CURRENT_SOURCE_DIR}/render/fx_renderer/shaders/${SNAME} > ${HEADER_OUT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/render/fx_renderer/shaders/${SNAME} ${CMAKE_CURRENT_SOURCE_DIR}/render/fx_renderer/shaders/embed.sh
    )
    list(APPEND SHADER_HEADERS ${HEADER_OUT})
endforeach()

add_library(scenefx STATIC
    render/color.c
    render/color_fallback.c
    render/egl.c
    render/pixel_format.c
    render/fx_renderer/util.c
    render/fx_renderer/tracy.c
    render/fx_renderer/shaders.c
    render/fx_renderer/pixel_format.c
    render/fx_renderer/fx_pass.c
    render/fx_renderer/fx_framebuffer.c
    render/fx_renderer/fx_offscreen_buffers.c
    render/fx_renderer/fx_texture.c
    render/fx_renderer/fx_renderer.c
    types/scene/wlr_scene.c
    types/scene/linked_node.c
    types/output/output.c
    types/fx/blur_data.c
    types/fx/clipped_region.c
    util/array.c
    util/env.c
    util/matrix.c
    util/time.c
    ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c
    ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.h
    ${SHADER_HEADERS}
)

target_include_directories(scenefx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${WLROOTS_INCLUDE_DIRS}
    ${WAYLAND_SERVER_INCLUDE_DIRS}
    ${PIXMAN_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS}
    ${LIBDRM_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GBM_INCLUDE_DIRS}
    ${GLESV2_INCLUDE_DIRS}
)

target_compile_definitions(scenefx PUBLIC
    WLR_USE_UNSTABLE
    WLR_PRIVATE=
    _POSIX_C_SOURCE=200809L
    WLR_LITTLE_ENDIAN=1
    WLR_BIG_ENDIAN=0
)

target_link_libraries(scenefx PUBLIC
    PkgConfig::WLROOTS
    PkgConfig::WAYLAND_SERVER
    PkgConfig::PIXMAN
    PkgConfig::XKBCOMMON
    PkgConfig::LIBDRM
    PkgConfig::EGL
    PkgConfig::GBM
    PkgConfig::GLESV2
    m
    rt
)
