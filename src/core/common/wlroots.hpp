#pragma once

// Include all standard C++ library headers FIRST
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <ctime>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <filesystem>

#include <wayland-server-core.h>
#include <pixman.h>
#include <xkbcommon/xkbcommon.h>
#include <cairo.h>
#include <drm_fourcc.h>

#ifndef WLR_USE_UNSTABLE
#define WLR_USE_UNSTABLE
#endif

#ifdef __cplusplus
#define static
#define namespace _namespace
#define class _class
extern "C" {
#endif

#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/backend/libinput.h>
#include <libinput.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_ext_workspace_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/xwayland.h>
#include <wlr/util/log.h>
#include <wlr/util/box.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/util/edges.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_tearing_control_v1.h>

#ifdef __cplusplus
}
#undef class
#undef namespace
#undef static
#endif
