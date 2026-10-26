// CPU スキニング + モーフ。
//   v' = Σ w_i (JointGlobal_i * InverseBind_i) (v + Σ morphWeight_k * dPos_k)
// スキンの無いメッシュノードは「そのノードに重み 1 でぶら下がる」として同じ経路で扱う。
#pragma once

#include <vector>

#include "rvrm/model.h"
#include "rvrm/skeleton.h"

namespace rvrm {

// 描画単位(メッシュノード × プリミティブ)。UE 側の 1 メッシュセクションに対応。
struct RenderSection {
    int node = -1;
    int mesh = -1;
    int primitive = -1;
    int material = -1;
};

struct SkinnedSectionOutput {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
};

class Skinner {
public:
    void init(const Model& model);

    const std::vector<RenderSection>& sections() const { return sections_; }

    // morphWeights: メッシュごとのモーフウェイト(model.meshes と同数、空ならモーフなし)
    // out は sections() と同数にリサイズされる。
    void skin(const Model& model, const Pose& pose, const std::vector<std::vector<float>>& morphWeights,
              std::vector<SkinnedSectionOutput>& out) const;

    // 1 セクションだけ
    void skinSection(const Model& model, const Pose& pose, const std::vector<std::vector<float>>& morphWeights,
                     size_t section, SkinnedSectionOutput& out) const;

private:
    std::vector<RenderSection> sections_;
};

// 表情の重み(model.expressions と同数)→ メッシュごとのモーフウェイト
void evaluateExpressions(const Model& model, const std::vector<float>& expressionWeights,
                         std::vector<std::vector<float>>& morphWeights);

// プリセット名または名前で表情を探す(見つからなければ -1)
int findExpression(const Model& model, const std::string& presetOrName);

}  // namespace rvrm
