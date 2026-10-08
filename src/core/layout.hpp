#pragma once

#include "core/common/util.hpp"
#include "core/config/config.hpp"
#include <vector>

namespace miquland {

enum class SplitMode {
    Horizontal, // Split left/right
    Vertical    // Split top/bottom
};

class Layout {
public:
    static std::vector<struct wlr_box> calculate(
        Config::LayoutMode mode,
        SplitMode split_mode,
        const struct wlr_box& usable_box,
        int gap,
        size_t count,
        double split_ratio = 0.5,
        double secondary_ratio = 0.5
    );

private:
    static std::vector<struct wlr_box> calculate_spiral(
        SplitMode split_mode,
        const struct wlr_box& box,
        int gap,
        size_t count,
        double split_ratio,
        double secondary_ratio
    );

    static std::vector<struct wlr_box> calculate_stack(
        SplitMode split_mode,
        const struct wlr_box& box,
        int gap,
        size_t count,
        double split_ratio,
        double secondary_ratio
    );
};

} // namespace miquland
