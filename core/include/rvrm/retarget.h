// ソース骨格(ゲームの MetaHuman 骨格)の姿勢 → VRM ヒューマノイドへのリターゲット。
//
// すべて同じ空間(ソースのスケルタルメッシュのコンポーネント空間 = convertModel 後の VRM のモデル空間)
// でのグローバル回転で計算する:
//   Target(h) = SrcCur(h) * SrcRest(h)^-1 * Align(h) * DstRest(h)
//   Align(h)  = ターゲットのレスト骨方向(ボーン→子)をソースのレスト骨方向へ向ける最小回転
// ボーンのロール(軸の取り方)の違いはグローバルな差分回転を使うことで吸収される。
// 腰の平行移動はソースの移動量を腰の高さの比でスケールする。
#pragma once

#include <string>
#include <vector>

#include "rvrm/humanoid.h"
#include "rvrm/model.h"
#include "rvrm/skeleton.h"

namespace rvrm {

struct SourceBone {
    bool valid = false;
    Vec3 position{0, 0, 0};  // コンポーネント空間
    Quat rotation{};         // コンポーネント空間
};

using SourcePose = HumanBoneArray<SourceBone>;

// ゲーム(MetaHuman / UE5 マネキン系)の既定ボーン名。対応の無いボーンは空文字。
HumanBoneArray<std::string> defaultMetaHumanBoneNames();

struct RetargetOptions {
    bool alignRestPose = true;
    bool translateHips = true;
    // 0 なら腰の高さの比から自動計算
    float hipsTranslationScale = 0;
};

class Retargeter {
public:
    // sourceRest: ソースのレスト姿勢(参照姿勢)のコンポーネント空間変換
    bool init(const Model& model, const Skeleton& skeleton, const SourcePose& sourceRest,
              const RetargetOptions& options, std::string* error);

    // sourceCurrent から pose(local と global)を作る。対象外のノードはレストのまま。
    void apply(const Skeleton& skeleton, const SourcePose& sourceCurrent, Pose& pose) const;

    bool isActive(HumanBone b) const { return active_[idx(b)]; }
    float hipsScale() const { return hipsScale_; }

private:
    RetargetOptions opt_;
    HumanBoneArray<bool> active_{};
    HumanBoneArray<Quat> preRot_{};      // Align * DstRest
    HumanBoneArray<Quat> srcRestInv_{};
    std::vector<int> humanOfNode_;
    SourceBone srcHipsRest_;
    Vec3 dstHipsRestPos_{0, 0, 0};
    float hipsScale_ = 1.0f;
};

}  // namespace rvrm
