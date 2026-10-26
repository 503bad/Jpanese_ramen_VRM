#include "rvrm/retarget.h"

#include <cmath>

namespace rvrm {

HumanBoneArray<std::string> defaultMetaHumanBoneNames() {
    using H = HumanBone;
    HumanBoneArray<std::string> n;
    auto set = [&](H b, const char* s) { n[idx(b)] = s; };
    set(H::hips, "pelvis");
    set(H::spine, "spine_01");
    set(H::chest, "spine_03");
    set(H::upperChest, "spine_05");
    set(H::neck, "neck_01");
    // 実機(2026-10-26 調査)のボーン名。FName は大文字小文字を区別しないが、ログと照合しやすいよう実名に合わせる
    set(H::head, "Head");
    set(H::leftEye, "FACIAL_L_Eye");
    set(H::rightEye, "FACIAL_R_Eye");
    set(H::jaw, "FACIAL_C_Jaw");
    for (int side = 0; side < 2; ++side) {
        const bool L = side == 0;
        const std::string s = L ? "_l" : "_r";
        auto b = [&](H left, H right) { return L ? left : right; };
        n[idx(b(H::leftShoulder, H::rightShoulder))] = "clavicle" + s;
        n[idx(b(H::leftUpperArm, H::rightUpperArm))] = "upperarm" + s;
        n[idx(b(H::leftLowerArm, H::rightLowerArm))] = "lowerarm" + s;
        n[idx(b(H::leftHand, H::rightHand))] = "hand" + s;
        n[idx(b(H::leftUpperLeg, H::rightUpperLeg))] = "thigh" + s;
        n[idx(b(H::leftLowerLeg, H::rightLowerLeg))] = "calf" + s;
        n[idx(b(H::leftFoot, H::rightFoot))] = "foot" + s;
        n[idx(b(H::leftToes, H::rightToes))] = "ball" + s;
        // 指: 親指は thumb_01..03、他は *_01..03(metacarpal は VRM に対応ボーンなし)
        const H base = b(H::leftThumbMetacarpal, H::rightThumbMetacarpal);
        const char* fingers[5] = {"thumb", "index", "middle", "ring", "pinky"};
        for (int f = 0; f < 5; ++f)
            for (int k = 0; k < 3; ++k)
                n[idx(base) + f * 3 + k] = std::string(fingers[f]) + "_0" + std::to_string(k + 1) + s;
    }
    return n;
}

bool Retargeter::init(const Model& model, const Skeleton& skeleton, const SourcePose& src,
                      const RetargetOptions& options, std::string* error) {
    opt_ = options;
    const Pose& rest = skeleton.restPose();
    humanOfNode_.assign(skeleton.nodeCount(), -1);
    HumanBoneArray<Quat> align{};

    for (int i = 0; i < kHumanBoneCount; ++i) {
        const auto h = static_cast<HumanBone>(i);
        const int node = model.humanoid[i];
        active_[i] = node >= 0 && node < static_cast<int>(skeleton.nodeCount()) && src[i].valid;
        if (!active_[i]) continue;
        humanOfNode_[node] = i;

        // 向き合わせ。列挙順は仕様上の親が先なので、継承元は計算済み。
        bool found = false;
        if (opt_.alignRestPose) {
            for (HumanBone c : humanBoneDirectionChildren(h)) {
                if (c == HumanBone::None) break;
                int cn = model.humanoid[idx(c)];
                if (cn < 0 || !src[idx(c)].valid) continue;
                Vec3 srcDir = src[idx(c)].position - src[i].position;
                Vec3 dstDir = rest.global[cn].translation() - rest.global[node].translation();
                if (length(srcDir) < 1e-5f || length(dstDir) < 1e-5f) continue;
                align[i] = fromToRotation(dstDir, srcDir);
                found = true;
                break;
            }
        }
        if (!found) {
            align[i] = Quat::identity();
            if (opt_.alignRestPose && h != HumanBone::hips) {
                // 向きを決める子が無いボーン(頭・指先など)は親の補正を引き継ぐ
                for (HumanBone p = humanBoneSpecParent(h); p != HumanBone::None; p = humanBoneSpecParent(p)) {
                    if (active_[idx(p)]) { align[i] = align[idx(p)]; break; }
                }
            }
        }
        preRot_[i] = normalize(align[i] * rest.global[node].rotation());
        srcRestInv_[i] = normalize(src[i].rotation).inverse();
    }

    const int hips = model.humanNode(HumanBone::hips);
    if (!active_[idx(HumanBone::hips)]) {
        if (error) *error = "hips をリターゲットできません(ソースまたはターゲットに無い)";
        return false;
    }
    srcHipsRest_ = src[idx(HumanBone::hips)];
    dstHipsRestPos_ = rest.global[hips].translation();
    hipsScale_ = opt_.hipsTranslationScale;
    if (hipsScale_ <= 0) {
        float sh = srcHipsRest_.position.z, dh = dstHipsRestPos_.z;
        hipsScale_ = (std::fabs(sh) > 1e-3f && dh > 1e-3f) ? dh / sh : 1.0f;
    }
    return true;
}

void Retargeter::apply(const Skeleton& skeleton, const SourcePose& cur, Pose& pose) const {
    const Pose& rest = skeleton.restPose();
    pose.local = rest.local;
    pose.global.resize(pose.local.size());
    for (int n : skeleton.order()) {
        const int p = skeleton.parent(n);
        const Mat4 parentG = p >= 0 ? pose.global[p] : Mat4::identity();
        const int h = n < static_cast<int>(humanOfNode_.size()) ? humanOfNode_[n] : -1;
        if (h >= 0 && cur[h].valid) {
            Quat target = normalize(cur[h].rotation) * srcRestInv_[h] * preRot_[h];
            Quat parentRot = p >= 0 ? parentG.rotation() : Quat::identity();
            pose.local[n].r = normalize(parentRot.inverse() * target);
            if (h == idx(HumanBone::hips) && opt_.translateHips) {
                Vec3 want = dstHipsRestPos_ + (cur[h].position - srcHipsRest_.position) * hipsScale_;
                pose.local[n].t = p >= 0 ? parentG.inverse().transformPoint(want) : want;
            }
        }
        Mat4 l = Mat4::fromTRS(pose.local[n]);
        pose.global[n] = p >= 0 ? parentG * l : l;
    }
}

}  // namespace rvrm
