#include "core/layout.hpp"
#include <algorithm>

namespace miquland {

std::vector<struct wlr_box> Layout::calculate(
    Config::LayoutMode mode,
    SplitMode split_mode,
    const struct wlr_box& usable_box,
    int gap,
    size_t count,
    double split_ratio,
    double secondary_ratio)
{
    if (count == 0) {
        return {};
    }

    if (count == 1) {
        return { usable_box };
    }

    if (mode == Config::LayoutMode::Stack) {
        return calculate_stack(split_mode, usable_box, gap, count, split_ratio, secondary_ratio);
    }

    return calculate_spiral(split_mode, usable_box, gap, count, split_ratio, secondary_ratio);
}

std::vector<struct wlr_box> Layout::calculate_spiral(
    SplitMode split_mode,
    const struct wlr_box& box,
    int gap,
    size_t count,
    double split_ratio,
    double secondary_ratio)
{
    std::vector<struct wlr_box> boxes;
    boxes.reserve(count);

    struct wlr_box cur = box;

    for (size_t i = 0; i < count; ++i) {
        if (i == count - 1) {
            boxes.push_back(cur);
            break;
        }

        bool split_horizontal = (i % 2 == 0);
        if (split_mode == SplitMode::Vertical) {
            split_horizontal = !split_horizontal;
        }

        double ratio = (i == 0) ? split_ratio : ((i == 1) ? secondary_ratio : 0.5);

        if (split_horizontal) {
            int total_w = std::max(40, cur.width - gap);
            int split_w = std::clamp(static_cast<int>(total_w * ratio), 40, total_w - 40);
            int rest_w = total_w - split_w;

            boxes.push_back({ cur.x, cur.y, split_w, cur.height });
            cur.x += split_w + gap;
            cur.width = rest_w;
        } else {
            int total_h = std::max(40, cur.height - gap);
            int split_h = std::clamp(static_cast<int>(total_h * ratio), 40, total_h - 40);
            int rest_h = total_h - split_h;

            boxes.push_back({ cur.x, cur.y, cur.width, split_h });
            cur.y += split_h + gap;
            cur.height = rest_h;
        }
    }

    return boxes;
}

std::vector<struct wlr_box> Layout::calculate_stack(
    SplitMode split_mode,
    const struct wlr_box& box,
    int gap,
    size_t count,
    double split_ratio,
    double secondary_ratio)
{
    std::vector<struct wlr_box> boxes;
    boxes.reserve(count);

    if (split_mode == SplitMode::Vertical) {
        int total_h = std::max(50, box.height - gap);
        int master_h = std::clamp(static_cast<int>(total_h * split_ratio), 50, total_h - 50);
        int stack_h = total_h - master_h;

        boxes.push_back({ box.x, box.y, box.width, master_h });

        size_t stack_count = count - 1;
        int total_stack_gaps = static_cast<int>(stack_count - 1) * gap;
        int avail_stack_w = std::max(static_cast<int>(20 * stack_count), box.width - total_stack_gaps);

        int cur_x = box.x;
        int stack_y = box.y + master_h + gap;

        if (stack_count == 2) {
            int w1 = std::clamp(static_cast<int>(avail_stack_w * secondary_ratio), 20, avail_stack_w - 20);
            int w2 = avail_stack_w - w1;
            boxes.push_back({ cur_x, stack_y, w1, stack_h });
            boxes.push_back({ cur_x + w1 + gap, stack_y, w2, stack_h });
        } else {
            int item_w = avail_stack_w / static_cast<int>(stack_count);
            for (size_t i = 1; i < count; ++i) {
                int win_w = (i == count - 1) ? (box.x + box.width - cur_x) : item_w;
                boxes.push_back({ cur_x, stack_y, std::max(20, win_w), stack_h });
                cur_x += win_w + gap;
            }
        }
    } else {
        int total_w = std::max(50, box.width - gap);
        int master_w = std::clamp(static_cast<int>(total_w * split_ratio), 50, total_w - 50);
        int stack_w = total_w - master_w;

        boxes.push_back({ box.x, box.y, master_w, box.height });

        size_t stack_count = count - 1;
        int total_stack_gaps = static_cast<int>(stack_count - 1) * gap;
        int avail_stack_h = std::max(static_cast<int>(20 * stack_count), box.height - total_stack_gaps);

        int cur_y = box.y;
        int stack_x = box.x + master_w + gap;

        if (stack_count == 2) {
            int h1 = std::clamp(static_cast<int>(avail_stack_h * secondary_ratio), 20, avail_stack_h - 20);
            int h2 = avail_stack_h - h1;
            boxes.push_back({ stack_x, cur_y, stack_w, h1 });
            boxes.push_back({ stack_x, cur_y + h1 + gap, stack_w, h2 });
        } else {
            int item_h = avail_stack_h / static_cast<int>(stack_count);
            for (size_t i = 1; i < count; ++i) {
                int win_h = (i == count - 1) ? (box.y + box.height - cur_y) : item_h;
                boxes.push_back({ stack_x, cur_y, stack_w, std::max(20, win_h) });
                cur_y += win_h + gap;
            }
        }
    }

    return boxes;
}

} // namespace miquland
