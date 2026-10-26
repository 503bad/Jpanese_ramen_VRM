// ノード階層の姿勢計算。
#pragma once

#include <vector>

#include "rvrm/model.h"

namespace rvrm {

struct Pose {
    std::vector<Transform> local;  // ノードごとのローカル
    std::vector<Mat4> global;      // モデル空間(ルート基準)
};

class Skeleton {
public:
    Skeleton() = default;
    explicit Skeleton(const Model& model) { init(model); }
    void init(const Model& model);

    size_t nodeCount() const { return parent_.size(); }
    int parent(int n) const { return parent_[n]; }
    const std::vector<int>& children(int n) const { return children_[n]; }
    // 親が必ず子より前に来る順序
    const std::vector<int>& order() const { return order_; }

    const Pose& restPose() const { return rest_; }

    void computeGlobals(Pose& pose) const;
    // node 以下の global を更新(node の親の global は最新である前提)
    void updateSubtree(Pose& pose, int node) const;

private:
    std::vector<int> parent_;
    std::vector<std::vector<int>> children_;
    std::vector<int> order_;
    Pose rest_;
};

}  // namespace rvrm
