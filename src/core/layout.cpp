#include "core/layout.hpp"
#include <algorithm>
#include <cmath>

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

    switch (mode) {
        case Config::LayoutMode::Stack:
            return calculate_stack(split_mode, usable_box, gap, count, split_ratio, secondary_ratio);
        case Config::LayoutMode::BStack:
            return calculate_bstack(usable_box, gap, count, split_ratio);
        case Config::LayoutMode::CenteredMaster:
            return calculate_centeredmaster(usable_box, gap, count, split_ratio);
        case Config::LayoutMode::Columns:
            return calculate_columns(usable_box, gap, count);
        case Config::LayoutMode::Grid:
            return calculate_grid(usable_box, gap, count);
        case Config::LayoutMode::Monocle:
            return calculate_monocle(usable_box, count);
        case Config::LayoutMode::Spiral:
        case Config::LayoutMode::Dwindle:
        default:
            return calculate_spiral(split_mode, usable_box, gap, count, split_ratio, secondary_ratio);
    }
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

std::vector<struct wlr_box> Layout::calculate_bstack(
    const struct wlr_box& box,
    int gap,
    size_t count,
    double split_ratio)
{
    std::vector<struct wlr_box> boxes;
    boxes.reserve(count);

    int total_h = std::max(50, box.height - gap);
    int master_h = std::clamp(static_cast<int>(total_h * split_ratio), 50, total_h - 50);
    int stack_h = total_h - master_h;

    boxes.push_back({ box.x, box.y, box.width, master_h });

    size_t stack_count = count - 1;
    int total_stack_gaps = static_cast<int>(stack_count - 1) * gap;
    int avail_stack_w = std::max(static_cast<int>(20 * stack_count), box.width - total_stack_gaps);

    int cur_x = box.x;
    int stack_y = box.y + master_h + gap;
    int item_w = avail_stack_w / static_cast<int>(stack_count);

    for (size_t i = 1; i < count; ++i) {
        int win_w = (i == count - 1) ? (box.x + box.width - cur_x) : item_w;
        boxes.push_back({ cur_x, stack_y, std::max(20, win_w), stack_h });
        cur_x += win_w + gap;
    }

    return boxes;
}

std::vector<struct wlr_box> Layout::calculate_centeredmaster(
    const struct wlr_box& box,
    int gap,
    size_t count,
    double split_ratio)
{
    std::vector<struct wlr_box> boxes;
    boxes.reserve(count);

    if (count == 2) {
        int total_w = std::max(50, box.width - gap);
        int w1 = std::clamp(static_cast<int>(total_w * split_ratio), 50, total_w - 50);
        int w2 = total_w - w1;
        boxes.push_back({ box.x, box.y, w1, box.height });
        boxes.push_back({ box.x + w1 + gap, box.y, w2, box.height });
        return boxes;
    }

    int total_gaps_h = 2 * gap;
    int avail_w = std::max(100, box.width - total_gaps_h);
    int master_w = std::clamp(static_cast<int>(avail_w * split_ratio), 50, avail_w - 100);
    int side_total_w = avail_w - master_w;
    int left_w = side_total_w / 2;
    int right_w = side_total_w - left_w;

    size_t stack_count = count - 1;
    size_t left_count = stack_count / 2;
    size_t right_count = stack_count - left_count;

    int master_x = box.x + left_w + gap;
    boxes.push_back({ master_x, box.y, master_w, box.height });

    // Left stack (indices 1 .. left_count)
    int left_gaps = static_cast<int>(left_count - 1) * gap;
    int avail_left_h = std::max(static_cast<int>(20 * left_count), box.height - left_gaps);
    int left_item_h = avail_left_h / static_cast<int>(left_count);
    int cur_y = box.y;
    for (size_t i = 0; i < left_count; ++i) {
        int win_h = (i == left_count - 1) ? (box.y + box.height - cur_y) : left_item_h;
        boxes.push_back({ box.x, cur_y, left_w, std::max(20, win_h) });
        cur_y += win_h + gap;
    }

    // Right stack (indices left_count + 1 .. count - 1)
    int right_x = master_x + master_w + gap;
    int right_gaps = static_cast<int>(right_count - 1) * gap;
    int avail_right_h = std::max(static_cast<int>(20 * right_count), box.height - right_gaps);
    int right_item_h = avail_right_h / static_cast<int>(right_count);
    cur_y = box.y;
    for (size_t i = 0; i < right_count; ++i) {
        int win_h = (i == right_count - 1) ? (box.y + box.height - cur_y) : right_item_h;
        boxes.push_back({ right_x, cur_y, right_w, std::max(20, win_h) });
        cur_y += win_h + gap;
    }

    return boxes;
}

std::vector<struct wlr_box> Layout::calculate_columns(
    const struct wlr_box& box,
    int gap,
    size_t count)
{
    std::vector<struct wlr_box> boxes;
    boxes.reserve(count);

    int total_gaps = static_cast<int>(count - 1) * gap;
    int avail_w = std::max(static_cast<int>(20 * count), box.width - total_gaps);
    int item_w = avail_w / static_cast<int>(count);
    int cur_x = box.x;

    for (size_t i = 0; i < count; ++i) {
        int win_w = (i == count - 1) ? (box.x + box.width - cur_x) : item_w;
        boxes.push_back({ cur_x, box.y, std::max(20, win_w), box.height });
        cur_x += win_w + gap;
    }

    return boxes;
}

std::vector<struct wlr_box> Layout::calculate_grid(
    const struct wlr_box& box,
    int gap,
    size_t count)
{
    std::vector<struct wlr_box> boxes;
    boxes.reserve(count);

    size_t cols = static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
    size_t rows = static_cast<size_t>(std::ceil(static_cast<double>(count) / static_cast<double>(cols)));

    int total_v_gaps = static_cast<int>(rows - 1) * gap;
    int avail_h = std::max(static_cast<int>(20 * rows), box.height - total_v_gaps);
    int item_h = avail_h / static_cast<int>(rows);

    int cur_y = box.y;
    size_t handled = 0;

    for (size_t r = 0; r < rows; ++r) {
        size_t row_count = (r == rows - 1) ? (count - handled) : cols;
        int row_h = (r == rows - 1) ? (box.y + box.height - cur_y) : item_h;
        int total_h_gaps = static_cast<int>(row_count - 1) * gap;
        int avail_row_w = std::max(static_cast<int>(20 * row_count), box.width - total_h_gaps);
        int col_w = avail_row_w / static_cast<int>(row_count);

        int cur_x = box.x;
        for (size_t c = 0; c < row_count; ++c) {
            int win_w = (c == row_count - 1) ? (box.x + box.width - cur_x) : col_w;
            boxes.push_back({ cur_x, cur_y, std::max(20, win_w), std::max(20, row_h) });
            cur_x += win_w + gap;
            handled++;
        }
        cur_y += row_h + gap;
    }

    return boxes;
}

std::vector<struct wlr_box> Layout::calculate_monocle(
    const struct wlr_box& box,
    size_t count)
{
    std::vector<struct wlr_box> boxes;
    boxes.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        boxes.push_back(box);
    }
    return boxes;
}

} // namespace miquland
