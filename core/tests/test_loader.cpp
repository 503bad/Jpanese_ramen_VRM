#include "rvrm/license.h"
#include "rvrm/loader.h"
#include "synthetic_vrm.h"
#include "test.h"

using namespace rvrm;

namespace {
Model load(const synth::Options& o, bool expectOk = true) {
    auto glb = synth::build(o);
    Model m;
    std::string err;
    bool ok = loadVrmFromMemory(glb.data(), glb.size(), m, &err);
    if (ok != expectOk) rt::fail(__FILE__, __LINE__, "load result unexpected: " + err);
    return m;
}
}  // namespace

TEST(load_vrm1_structure) {
    Model m = load({});
    CHECK(m.version == VrmVersion::Vrm1);
    CHECK(m.nodes.size() == synth::bones().size() + 1);
    CHECK(m.humanNode(HumanBone::hips) == 0);
    CHECK(m.humanNode(HumanBone::leftHand) == synth::boneIndex("lHand"));
    CHECK(m.humanNode(HumanBone::leftThumbMetacarpal) == synth::boneIndex("lThumb1"));
    CHECK(m.humanNode(HumanBone::leftThumbProximal) == synth::boneIndex("lThumb2"));
    CHECK(m.humanNode(HumanBone::upperChest) == -1);
    CHECK(m.meshes.size() == 1 && m.meshes[0].primitives.size() == 1);
    const Primitive& p = m.meshes[0].primitives[0];
    CHECK(p.positions.size() == 4 && p.indices.size() == 6 && p.uv0.size() == 4);
    CHECK(p.joints.size() == 4 && p.weights.size() == 4);
    CHECK(p.morphs.size() == 1 && p.morphs[0].name == "Blink");
    CHECK(m.skins.size() == 1 && m.skins[0].joints.size() == synth::bones().size());
    CHECK(m.images.size() == 1 && m.images[0].mimeType == "image/png" && m.images[0].bytes.size() == 12);
    CHECK(m.materials.size() == 1);
    const Material& mat = m.materials[0];
    CHECK(mat.baseColorTexture == 0 && mat.alphaMode == AlphaMode::Mask && mat.doubleSided);
    CHECK_NEAR(mat.alphaCutoff, 0.3, 1e-6);
    CHECK(mat.isMToon);
    CHECK_NEAR(mat.shadingToonyFactor, 0.8, 1e-6);
    CHECK_NEAR(mat.outlineWidth, 0.002, 1e-6);
    CHECK(m.firstPerson.size() == 1 && m.firstPerson[0].flag == FirstPersonFlag::ThirdPersonOnly);
}

TEST(load_vrm1_meta_expressions_springs) {
    Model m = load({});
    CHECK(m.meta.name == "Synthetic");
    CHECK(m.meta.authors == "tester, other");
    CHECK(m.meta.avatarPermission == "everyone");
    CHECK(m.meta.allowRedistribution == 1);
    CHECK(m.expressions.size() == 2);
    CHECK(m.expressions[0].preset == "blink" && m.expressions[0].binds.size() == 1);
    CHECK(m.expressions[1].name == "wink" && m.expressions[1].preset.empty() && m.expressions[1].isBinary);
    CHECK_NEAR(m.expressions[1].binds[0].weight, 0.5, 1e-6);
    CHECK(m.colliders.size() == 1 && m.colliderGroups.size() == 1);
    CHECK_NEAR(m.colliders[0].radius, 0.1, 1e-6);
    CHECK(m.springs.size() == 1);
    CHECK(m.springs[0].joints.size() == 3 && m.springs[0].virtualTailLength == 0);
    CHECK_NEAR(m.springs[0].joints[0].gravityPower, 1, 1e-6);
    CHECK_NEAR(m.springs[0].joints[2].stiffness, 1, 1e-6);  // 既定値
}

TEST(load_vrm0_normalization) {
    Model m = load({0});
    CHECK(m.version == VrmVersion::Vrm0);
    // 0.x の親指名が 1.0 の名前に読み替えられる
    CHECK(m.humanNode(HumanBone::leftThumbMetacarpal) == synth::boneIndex("lThumb1"));
    CHECK(m.humanNode(HumanBone::leftThumbProximal) == synth::boneIndex("lThumb2"));
    CHECK(m.humanNode(HumanBone::leftThumbDistal) == synth::boneIndex("lThumb3"));
    CHECK(m.meta.name == "Synthetic0");
    CHECK(m.meta.avatarPermission == "everyone");
    CHECK(m.meta.allowExcessivelySexualUsage && !m.meta.allowExcessivelyViolentUsage);
    CHECK(m.meta.allowRedistribution == 1 && m.meta.creditNotation == "required");
    CHECK(m.meta.thumbnailImage == 0);
    // blendShape: weight 0..100 → 0..1、プリセット名の変換
    CHECK(m.expressions.size() == 2);
    CHECK(m.expressions[0].preset == "blink");
    CHECK_NEAR(m.expressions[0].binds[0].weight, 1.0, 1e-6);
    CHECK(m.expressions[1].preset == "aa");
    CHECK_NEAR(m.expressions[1].binds[0].weight, 0.5, 1e-6);
    // secondaryAnimation: hair0→hair1→hair2 と分岐 hairB の 2 本に分かれる
    CHECK(m.springs.size() == 2);
    CHECK(m.springs[0].joints.size() == 3);
    CHECK(m.springs[0].joints[0].node == synth::boneIndex("hair0"));
    CHECK(m.springs[0].joints[2].node == synth::boneIndex("hair2"));
    CHECK(m.springs[1].joints.size() == 1 && m.springs[1].joints[0].node == synth::boneIndex("hairB"));
    CHECK(m.springs[0].virtualTailLength > 0);
    CHECK_NEAR(m.springs[0].joints[0].stiffness, 0, 1e-6);
    // Unity 座標の offset は Z 反転
    CHECK(m.colliders.size() == 1);
    CHECK_VEC(m.colliders[0].offset, Vec3(0, 0, -0.05f), 1e-6f);
    CHECK(m.materials[0].isMToon);
    CHECK_NEAR(m.materials[0].shadingToonyFactor, 0.7, 1e-6);
    CHECK(m.firstPerson.size() == 1 && m.firstPerson[0].flag == FirstPersonFlag::Both);
}

TEST(license_decisions) {
    {
        Model m = load({});
        CHECK(canTransferToOtherPlayers(m.meta, m.version).allowed);
    }
    {
        synth::Options o;
        o.allowRedistribution = false;
        Model m = load(o);
        CHECK(!canTransferToOtherPlayers(m.meta, m.version).allowed);
    }
    const char* denied[] = {"Redistribution_Prohibited", "Other", ""};
    for (const char* lic : denied) {
        synth::Options o;
        o.version = 0;
        o.vrm0License = lic;
        Model m = load(o);
        CHECK(!canTransferToOtherPlayers(m.meta, m.version).allowed);
    }
    const char* allowed[] = {"CC0", "CC_BY", "CC_BY_NC_ND"};
    for (const char* lic : allowed) {
        synth::Options o;
        o.version = 0;
        o.vrm0License = lic;
        Model m = load(o);
        CHECK(canTransferToOtherPlayers(m.meta, m.version).allowed);
    }
}

TEST(loader_rejects_bad_input) {
    synth::Options o;
    o.badIndex = true;
    load(o, false);

    Model m;
    std::string err;
    CHECK(!loadVrmFromMemory("", 0, m, &err));
    const char junk[] = "glTF\x02\0\0\0garbage-garbage-garbage";
    CHECK(!loadVrmFromMemory(junk, sizeof junk, m, &err));

    // 途中で切れたファイル
    auto glb = synth::build({});
    for (size_t cut : {size_t(20), glb.size() / 2, glb.size() - 5}) {
        Model t;
        CHECK(!loadVrmFromMemory(glb.data(), cut, t, &err));
    }
    // 1 バイトずつ壊しても落ちない(結果の成否は問わない)
    for (size_t i = 0; i < glb.size(); i += 97) {
        auto broken = glb;
        broken[i] ^= 0x5A;
        Model t;
        loadVrmFromMemory(broken.data(), broken.size(), t, &err);
    }
}

TEST(loader_rejects_plain_gltf) {
    // VRM 拡張の無い glTF は既定で拒否
    const char gltf[] = R"({"asset":{"version":"2.0"},"nodes":[{"name":"a"}],"scenes":[{"nodes":[0]}]})";
    Model m;
    std::string err;
    CHECK(!loadVrmFromMemory(gltf, sizeof gltf - 1, m, &err));
    LoadOptions lo;
    lo.allowNonVrm = true;
    CHECK(loadVrmFromMemory(gltf, sizeof gltf - 1, m, &err, lo));
    CHECK(m.version == VrmVersion::Unknown && m.nodes.size() == 1);
}
