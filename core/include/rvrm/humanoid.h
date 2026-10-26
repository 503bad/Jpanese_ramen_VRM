// VRM ヒューマノイドボーンの定義。
// 名前は VRM 1.0(VRMC_vrm humanoid)に合わせ、VRM 0.x の名前は読み込み時に変換する。
#pragma once

#include <array>
#include <string_view>

namespace rvrm {

enum class HumanBone : int {
    hips, spine, chest, upperChest, neck, head, leftEye, rightEye, jaw,
    leftUpperLeg, leftLowerLeg, leftFoot, leftToes,
    rightUpperLeg, rightLowerLeg, rightFoot, rightToes,
    leftShoulder, leftUpperArm, leftLowerArm, leftHand,
    rightShoulder, rightUpperArm, rightLowerArm, rightHand,
    leftThumbMetacarpal, leftThumbProximal, leftThumbDistal,
    leftIndexProximal, leftIndexIntermediate, leftIndexDistal,
    leftMiddleProximal, leftMiddleIntermediate, leftMiddleDistal,
    leftRingProximal, leftRingIntermediate, leftRingDistal,
    leftLittleProximal, leftLittleIntermediate, leftLittleDistal,
    rightThumbMetacarpal, rightThumbProximal, rightThumbDistal,
    rightIndexProximal, rightIndexIntermediate, rightIndexDistal,
    rightMiddleProximal, rightMiddleIntermediate, rightMiddleDistal,
    rightRingProximal, rightRingIntermediate, rightRingDistal,
    rightLittleProximal, rightLittleIntermediate, rightLittleDistal,
    Count,
    None = -1,
};

constexpr int kHumanBoneCount = static_cast<int>(HumanBone::Count);

template <class T>
using HumanBoneArray = std::array<T, kHumanBoneCount>;

inline int idx(HumanBone b) { return static_cast<int>(b); }

// VRM 1.0 での名前(例 "leftUpperArm")
const char* humanBoneName(HumanBone b);

// 名前 → ボーン。vrm0 = true のとき VRM 0.x の親指名
// (leftThumbProximal/Intermediate/Distal)を 1.0 の Metacarpal/Proximal/Distal に読み替える。
HumanBone humanBoneFromName(std::string_view name, bool vrm0);

// 仕様上のヒューマノイド階層での親(hips は None)。
// 実モデルで親が省略されている(例: upperChest なし)場合は humanoidParentPresent を使う。
HumanBone humanBoneSpecParent(HumanBone b);

// 仕様で必須のボーンか
bool humanBoneRequired(HumanBone b);

// ボーンの「向き」を決める子ボーンの候補(優先順、None 終端、最大 4)。
// リターゲットのレスト姿勢合わせに使う。
const std::array<HumanBone, 4>& humanBoneDirectionChildren(HumanBone b);

}  // namespace rvrm
