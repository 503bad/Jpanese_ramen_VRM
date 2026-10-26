#include "rvrm/skeleton.h"

namespace rvrm {

void Skeleton::init(const Model& model) {
    const size_t n = model.nodes.size();
    parent_.resize(n);
    children_.assign(n, {});
    for (size_t i = 0; i < n; ++i) {
        parent_[i] = model.nodes[i].parent;
        if (parent_[i] >= 0) children_[parent_[i]].push_back(static_cast<int>(i));
    }
    order_.clear();
    order_.reserve(n);
    std::vector<int> stack;
    for (size_t i = 0; i < n; ++i)
        if (parent_[i] < 0) stack.push_back(static_cast<int>(i));
    while (!stack.empty()) {
        int v = stack.back();
        stack.pop_back();
        order_.push_back(v);
        for (int c : children_[v]) stack.push_back(c);
    }
    rest_.local.resize(n);
    for (size_t i = 0; i < n; ++i) rest_.local[i] = model.nodes[i].rest;
    rest_.global.assign(n, Mat4::identity());
    computeGlobals(rest_);
}

void Skeleton::computeGlobals(Pose& pose) const {
    pose.global.resize(pose.local.size());
    for (int n : order_) {
        Mat4 l = Mat4::fromTRS(pose.local[n]);
        pose.global[n] = parent_[n] >= 0 ? pose.global[parent_[n]] * l : l;
    }
}

void Skeleton::updateSubtree(Pose& pose, int node) const {
    std::vector<int> stack{node};
    while (!stack.empty()) {
        int v = stack.back();
        stack.pop_back();
        Mat4 l = Mat4::fromTRS(pose.local[v]);
        pose.global[v] = parent_[v] >= 0 ? pose.global[parent_[v]] * l : l;
        for (int c : children_[v]) stack.push_back(c);
    }
}

}  // namespace rvrm
