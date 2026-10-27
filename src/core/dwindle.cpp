#include "core/dwindle.hpp"
#include "core/view.hpp"
#include "core/config/config.hpp"
#include <algorithm>
#include <unordered_set>

namespace miquland {

void DwindleTree::clear() {
    m_root.reset();
}

size_t DwindleTree::size() const {
    if (!m_root) return 0;
    std::vector<View*> views;
    collect_views(m_root.get(), views);
    return views.size();
}

bool DwindleTree::contains(View* view) const {
    return find_leaf(view) != nullptr;
}

DwindleNode* DwindleTree::find_leaf(View* view) {
    if (!view || !m_root) return nullptr;

    std::vector<DwindleNode*> stack = { m_root.get() };
    while (!stack.empty()) {
        DwindleNode* cur = stack.back();
        stack.pop_back();

        if (cur->is_leaf()) {
            if (cur->view == view) return cur;
        } else {
            if (cur->second) stack.push_back(cur->second.get());
            if (cur->first) stack.push_back(cur->first.get());
        }
    }
    return nullptr;
}

const DwindleNode* DwindleTree::find_leaf(View* view) const {
    return const_cast<DwindleTree*>(this)->find_leaf(view);
}

DwindleNode* DwindleTree::find_rightmost_leaf(DwindleNode* node) {
    if (!node) return nullptr;
    DwindleNode* cur = node;
    while (!cur->is_leaf()) {
        if (cur->second) {
            cur = cur->second.get();
        } else if (cur->first) {
            cur = cur->first.get();
        } else {
            break;
        }
    }
    return cur;
}

const DwindleNode* DwindleTree::find_rightmost_leaf(const DwindleNode* node) const {
    return const_cast<DwindleTree*>(this)->find_rightmost_leaf(const_cast<DwindleNode*>(node));
}

void DwindleTree::collect_views(const DwindleNode* node, std::vector<View*>& out) const {
    if (!node) return;
    if (node->is_leaf()) {
        if (node->view) out.push_back(node->view);
    } else {
        collect_views(node->first.get(), out);
        collect_views(node->second.get(), out);
    }
}

namespace {

struct SplitDecision {
    DwindleSplit split = DwindleSplit::Horizontal;
    bool new_view_is_first = false;
};

SplitDecision decide_split(
    const struct wlr_box& target_box,
    const DwindleNode* target_node,
    std::optional<std::pair<double, double>> cursor_pos)
{
    SplitDecision result;

    // 1. Orientation (Horizontal vs Vertical) is strictly decided by aspect ratio
    double multiplier = Config::get().get_dwindle_split_width_multiplier();
    if (target_box.width > 0 && target_box.height > 0) {
        result.split = (static_cast<double>(target_box.width) >= static_cast<double>(target_box.height) * multiplier)
            ? DwindleSplit::Horizontal
            : DwindleSplit::Vertical;
    } else if (target_node && target_node->parent) {
        result.split = (target_node->parent->split == DwindleSplit::Horizontal)
            ? DwindleSplit::Vertical
            : DwindleSplit::Horizontal;
    } else {
        result.split = DwindleSplit::Horizontal;
    }

    // 2. Default: new view is placed second (Right for horizontal, Bottom for vertical)
    result.new_view_is_first = false;

    // 3. When smart_split is enabled, cursor position within the target tile decides
    // which side the new window occupies (Left vs Right on horizontal, Top vs Bottom on vertical)
    if (Config::get().is_dwindle_smart_split_enabled() && cursor_pos &&
        target_box.width > 0 && target_box.height > 0)
    {
        double cx = cursor_pos->first;
        double cy = cursor_pos->second;

        if (cx >= target_box.x && cx <= target_box.x + target_box.width &&
            cy >= target_box.y && cy <= target_box.y + target_box.height)
        {
            if (result.split == DwindleSplit::Horizontal) {
                double mid_x = target_box.x + (target_box.width / 2.0);
                result.new_view_is_first = (cx < mid_x);
            } else {
                double mid_y = target_box.y + (target_box.height / 2.0);
                result.new_view_is_first = (cy < mid_y);
            }
        }
    }

    return result;
}

} // anonymous namespace

void DwindleTree::add_view(View* view, View* target_focus, std::optional<std::pair<double, double>> cursor_pos) {
    if (!view) return;
    if (contains(view)) return;

    if (!m_root) {
        m_root = std::make_unique<DwindleNode>();
        m_root->view = view;
        return;
    }

    DwindleNode* target = nullptr;
    if (target_focus) {
        target = find_leaf(target_focus);
    }
    if (!target) {
        target = find_rightmost_leaf(m_root.get());
    }
    if (!target || !target->is_leaf()) {
        return;
    }

    View* old_view = target->view;
    target->view = nullptr;

    SplitDecision decision = decide_split(target->box, target, cursor_pos);

    target->split = decision.split;
    target->split_ratio = Config::get().get_default_split_ratio();

    auto child1 = std::make_unique<DwindleNode>();
    child1->parent = target;

    auto child2 = std::make_unique<DwindleNode>();
    child2->parent = target;

    if (decision.new_view_is_first) {
        child1->view = view;
        child2->view = old_view;
    } else {
        child1->view = old_view;
        child2->view = view;
    }

    if (target->box.width > 0 && target->box.height > 0) {
        if (decision.split == DwindleSplit::Horizontal) {
            int w1 = static_cast<int>(target->box.width * target->split_ratio);
            child1->box = { target->box.x, target->box.y, w1, target->box.height };
            child2->box = { target->box.x + w1, target->box.y, target->box.width - w1, target->box.height };
        } else {
            int h1 = static_cast<int>(target->box.height * target->split_ratio);
            child1->box = { target->box.x, target->box.y, target->box.width, h1 };
            child2->box = { target->box.x, target->box.y + h1, target->box.width, target->box.height - h1 };
        }
    }

    target->first = std::move(child1);
    target->second = std::move(child2);
}

bool DwindleTree::remove_view(View* view) {
    if (!view || !m_root) return false;

    if (m_root->is_leaf()) {
        if (m_root->view == view) {
            m_root.reset();
            return true;
        }
        return false;
    }

    DwindleNode* node = find_leaf(view);
    if (!node || !node->parent) return false;

    DwindleNode* parent = node->parent;
    std::unique_ptr<DwindleNode> sibling;

    if (parent->first.get() == node) {
        sibling = std::move(parent->second);
    } else {
        sibling = std::move(parent->first);
    }

    if (!sibling) return false;

    DwindleNode* grandparent = parent->parent;
    sibling->parent = grandparent;

    if (grandparent) {
        if (grandparent->first.get() == parent) {
            grandparent->first = std::move(sibling);
        } else {
            grandparent->second = std::move(sibling);
        }
    } else {
        m_root = std::move(sibling);
    }

    return true;
}

bool DwindleTree::swap_views(View* v1, View* v2) {
    if (!v1 || !v2 || v1 == v2) return false;
    DwindleNode* n1 = find_leaf(v1);
    DwindleNode* n2 = find_leaf(v2);
    if (!n1 || !n2) return false;

    std::swap(n1->view, n2->view);
    return true;
}

void DwindleTree::sync_views(const std::vector<View*>& views) {
    if (views.empty()) {
        clear();
        return;
    }

    std::unordered_set<View*> target_set(views.begin(), views.end());

    // 1. Remove views currently in tree that are no longer in the views list
    std::vector<View*> cur_views;
    if (m_root) {
        collect_views(m_root.get(), cur_views);
    }
    for (View* v : cur_views) {
        if (target_set.find(v) == target_set.end()) {
            remove_view(v);
        }
    }

    // 2. Add views in views list that are not yet in tree
    for (View* v : views) {
        if (!contains(v)) {
            add_view(v, nullptr);
        }
    }
}

std::vector<View*> DwindleTree::get_views() const {
    std::vector<View*> out;
    collect_views_into(out);
    return out;
}

void DwindleTree::collect_views_into(std::vector<View*>& out) const {
    if (m_root) {
        collect_views(m_root.get(), out);
    }
}

bool DwindleTree::toggle_split(View* view) {
    if (!view || !m_root) return false;
    DwindleNode* leaf = find_leaf(view);
    if (!leaf || !leaf->parent) return false;

    leaf->parent->split = (leaf->parent->split == DwindleSplit::Horizontal)
        ? DwindleSplit::Vertical
        : DwindleSplit::Horizontal;
    return true;
}

bool DwindleTree::adjust_split_ratio(View* view, double delta) {
    if (!view || !m_root) return false;
    DwindleNode* leaf = find_leaf(view);
    if (!leaf || !leaf->parent) return false;

    double new_ratio = std::clamp(leaf->parent->split_ratio + delta, 0.1, 0.9);
    leaf->parent->split_ratio = new_ratio;
    return true;
}

bool DwindleTree::set_split_ratio(View* view, double ratio) {
    if (!view || !m_root) return false;
    DwindleNode* leaf = find_leaf(view);
    if (!leaf || !leaf->parent) return false;

    leaf->parent->split_ratio = std::clamp(ratio, 0.1, 0.9);
    return true;
}

double DwindleTree::get_split_ratio(View* view) const {
    const DwindleNode* leaf = find_leaf(view);
    if (!leaf || !leaf->parent) return Config::get().get_default_split_ratio();
    return leaf->parent->split_ratio;
}

DwindleSplit DwindleTree::get_split_direction(View* view) const {
    const DwindleNode* leaf = find_leaf(view);
    if (!leaf || !leaf->parent) return DwindleSplit::Horizontal;
    return leaf->parent->split;
}

bool DwindleTree::is_first_child(View* view) const {
    const DwindleNode* leaf = find_leaf(view);
    if (!leaf || !leaf->parent) return true;
    return leaf->parent->first.get() == leaf;
}

std::unordered_map<View*, struct wlr_box> DwindleTree::calculate(
    const struct wlr_box& usable_box,
    int gap) const
{
    std::unordered_map<View*, struct wlr_box> result;
    if (!m_root) return result;

    calculate_recursive(m_root.get(), usable_box, gap, result);
    return result;
}

void DwindleTree::calculate_recursive(
    const DwindleNode* node,
    const struct wlr_box& box,
    int gap,
    std::unordered_map<View*, struct wlr_box>& out) const
{
    if (!node) return;

    const_cast<DwindleNode*>(node)->box = box;

    if (node->is_leaf()) {
        if (node->view) {
            out[node->view] = box;
        }
        return;
    }

    constexpr int min_dim = 20;
    if (node->split == DwindleSplit::Horizontal) {
        int total_w = std::max(min_dim * 2, box.width - gap);
        int max_w1 = std::max(min_dim, total_w - min_dim);
        int w1 = std::clamp(static_cast<int>(total_w * node->split_ratio), min_dim, max_w1);
        int w2 = total_w - w1;

        struct wlr_box b1 = { box.x, box.y, w1, box.height };
        struct wlr_box b2 = { box.x + w1 + gap, box.y, w2, box.height };

        calculate_recursive(node->first.get(), b1, gap, out);
        calculate_recursive(node->second.get(), b2, gap, out);
    } else {
        int total_h = std::max(min_dim * 2, box.height - gap);
        int max_h1 = std::max(min_dim, total_h - min_dim);
        int h1 = std::clamp(static_cast<int>(total_h * node->split_ratio), min_dim, max_h1);
        int h2 = total_h - h1;

        struct wlr_box b1 = { box.x, box.y, box.width, h1 };
        struct wlr_box b2 = { box.x, box.y + h1 + gap, box.width, h2 };

        calculate_recursive(node->first.get(), b1, gap, out);
        calculate_recursive(node->second.get(), b2, gap, out);
    }
}

struct wlr_box DwindleTree::calculate_geometry_for_new_view(
    const struct wlr_box& usable_box,
    int gap,
    View* target_focus,
    std::optional<std::pair<double, double>> cursor_pos) const
{
    if (!m_root) {
        return usable_box;
    }

    auto boxes = calculate(usable_box, gap);

    View* target_v = target_focus;
    if (!target_v || boxes.find(target_v) == boxes.end()) {
        const DwindleNode* rightmost = find_rightmost_leaf(m_root.get());
        if (rightmost && rightmost->view && boxes.find(rightmost->view) != boxes.end()) {
            target_v = rightmost->view;
        }
    }

    if (!target_v || boxes.find(target_v) == boxes.end()) {
        return usable_box;
    }

    struct wlr_box cur_b = boxes[target_v];
    double ratio = Config::get().get_default_split_ratio();
    constexpr int min_dim = 20;

    SplitDecision decision = decide_split(cur_b, find_leaf(target_v), cursor_pos);

    if (decision.split == DwindleSplit::Horizontal) {
        int total_w = std::max(min_dim * 2, cur_b.width - gap);
        int max_w1 = std::max(min_dim, total_w - min_dim);
        int w1 = std::clamp(static_cast<int>(total_w * ratio), min_dim, max_w1);
        int w2 = total_w - w1;
        if (decision.new_view_is_first) {
            return { cur_b.x, cur_b.y, w1, cur_b.height };
        } else {
            return { cur_b.x + w1 + gap, cur_b.y, w2, cur_b.height };
        }
    } else {
        int total_h = std::max(min_dim * 2, cur_b.height - gap);
        int max_h1 = std::max(min_dim, total_h - min_dim);
        int h1 = std::clamp(static_cast<int>(total_h * ratio), min_dim, max_h1);
        int h2 = total_h - h1;
        if (decision.new_view_is_first) {
            return { cur_b.x, cur_b.y, cur_b.width, h1 };
        } else {
            return { cur_b.x, cur_b.y + h1 + gap, cur_b.width, h2 };
        }
    }
}

} // namespace miquland
