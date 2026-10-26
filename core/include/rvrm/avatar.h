// 1 体分の VRM アバター: 読み込み → 変換 → リターゲット → 揺れもの → スキニング をまとめたもの。
// MOD 側はこのクラスと出力バッファだけを触ればよいようにする。
#pragma once

#include <string>
#include <vector>

#include "rvrm/convert.h"
#include "rvrm/loader.h"
#include "rvrm/retarget.h"
#include "rvrm/skinning.h"
#include "rvrm/springbone.h"

namespace rvrm {

struct AvatarOptions {
    LoadOptions load;
    ConvertOptions convert;
    RetargetOptions retarget;
    SpringBoneOptions spring;
    bool enableSpringBones = true;
};

class Avatar {
public:
    bool loadFromFile(const std::string& utf8Path, const AvatarOptions& options, std::string* error);
    bool loadFromMemory(const void* data, size_t size, const AvatarOptions& options, std::string* error);

    // ソース骨格のレスト姿勢を渡してリターゲットを準備する
    bool bindSource(const SourcePose& sourceRest, std::string* error);

    // 1 フレーム分の更新。componentToWorld はソースメッシュのコンポーネント → ワールド。
    void update(const SourcePose& sourceCurrent, const Mat4& componentToWorld, float deltaTime);
    // 揺れものの状態を現在の姿勢に合わせ直す(テレポート時など)
    void resetDynamics(const Mat4& componentToWorld);

    // 表情(プリセット名 or 名前)。見つからなければ false
    bool setExpression(const std::string& presetOrName, float weight);

    const Model& model() const { return model_; }
    const Skeleton& skeleton() const { return skeleton_; }
    const Pose& pose() const { return pose_; }
    const std::vector<RenderSection>& sections() const { return skinner_.sections(); }
    const std::vector<SkinnedSectionOutput>& skinned() const { return skinned_; }
    bool isBound() const { return bound_; }
    const Retargeter& retargeter() const { return retargeter_; }

private:
    bool finishLoad(const AvatarOptions& options);

    AvatarOptions options_;
    Model model_;
    Skeleton skeleton_;
    Skinner skinner_;
    Retargeter retargeter_;
    SpringBoneSim springs_;
    Pose pose_;
    std::vector<float> expressionWeights_;
    std::vector<std::vector<float>> morphWeights_;
    std::vector<SkinnedSectionOutput> skinned_;
    bool bound_ = false;
};

}  // namespace rvrm
