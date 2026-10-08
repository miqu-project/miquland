#include "core/common/border_paint.hpp"
#include <sstream>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace miquland {

bool BorderPaint::parse_hex_color(const std::string& hex, float& r, float& g, float& b, float& a) {
    if (hex.empty()) return false;
    std::string s = hex;

    // Strip surrounding whitespace and quotes
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '"' || s.front() == '\'')) {
        s.erase(0, 1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '"' || s.back() == '\'')) {
        s.pop_back();
    }

    if (s.empty()) return false;
    if (s[0] == '#') {
        s = s.substr(1);
    }

    if (s.length() == 6) {
        try {
            unsigned long val = std::stoul(s, nullptr, 16);
            r = ((val >> 16) & 0xFF) / 255.0f;
            g = ((val >> 8) & 0xFF) / 255.0f;
            b = (val & 0xFF) / 255.0f;
            a = 1.0f;
            return true;
        } catch (...) {
            return false;
        }
    } else if (s.length() == 8) {
        try {
            unsigned long val = std::stoul(s, nullptr, 16);
            r = ((val >> 24) & 0xFF) / 255.0f;
            g = ((val >> 16) & 0xFF) / 255.0f;
            b = ((val >> 8) & 0xFF) / 255.0f;
            a = (val & 0xFF) / 255.0f;
            return true;
        } catch (...) {
            return false;
        }
    } else if (s.length() == 3) {
        try {
            unsigned long val = std::stoul(s, nullptr, 16);
            r = (((val >> 8) & 0xF) * 17) / 255.0f;
            g = (((val >> 4) & 0xF) * 17) / 255.0f;
            b = ((val & 0xF) * 17) / 255.0f;
            a = 1.0f;
            return true;
        } catch (...) {
            return false;
        }
    } else if (s.length() == 4) {
        try {
            unsigned long val = std::stoul(s, nullptr, 16);
            r = (((val >> 12) & 0xF) * 17) / 255.0f;
            g = (((val >> 8) & 0xF) * 17) / 255.0f;
            b = (((val >> 4) & 0xF) * 17) / 255.0f;
            a = ((val & 0xF) * 17) / 255.0f;
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

static bool parse_angle_token(const std::string& token, float& angle_deg) {
    if (token.empty()) return false;
    std::string s = token;
    for (auto& c : s) c = std::tolower(c);

    if (s.size() > 3 && s.substr(s.size() - 3) == "deg") {
        try {
            angle_deg = std::stof(s.substr(0, s.size() - 3));
            return true;
        } catch (...) {
            return false;
        }
    } else if (s.size() > 3 && s.substr(s.size() - 3) == "rad") {
        try {
            float rad = std::stof(s.substr(0, s.size() - 3));
            angle_deg = rad * 180.0f / static_cast<float>(M_PI);
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

static bool parse_stop_percentage(const std::string& token, float& offset) {
    if (token.empty() || token.back() != '%') return false;
    try {
        float pct = std::stof(token.substr(0, token.size() - 1));
        offset = std::clamp(pct / 100.0f, 0.0f, 1.0f);
        return true;
    } catch (...) {
        return false;
    }
}

static std::vector<std::string> tokenize_paint_input(const std::string& input) {
    std::vector<std::string> tokens;
    std::string current;
    for (char c : input) {
        if (c == ' ' || c == '\t' || c == ',') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

struct ParsedStop {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
    float offset = 0.0f;
    bool has_offset = false;
};

static void parse_stops_and_angle(const std::vector<std::string>& tokens,
                                  std::vector<ParsedStop>& stops, float& angle_deg) {
    angle_deg = 45.0f;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& tok = tokens[i];
        if (tok == "gradient") continue;

        float angle = 0.0f;
        if (parse_angle_token(tok, angle)) {
            angle_deg = angle;
            continue;
        }

        float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
        if (BorderPaint::parse_hex_color(tok, r, g, b, a)) {
            ParsedStop stop{r, g, b, a, 0.0f, false};
            if (i + 1 < tokens.size()) {
                float off = 0.0f;
                if (parse_stop_percentage(tokens[i + 1], off)) {
                    stop.offset = off;
                    stop.has_offset = true;
                    ++i;
                }
            }
            stops.push_back(stop);
        }
    }
}

BorderPaint BorderPaint::parse(const std::string& input) {
    BorderPaint paint;
    if (input.empty()) return paint;

    std::vector<std::string> tokens = tokenize_paint_input(input);
    std::vector<ParsedStop> parsed_stops;
    float parsed_angle = 45.0f;
    parse_stops_and_angle(tokens, parsed_stops, parsed_angle);

    paint.m_angle_deg = parsed_angle;

    if (parsed_stops.size() >= 2) {
        paint.m_is_gradient = true;
        size_t n = parsed_stops.size();
        for (size_t i = 0; i < n; ++i) {
            float off = parsed_stops[i].has_offset
                ? parsed_stops[i].offset
                : (static_cast<float>(i) / static_cast<float>(n - 1));
            paint.m_stops.push_back({
                parsed_stops[i].r,
                parsed_stops[i].g,
                parsed_stops[i].b,
                parsed_stops[i].a,
                off
            });
        }
        paint.m_single_r = parsed_stops[0].r;
        paint.m_single_g = parsed_stops[0].g;
        paint.m_single_b = parsed_stops[0].b;
        paint.m_single_a = parsed_stops[0].a;
    } else if (parsed_stops.size() == 1) {
        paint.m_is_gradient = false;
        paint.m_single_r = parsed_stops[0].r;
        paint.m_single_g = parsed_stops[0].g;
        paint.m_single_b = parsed_stops[0].b;
        paint.m_single_a = parsed_stops[0].a;
    } else {
        paint.m_is_gradient = false;
        paint.m_single_r = 0.0f;
        paint.m_single_g = 0.4f;
        paint.m_single_b = 1.0f;
        paint.m_single_a = 1.0f;
    }

    return paint;
}

cairo_pattern_t* BorderPaint::create_cairo_pattern(double width, double height) const {
    if (width <= 0.0 || height <= 0.0) return nullptr;

    double rad = m_angle_deg * (M_PI / 180.0);
    double dx = std::cos(rad);
    double dy = std::sin(rad);

    double hx = width / 2.0;
    double hy = height / 2.0;

    // Project rectangle half-extents onto direction vector
    double l = hx * std::abs(dx) + hy * std::abs(dy);
    double x0 = hx - l * dx;
    double y0 = hy - l * dy;
    double x1 = hx + l * dx;
    double y1 = hy + l * dy;

    cairo_pattern_t* pat = cairo_pattern_create_linear(x0, y0, x1, y1);
    if (!pat) return nullptr;

    if (m_stops.empty()) {
        cairo_pattern_add_color_stop_rgba(pat, 0.0, m_single_r, m_single_g, m_single_b, m_single_a);
        cairo_pattern_add_color_stop_rgba(pat, 1.0, m_single_r, m_single_g, m_single_b, m_single_a);
    } else {
        for (const auto& s : m_stops) {
            cairo_pattern_add_color_stop_rgba(pat, s.offset, s.r, s.g, s.b, s.a);
        }
    }

    return pat;
}

} // namespace miquland
