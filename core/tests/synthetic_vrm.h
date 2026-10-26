// テスト用に合成した最小の VRM(GLB)を生成する。実在のモデルは使わない。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rvrm/math.h"

namespace synth {

struct Bone {
    const char* name;
    int parent;
    rvrm::Vec3 t;          // VRM 1.0 向き(+Z 正面、キャラの左 = +X)の glTF 座標
    const char* human1;    // VRM 1.0 のヒューマノイド名(無ければ nullptr)
    const char* human0;    // VRM 0.x のヒューマノイド名
};

const std::vector<Bone>& bones();
int boneIndex(const char* name);
// VRM 1.0 向きでのレスト時のグローバル位置
rvrm::Vec3 boneGlobal(int index);
int bodyNodeIndex();

struct Options {
    int version = 1;                       // 0 or 1
    bool allowRedistribution = true;       // VRM 1.0
    std::string vrm0License = "CC_BY";     // VRM 0.x
    bool badIndex = false;                 // 頂点数を超えるインデックスを入れる
};

std::vector<uint8_t> build(const Options& o);

}  // namespace synth
