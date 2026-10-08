#include "core/common/cairo_buffer.hpp"
#include <sys/mman.h>
#include <unistd.h>
#include <iostream>

namespace miquland {

const struct wlr_buffer_impl CairoBuffer::s_buffer_impl = {
    .destroy = CairoBuffer::buffer_destroy,
    .get_dmabuf = nullptr,
    .get_shm = nullptr,
    .begin_data_ptr_access = CairoBuffer::buffer_begin_data_ptr_access,
    .end_data_ptr_access = CairoBuffer::buffer_end_data_ptr_access,
};

CairoBuffer::CairoBuffer(int width, int height)
    : m_width(width), m_height(height)
{
    wlr_buffer_init(&m_base, &s_buffer_impl, width, height);

    m_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    m_cr = cairo_create(m_surface);
}

CairoBuffer::~CairoBuffer() {
    if (m_cr) cairo_destroy(m_cr);
    if (m_surface) cairo_surface_destroy(m_surface);
}

void CairoBuffer::resize(int width, int height) {
    if (m_width == width && m_height == height) return;

    m_width = width;
    m_height = height;

    if (m_cr) cairo_destroy(m_cr);
    if (m_surface) cairo_surface_destroy(m_surface);

    m_base.width = width;
    m_base.height = height;

    m_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    m_cr = cairo_create(m_surface);
}

void CairoBuffer::buffer_destroy(struct wlr_buffer* buffer) {
    // Managed by C++ lifetime
}

bool CairoBuffer::buffer_begin_data_ptr_access(struct wlr_buffer* buffer, uint32_t flags,
    void** data, uint32_t* format, size_t* stride)
{
    CairoBuffer* self = wl_container_of(buffer, self, m_base);
    *data = cairo_image_surface_get_data(self->m_surface);
    *stride = cairo_image_surface_get_stride(self->m_surface);
    *format = DRM_FORMAT_ARGB8888;
    return true;
}

void CairoBuffer::buffer_end_data_ptr_access(struct wlr_buffer* buffer) {
    CairoBuffer* self = wl_container_of(buffer, self, m_base);
    cairo_surface_mark_dirty(self->m_surface);
}

} // namespace miquland
