#include "rvrm/avatar.h"
#include "rvrm/firstperson.h"
#include "synthetic_vrm.h"
#include "test.h"

using namespace rvrm;

namespace {

Avatar makeAvatar(int version = 1, bool springs = true) {
    synth::Options o;
    o.version = version;
    auto glb = synth::build(o);
    Avatar a;
    AvatarOptions opt;
    opt.enableSpringBones = springs;
    std::string err;
    if (!a.loadFromMemory(glb.data(), glb.size(), opt, &err)) rt::fail(__FILE__, __LINE__, err);
    return a;
}

// 骨格の姿勢からヒューマノイドの SourcePose を作る
SourcePose sourceFrom(const Model& m, const Pose& p) {
    SourcePose s;
    for (int i = 0; i < kHumanBoneCount; ++i) {
        int n = m.humanoid[i];
        if (n < 0) continue;
        s[i].valid = true;
        s[i].position = p.global[n].translation();
        s[i].rotation = p.global[n].rotation();
    }
    return s;
}

Vec3 nodePos(const Pose& p, int n) { return p.global[n].translation(); }

}  // namespace

TEST(convert_to_source_mesh_space) {
    for (int version : {1, 0}) {
        Avatar a = makeAvatar(version, false);
        const Pose& rest = a.skeleton().restPose();
        const Model& m = a.model();
        CHECK(m.unitsPerMeter == 100.0f);
        // 既定: メッシュ空間は +Y 前, -X 右(= +X 左), +Z 上, cm
        CHECK_VEC(nodePos(rest, m.humanNode(HumanBone::leftHand)), Vec3(65, 0, 140), 1e-3f);
        CHECK_VEC(nodePos(rest, m.humanNode(HumanBone::rightHand)), Vec3(-65, 0, 140), 1e-3f);
        CHECK_VEC(nodePos(rest, m.humanNode(HumanBone::head)), Vec3(0, 0, 155), 1e-3f);
        CHECK_VEC(nodePos(rest, synth::boneIndex("hair0")), Vec3(0, -5, 165), 1e-3f);  // 後ろ = -Y
    }
}

TEST(rest_pose_skinning_reproduces_mesh) {
    for (int version : {1, 0}) {
        Avatar a = makeAvatar(version, false);
        CHECK(a.sections().size() == 1);
        const auto& out = a.skinned()[0];
        CHECK(out.positions.size() == 4);
        CHECK_VEC(out.positions[0], Vec3(0, 0, 100), 1e-3f);
        CHECK_VEC(out.positions[1], Vec3(65, 0, 140), 1e-3f);
        CHECK_VEC(out.positions[3], Vec3(0, 0, 155), 1e-3f);
        // 法線(glTF +Z = 前)→ +Y
        CHECK_VEC(out.normals[0], Vec3(0, 1, 0), 1e-4f);
    }
}

TEST(winding_default_and_flip_option) {
    // 既定は glTF の巻き順のまま(実機で確認済み)。flipWinding で反転できる
    Avatar a = makeAvatar(1, false);
    const auto& idx = a.model().meshes[0].primitives[0].indices;
    CHECK(idx[0] == 0 && idx[1] == 1 && idx[2] == 2);
    synth::Options o;
    auto glb = synth::build(o);
    Avatar b;
    AvatarOptions opt;
    opt.convert.flipWinding = true;
    std::string err;
    CHECK(b.loadFromMemory(glb.data(), glb.size(), opt, &err));
    const auto& idx2 = b.model().meshes[0].primitives[0].indices;
    CHECK(idx2[0] == 0 && idx2[1] == 2 && idx2[2] == 1);
}

TEST(retarget_identity) {
    Avatar a = makeAvatar(1, false);
    const Model& m = a.model();
    SourcePose rest = sourceFrom(m, a.skeleton().restPose());
    std::string err;
    CHECK(a.bindSource(rest, &err));
    CHECK_NEAR(a.isBound() ? 1 : 0, 1, 0);
    a.update(rest, Mat4::identity(), 1.0f / 60);
    for (size_t n = 0; n < m.nodes.size(); ++n)
        CHECK_VEC(nodePos(a.pose(), static_cast<int>(n)), nodePos(a.skeleton().restPose(), static_cast<int>(n)), 1e-3f);
}

TEST(retarget_follows_source_rotation) {
    Avatar a = makeAvatar(1, false);
    const Model& m = a.model();
    const Skeleton& sk = a.skeleton();
    SourcePose rest = sourceFrom(m, sk.restPose());
    std::string err;
    CHECK(a.bindSource(rest, &err));

    // 同じ骨格で左上腕・左前腕・首・腰を回したソース
    Pose src = sk.restPose();
    src.local[m.humanNode(HumanBone::leftUpperArm)].r = Quat::fromAxisAngle({0.3f, 1, 0.2f}, 0.9f) * src.local[m.humanNode(HumanBone::leftUpperArm)].r;
    src.local[m.humanNode(HumanBone::leftLowerArm)].r = Quat::fromAxisAngle({0, 0, 1}, -0.6f);
    src.local[m.humanNode(HumanBone::neck)].r = Quat::fromAxisAngle({1, 0, 0}, 0.4f);
    src.local[0].r = Quat::fromAxisAngle({0, 0, 1}, 0.5f);
    src.local[0].t = src.local[0].t + Vec3{3, 4, -10};
    sk.computeGlobals(src);

    a.update(sourceFrom(m, src), Mat4::identity(), 1.0f / 60);
    for (HumanBone b : {HumanBone::hips, HumanBone::leftHand, HumanBone::leftLowerArm, HumanBone::head, HumanBone::rightFoot,
                        HumanBone::leftThumbDistal}) {
        int n = m.humanNode(b);
        CHECK_VEC(nodePos(a.pose(), n), nodePos(src, n), 1e-2f);
    }
    // 非ヒューマノイド(髪)も親に追従
    int h2 = synth::boneIndex("hair2");
    CHECK_VEC(nodePos(a.pose(), h2), nodePos(src, h2), 1e-2f);
}

TEST(retarget_aligns_apose_source_and_scales_hips) {
    Avatar a = makeAvatar(1, false);
    const Model& m = a.model();
    const Skeleton& sk = a.skeleton();

    // ソース: 2 倍の大きさ・腕を 45° 下げた A ポーズ・ボーンのロールがターゲットと異なる
    Pose srcRest = sk.restPose();
    for (auto& l : srcRest.local) l.t = l.t * 2.0f;
    const float k = 0.78539816f;
    int lua = m.humanNode(HumanBone::leftUpperArm), rua = m.humanNode(HumanBone::rightUpperArm);
    srcRest.local[lua].r = Quat::fromAxisAngle({0, 1, 0}, k);   // +X 腕を下へ(Y 軸回り)
    srcRest.local[rua].r = Quat::fromAxisAngle({0, 1, 0}, -k);
    sk.computeGlobals(srcRest);
    SourcePose rest = sourceFrom(m, srcRest);
    // ロールの違いを再現: ソースの回転だけ骨軸回りにひねる(位置は同じ)
    for (auto& b : rest) b.rotation = b.rotation * Quat::fromAxisAngle({1, 0, 0}, 0.3f);

    std::string err;
    CHECK(a.bindSource(rest, &err));
    CHECK_NEAR(a.retargeter().hipsScale(), 0.5, 1e-4);

    a.update(rest, Mat4::identity(), 1.0f / 60);
    int lla = m.humanNode(HumanBone::leftLowerArm), lh = m.humanNode(HumanBone::leftHand);
    Vec3 dstDir = normalize(nodePos(a.pose(), lla) - nodePos(a.pose(), lua));
    Vec3 srcDir = normalize(rest[idx(HumanBone::leftLowerArm)].position - rest[idx(HumanBone::leftUpperArm)].position);
    CHECK_VEC(dstDir, srcDir, 1e-3f);
    CHECK(dstDir.z < -0.6f);  // 腕が下がっている
    // ターゲットの骨の長さは保たれる
    CHECK_NEAR(length(nodePos(a.pose(), lh) - nodePos(a.pose(), lla)), 25.0, 1e-2);

    // 腰の移動は 0.5 倍
    SourcePose cur = rest;
    cur[idx(HumanBone::hips)].position += Vec3{10, 0, 0};
    a.update(cur, Mat4::identity(), 1.0f / 60);
    CHECK_VEC(nodePos(a.pose(), 0), nodePos(sk.restPose(), 0) + Vec3(5, 0, 0), 1e-3f);
}

TEST(expressions_drive_morphs) {
    Avatar a = makeAvatar(1, false);
    CHECK(a.setExpression("blink", 1.0f));
    a.update({}, Mat4::identity(), 0);
    CHECK_VEC(a.skinned()[0].positions[3], Vec3(0, 0, 156), 1e-3f);
    CHECK(a.setExpression("blink", 0.0f));
    CHECK(a.setExpression("wink", 0.4f));  // isBinary: 0.5 未満は 0
    a.update({}, Mat4::identity(), 0);
    CHECK_VEC(a.skinned()[0].positions[3], Vec3(0, 0, 155), 1e-3f);
    CHECK(!a.setExpression("nonexistent", 1.0f));
}

TEST(springbone_gravity_length_and_collision) {
    for (int version : {1, 0}) {
        Avatar a = makeAvatar(version, true);
        const Model& m = a.model();
        SourcePose rest = sourceFrom(m, a.skeleton().restPose());
        std::string err;
        CHECK(a.bindSource(rest, &err));
        for (int i = 0; i < 600; ++i) a.update(rest, Mat4::identity(), 1.0f / 60);

        const Pose& p = a.pose();
        int h0 = synth::boneIndex("hair0"), h1 = synth::boneIndex("hair1"), h2 = synth::boneIndex("hair2");
        int head = m.humanNode(HumanBone::head);
        CHECK_NEAR(length(nodePos(p, h1) - nodePos(p, h0)), 10.0, 1e-2);
        CHECK_NEAR(length(nodePos(p, h2) - nodePos(p, h1)), 10.0, 1e-2);
        // 重力で垂れる
        CHECK(nodePos(p, h2).z < nodePos(p, h0).z - 5.0f);
        // 頭のコライダー(半径 10cm + hitRadius 2cm)で押し出される。
        // 仕様のアルゴリズムは押し出し後にボーン長へ戻すため、重力が強いとわずかにめり込むのは仕様通り。
        // コライダーが無ければ真下 (0,-5,155) に垂れる(中心から 5〜10cm)ので、11cm 以上なら効いている。
        Vec3 c = nodePos(p, head);
        if (version == 0) c = c + Vec3{0, 5, 0};  // 0.x のコライダーは前(Unity +Z)に 5cm ずらしてある
        CHECK(length(nodePos(p, h1) - c) >= 11.0f);
    }
}

TEST(springbone_inertia_follows_movement) {
    Avatar a = makeAvatar(1, true);
    const Model& m = a.model();
    SourcePose rest = sourceFrom(m, a.skeleton().restPose());
    std::string err;
    CHECK(a.bindSource(rest, &err));
    Mat4 w = Mat4::identity();
    for (int i = 0; i < 300; ++i) a.update(rest, w, 1.0f / 60);
    int h0 = synth::boneIndex("hair0"), h1 = synth::boneIndex("hair1");
    Vec3 before = nodePos(a.pose(), h1) - nodePos(a.pose(), h0);
    // 前(+Y)へ急に 20cm 移動 → 髪は慣性で後ろへ取り残される
    w.m[3][1] = 20.0f;
    a.update(rest, w, 1.0f / 60);
    Vec3 after = nodePos(a.pose(), h1) - nodePos(a.pose(), h0);
    CHECK(after.y < before.y - 1.0f);
    CHECK_NEAR(length(after), 10.0, 1e-2);
    // 大きな dt でも NaN にならない
    a.update(rest, w, 10.0f);
    Vec3 v = nodePos(a.pose(), h1);
    CHECK(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z));
}

TEST(first_person_cuts_head_triangles) {
    Avatar a = makeAvatar(1, false);
    const Model& m = a.model();
    auto head = headSubtreeNodes(m);
    CHECK(head[m.humanNode(HumanBone::head)]);
    CHECK(head[synth::boneIndex("hair2")]);
    CHECK(!head[m.humanNode(HumanBone::neck)]);
    CHECK(!head[0]);
    // 合成モデルの三角形 (0,1,2) と (0,2,3)。頂点 3 は頭に付いている
    auto idx = firstPersonIndices(m, a.sections()[0], head);
    CHECK(idx.size() == 3);
    CHECK(sectionFirstPersonFlag(m, a.sections()[0]) == FirstPersonFlag::ThirdPersonOnly);
}
