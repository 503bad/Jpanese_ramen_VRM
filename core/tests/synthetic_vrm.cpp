#include "synthetic_vrm.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace synth {

using rvrm::Vec3;

const std::vector<Bone>& bones() {
    static const std::vector<Bone> b = {
        {"hips", -1, {0, 1.0f, 0}, "hips", "hips"},
        {"spine", 0, {0, 0.1f, 0}, "spine", "spine"},
        {"chest", 1, {0, 0.15f, 0}, "chest", "chest"},
        {"neck", 2, {0, 0.2f, 0}, "neck", "neck"},
        {"head", 3, {0, 0.1f, 0}, "head", "head"},
        {"lShoulder", 2, {0.05f, 0.15f, 0}, "leftShoulder", "leftShoulder"},
        {"lUpperArm", 5, {0.1f, 0, 0}, "leftUpperArm", "leftUpperArm"},
        {"lLowerArm", 6, {0.25f, 0, 0}, "leftLowerArm", "leftLowerArm"},
        {"lHand", 7, {0.25f, 0, 0}, "leftHand", "leftHand"},
        {"lMiddle1", 8, {0.08f, 0, 0}, "leftMiddleProximal", "leftMiddleProximal"},
        {"lThumb1", 8, {0.02f, 0, 0.03f}, "leftThumbMetacarpal", "leftThumbProximal"},
        {"lThumb2", 10, {0.03f, 0, 0.01f}, "leftThumbProximal", "leftThumbIntermediate"},
        {"lThumb3", 11, {0.02f, 0, 0.01f}, "leftThumbDistal", "leftThumbDistal"},
        {"rShoulder", 2, {-0.05f, 0.15f, 0}, "rightShoulder", "rightShoulder"},
        {"rUpperArm", 13, {-0.1f, 0, 0}, "rightUpperArm", "rightUpperArm"},
        {"rLowerArm", 14, {-0.25f, 0, 0}, "rightLowerArm", "rightLowerArm"},
        {"rHand", 15, {-0.25f, 0, 0}, "rightHand", "rightHand"},
        {"lUpperLeg", 0, {0.1f, -0.05f, 0}, "leftUpperLeg", "leftUpperLeg"},
        {"lLowerLeg", 17, {0, -0.45f, 0}, "leftLowerLeg", "leftLowerLeg"},
        {"lFoot", 18, {0, -0.42f, 0}, "leftFoot", "leftFoot"},
        {"rUpperLeg", 0, {-0.1f, -0.05f, 0}, "rightUpperLeg", "rightUpperLeg"},
        {"rLowerLeg", 20, {0, -0.45f, 0}, "rightLowerLeg", "rightLowerLeg"},
        {"rFoot", 21, {0, -0.42f, 0}, "rightFoot", "rightFoot"},
        {"hair0", 4, {0, 0.1f, -0.05f}, nullptr, nullptr},
        {"hair1", 23, {0, 0, -0.1f}, nullptr, nullptr},
        {"hair2", 24, {0, 0, -0.1f}, nullptr, nullptr},
        {"hairB", 24, {0.05f, 0, -0.05f}, nullptr, nullptr},
    };
    return b;
}

int boneIndex(const char* name) {
    const auto& b = bones();
    for (size_t i = 0; i < b.size(); ++i)
        if (std::strcmp(b[i].name, name) == 0) return static_cast<int>(i);
    return -1;
}

Vec3 boneGlobal(int i) {
    Vec3 p{0, 0, 0};
    for (; i >= 0; i = bones()[i].parent) p += bones()[i].t;
    return p;
}

int bodyNodeIndex() { return static_cast<int>(bones().size()); }

namespace {

std::string f3(Vec3 v) {
    char b[128];
    std::snprintf(b, sizeof b, "[%.6g,%.6g,%.6g]", v.x, v.y, v.z);
    return b;
}
std::string fxyz(Vec3 v) {
    char b[128];
    std::snprintf(b, sizeof b, "{\"x\":%.6g,\"y\":%.6g,\"z\":%.6g}", v.x, v.y, v.z);
    return b;
}

struct BinWriter {
    std::vector<uint8_t> bin;
    std::string views, accessors;
    int viewCount = 0, accessorCount = 0;

    int add(const void* data, size_t bytes, int componentType, const char* type, size_t count,
            const std::string& extra = "") {
        while (bin.size() % 4) bin.push_back(0);
        size_t off = bin.size();
        const auto* p = static_cast<const uint8_t*>(data);
        bin.insert(bin.end(), p, p + bytes);
        if (viewCount) views += ",";
        views += "{\"buffer\":0,\"byteOffset\":" + std::to_string(off) + ",\"byteLength\":" + std::to_string(bytes) + "}";
        if (accessorCount) accessors += ",";
        accessors += "{\"bufferView\":" + std::to_string(viewCount) + ",\"componentType\":" + std::to_string(componentType) +
                     ",\"count\":" + std::to_string(count) + ",\"type\":\"" + type + "\"" + extra + "}";
        ++viewCount;
        return accessorCount++;
    }
};

}  // namespace

std::vector<uint8_t> build(const Options& o) {
    const auto& B = bones();
    const bool v0 = o.version == 0;
    // VRM 0.x は Y 軸 180° 回した向き(-Z 正面)で書く
    auto orient = [&](Vec3 v) { return v0 ? Vec3{-v.x, v.y, -v.z} : v; };

    BinWriter w;
    // ---- メッシュ ----
    const int hips = 0, lHand = boneIndex("lHand"), head = boneIndex("head");
    Vec3 pos[4] = {orient(boneGlobal(hips)), orient(boneGlobal(lHand)),
                   orient(boneGlobal(lHand) + Vec3{0, 0.05f, 0}), orient(boneGlobal(head))};
    Vec3 nrm[4];
    for (auto& n : nrm) n = orient({0, 0, 1});
    float uv[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    uint16_t joints[16] = {static_cast<uint16_t>(hips), 0, 0, 0, static_cast<uint16_t>(lHand), 0, 0, 0,
                           static_cast<uint16_t>(lHand), 0, 0, 0, static_cast<uint16_t>(head), 0, 0, 0};
    float weights[16] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    if (o.badIndex) indices[5] = 99;
    Vec3 morph[4] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, orient({0, 0.01f, 0})};

    Vec3 mn = pos[0], mx = pos[0];
    for (auto& p : pos)
        for (int k = 0; k < 3; ++k) { mn[k] = std::min(mn[k], p[k]); mx[k] = std::max(mx[k], p[k]); }
    int aPos = w.add(pos, sizeof pos, 5126, "VEC3", 4, ",\"min\":" + f3(mn) + ",\"max\":" + f3(mx));
    int aNrm = w.add(nrm, sizeof nrm, 5126, "VEC3", 4);
    int aUv = w.add(uv, sizeof uv, 5126, "VEC2", 4);
    int aJ = w.add(joints, sizeof joints, 5123, "VEC4", 4);
    int aW = w.add(weights, sizeof weights, 5126, "VEC4", 4);
    int aIdx = w.add(indices, sizeof indices, 5123, "SCALAR", 6);
    int aMorph = w.add(morph, sizeof morph, 5126, "VEC3", 4, ",\"min\":[0,0,0],\"max\":" + f3(morph[3]));

    std::vector<float> ibm(B.size() * 16, 0.0f);
    for (size_t j = 0; j < B.size(); ++j) {
        Vec3 g = orient(boneGlobal(static_cast<int>(j)));
        float* m = &ibm[j * 16];
        m[0] = m[5] = m[10] = m[15] = 1;
        m[12] = -g.x; m[13] = -g.y; m[14] = -g.z;
    }
    int aIbm = w.add(ibm.data(), ibm.size() * 4, 5126, "MAT4", B.size());

    // 1x1 PNG(中身の正しさは問わない。バイト列として運べることだけ確認)
    const uint8_t png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 0};
    while (w.bin.size() % 4) w.bin.push_back(0);
    size_t pngOff = w.bin.size();
    w.bin.insert(w.bin.end(), png, png + sizeof png);
    w.views += ",{\"buffer\":0,\"byteOffset\":" + std::to_string(pngOff) + ",\"byteLength\":" + std::to_string(sizeof png) + "}";
    int pngView = w.viewCount++;

    // ---- ノード ----
    std::string nodes;
    for (size_t i = 0; i < B.size(); ++i) {
        if (i) nodes += ",";
        nodes += "{\"name\":\"" + std::string(B[i].name) + "\",\"translation\":" + f3(orient(B[i].t));
        std::string ch;
        for (size_t c = 0; c < B.size(); ++c)
            if (B[c].parent == static_cast<int>(i)) ch += (ch.empty() ? "" : ",") + std::to_string(c);
        if (!ch.empty()) nodes += ",\"children\":[" + ch + "]";
        nodes += "}";
    }
    const int body = bodyNodeIndex();
    nodes += ",{\"name\":\"Body\",\"mesh\":0,\"skin\":0}";
    std::string skinJoints;
    for (size_t i = 0; i < B.size(); ++i) skinJoints += (i ? "," : "") + std::to_string(i);

    std::string json = "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0," + std::to_string(body) + "]}]";
    json += ",\"nodes\":[" + nodes + "]";
    json += ",\"skins\":[{\"joints\":[" + skinJoints + "],\"inverseBindMatrices\":" + std::to_string(aIbm) + "}]";
    json += ",\"meshes\":[{\"name\":\"BodyMesh\",\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(aPos) +
            ",\"NORMAL\":" + std::to_string(aNrm) + ",\"TEXCOORD_0\":" + std::to_string(aUv) +
            ",\"JOINTS_0\":" + std::to_string(aJ) + ",\"WEIGHTS_0\":" + std::to_string(aW) +
            "},\"indices\":" + std::to_string(aIdx) + ",\"material\":0,\"targets\":[{\"POSITION\":" +
            std::to_string(aMorph) + "}]}],\"extras\":{\"targetNames\":[\"Blink\"]}}]";
    json += ",\"materials\":[{\"name\":\"Skin\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0.9,0.8,1],"
            "\"baseColorTexture\":{\"index\":0}},\"alphaMode\":\"MASK\",\"alphaCutoff\":0.3,\"doubleSided\":true";
    if (!v0)
        json += ",\"extensions\":{\"VRMC_materials_mtoon\":{\"specVersion\":\"1.0\",\"shadeColorFactor\":[0.5,0.4,0.4],"
                "\"shadingToonyFactor\":0.8,\"outlineWidthMode\":\"worldCoordinates\",\"outlineWidthFactor\":0.002}}";
    json += "}]";
    json += ",\"textures\":[{\"source\":0}],\"images\":[{\"name\":\"tex\",\"bufferView\":" + std::to_string(pngView) +
            ",\"mimeType\":\"image/png\"}]";
    json += ",\"buffers\":[{\"byteLength\":" + std::to_string(w.bin.size()) + "}]";
    json += ",\"bufferViews\":[" + w.views + "],\"accessors\":[" + w.accessors + "]";

    const int hair0 = boneIndex("hair0"), hair1 = boneIndex("hair1"), hair2 = boneIndex("hair2");
    if (!v0) {
        std::string hb;
        for (size_t i = 0; i < B.size(); ++i)
            if (B[i].human1) hb += std::string(hb.empty() ? "" : ",") + "\"" + B[i].human1 + "\":{\"node\":" + std::to_string(i) + "}";
        json += ",\"extensionsUsed\":[\"VRMC_vrm\",\"VRMC_springBone\",\"VRMC_materials_mtoon\"]";
        json += ",\"extensions\":{\"VRMC_vrm\":{\"specVersion\":\"1.0\",\"humanoid\":{\"humanBones\":{" + hb + "}}";
        json += ",\"meta\":{\"name\":\"Synthetic\",\"version\":\"1\",\"authors\":[\"tester\",\"other\"],"
                "\"licenseUrl\":\"https://vrm.dev/licenses/1.0/\",\"avatarPermission\":\"everyone\","
                "\"commercialUsage\":\"personalNonProfit\",\"allowRedistribution\":" +
                std::string(o.allowRedistribution ? "true" : "false") + ",\"modification\":\"prohibited\"}";
        json += ",\"expressions\":{\"preset\":{\"blink\":{\"morphTargetBinds\":[{\"node\":" + std::to_string(body) +
                ",\"index\":0,\"weight\":1}]}},\"custom\":{\"wink\":{\"isBinary\":true,\"morphTargetBinds\":[{\"node\":" +
                std::to_string(body) + ",\"index\":0,\"weight\":0.5}]}}}";
        json += ",\"firstPerson\":{\"meshAnnotations\":[{\"node\":" + std::to_string(body) + ",\"type\":\"thirdPersonOnly\"}]}}";
        json += ",\"VRMC_springBone\":{\"specVersion\":\"1.0\",\"colliders\":[{\"node\":" + std::to_string(head) +
                ",\"shape\":{\"sphere\":{\"offset\":[0,0,0],\"radius\":0.1}}}],"
                "\"colliderGroups\":[{\"name\":\"head\",\"colliders\":[0]}],"
                "\"springs\":[{\"name\":\"hair\",\"joints\":["
                "{\"node\":" + std::to_string(hair0) + ",\"stiffness\":0,\"gravityPower\":1,\"dragForce\":0.4,\"hitRadius\":0.02},"
                "{\"node\":" + std::to_string(hair1) + ",\"stiffness\":0,\"gravityPower\":1,\"dragForce\":0.4,\"hitRadius\":0.02},"
                "{\"node\":" + std::to_string(hair2) + "}],\"colliderGroups\":[0]}]}}";
    } else {
        std::string hb;
        for (size_t i = 0; i < B.size(); ++i)
            if (B[i].human0) hb += std::string(hb.empty() ? "" : ",") + "{\"bone\":\"" + B[i].human0 + "\",\"node\":" + std::to_string(i) + "}";
        json += ",\"extensionsUsed\":[\"VRM\"]";
        json += ",\"extensions\":{\"VRM\":{\"specVersion\":\"0.0\",\"humanoid\":{\"humanBones\":[" + hb + "]}";
        json += ",\"meta\":{\"title\":\"Synthetic0\",\"author\":\"tester\",\"allowedUserName\":\"Everyone\","
                "\"commercialUssageName\":\"Disallow\",\"violentUssageName\":\"Disallow\",\"sexualUssageName\":\"Allow\","
                "\"licenseName\":\"" + o.vrm0License + "\",\"texture\":0}";
        json += ",\"blendShapeMaster\":{\"blendShapeGroups\":[{\"name\":\"Blink\",\"presetName\":\"blink\",\"binds\":["
                "{\"mesh\":0,\"index\":0,\"weight\":100}]},{\"name\":\"A\",\"presetName\":\"a\",\"binds\":["
                "{\"mesh\":0,\"index\":0,\"weight\":50}]}]}";
        json += ",\"firstPerson\":{\"firstPersonBone\":" + std::to_string(head) +
                ",\"meshAnnotations\":[{\"mesh\":0,\"firstPersonFlag\":\"Both\"}]}";
        json += ",\"secondaryAnimation\":{\"boneGroups\":[{\"comment\":\"hair\",\"stiffiness\":0,\"gravityPower\":1,"
                "\"gravityDir\":" + fxyz({0, -1, 0}) + ",\"dragForce\":0.4,\"center\":-1,\"hitRadius\":0.02,"
                "\"bones\":[" + std::to_string(hair0) + "],\"colliderGroups\":[0]}],"
                "\"colliderGroups\":[{\"node\":" + std::to_string(head) + ",\"colliders\":[{\"offset\":" + fxyz({0, 0, 0.05f}) +
                ",\"radius\":0.1}]}]}";
        json += ",\"materialProperties\":[{\"name\":\"Skin\",\"shader\":\"VRM/MToon\",\"floatProperties\":{\"_ShadeToony\":0.7},"
                "\"vectorProperties\":{\"_ShadeColor\":[0.3,0.2,0.2,1]},\"textureProperties\":{\"_MainTex\":0}}]}}";
    }
    json += "}";
    while (json.size() % 4) json += ' ';
    while (w.bin.size() % 4) w.bin.push_back(0);

    std::vector<uint8_t> glb;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) glb.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    u32(0x46546C67);  // "glTF"
    u32(2);
    u32(static_cast<uint32_t>(12 + 8 + json.size() + 8 + w.bin.size()));
    u32(static_cast<uint32_t>(json.size()));
    u32(0x4E4F534A);  // "JSON"
    glb.insert(glb.end(), json.begin(), json.end());
    u32(static_cast<uint32_t>(w.bin.size()));
    u32(0x004E4942);  // "BIN\0"
    glb.insert(glb.end(), w.bin.begin(), w.bin.end());
    return glb;
}

}  // namespace synth
