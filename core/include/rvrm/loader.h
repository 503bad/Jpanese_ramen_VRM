// VRM(GLB)ファイルの読み込み。
// 他プレイヤーから受け取ったファイルも読む前提なので、不正なデータは例外ではなく false で弾く。
#pragma once

#include <cstddef>
#include <string>

#include "rvrm/model.h"

namespace rvrm {

struct LoadOptions {
    // VRM 0.x の secondaryAnimation(コライダーの offset / gravityDir)は
    // Unity 座標で書かれているとみなし、glTF 座標へ Z 反転して取り込む。要実機確認のため切替可能にしておく。
    bool vrm0SpringVectorsInUnitySpace = true;
    // VRM 0.x の末端ボーンに付ける仮想の尾の長さ(メートル)
    float vrm0VirtualTailLength = 0.07f;
    // ヒューマノイド拡張が無い glTF も読み込みを許すか(vrm_inspect 用)
    bool allowNonVrm = false;
};

bool loadVrmFromMemory(const void* data, size_t size, Model& out, std::string* error,
                       const LoadOptions& options = {});

// path は UTF-8
bool loadVrmFromFile(const std::string& path, Model& out, std::string* error,
                     const LoadOptions& options = {});

}  // namespace rvrm
