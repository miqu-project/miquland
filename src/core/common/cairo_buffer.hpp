#pragma once

#include "core/common/util.hpp"

#ifndef DRM_FORMAT_ARGB8888
#define DRM_FORMAT_ARGB8888 0x34325241
#endif

namespace miquland {

class CairoBuffer {
public:
    CairoBuffer(int width, int height);
    ~CairoBuffer();

    int get_width() const { return m_width; }
    int get_height() const { return m_height; }
    cairo_surface_t* get_surface() const { return m_surface; }
    cairo_t* get_cairo() const { return m_cr; }
    struct wlr_buffer* get_wlr_buffer() { return &m_base; }

    void resize(int width, int height);

private:
    static void buffer_destroy(struct wlr_buffer* buffer);
    static bool buffer_begin_data_ptr_access(struct wlr_buffer* buffer, uint32_t flags,
        void** data, uint32_t* format, size_t* stride);
    static void buffer_end_data_ptr_access(struct wlr_buffer* buffer);

    struct wlr_buffer m_base;
    int m_width = 0;
    int m_height = 0;
    cairo_surface_t* m_surface = nullptr;
    cairo_t* m_cr = nullptr;

    static const struct wlr_buffer_impl s_buffer_impl;
};

} // namespace miquland
