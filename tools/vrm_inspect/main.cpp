// vrm_inspect: VRM ファイルの中身(ヒューマノイド対応・メッシュ・meta/許諾・揺れもの)を表示し、
// 必要なら core のパイプライン(変換 → リターゲット → スキニング)を通した結果を OBJ に書き出す。
//
//   vrm_inspect <file.vrm> [--obj out.obj] [--apose 度] [--textures]
//
//   --obj      : core で姿勢を付けてスキニングした結果を OBJ(Y-up, メートル)で書き出す
//   --apose N  : 腕を N 度下げたソース姿勢をリターゲットして書き出す(既定 0 = レスト姿勢)
//   --textures : OBJ と同じ場所にテクスチャを書き出し、MTL から参照する
//   --source-pose F [--use-cur]
//              : ソース骨格のレスト姿勢と実際の姿勢を書いたファイル(1 行 = 名前 + レスト 7 値 + 現在 7 値、
//                コンポーネント空間)を使ってリターゲットする。--use-cur で現在の姿勢、無ければレスト姿勢を適用
//   --bench N  : 揺れもの込みの毎フレーム更新(リターゲット + SpringBone + スキニング)を N 回計測する

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

#include "rvrm/avatar.h"
#include "rvrm/license.h"

using namespace rvrm;
namespace fs = std::filesystem;

namespace {

const char* versionName(VrmVersion v) {
    switch (v) {
        case VrmVersion::Vrm0: return "VRM 0.x";
        case VrmVersion::Vrm1: return "VRM 1.0";
        default: return "(VRM ではない)";
    }
}

const char* alphaName(AlphaMode a) {
    switch (a) {
        case AlphaMode::Mask: return "MASK";
        case AlphaMode::Blend: return "BLEND";
        default: return "OPAQUE";
    }
}

const char* fpName(FirstPersonFlag f) {
    switch (f) {
        case FirstPersonFlag::Both: return "both";
        case FirstPersonFlag::ThirdPersonOnly: return "thirdPersonOnly";
        case FirstPersonFlag::FirstPersonOnly: return "firstPersonOnly";
        default: return "auto";
    }
}

void printReport(const Model& m) {
    std::printf("== 基本 ==\n");
    std::printf("  形式      : %s\n", versionName(m.version));
    size_t verts = 0, tris = 0, prims = 0, morphs = 0;
    for (const Mesh& me : m.meshes)
        for (const Primitive& p : me.primitives) {
            verts += p.positions.size();
            tris += p.indices.size() / 3;
            ++prims;
            morphs += p.morphs.size();
        }
    std::printf("  ノード    : %zu\n", m.nodes.size());
    std::printf("  メッシュ  : %zu(プリミティブ %zu、頂点 %zu、三角形 %zu、モーフ %zu)\n", m.meshes.size(), prims, verts,
                tris, morphs);
    std::printf("  スキン    : %zu\n", m.skins.size());
    std::printf("  マテリアル: %zu / テクスチャ %zu / 画像 %zu\n", m.materials.size(), m.textures.size(), m.images.size());

    const Meta& mt = m.meta;
    std::printf("\n== meta(利用許諾) ==\n");
    std::printf("  名前              : %s\n", mt.name.c_str());
    std::printf("  作者              : %s\n", mt.authors.c_str());
    std::printf("  バージョン        : %s\n", mt.version.c_str());
    std::printf("  アバター利用      : %s\n", mt.avatarPermission.c_str());
    std::printf("  商用利用          : %s\n", mt.commercialUsage.c_str());
    std::printf("  改変              : %s\n", mt.modification.c_str());
    std::printf("  クレジット表記    : %s\n", mt.creditNotation.c_str());
    std::printf("  暴力/性的表現     : %s / %s\n", mt.allowExcessivelyViolentUsage ? "可" : "不可",
                mt.allowExcessivelySexualUsage ? "可" : "不可");
    std::printf("  再配布            : %s\n", mt.allowRedistribution == 1 ? "可" : (mt.allowRedistribution == 0 ? "不可" : "不明"));
    if (!mt.vrm0LicenseName.empty()) std::printf("  ライセンス(0.x)   : %s\n", mt.vrm0LicenseName.c_str());
    if (!mt.licenseUrl.empty()) std::printf("  licenseUrl        : %s\n", mt.licenseUrl.c_str());
    if (!mt.otherLicenseUrl.empty()) std::printf("  otherLicenseUrl   : %s\n", mt.otherLicenseUrl.c_str());
    TransferDecision td = canTransferToOtherPlayers(mt, m.version);
    std::printf("  → マルチプレイでの送信: %s(%s)\n", td.allowed ? "する" : "しない", td.reason.c_str());

    std::printf("\n== ヒューマノイド ==\n");
    for (int i = 0; i < kHumanBoneCount; ++i) {
        auto b = static_cast<HumanBone>(i);
        int n = m.humanoid[i];
        if (n < 0 && !humanBoneRequired(b)) continue;
        std::printf("  %-24s %s\n", humanBoneName(b),
                    n >= 0 ? (std::to_string(n) + " " + m.nodes[n].name).c_str() : "!! 未定義(必須)");
    }

    std::printf("\n== マテリアル ==\n");
    for (size_t i = 0; i < m.materials.size(); ++i) {
        const Material& ma = m.materials[i];
        std::printf("  [%zu] %-28s %-6s %s%s tex=%d\n", i, ma.name.c_str(), alphaName(ma.alphaMode),
                    ma.doubleSided ? "両面 " : "", ma.isMToon ? "MToon" : (ma.unlit ? "Unlit" : "PBR"), ma.baseColorTexture);
    }

    std::printf("\n== 表情 (%zu) ==\n", m.expressions.size());
    for (const Expression& e : m.expressions)
        std::printf("  %-20s preset=%-12s binds=%zu%s\n", e.name.c_str(), e.preset.empty() ? "-" : e.preset.c_str(),
                    e.binds.size(), e.isBinary ? " binary" : "");

    size_t joints = 0;
    for (const Spring& s : m.springs) joints += s.joints.size();
    std::printf("\n== 揺れもの ==\n");
    std::printf("  スプリング %zu 本(ジョイント %zu)/ コライダー %zu / コライダーグループ %zu\n", m.springs.size(), joints,
                m.colliders.size(), m.colliderGroups.size());
    for (size_t i = 0; i < m.springs.size() && i < 20; ++i) {
        const Spring& s = m.springs[i];
        std::printf("  %-20s joints=%zu root=%s stiff=%.2f grav=%.2f drag=%.2f\n", s.name.c_str(), s.joints.size(),
                    s.joints.empty() ? "" : m.nodes[s.joints[0].node].name.c_str(),
                    s.joints.empty() ? 0.f : s.joints[0].stiffness, s.joints.empty() ? 0.f : s.joints[0].gravityPower,
                    s.joints.empty() ? 0.f : s.joints[0].dragForce);
    }
    if (m.springs.size() > 20) std::printf("  ...(他 %zu 本)\n", m.springs.size() - 20);

    std::printf("\n== 一人称 ==\n");
    for (const auto& f : m.firstPerson) std::printf("  %-28s %s\n", m.nodes[f.node].name.c_str(), fpName(f.flag));

    if (!m.warnings.empty()) {
        std::printf("\n== 警告 ==\n");
        for (const auto& w : m.warnings) std::printf("  %s\n", w.c_str());
    }
}

SourcePose poseFromGlobals(const Model& m, const Pose& p) {
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

// 腕をワールドで下向きに deg 度回した姿勢
Pose armsDown(const Model& m, const Skeleton& sk, float deg) {
    Pose p = sk.restPose();
    const float rad = deg * 3.14159265f / 180.0f;
    for (HumanBone b : {HumanBone::leftUpperArm, HumanBone::rightUpperArm}) {
        int n = m.humanNode(b);
        int c = m.humanNode(b == HumanBone::leftUpperArm ? HumanBone::leftLowerArm : HumanBone::rightLowerArm);
        if (n < 0 || c < 0) continue;
        Vec3 dir = normalize(p.global[c].translation() - p.global[n].translation());
        Vec3 axis = cross(dir, {0, 0, -1});  // UE 空間: 下 = -Z
        if (length(axis) < 1e-4f) continue;
        Quat delta = Quat::fromAxisAngle(axis, rad);
        int parent = sk.parent(n);
        Quat parentRot = parent >= 0 ? p.global[parent].rotation() : Quat::identity();
        p.local[n].r = normalize(parentRot.inverse() * delta * p.global[n].rotation());
        sk.computeGlobals(p);
    }
    return p;
}

bool writeObj(const Avatar& a, const fs::path& objPath, bool textures, bool flipped) {
    const Model& m = a.model();
    const Mat4 back = m.basis.inverse();  // 変換後空間 → glTF 空間
    fs::path mtlPath = objPath;
    mtlPath.replace_extension(".mtl");
    std::ofstream obj(objPath), mtl(mtlPath);
    if (!obj || !mtl) return false;
    obj << "# vrm_inspect\nmtllib " << mtlPath.filename().u8string() << "\n";

    std::map<int, std::string> texFiles;
    if (textures) {
        for (size_t i = 0; i < m.images.size(); ++i) {
            const Image& im = m.images[i];
            if (im.bytes.empty()) continue;
            std::string ext = im.mimeType == "image/jpeg" ? ".jpg" : ".png";
            std::string name = objPath.stem().u8string() + "_tex" + std::to_string(i) + ext;
            std::ofstream f(objPath.parent_path() / fs::u8path(name), std::ios::binary);
            f.write(reinterpret_cast<const char*>(im.bytes.data()), static_cast<std::streamsize>(im.bytes.size()));
            texFiles[static_cast<int>(i)] = name;
        }
    }
    for (size_t i = 0; i < m.materials.size(); ++i) {
        const Material& ma = m.materials[i];
        mtl << "newmtl m" << i << "\nKd " << ma.baseColorFactor.x << " " << ma.baseColorFactor.y << " "
            << ma.baseColorFactor.z << "\n";
        if (ma.baseColorTexture >= 0 && ma.baseColorTexture < static_cast<int>(m.textures.size())) {
            auto it = texFiles.find(m.textures[ma.baseColorTexture].image);
            if (it != texFiles.end()) mtl << "map_Kd " << it->second << "\n";
        }
    }

    size_t base = 1;
    const auto& secs = a.sections();
    for (size_t s = 0; s < secs.size(); ++s) {
        const auto& out = a.skinned()[s];
        const Primitive& prim = m.meshes[secs[s].mesh].primitives[secs[s].primitive];
        obj << "o " << m.nodes[secs[s].node].name << "_" << secs[s].primitive << "\n";
        if (secs[s].material >= 0) obj << "usemtl m" << secs[s].material << "\n";
        for (const Vec3& v : out.positions) {
            Vec3 g = back.transformPoint(v);
            obj << "v " << g.x << " " << g.y << " " << g.z << "\n";
        }
        bool hasUv = prim.uv0.size() == out.positions.size();
        if (hasUv)
            for (const Vec2& t : prim.uv0) obj << "vt " << t.x << " " << 1.0f - t.y << "\n";
        for (const Vec3& n : out.normals) {
            Vec3 g = normalize(back.transformVector(n));
            obj << "vn " << g.x << " " << g.y << " " << g.z << "\n";
        }
        for (size_t i = 0; i + 2 < prim.indices.size(); i += 3) {
            uint32_t tri[3] = {prim.indices[i], prim.indices[i + 1], prim.indices[i + 2]};
            if (flipped) std::swap(tri[1], tri[2]);  // glTF 空間に戻すので巻き順も戻す
            obj << "f";
            for (uint32_t t : tri) {
                size_t k = base + t;
                if (hasUv) obj << " " << k << "/" << k << "/" << k;
                else obj << " " << k << "//" << k;
            }
            obj << "\n";
        }
        base += out.positions.size();
    }
    return true;
}

std::vector<std::string> utf8Args(int argc, char** argv) {
    std::vector<std::string> r;
#ifdef _WIN32
    (void)argc; (void)argv;
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 0; i < n; ++i) {
        int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(len > 0 ? len - 1 : 0, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w[i], -1, s.data(), len, nullptr, nullptr);
        r.push_back(s);
    }
    LocalFree(w);
#else
    for (int i = 0; i < argc; ++i) r.push_back(argv[i]);
#endif
    return r;
}

// --source-pose のファイルを読み、既定のボーン名表でヒューマノイドへ対応付ける
bool readSourcePose(const std::string& path, SourcePose& rest, SourcePose& cur) {
    std::ifstream f(fs::u8path(path));
    if (!f) return false;
    std::unordered_map<std::string, std::vector<float>> rows;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string name;
        ss >> name;
        std::vector<float> v(14);
        for (float& x : v) ss >> x;
        if (ss) rows[name] = v;
    }
    auto names = defaultMetaHumanBoneNames();
    for (int i = 0; i < kHumanBoneCount; ++i) {
        auto it = rows.find(names[i]);
        if (names[i].empty() || it == rows.end()) continue;
        const auto& v = it->second;
        rest[i] = {true, {v[0], v[1], v[2]}, normalize(Quat{v[3], v[4], v[5], v[6]})};
        cur[i] = {true, {v[7], v[8], v[9]}, normalize(Quat{v[10], v[11], v[12], v[13]})};
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    auto args = utf8Args(argc, argv);
    std::string input, objOut;
    float apose = 0;
    bool textures = false;
    int bench = 0;
    std::string sourcePose;
    bool useCur = false;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--obj" && i + 1 < args.size()) objOut = args[++i];
        else if (args[i] == "--apose" && i + 1 < args.size()) apose = std::stof(args[++i]);
        else if (args[i] == "--textures") textures = true;
        else if (args[i] == "--bench" && i + 1 < args.size()) bench = std::stoi(args[++i]);
        else if (args[i] == "--source-pose" && i + 1 < args.size()) sourcePose = args[++i];
        else if (args[i] == "--use-cur") useCur = true;
        else if (input.empty()) input = args[i];
    }
    if (input.empty()) {
        std::printf("使い方: vrm_inspect <file.vrm> [--obj out.obj] [--apose 度] [--textures]\n");
        return 2;
    }

    Avatar avatar;
    AvatarOptions opt;
    opt.enableSpringBones = false;
    std::string err;
    if (!avatar.loadFromFile(input, opt, &err)) {
        std::printf("読み込み失敗: %s\n", err.c_str());
        return 1;
    }
    printReport(avatar.model());

    if (!objOut.empty()) {
        const Model& m = avatar.model();
        SourcePose rest = poseFromGlobals(m, avatar.skeleton().restPose());
        SourcePose apply;
        if (!sourcePose.empty()) {
            SourcePose cur;
            if (!readSourcePose(sourcePose, rest, cur)) {
                std::printf("ソース姿勢を読めません: %s\n", sourcePose.c_str());
                return 1;
            }
            apply = useCur ? cur : rest;
        } else {
            Pose src = apose != 0 ? armsDown(m, avatar.skeleton(), apose) : avatar.skeleton().restPose();
            apply = poseFromGlobals(m, src);
        }
        if (!avatar.bindSource(rest, &err)) {
            std::printf("リターゲットの準備に失敗: %s\n", err.c_str());
            return 1;
        }
        avatar.update(apply, Mat4::identity(), 0);
        if (!writeObj(avatar, fs::u8path(objOut), textures, opt.convert.flipWinding)) {
            std::printf("OBJ を書き出せません: %s\n", objOut.c_str());
            return 1;
        }
        std::printf("\nOBJ を書き出しました: %s\n", objOut.c_str());
    }

    if (bench > 0) {
        Avatar b;
        AvatarOptions bo;
        auto t0 = std::chrono::steady_clock::now();
        if (!b.loadFromFile(input, bo, &err)) return 1;
        auto t1 = std::chrono::steady_clock::now();
        const Model& m = b.model();
        SourcePose rest = poseFromGlobals(m, b.skeleton().restPose());
        if (!b.bindSource(rest, &err)) return 1;
        SourcePose moved = poseFromGlobals(m, armsDown(m, b.skeleton(), 50));
        auto t2 = std::chrono::steady_clock::now();
        for (int i = 0; i < bench; ++i) b.update((i & 1) ? moved : rest, Mat4::identity(), 1.0f / 60);
        auto t3 = std::chrono::steady_clock::now();
        auto ms = [](auto a, auto c) { return std::chrono::duration<double, std::milli>(c - a).count(); };
        std::printf("\n== 計測 ==\n  読み込み+変換: %.1f ms\n  1 フレーム更新: %.3f ms(%d 回平均、揺れもの込み)\n",
                    ms(t0, t1), ms(t2, t3) / bench, bench);
        // 揺れものが発散していないか(全頂点が有限で、レストの包囲箱から大きく外れていない)
        size_t bad = 0, total = 0;
        float maxAbs = 0;
        for (const auto& s : b.skinned())
            for (const Vec3& v : s.positions) {
                ++total;
                if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) ++bad;
                else maxAbs = std::max({maxAbs, std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)});
            }
        std::printf("  頂点の健全性: 非有限 %zu / %zu、最大座標 %.1f cm\n", bad, total, maxAbs);
    }
    return 0;
}
