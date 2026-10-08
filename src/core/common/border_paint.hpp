#pragma once

#include <cairo.h>
#include <string>
#include <vector>

namespace miquland {

struct ColorStop {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
    float offset = 0.0f; // 0.0 to 1.0
};

class BorderPaint {
public:
    BorderPaint() = default;

    bool is_gradient() const { return m_is_gradient; }
    float get_angle_deg() const { return m_angle_deg; }
    const std::vector<ColorStop>& get_stops() const { return m_stops; }

    void get_single_color(float& r, float& g, float& b, float& a) const {
        r = m_single_r;
        g = m_single_g;
        b = m_single_b;
        a = m_single_a;
    }

    // Creates a Cairo pattern for the border gradient across the given width and height.
    // Caller is responsible for cairo_pattern_destroy(pat).
    cairo_pattern_t* create_cairo_pattern(double width, double height) const;

    // Parses a single color or multi-stop gradient string (e.g. "#0066ff", "#0066ff #00ffcc 45deg")
    static BorderPaint parse(const std::string& input);

    // Static helper to parse hex colors (#RGB, #RGBA, #RRGGBB, #RRGGBBAA)
    static bool parse_hex_color(const std::string& hex, float& r, float& g, float& b, float& a);

private:
    bool m_is_gradient = false;
    float m_angle_deg = 45.0f;
    std::vector<ColorStop> m_stops;
    float m_single_r = 0.0f;
    float m_single_g = 0.8f;
    float m_single_b = 1.0f;
    float m_single_a = 1.0f;
};

} // namespace miquland
