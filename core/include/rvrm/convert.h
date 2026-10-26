// glTF 空間 → Unreal(ソースのスケルタルメッシュのコンポーネント空間)への変換。
//
//   glTF : 右手系, +Y 上, メートル。VRM 1.0 は +Z 正面、VRM 0.x は多くが -Z 正面。
//   UE   : 左手系, +X 前, +Y 右, +Z 上, センチメートル。
//   基本変換 UE = (z, -x, y) * 100(行列式 -1。掌性の反転を含む)
//   その後 sourceMeshRotation で「ゲームのメッシュ空間の向き」に合わせる
//   (UE のキャラクターはメッシュが -90° Yaw されていて、メッシュ空間では +Y が前のことが多い)。
#pragma once

#include "rvrm/model.h"

namespace rvrm {

struct ConvertOptions {
    float unitsPerMeter = 100.0f;
    // VRM 0.x を Y 軸 180° 回して +Z 正面にそろえる
    bool rotateVrm0 = true;
    // UE 空間(+X 前)に変換した後に掛ける回転。既定は Z 軸 +90°(+X 前 → +Y 前)。
    // 符号付き置換行列になる回転(90° 単位)のみ想定(非一様スケールを保つため)。
    Quat sourceMeshRotation = Quat::fromAxisAngle({0, 0, 1}, 1.5707963267948966f);
    // 三角形の巻き順を反転する。実機(UE の ProceduralMesh)では反転しないのが正しかった(2026-10-26 確認)。
    bool flipWinding = false;
};

// glTF 空間 → 変換後空間の線形部分(スケール込み)
Mat3 conversionLinear(VrmVersion version, const ConvertOptions& o);

// model をその場で変換する(1 回だけ呼ぶこと)
void convertModel(Model& model, const ConvertOptions& o = {});

}  // namespace rvrm
