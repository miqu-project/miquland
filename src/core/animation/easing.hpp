#pragma once

#include <cmath>
#include <string>
#include <functional>
#include <algorithm>
#include <sstream>
#include <vector>
#include <unordered_map>

namespace miquland {

class Easing {
public:
    struct BezierPoints {
        float x1 = 0.0f;
        float y1 = 0.0f;
        float x2 = 1.0f;
        float y2 = 1.0f;
    };

    static std::unordered_map<std::string, BezierPoints>& get_named_beziers() {
        static std::unordered_map<std::string, BezierPoints> named_beziers;
        return named_beziers;
    }

    static void register_named_bezier(const std::string& name, float x1, float y1, float x2, float y2) {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        get_named_beziers()[lower] = { x1, y1, x2, y2 };
    }

    static void clear_named_beziers() {
        get_named_beziers().clear();
    }

    static bool has_named_bezier(const std::string& name) {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        auto& map = get_named_beziers();
        return map.find(lower) != map.end();
    }

    static float linear(float t) {
        return std::clamp(t, 0.0f, 1.0f);
    }

    static float ease_in_cubic(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * t;
    }

    static float ease_out_cubic(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        float f = 1.0f - t;
        return 1.0f - f * f * f;
    }

    static float ease_in_out_cubic(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    }

    static float ease_in_out_quad(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
    }

    static float ease_out_quad(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return 1.0f - (1.0f - t) * (1.0f - t);
    }

    static float ease_out_expo(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return (t >= 1.0f) ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
    }

    static float ease_out_back(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        constexpr float c1 = 1.70158f;
        constexpr float c3 = c1 + 1.0f;
        float f = t - 1.0f;
        return 1.0f + c3 * f * f * f + c1 * f * f;
    }

    static float ease_in_out_circ(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        if (t < 0.5f) {
            return (1.0f - std::sqrt(std::max(0.0f, 1.0f - 4.0f * t * t))) / 2.0f;
        } else {
            return (std::sqrt(std::max(0.0f, 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f))) + 1.0f) / 2.0f;
        }
    }

    // High-performance Cubic Bezier (x1, y1, x2, y2) solver with Newton-Raphson & bisection fallback
    static float cubic_bezier(float x1, float y1, float x2, float y2, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;

        float ax = 1.0f - 3.0f * x2 + 3.0f * x1;
        float bx = 3.0f * x2 - 6.0f * x1;
        float cx = 3.0f * x1;

        float ay = 1.0f - 3.0f * y2 + 3.0f * y1;
        float by = 3.0f * y2 - 6.0f * y1;
        float cy = 3.0f * y1;

        float low = 0.0f, high = 1.0f;
        float u = t;

        for (int i = 0; i < 8; ++i) {
            float cur_x = ((ax * u + bx) * u + cx) * u;
            float diff = cur_x - t;
            if (std::abs(diff) < 1e-4f) break;

            float d = (3.0f * ax * u + 2.0f * bx) * u + cx;
            if (std::abs(d) > 1e-5f) {
                float next_u = u - diff / d;
                if (next_u >= low && next_u <= high) {
                    u = next_u;
                    continue;
                }
            }

            if (diff > 0.0f) high = u;
            else low = u;
            u = 0.5f * (low + high);
        }

        return ((ay * u + by) * u + cy) * u;
    }

    static std::function<float(float)> from_name(const std::string& name_or_curve) {
        std::string s = name_or_curve;
        // Trim leading/trailing whitespace
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        s.erase(s.find_last_not_of(" \t\r\n") + 1);

        std::string lower = s;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // Check for custom inline bezier: bezier(x1, y1, x2, y2) or cubic-bezier(...) or raw floats
        float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        if (try_parse_bezier(lower, x1, y1, x2, y2)) {
            return [x1, y1, x2, y2](float t) {
                return cubic_bezier(x1, y1, x2, y2, t);
            };
        }

        // Check user-configured named bezier registry (e.g., from 'bezier = name, x1, y1, x2, y2')
        auto& named = get_named_beziers();
        auto it = named.find(lower);
        if (it != named.end()) {
            float nx1 = it->second.x1, ny1 = it->second.y1, nx2 = it->second.x2, ny2 = it->second.y2;
            return [nx1, ny1, nx2, ny2](float t) {
                return cubic_bezier(nx1, ny1, nx2, ny2, t);
            };
        }

        // Check for custom inline spring: spring(damping, frequency)
        float s_damping = 0.0f, s_freq = 0.0f;
        if (try_parse_spring(lower, s_damping, s_freq)) {
            return [s_damping, s_freq](float t) {
                return spring_oscillator(s_damping, s_freq, t);
            };
        }

        // Standard presets
        if (lower == "default") {
            // Smooth pop-in curve with silky deceleration (Hyprland iconic profile)
            return [](float t) { return cubic_bezier(0.05f, 0.9f, 0.1f, 1.05f, t); };
        }
        if (lower == "fluent" || lower == "smooth_slide" || lower == "glide") {
            // Ultra-smooth workspace slide and relocation curve
            return [](float t) { return cubic_bezier(0.1f, 1.0f, 0.1f, 1.0f, t); };
        }
        if (lower == "spring") {
            // Natural physical harmonic spring calibrated to settle organically across full duration
            return [](float t) { return spring_oscillator(0.72f, 5.6f, t); };
        }
        if (lower == "spring_gentle" || lower == "gentle_spring") {
            // Critically damped spring with pure silky deceleration and zero overshoot
            return [](float t) { return spring_oscillator(0.85f, 4.8f, t); };
        }
        if (lower == "spring_bouncy" || lower == "bouncy_spring") {
            // Subtle spring bounce that settles naturally by end of duration
            return [](float t) { return spring_oscillator(0.58f, 6.8f, t); };
        }
        if (lower == "overshoot" || lower == "popin") {
            return [](float t) { return cubic_bezier(0.05f, 0.9f, 0.1f, 1.08f, t); };
        }
        if (lower == "smooth_pop" || lower == "smooth_out" || lower == "smoothout" || lower == "ease_out_quart") {
            return [](float t) { return cubic_bezier(0.16f, 1.0f, 0.3f, 1.0f, t); };
        }
        if (lower == "smooth_in" || lower == "smoothin") {
            return [](float t) { return cubic_bezier(0.3f, 0.0f, 0.8f, 0.15f, t); };
        }
        if (lower == "md3_decel" || lower == "md3") {
            return [](float t) { return cubic_bezier(0.05f, 0.7f, 0.1f, 1.0f, t); };
        }
        if (lower == "md3_accel") {
            return [](float t) { return cubic_bezier(0.3f, 0.0f, 0.8f, 0.15f, t); };
        }
        if (lower == "ease_in_out_circ" || lower == "easeinoutcirc" || lower == "circ") {
            return ease_in_out_circ;
        }
        if (lower == "linear") return linear;
        if (lower == "ease_in_cubic" || lower == "easeincubic") return ease_in_cubic;
        if (lower == "ease_out_cubic" || lower == "easeoutcubic") return ease_out_cubic;
        if (lower == "ease_in_out_cubic" || lower == "easeinoutcubic") return ease_in_out_cubic;
        if (lower == "ease_out_quad") return ease_out_quad;
        if (lower == "ease_in_out_quad") return ease_in_out_quad;
        if (lower == "ease_out_expo") return ease_out_expo;
        if (lower == "ease_out_back") return ease_out_back;

        // Default fallback: Smooth pop-in
        return [](float t) { return cubic_bezier(0.05f, 0.9f, 0.1f, 1.05f, t); };
    }

    // Analytical underdamped harmonic oscillator (spring physics)
    static float spring_oscillator(float damping, float frequency, float t) {
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;

        float zeta = std::clamp(damping, 0.01f, 0.99f);
        float omega_0 = std::max(0.1f, frequency);
        float omega_d = omega_0 * std::sqrt(1.0f - zeta * zeta);
        float decay = std::exp(-zeta * omega_0 * t);
        float val = 1.0f - decay * (std::cos(omega_d * t) + (zeta * omega_0 / omega_d) * std::sin(omega_d * t));
        return val;
    }

private:
    static bool try_parse_spring(const std::string& str, float& out_damping, float& out_freq) {
        std::string raw = str;
        if (raw.rfind("spring(", 0) == 0 && raw.back() == ')') {
            raw = raw.substr(7, raw.size() - 8);
            std::replace(raw.begin(), raw.end(), ',', ' ');
            std::istringstream iss(raw);
            float d = 0, f = 0;
            if (iss >> d >> f) {
                out_damping = d;
                out_freq = f;
                return true;
            }
        }
        return false;
    }

    static bool try_parse_bezier(const std::string& str, float& out_x1, float& out_y1, float& out_x2, float& out_y2) {
        std::string raw = str;
        if (raw.rfind("bezier(", 0) == 0 && raw.back() == ')') {
            raw = raw.substr(7, raw.size() - 8);
        } else if (raw.rfind("cubic_bezier(", 0) == 0 && raw.back() == ')') {
            raw = raw.substr(13, raw.size() - 14);
        } else if (raw.rfind("cubic-bezier(", 0) == 0 && raw.back() == ')') {
            raw = raw.substr(13, raw.size() - 14);
        }

        std::replace(raw.begin(), raw.end(), ',', ' ');
        std::istringstream iss(raw);
        float p1 = 0, p2 = 0, p3 = 0, p4 = 0;
        if (iss >> p1 >> p2 >> p3 >> p4) {
            out_x1 = p1;
            out_y1 = p2;
            out_x2 = p3;
            out_y2 = p4;
            return true;
        }
        return false;
    }
};

} // namespace miquland
