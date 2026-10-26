// 一人称視点の表示(VRM の firstPerson 注釈に従う)。
//   both            : 常に表示
//   thirdPersonOnly : 一人称では隠す
//   firstPersonOnly : 一人称でだけ表示
//   auto            : 一人称では頭(head ボーン以下)の影響を受ける三角形を取り除く
// 注釈の無いメッシュは auto として扱う。
#pragma once

#include <cstdint>
#include <vector>

#include "rvrm/model.h"
#include "rvrm/skinning.h"

namespace rvrm {

FirstPersonFlag sectionFirstPersonFlag(const Model& model, const RenderSection& section);

// head ボーン以下のノードなら true(ノード番号で引く)
std::vector<bool> headSubtreeNodes(const Model& model);

// 一人称用のインデックス(auto のとき頭の影響を受ける三角形を除く)。
// weightThreshold を超える重みで頭側のジョイントに付いている頂点を含む三角形を除外する。
std::vector<uint32_t> firstPersonIndices(const Model& model, const RenderSection& section,
                                         const std::vector<bool>& headNodes, float weightThreshold = 0.01f);

}  // namespace rvrm
