#include "rvrm/humanoid.h"

#include <string>

namespace rvrm {

namespace {
using H = HumanBone;

const char* const kNames[kHumanBoneCount] = {
    "hips", "spine", "chest", "upperChest", "neck", "head", "leftEye", "rightEye", "jaw",
    "leftUpperLeg", "leftLowerLeg", "leftFoot", "leftToes",
    "rightUpperLeg", "rightLowerLeg", "rightFoot", "rightToes",
    "leftShoulder", "leftUpperArm", "leftLowerArm", "leftHand",
    "rightShoulder", "rightUpperArm", "rightLowerArm", "rightHand",
    "leftThumbMetacarpal", "leftThumbProximal", "leftThumbDistal",
    "leftIndexProximal", "leftIndexIntermediate", "leftIndexDistal",
    "leftMiddleProximal", "leftMiddleIntermediate", "leftMiddleDistal",
    "leftRingProximal", "leftRingIntermediate", "leftRingDistal",
    "leftLittleProximal", "leftLittleIntermediate", "leftLittleDistal",
    "rightThumbMetacarpal", "rightThumbProximal", "rightThumbDistal",
    "rightIndexProximal", "rightIndexIntermediate", "rightIndexDistal",
    "rightMiddleProximal", "rightMiddleIntermediate", "rightMiddleDistal",
    "rightRingProximal", "rightRingIntermediate", "rightRingDistal",
    "rightLittleProximal", "rightLittleIntermediate", "rightLittleDistal",
};

// VRM 1.0 仕様のヒューマノイド親子関係
H specParent(H b) {
    switch (b) {
        case H::hips: return H::None;
        case H::spine: return H::hips;
        case H::chest: return H::spine;
        case H::upperChest: return H::chest;
        case H::neck: return H::upperChest;
        case H::head: return H::neck;
        case H::leftEye: case H::rightEye: case H::jaw: return H::head;
        case H::leftUpperLeg: case H::rightUpperLeg: return H::hips;
        case H::leftLowerLeg: return H::leftUpperLeg;
        case H::leftFoot: return H::leftLowerLeg;
        case H::leftToes: return H::leftFoot;
        case H::rightLowerLeg: return H::rightUpperLeg;
        case H::rightFoot: return H::rightLowerLeg;
        case H::rightToes: return H::rightFoot;
        case H::leftShoulder: case H::rightShoulder: return H::upperChest;
        case H::leftUpperArm: return H::leftShoulder;
        case H::leftLowerArm: return H::leftUpperArm;
        case H::leftHand: return H::leftLowerArm;
        case H::rightUpperArm: return H::rightShoulder;
        case H::rightLowerArm: return H::rightUpperArm;
        case H::rightHand: return H::rightLowerArm;
        default: break;
    }
    // 指: 各指の第1関節は手、それ以降は 1 つ前
    int i = idx(b);
    int leftBase = idx(H::leftThumbMetacarpal), rightBase = idx(H::rightThumbMetacarpal);
    if (i >= leftBase && i < rightBase) {
        int k = (i - leftBase) % 3;
        return k == 0 ? H::leftHand : static_cast<H>(i - 1);
    }
    if (i >= rightBase && i < kHumanBoneCount) {
        int k = (i - rightBase) % 3;
        return k == 0 ? H::rightHand : static_cast<H>(i - 1);
    }
    return H::None;
}

std::array<HumanBone, 4> dirChildren(H b) {
    constexpr H N = H::None;
    switch (b) {
        case H::hips: return {N, N, N, N};  // 腰は向き補正しない
        case H::spine: return {H::chest, H::upperChest, H::neck, N};
        case H::chest: return {H::upperChest, H::neck, N, N};
        case H::upperChest: return {H::neck, N, N, N};
        case H::neck: return {H::head, N, N, N};
        case H::leftShoulder: return {H::leftUpperArm, N, N, N};
        case H::leftUpperArm: return {H::leftLowerArm, N, N, N};
        case H::leftLowerArm: return {H::leftHand, N, N, N};
        case H::leftHand: return {H::leftMiddleProximal, H::leftIndexProximal, H::leftRingProximal, N};
        case H::rightShoulder: return {H::rightUpperArm, N, N, N};
        case H::rightUpperArm: return {H::rightLowerArm, N, N, N};
        case H::rightLowerArm: return {H::rightHand, N, N, N};
        case H::rightHand: return {H::rightMiddleProximal, H::rightIndexProximal, H::rightRingProximal, N};
        case H::leftUpperLeg: return {H::leftLowerLeg, N, N, N};
        case H::leftLowerLeg: return {H::leftFoot, N, N, N};
        case H::leftFoot: return {H::leftToes, N, N, N};
        case H::rightUpperLeg: return {H::rightLowerLeg, N, N, N};
        case H::rightLowerLeg: return {H::rightFoot, N, N, N};
        case H::rightFoot: return {H::rightToes, N, N, N};
        default: break;
    }
    int i = idx(b);
    int leftBase = idx(H::leftThumbMetacarpal), rightBase = idx(H::rightThumbMetacarpal);
    int base = (i >= leftBase && i < rightBase) ? leftBase : ((i >= rightBase && i < kHumanBoneCount) ? rightBase : -1);
    if (base >= 0) {
        int k = (i - base) % 3;
        if (k < 2) return {static_cast<H>(i + 1), N, N, N};
    }
    return {N, N, N, N};
}

struct DirTable {
    HumanBoneArray<std::array<HumanBone, 4>> t;
    DirTable() {
        for (int i = 0; i < kHumanBoneCount; ++i) t[i] = dirChildren(static_cast<H>(i));
    }
};
}  // namespace

const char* humanBoneName(HumanBone b) {
    int i = idx(b);
    return (i >= 0 && i < kHumanBoneCount) ? kNames[i] : "";
}

HumanBone humanBoneFromName(std::string_view name, bool vrm0) {
    if (vrm0) {
        // VRM 0.x の親指 3 関節は Proximal/Intermediate/Distal
        static const struct { const char* from; H to; } kThumb0[] = {
            {"leftThumbProximal", H::leftThumbMetacarpal},
            {"leftThumbIntermediate", H::leftThumbProximal},
            {"leftThumbDistal", H::leftThumbDistal},
            {"rightThumbProximal", H::rightThumbMetacarpal},
            {"rightThumbIntermediate", H::rightThumbProximal},
            {"rightThumbDistal", H::rightThumbDistal},
        };
        for (const auto& e : kThumb0)
            if (name == e.from) return e.to;
    }
    for (int i = 0; i < kHumanBoneCount; ++i)
        if (name == kNames[i]) return static_cast<H>(i);
    return H::None;
}

HumanBone humanBoneSpecParent(HumanBone b) { return specParent(b); }

bool humanBoneRequired(HumanBone b) {
    switch (b) {
        case H::hips: case H::spine: case H::head:
        case H::leftUpperLeg: case H::leftLowerLeg: case H::leftFoot:
        case H::rightUpperLeg: case H::rightLowerLeg: case H::rightFoot:
        case H::leftUpperArm: case H::leftLowerArm: case H::leftHand:
        case H::rightUpperArm: case H::rightLowerArm: case H::rightHand:
            return true;
        default:
            return false;
    }
}

const std::array<HumanBone, 4>& humanBoneDirectionChildren(HumanBone b) {
    static const DirTable table;
    static const std::array<HumanBone, 4> none{H::None, H::None, H::None, H::None};
    int i = idx(b);
    return (i >= 0 && i < kHumanBoneCount) ? table.t[i] : none;
}

}  // namespace rvrm
