// 読み込んだ VRM モデルのエンジン非依存な表現。
// VRM 0.x / 1.0 の違いは読み込み時に吸収し、ここでは 1.0 寄りの形に正規化する。
// 座標系は読み込み直後は glTF(右手系 Y-up、メートル)。convertToUnreal() 後は UE 側の空間。
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "rvrm/humanoid.h"
#include "rvrm/math.h"

namespace rvrm {

enum class VrmVersion { Unknown, Vrm0, Vrm1 };

struct Node {
    std::string name;
    int parent = -1;
    std::vector<int> children;
    Transform rest;     // ローカルのレスト姿勢
    int mesh = -1;
    int skin = -1;
};

struct MorphTarget {
    std::string name;
    std::vector<Vec3> dPosition;  // 空のこともある
    std::vector<Vec3> dNormal;    // 空のこともある
};

struct Primitive {
    int material = -1;
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;     // 無ければ読み込み時に計算
    std::vector<Vec2> uv0;         // 無ければ空
    std::vector<Vec4> colors;      // COLOR_0(無ければ空)
    std::vector<std::array<uint16_t, 4>> joints;  // JOINTS_0(スキン無しなら空)
    std::vector<Vec4> weights;     // WEIGHTS_0(正規化済み)
    std::vector<uint32_t> indices; // 三角形リスト
    std::vector<MorphTarget> morphs;
};

struct Mesh {
    std::string name;
    std::vector<Primitive> primitives;
    std::vector<float> defaultWeights;
};

struct Skin {
    std::vector<int> joints;        // ノード番号
    std::vector<Mat4> inverseBind;  // joints と同数
};

struct Image {
    std::string name;
    std::string mimeType;           // "image/png" / "image/jpeg" 等
    std::vector<uint8_t> bytes;
};

struct Texture {
    int image = -1;
    int wrapS = 10497, wrapT = 10497;  // glTF の既定値 REPEAT
    int magFilter = 0, minFilter = 0;  // 0 = 未指定
};

enum class AlphaMode { Opaque, Mask, Blend };

struct Material {
    std::string name;
    Vec4 baseColorFactor{1, 1, 1, 1};
    int baseColorTexture = -1;
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    Vec3 emissiveFactor{0, 0, 0};
    int emissiveTexture = -1;
    int normalTexture = -1;
    bool unlit = false;  // KHR_materials_unlit

    // MToon の主要パラメータ(Phase 4 で使用。読めた範囲だけ)
    bool isMToon = false;
    Vec3 shadeColorFactor{0, 0, 0};
    int shadeMultiplyTexture = -1;
    float shadingShiftFactor = 0;
    float shadingToonyFactor = 0.9f;
    float outlineWidth = 0;          // メートル(convertToUnreal 後は UE 単位)
    Vec3 outlineColor{0, 0, 0};
};

// 利用許諾(VRM 0.x / 1.0 を正規化)。生の値も残す。
struct Meta {
    std::string name;
    std::string version;
    std::string authors;           // 複数はカンマ区切り
    std::string contactInformation;
    std::string references;
    int thumbnailImage = -1;

    // VRM 1.0 の値に正規化:
    //  avatarPermission: onlyAuthor / onlySeparatelyLicensedPerson / everyone
    //  commercialUsage: personalNonProfit / personalProfit / corporation
    //  modification: prohibited / allowModification / allowModificationRedistribution
    std::string avatarPermission;
    std::string commercialUsage;
    std::string modification;
    std::string creditNotation;
    bool allowExcessivelyViolentUsage = false;
    bool allowExcessivelySexualUsage = false;
    bool allowPoliticalOrReligiousUsage = false;
    bool allowAntisocialOrHateUsage = false;
    // 再配布可否: 1 = 可, 0 = 不可, -1 = 不明
    int allowRedistribution = -1;
    std::string licenseUrl;
    std::string otherLicenseUrl;
    std::string vrm0LicenseName;   // VRM 0.x の licenseName(参考)
};

struct MorphBind {
    int mesh = -1;
    int morphIndex = -1;
    float weight = 1.0f;  // 0..1
};

struct Expression {
    std::string name;    // 元の名前
    std::string preset;  // VRM 1.0 のプリセット名(happy, aa, blink...)。カスタムは空
    bool isBinary = false;
    std::vector<MorphBind> binds;
};

struct Collider {
    enum class Shape { Sphere, Capsule };
    int node = -1;
    Shape shape = Shape::Sphere;
    Vec3 offset{0, 0, 0};  // ノードのローカル空間
    float radius = 0;
    Vec3 tail{0, 0, 0};    // Capsule のみ(ローカル空間)
};

struct ColliderGroup {
    std::string name;
    std::vector<int> colliders;
};

struct SpringJoint {
    int node = -1;
    float hitRadius = 0;
    float stiffness = 1.0f;
    float gravityPower = 0;
    Vec3 gravityDir{0, -1, 0};
    float dragForce = 0.5f;
};

struct Spring {
    std::string name;
    // 先頭から順に親 → 子の鎖。最後のジョイントは(virtualTailLength == 0 なら)尾としてのみ使う。
    std::vector<SpringJoint> joints;
    std::vector<int> colliderGroups;
    int center = -1;
    // > 0 のとき、最後のジョイントも「親→自分の方向に virtualTailLength 伸ばした仮想の尾」で揺らす
    // (VRM 0.x の末端ボーンの扱い)。単位はモデル座標(読み込み直後はメートル)。
    float virtualTailLength = 0;
};

enum class FirstPersonFlag { Auto, Both, ThirdPersonOnly, FirstPersonOnly };

struct FirstPersonAnnotation {
    int node = -1;  // メッシュを持つノード
    FirstPersonFlag flag = FirstPersonFlag::Auto;
};

struct Model {
    VrmVersion version = VrmVersion::Unknown;
    std::vector<Node> nodes;
    std::vector<int> sceneRoots;
    std::vector<Mesh> meshes;
    std::vector<Skin> skins;
    std::vector<Image> images;
    std::vector<Texture> textures;
    std::vector<Material> materials;

    Meta meta;
    HumanBoneArray<int> humanoid;  // ノード番号、無ければ -1
    std::vector<Expression> expressions;
    std::vector<Collider> colliders;
    std::vector<ColliderGroup> colliderGroups;
    std::vector<Spring> springs;
    std::vector<FirstPersonAnnotation> firstPerson;

    // 1 メートルあたりの単位数(glTF のままなら 1、UE 変換後は 100)
    float unitsPerMeter = 1.0f;
    // 適用済みの座標変換(glTF 空間 → 現在の空間)
    Mat4 basis = Mat4::identity();

    std::vector<std::string> warnings;

    Model() { humanoid.fill(-1); }
    int humanNode(HumanBone b) const { return humanoid[idx(b)]; }
};

}  // namespace rvrm
