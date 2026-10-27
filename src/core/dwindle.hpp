#pragma once

#include "core/common/util.hpp"
#include <memory>
#include <vector>
#include <unordered_map>
#include <optional>
#include <utility>

namespace miquland {

class View;

enum class DwindleSplit {
    Horizontal, // Split left/right (child 2 is right)
    Vertical    // Split top/bottom (child 2 is bottom)
};

struct DwindleNode {
    DwindleNode* parent = nullptr;
    std::unique_ptr<DwindleNode> first;
    std::unique_ptr<DwindleNode> second;
    View* view = nullptr;
    DwindleSplit split = DwindleSplit::Horizontal;
    double split_ratio = 0.5;
    struct wlr_box box = { 0, 0, 0, 0 };

    bool is_leaf() const { return view != nullptr; }
};

class DwindleTree {
public:
    DwindleTree() = default;
    ~DwindleTree() = default;

    // View lifecycle
    void add_view(View* view, View* target_focus = nullptr, std::optional<std::pair<double, double>> cursor_pos = std::nullopt);
    bool remove_view(View* view);
    bool swap_views(View* v1, View* v2);
    void sync_views(const std::vector<View*>& views);
    void clear();

    // Split manipulation & queries
    bool toggle_split(View* view);
    bool adjust_split_ratio(View* view, double delta);
    bool set_split_ratio(View* view, double ratio);
    double get_split_ratio(View* view) const;
    DwindleSplit get_split_direction(View* view) const;
    bool is_first_child(View* view) const;

    // View collection
    std::vector<View*> get_views() const;
    void collect_views_into(std::vector<View*>& out) const;

    // Layout calculation
    std::unordered_map<View*, struct wlr_box> calculate(
        const struct wlr_box& usable_box,
        int gap
    ) const;

    struct wlr_box calculate_geometry_for_new_view(
        const struct wlr_box& usable_box,
        int gap,
        View* target_focus,
        std::optional<std::pair<double, double>> cursor_pos = std::nullopt
    ) const;

    bool empty() const { return !m_root; }
    size_t size() const;
    bool contains(View* view) const;

private:
    std::unique_ptr<DwindleNode> m_root;

    DwindleNode* find_leaf(View* view);
    const DwindleNode* find_leaf(View* view) const;
    DwindleNode* find_rightmost_leaf(DwindleNode* node);
    const DwindleNode* find_rightmost_leaf(const DwindleNode* node) const;

    void collect_views(const DwindleNode* node, std::vector<View*>& out) const;
    void calculate_recursive(
        const DwindleNode* node,
        const struct wlr_box& box,
        int gap,
        std::unordered_map<View*, struct wlr_box>& out
    ) const;
};

} // namespace miquland
