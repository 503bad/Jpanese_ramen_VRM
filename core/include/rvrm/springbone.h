// VRM SpringBone(揺れもの)。VRMC_springBone 1.0 仕様の説明に従った自前実装。
// 尾の位置をワールド空間で Verlet 積分し、ボーン長への拘束とコライダー押し出しを行う。
//   next = cur + (cur - prev) * (1 - drag)
//        + parentRot * initLocalRot * boneAxis * stiffness * dt
//        + gravityDir * gravityPower * dt
// (長さの項はメートル基準なので model.unitsPerMeter を掛ける)
#pragma once

#include <vector>

#include "rvrm/model.h"
#include "rvrm/skeleton.h"

namespace rvrm {

struct SpringBoneOptions {
    // これより長い dt は分割して計算する(フレーム落ち時の暴れ防止)
    float maxStep = 1.0f / 50.0f;
    // 1 フレームで扱う最大時間(ロード直後などの巨大な dt を捨てる)
    float maxDeltaTime = 0.1f;
    int maxSubSteps = 4;
};

class SpringBoneSim {
public:
    void init(const Model& model, const Skeleton& skeleton, const SpringBoneOptions& options = {});

    // 現在の姿勢で尾の状態を初期化(ワープ・リスポーン時にも呼ぶ)
    void reset(const Pose& pose, const Mat4& componentToWorld);

    // pose(リターゲット済み)を受け取り、揺れものノードのローカル回転と global を更新する
    void update(Pose& pose, const Mat4& componentToWorld, float deltaTime);

    size_t jointCount() const { return joints_.size(); }

private:
    struct JointState {
        int node = -1;
        int tailNode = -1;       // -1 なら仮想の尾
        Vec3 boneAxis{0, 0, 1};  // ノードのローカル空間での尾の方向
        float virtualLength = 0; // 仮想の尾のローカル長
        Quat initLocalRot{};
        float hitRadius = 0, stiffness = 1, gravityPower = 0, dragForce = 0.5f;
        Vec3 gravityDir{0, 0, -1};
        int spring = -1;
        Vec3 currentTail, prevTail;  // center があれば center 空間、無ければワールド
    };
    struct SpringInfo {
        int center = -1;
        std::vector<int> colliders;
    };

    void step(Pose& pose, const Mat4& toWorld, float dt);
    Vec3 tailWorld(const Pose& pose, const Mat4& toWorld, const JointState& j) const;
    Mat4 centerMatrix(const Pose& pose, const Mat4& toWorld, int center) const;

    const Model* model_ = nullptr;
    const Skeleton* skeleton_ = nullptr;
    SpringBoneOptions opt_;
    std::vector<JointState> joints_;
    std::vector<SpringInfo> springs_;
    bool initialized_ = false;
};

}  // namespace rvrm
