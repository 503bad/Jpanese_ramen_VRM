#include "vrm_character.hpp"

#include <cmath>
#include <map>
#include <optional>
#include <random>
#include <unordered_map>

#include <Unreal/UObjectGlobals.hpp>

#include "log.hpp"
#include "rvrm/firstperson.h"

namespace rvrm_mod {

using namespace RC::Unreal;

namespace {

// ERelativeTransformSpace / EAttachmentRule の値(UE の定義順)
constexpr uint8_t RTS_World = 0;
constexpr uint8_t RTS_Component = 2;
constexpr uint8_t SnapToTarget = 2;
// EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones
constexpr uint8_t AlwaysTickPoseAndRefreshBones = 0;

// 1x1 の白(テクスチャの無いマテリアル用)
const std::vector<uint8_t> kWhitePng = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00,
    0x00, 0x00, 0x0B, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0x0F, 0x04, 0x00, 0x09, 0xFB, 0x03,
    0xFD, 0xFB, 0x5E, 0x6B, 0x2B, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

UObject* findObject(const wchar_t* path) { return UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path); }

rvrm::Transform toTransform(const FTransformD& t) {
    rvrm::Transform x;
    x.t = {static_cast<float>(t.Translation.X), static_cast<float>(t.Translation.Y), static_cast<float>(t.Translation.Z)};
    x.r = rvrm::normalize(rvrm::Quat{static_cast<float>(t.Rotation.X), static_cast<float>(t.Rotation.Y),
                                     static_cast<float>(t.Rotation.Z), static_cast<float>(t.Rotation.W)});
    x.s = {static_cast<float>(t.Scale3D.X), static_cast<float>(t.Scale3D.Y), static_cast<float>(t.Scale3D.Z)};
    return x;
}

// 親 * 子(どちらもスケール 1 前提の合成)
rvrm::Transform compose(const rvrm::Transform& parent, const rvrm::Transform& local) {
    rvrm::Transform r;
    r.t = parent.t + parent.r.rotate({local.t.x * parent.s.x, local.t.y * parent.s.y, local.t.z * parent.s.z});
    r.r = rvrm::normalize(parent.r * local.r);
    r.s = {parent.s.x * local.s.x, parent.s.y * local.s.y, parent.s.z * local.s.z};
    return r;
}

bool callBool(UObject* obj, const wchar_t* fn, const wchar_t* param, bool value) {
    Call c(obj, fn);
    if (!c.ok()) return false;
    c.set(param, value);
    return c.invoke();
}

// bool の UPROPERTY(ビットフィールドを含む)を読む
bool readBool(UObject* obj, const wchar_t* name, bool def) {
    FProperty* p = obj ? obj->GetPropertyByNameInChain(name) : nullptr;
    auto* bp = p ? CastField<FBoolProperty>(p) : nullptr;
    return bp ? bp->GetPropertyValueInContainer(obj) : def;
}

// SceneComponent の AttachChildren を再帰的に集める
void collectDescendants(UObject* comp, std::vector<UObject*>& out, int depth = 0) {
    if (!comp || depth > 16) return;
    auto* arr = propertyPtr<FakeTArray>(comp, L"AttachChildren");
    if (!arr || !arr->Data || arr->Num <= 0 || arr->Num > 4096) return;
    auto* items = static_cast<UObject* const*>(arr->Data);
    for (int32_t i = 0; i < arr->Num; ++i) {
        UObject* c = items[i];
        if (!c) continue;
        out.push_back(c);
        collectDescendants(c, out, depth + 1);
    }
}

}  // namespace

VrmCharacter::VrmCharacter(UObject* owner, std::unique_ptr<rvrm::Avatar> avatar, const Settings& settings,
                           std::wstring meshProperty, bool firstPerson, const MaterialFinder* materials,
                           CustomMaterials* custom)
    : avatar_(std::move(avatar)),
      settings_(settings),
      meshProperty_(std::move(meshProperty)),
      useFirstPerson_(firstPerson),
      materials_(materials),
      custom_(custom) {
    pawn_ = owner;
}

VrmCharacter::~VrmCharacter() = default;

void VrmCharacter::fail(const std::wstring& why) {
    logError(L"このキャラクターへの VRM 適用を中止します: " + why);
    shutdown();
    failed_ = true;
    state_ = State::Failed;
}

bool VrmCharacter::tick(float dt) {
    if (state_ == State::Failed) return false;
    UObject* pawn = pawn_.Get();
    if (!pawn) return false;

    if (state_ == State::WaitingForMesh) {
        waitTime_ += dt;
        if (waitTime_ < 0.5f) return true;  // スポーン直後は少し待つ
        if (!bind()) {
            if (failed_) return false;
            if (waitTime_ > 60.0f) {
                fail(L"キャラクターの骨格が準備できませんでした(60 秒)");
                return false;
            }
            return true;
        }
        state_ = State::Running;
    }

    UObject* mesh = mesh_.Get();
    UObject* pm = procMesh_.Get();
    if (!mesh || !pm) return false;  // 作り直し(呼び出し側が新しく作る)

    rvrm::SourcePose cur;
    rvrm::Mat4 toWorld;
    if (!readCurrentPose(cur, toWorld)) {
        fail(L"骨の姿勢を読めません");
        return false;
    }
    updateBlink(dt);
    avatar_->update(cur, toWorld, dt);

    if (useFirstPerson_ && settings_.hideHeadInFirstPerson) {
        bool fp = detectFirstPerson();
        if (fp != firstPerson_) {
            firstPerson_ = fp;
            for (size_t i = 0; i < fpFlags_.size(); ++i)
                if (fpFlags_[i] != rvrm::FirstPersonFlag::Both) createSection(i, firstPerson_);
        }
    }
    updateSections();
    if (!boundsLogged_ && !avatar_->skinned().empty()) {
        boundsLogged_ = true;
        rvrm::Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
        for (const auto& s : avatar_->skinned())
            for (const auto& v : s.positions)
                for (int k = 0; k < 3; ++k) {
                    lo[k] = std::min(lo[k], v[k]);
                    hi[k] = std::max(hi[k], v[k]);
                }
        logInfo(L"[診断] VRM の頂点の範囲(" + meshProperty_ + L"): 高さ " + std::to_wstring(static_cast<int>(lo.z)) + L"〜" +
                std::to_wstring(static_cast<int>(hi.z)) + L"cm 幅 " + std::to_wstring(static_cast<int>(hi.x - lo.x)) + L"cm");
    }
    // プレビューのカメラが毎フレーム撮影しない設定なら、こちらで撮り直す(揺れもの・まばたきを反映)
    if (UObject* capture = capture_.Get()) {
        // ゲームは撮影の直前に「写す対象」を作り直すことがあるので、撮る直前に毎回登録し直す
        UObject* pm2 = procMesh_.Get();
        bool present = false;
        if (auto* list = propertyPtr<FakeTArray>(capture, L"ShowOnlyComponents"); list && list->Data && list->Num > 0 && list->Num < 4096) {
            // TArray<TWeakObjectPtr<UPrimitiveComponent>>: 要素は FWeakObjectPtr(8 バイト)
            const auto* weaks = static_cast<const RC::Unreal::FWeakObjectPtr*>(list->Data);
            for (int32_t i = 0; i < list->Num && !present; ++i) present = weaks[i].Get() == pm2;
        }
        if (!present && pm2) {
            Call(capture, L"ShowOnlyComponent").set(L"InComponent", pm2).invoke();
            ++captureReadds_;
        }
        bool everyFrame = false;
        if (FProperty* p = capture->GetPropertyByNameInChain(L"bCaptureEveryFrame"))
            if (auto* bp = CastField<FBoolProperty>(p)) everyFrame = bp->GetPropertyValueInContainer(capture);
        const bool invoked = !everyFrame && Call(capture, L"CaptureScene").invoke();
        captureLogTimer_ -= dt;
        if (captureLogCount_ < 5 && captureLogTimer_ <= 0) {
            captureLogTimer_ = 1.0f;
            ++captureLogCount_;
            Call vis(capture, L"IsVisible");
            bool visible = false;
            vis.invoke() && vis.result(visible);
            Call act(capture, L"IsActive");
            bool active = false;
            act.invoke() && act.result(active);
            auto* list = propertyPtr<FakeTArray>(capture, L"ShowOnlyComponents");
            logInfo(L"[診断] プレビュー撮影: 対象の数=" + std::to_wstring(list ? list->Num : -1) + L" VRMを含む=" +
                    std::to_wstring(present ? 1 : 0) + L" 登録し直した回数=" + std::to_wstring(captureReadds_) +
                    L" カメラ可視=" + std::to_wstring(visible ? 1 : 0) + L" 有効=" + std::to_wstring(active ? 1 : 0) +
                    L" 撮影を呼んだ=" + std::to_wstring(invoked ? 1 : 0));
        }
    }

    if (--hideCheckCountdown_ <= 0) {
        hideOriginal(true);  // 着替え等で新しく付いた部品も隠す
        // プレビューのカメラの対象一覧はゲーム側で作り直されることがあるので、登録し直す(重複は無視される)
        if (UObject* capture = capture_.Get())
            if (UObject* pm2 = procMesh_.Get()) Call(capture, L"ShowOnlyComponent").set(L"InComponent", pm2).invoke();
        // プレビューはゲーム側が定期的に見た目を同期し直すので、こまめに隠し直す
        hideCheckCountdown_ = useFirstPerson_ ? 60 : 5;
    }
    return true;
}

bool VrmCharacter::bind() {
    UObject* pawn = pawn_.Get();
    UObject* mesh = objectProperty(pawn, meshProperty_.c_str());
    if (!mesh) return false;
    int32_t numBones = 0;
    {
        Call c(mesh, L"GetNumBones");
        if (!c.invoke() || !c.result(numBones)) {
            fail(L"GetNumBones を呼べません: " + c.error());
            return false;
        }
    }
    if (numBones <= 0) return false;  // メッシュがまだ読み込まれていない
    mesh_ = mesh;

    rvrm::SourcePose rest;
    if (!readRestPose(rest)) return false;
    restPose_ = rest;
    std::string err;
    if (!avatar_->bindSource(rest, &err)) {
        fail(L"リターゲットを準備できません: " + widen(err));
        return false;
    }
    if (!createMeshComponent() || !createSections() || !createMaterials()) return false;

    hideOriginal(true);
    hideCheckCountdown_ = 60;

    const auto& model = avatar_->model();
    logInfo(L"VRM を適用しました: " + widen(model.meta.name) + L"(ボーン " + std::to_wstring(boneNames_.size()) +
            L" 本、セクション " + std::to_wstring(avatar_->sections().size()) + L")");
    return true;
}

bool VrmCharacter::readRestPose(rvrm::SourcePose& rest) {
    UObject* mesh = mesh_.Get();
    std::unordered_map<std::wstring, std::optional<rvrm::Transform>> cache;

    // ボーン名 → コンポーネント空間のレスト姿勢(GetRefPoseTransform は親ボーン空間なので親をたどって合成)
    std::function<std::optional<rvrm::Transform>(const std::wstring&, int)> compRest =
        [&](const std::wstring& name, int depth) -> std::optional<rvrm::Transform> {
        if (depth > 64) return std::nullopt;
        auto it = cache.find(name);
        if (it != cache.end()) return it->second;
        FName fname(name.c_str(), FNAME_Add);
        int32_t index = -1;
        Call bi(mesh, L"GetBoneIndex");
        bi.set(L"BoneName", fname);
        if (!bi.invoke() || !bi.result(index) || index < 0) return cache[name] = std::nullopt;
        Call rp(mesh, L"GetRefPoseTransform");
        rp.set(L"BoneIndex", index);
        FTransformD local;
        if (!rp.invoke() || !rp.result(local)) return cache[name] = std::nullopt;
        Call pb(mesh, L"GetParentBone");
        pb.set(L"BoneName", fname);
        FName parent;
        std::wstring parentName;
        if (pb.invoke() && pb.result(parent)) parentName = parent.ToString();
        rvrm::Transform l = toTransform(local);
        if (parentName.empty() || parentName == L"None") return cache[name] = l;
        auto p = compRest(parentName, depth + 1);
        if (!p) return cache[name] = std::nullopt;
        return cache[name] = compose(*p, l);
    };

    boneNames_.clear();
    auto names = rvrm::defaultMetaHumanBoneNames();
    for (int h = 0; h < rvrm::kHumanBoneCount; ++h) {
        if (names[h].empty()) continue;
        std::wstring wname = widen(names[h]);
        auto t = compRest(wname, 0);
        if (!t) continue;
        rest[h] = {true, t->t, t->r};
        boneNames_.emplace_back(h, FName(wname.c_str(), FNAME_Add));
    }
    if (!rest[rvrm::idx(rvrm::HumanBone::hips)].valid) {
        fail(L"ゲームの骨格に pelvis が見つかりません(ゲームの更新で骨格が変わった可能性があります)");
        return false;
    }
    return true;
}

bool VrmCharacter::readCurrentPose(rvrm::SourcePose& cur, rvrm::Mat4& toWorld) {
    UObject* mesh = mesh_.Get();
    Call c(mesh, L"GetSocketTransform");
    if (!c.ok()) return false;
    for (const auto& [h, fname] : boneNames_) {
        c.set(L"InSocketName", fname).set(L"TransformSpace", RTS_Component);
        FTransformD t;
        if (!c.invoke() || !c.result(t)) return false;
        rvrm::Transform x = toTransform(t);
        cur[h] = {true, x.t, x.r};
    }
    Call w(mesh, L"K2_GetComponentToWorld");
    FTransformD t;
    if (!w.invoke() || !w.result(t)) return false;
    toWorld = rvrm::Mat4::fromTRS(toTransform(t));

    // 骨の姿勢が計算されていない(全部の骨が 1 点に潰れている)ときは、基本姿勢で表示する。
    // ロッカーのプレビュー用キャラは、アニメーションの計算のされ方が通常のキャラと違うことがある
    const auto& hips = cur[rvrm::idx(rvrm::HumanBone::hips)];
    const auto& head = cur[rvrm::idx(rvrm::HumanBone::head)];
    const float span = (hips.valid && head.valid) ? rvrm::length(head.position - hips.position) : 0.0f;
    const bool degenerate = !(span > 20.0f);  // NaN も弾く
    if (degenerate) cur = restPose_;
    if (!poseLogged_) {
        poseLogged_ = true;
        logInfo(L"[診断] 骨の姿勢(" + meshProperty_ + L"): 腰=" + std::to_wstring(static_cast<int>(hips.position.z)) +
                L"cm 頭=" + std::to_wstring(static_cast<int>(head.position.z)) + L"cm 腰〜頭=" + std::to_wstring(static_cast<int>(span)) +
                L"cm" + (degenerate ? L" → 潰れているので基本姿勢で表示" : L""));
    }
    return true;
}

bool VrmCharacter::createMeshComponent() {
    UObject* pawn = pawn_.Get();
    UObject* mesh = mesh_.Get();
    UObject* cls = findObject(L"/Script/ProceduralMeshComponent.ProceduralMeshComponent");
    if (!cls) {
        fail(L"ProceduralMeshComponent クラスが見つかりません");
        return false;
    }
    FTransformD identity;
    Call add(pawn, L"AddComponentByClass");
    add.set(L"Class", cls).set(L"bManualAttachment", true).set(L"RelativeTransform", identity).set(L"bDeferredFinish", false);
    UObject* comp = nullptr;
    if (!add.invoke() || !add.result(comp) || !comp) {
        fail(L"メッシュを追加できません: " + add.error());
        return false;
    }
    procMesh_ = comp;
    Call att(comp, L"K2_AttachToComponent");
    att.set(L"Parent", mesh)
        .set(L"LocationRule", SnapToTarget)
        .set(L"RotationRule", SnapToTarget)
        .set(L"ScaleRule", SnapToTarget)
        .set(L"bWeldSimulatedBodies", false);
    if (!att.invoke()) {
        fail(L"メッシュを取り付けられません: " + att.error());
        return false;
    }
    // 元のメッシュと同じライティングチャンネルにする(プレビューは専用のチャンネルの照明で照らしていることがある)
    if (auto* ch = propertyPtr<uint8_t>(mesh, L"LightingChannels")) {
        Call lc(comp, L"SetLightingChannels");
        lc.set(L"bChannel0", (*ch & 1) != 0).set(L"bChannel1", (*ch & 2) != 0).set(L"bChannel2", (*ch & 4) != 0).invoke();
    }
    // カスタム深度・ステンシルも元のメッシュに合わせる(キャラだけを切り抜く処理などで使われる)
    {
        FProperty* p = mesh->GetPropertyByNameInChain(L"bRenderCustomDepth");
        auto* bp = p ? CastField<FBoolProperty>(p) : nullptr;
        const bool customDepth = bp && bp->GetPropertyValueInContainer(mesh);
        if (customDepth) {
            Call(comp, L"SetRenderCustomDepth").set(L"bValue", true).invoke();
            if (auto* v = propertyPtr<int32_t>(mesh, L"CustomDepthStencilValue"))
                Call(comp, L"SetCustomDepthStencilValue").set(L"Value", *v).invoke();
            if (auto* m = propertyPtr<uint8_t>(mesh, L"CustomDepthStencilWriteMask"))
                Call(comp, L"SetCustomDepthStencilWriteMask").set(L"WriteMaskBit", *m).invoke();
        }
    }
    // ロッカーのプレビューは専用のカメラ(SceneCapture)で撮っていて、写す対象が限定されている。
    // 足したメッシュも写るよう登録する
    if (UObject* capture = objectProperty(pawn, L"SceneCaptureComponent")) {
        Call so(capture, L"ShowOnlyComponent");
        so.set(L"InComponent", comp);
        auto* list = propertyPtr<FakeTArray>(capture, L"ShowOnlyComponents");
        const int before = list ? list->Num : -1;
        if (so.invoke())
            logInfo(L"プレビューのカメラに VRM を登録しました(対象 " + std::to_wstring(before) + L" → " +
                    std::to_wstring(list ? list->Num : -1) + L")");
        else logWarn(L"プレビューのカメラに登録できません: " + so.error());
        capture_ = capture;
        logPreviewDiagnostics(capture, comp);
    }
    return true;
}

bool VrmCharacter::createSections() {
    const auto& model = avatar_->model();
    const auto& secs = avatar_->sections();
    auto headNodes = rvrm::headSubtreeNodes(model);
    fpFlags_.assign(secs.size(), rvrm::FirstPersonFlag::Both);
    fullIndices_.assign(secs.size(), {});
    fpIndices_.assign(secs.size(), {});
    uvs_.assign(secs.size(), {});
    for (size_t i = 0; i < secs.size(); ++i) {
        const auto& prim = model.meshes[secs[i].mesh].primitives[secs[i].primitive];
        fpFlags_[i] = rvrm::sectionFirstPersonFlag(model, secs[i]);
        fullIndices_[i].assign(prim.indices.begin(), prim.indices.end());
        if (fpFlags_[i] == rvrm::FirstPersonFlag::Auto) {
            auto fi = rvrm::firstPersonIndices(model, secs[i], headNodes);
            fpIndices_[i].assign(fi.begin(), fi.end());
        }
        // VRM で両面のマテリアル(髪・スカートなど)を片面のマテリアルで表示するときは、
        // 裏向きの三角形を足して裏側も見えるようにする(頂点は共有するので法線は表のまま)
        const bool hasMat = secs[i].material >= 0 && secs[i].material < static_cast<int>(model.materials.size());
        const bool doubleSided = settings_.twoSidedAll || (hasMat && model.materials[secs[i].material].doubleSided);
        const rvrm::AlphaMode alpha = hasMat ? model.materials[secs[i].material].alphaMode : rvrm::AlphaMode::Opaque;
        // 専用マテリアルは両面なので不要
        const bool customOk = custom_ && custom_->get(settings_.shadingLit, alpha);
        const DisplayMaterial* dm = materials_ ? materials_->pick(alpha) : nullptr;
        if (doubleSided && !customOk && dm && !dm->twoSided) {
            for (auto* list : {&fullIndices_[i], &fpIndices_[i]}) {
                const size_t n = list->size();
                list->reserve(n * 2);
                for (size_t t = 0; t + 2 < n; t += 3) {
                    const int32_t a = (*list)[t], b = (*list)[t + 1], c = (*list)[t + 2];
                    list->push_back(a);
                    list->push_back(c);
                    list->push_back(b);
                }
            }
        }
        uvs_[i].resize(prim.positions.size());
        for (size_t v = 0; v < prim.uv0.size() && v < uvs_[i].size(); ++v) uvs_[i][v] = {prim.uv0[v].x, prim.uv0[v].y};
        if (!createSection(i, false)) return false;
    }
    return true;
}

bool VrmCharacter::createSection(size_t i, bool firstPerson) {
    UObject* pm = procMesh_.Get();
    const auto& out = avatar_->skinned()[i];
    static const std::vector<int32_t> kEmpty;
    const std::vector<int32_t>* idx = &fullIndices_[i];
    switch (fpFlags_[i]) {
        case rvrm::FirstPersonFlag::ThirdPersonOnly: if (firstPerson) idx = &kEmpty; break;
        case rvrm::FirstPersonFlag::FirstPersonOnly: if (!firstPerson) idx = &kEmpty; break;
        case rvrm::FirstPersonFlag::Auto: if (firstPerson) idx = &fpIndices_[i]; break;
        default: break;
    }
    vtx_.resize(out.positions.size());
    nrm_.resize(out.normals.size());
    for (size_t v = 0; v < vtx_.size(); ++v) vtx_[v] = {out.positions[v].x, out.positions[v].y, out.positions[v].z};
    for (size_t v = 0; v < nrm_.size(); ++v) nrm_[v] = {out.normals[v].x, out.normals[v].y, out.normals[v].z};
    Call c(pm, L"CreateMeshSection_LinearColor");
    c.set(L"SectionIndex", static_cast<int32_t>(i))
        .setArray(L"Vertices", vtx_)
        .setArray(L"Triangles", *idx)
        .setArray(L"Normals", nrm_)
        .setArray(L"UV0", uvs_[i])
        .set(L"bCreateCollision", false);
    if (!c.invoke()) {
        fail(L"メッシュのセクションを作れません: " + c.error());
        return false;
    }
    return true;
}

void VrmCharacter::updateSections() {
    UObject* pm = procMesh_.Get();
    const auto& skinned = avatar_->skinned();
    for (size_t i = 0; i < skinned.size(); ++i) {
        const auto& out = skinned[i];
        vtx_.resize(out.positions.size());
        nrm_.resize(out.normals.size());
        for (size_t v = 0; v < vtx_.size(); ++v) vtx_[v] = {out.positions[v].x, out.positions[v].y, out.positions[v].z};
        for (size_t v = 0; v < nrm_.size(); ++v) nrm_[v] = {out.normals[v].x, out.normals[v].y, out.normals[v].z};
        Call c(pm, L"UpdateMeshSection_LinearColor");
        c.set(L"SectionIndex", static_cast<int32_t>(i)).setArray(L"Vertices", vtx_).setArray(L"Normals", nrm_);
        c.invoke();
    }
}

bool VrmCharacter::createMaterials() {
    UObject* pawn = pawn_.Get();
    UObject* pm = procMesh_.Get();
    UObject* materialLib = findObject(L"/Script/Engine.Default__KismetMaterialLibrary");
    UObject* renderLib = findObject(L"/Script/Engine.Default__KismetRenderingLibrary");
    if (!materialLib || !renderLib) {
        fail(L"マテリアル/テクスチャ作成用のライブラリが見つかりません");
        return false;
    }
    const auto& model = avatar_->model();
    std::map<int, UObject*> textures;  // 画像番号 → UTexture2D(-1 = 白)
    auto textureFor = [&](int image) -> UObject* {
        auto it = textures.find(image);
        if (it != textures.end()) return it->second;
        const std::vector<uint8_t>* bytes = &kWhitePng;
        if (image >= 0 && image < static_cast<int>(model.images.size()) && !model.images[image].bytes.empty())
            bytes = &model.images[image].bytes;
        Call imp(renderLib, L"ImportBufferAsTexture2D");
        imp.set(L"WorldContextObject", pawn).setBytes(L"Buffer", *bytes);
        UObject* tex = nullptr;
        if (!imp.invoke() || !imp.result(tex) || !tex) {
            logWarn(L"テクスチャを読み込めません(画像 " + std::to_wstring(image) + L"): " + imp.error());
            tex = nullptr;
        }
        return textures[image] = tex;
    };

    std::map<int, UObject*> mids;  // VRM マテリアル番号 → MID
    const FName slateUI(L"SlateUI", FNAME_Add), tint(L"TintColorAndOpacity", FNAME_Add), opacity(L"OpacityFromTexture", FNAME_Add);
    const auto& secs = avatar_->sections();
    for (size_t i = 0; i < secs.size(); ++i) {
        int mi = secs[i].material;
        UObject* mid = nullptr;
        if (auto it = mids.find(mi); it != mids.end()) {
            mid = it->second;
        } else {
            rvrm::Material mat;
            if (mi >= 0 && mi < static_cast<int>(model.materials.size())) mat = model.materials[mi];
            int image = -1;
            if (mat.baseColorTexture >= 0 && mat.baseColorTexture < static_cast<int>(model.textures.size()))
                image = model.textures[mat.baseColorTexture].image;

            // 1. MOD に同梱した専用マテリアル(VRM のアルファモードどおりに使える)
            UObject* custom = custom_ ? custom_->get(settings_.shadingLit, mat.alphaMode) : nullptr;
            // 2. ライティングの効くゲーム内のマテリアル(material_finder が探したもの)
            const DisplayMaterial* lit = (!custom && materials_) ? materials_->pick(mat.alphaMode) : nullptr;
            UObject* parent = custom ? custom : (lit ? lit->material.Get() : nullptr);
            // 3. 見つからなければエンジンの UI 用マテリアル(アンリット。明るい場所では暗く見える)
            std::wstring kind = mat.alphaMode == rvrm::AlphaMode::Blend  ? L"Translucent"
                              : mat.alphaMode == rvrm::AlphaMode::Mask ? L"Masked" : L"Opaque";
            if (!parent) {
                bool oneSided = !(settings_.twoSidedAll || mat.doubleSided);
                std::wstring name = L"Widget3DPassThrough_" + kind + (oneSided ? L"_OneSided" : L"");
                parent = findObject((L"/Engine/EngineMaterials/" + name + L"." + name).c_str());
                if (!parent) parent = findObject(L"/Engine/EngineMaterials/Widget3DPassThrough.Widget3DPassThrough");
            }
            if (!parent) {
                fail(L"表示用のマテリアルが見つかりません");
                return false;
            }
            Call cm(materialLib, L"CreateDynamicMaterialInstance");
            cm.set(L"WorldContextObject", pawn).set(L"Parent", parent);
            if (!cm.invoke() || !cm.result(mid) || !mid) {
                fail(L"マテリアルを作れません: " + cm.error());
                return false;
            }
            UObject* tex = textureFor(image);
            if (custom) {
                if (tex) {
                    Call st(mid, L"SetTextureParameterValue");
                    st.set(L"ParameterName", FName(L"BaseColorTexture", FNAME_Add)).set(L"Value", tex).invoke();
                }
                Call sv(mid, L"SetVectorParameterValue");
                FLinearColorF c{mat.baseColorFactor.x, mat.baseColorFactor.y, mat.baseColorFactor.z, mat.baseColorFactor.w};
                sv.set(L"ParameterName", FName(L"BaseColorFactor", FNAME_Add)).set(L"Value", c).invoke();
                Call ss(mid, L"SetScalarParameterValue");
                ss.set(L"ParameterName", FName(L"AlphaCutoff", FNAME_Add)).set(L"Value", mat.alphaCutoff).invoke();
                // プレビュー用カメラでは自動露出の値が無いことがあるので使わない
                Call ea(mid, L"SetScalarParameterValue");
                ea.set(L"ParameterName", FName(L"UseEyeAdaptation", FNAME_Add)).set(L"Value", meshProperty_ == L"PreviewMeshComponent" ? 0.0f : 1.0f).invoke();
            } else if (lit) {
                if (tex) {
                    Call st(mid, L"SetTextureParameterValue");
                    st.set(L"ParameterName", FName(lit->textureParam.c_str(), FNAME_Add)).set(L"Value", tex).invoke();
                }
                // ノーマルマップや汚れなど、元のマテリアルの余計な入力を打ち消す
                materials_->neutralize(mid, *lit);
            } else {
                if (tex) {
                    Call st(mid, L"SetTextureParameterValue");
                    st.set(L"ParameterName", slateUI).set(L"Value", tex).invoke();
                }
                Call sv(mid, L"SetVectorParameterValue");
                FLinearColorF c{mat.baseColorFactor.x, mat.baseColorFactor.y, mat.baseColorFactor.z, mat.baseColorFactor.w};
                sv.set(L"ParameterName", tint).set(L"Value", c).invoke();
                Call ss(mid, L"SetScalarParameterValue");
                ss.set(L"ParameterName", opacity).set(L"Value", 1.0f).invoke();
            }
            if (mids.empty())
                logInfo(L"使用するマテリアル: " +
                        (custom ? std::wstring(L"専用マテリアル(") + (settings_.shadingLit ? L"ライティングあり" : L"アニメ調") + L")"
                         : lit  ? lit->path + L" [" + lit->textureParam + L"]"
                                : std::wstring(L"Widget3DPassThrough(アンリット)")));
            mids[mi] = mid;
        }
        Call sm(pm, L"SetMaterial");
        sm.set(L"ElementIndex", static_cast<int32_t>(i)).set(L"Material", mid);
        if (!sm.invoke()) {
            fail(L"マテリアルを設定できません: " + sm.error());
            return false;
        }
    }
    return true;
}

// ロッカーのプレビューに写らない原因を調べるための情報(1 回だけ)
void VrmCharacter::logPreviewDiagnostics(UObject* capture, UObject* procMesh) {
    auto byteProp = [](UObject* o, const wchar_t* n) -> int {
        auto* p = propertyPtr<uint8_t>(o, n);
        return p ? *p : -1;
    };
    auto boolProp = [](UObject* o, const wchar_t* n) -> int {
        FProperty* p = o ? o->GetPropertyByNameInChain(n) : nullptr;
        auto* bp = p ? CastField<FBoolProperty>(p) : nullptr;
        return bp ? (bp->GetPropertyValueInContainer(o) ? 1 : 0) : -1;
    };
    auto arrayNum = [](UObject* o, const wchar_t* n) -> int {
        auto* a = propertyPtr<FakeTArray>(o, n);
        return a ? a->Num : -1;
    };
    auto location = [](UObject* o) -> std::wstring {
        Call c(o, L"K2_GetComponentLocation");
        FVectorD v;
        if (!c.invoke() || !c.result(v)) return L"?";
        return std::to_wstring(static_cast<int>(v.X)) + L"," + std::to_wstring(static_cast<int>(v.Y)) + L"," +
               std::to_wstring(static_cast<int>(v.Z));
    };
    UObject* mesh = mesh_.Get();
    logInfo(L"[診断] プレビューのカメラ: PrimitiveRenderMode=" + std::to_wstring(byteProp(capture, L"PrimitiveRenderMode")) +
            L" ShowOnlyComponents=" + std::to_wstring(arrayNum(capture, L"ShowOnlyComponents")) +
            L" ShowOnlyActors=" + std::to_wstring(arrayNum(capture, L"ShowOnlyActors")) +
            L" HiddenComponents=" + std::to_wstring(arrayNum(capture, L"HiddenComponents")) +
            L" CaptureEveryFrame=" + std::to_wstring(boolProp(capture, L"bCaptureEveryFrame")) +
            L" CaptureOnMovement=" + std::to_wstring(boolProp(capture, L"bCaptureOnMovement")) +
            L" 位置=" + location(capture));
    logInfo(L"[診断] プレビューのキャラ: 位置=" + location(mesh) + L" 可視=" + std::to_wstring(boolProp(mesh, L"bVisible")) +
            L" / VRM: 位置=" + location(procMesh) + L" 可視=" + std::to_wstring(boolProp(procMesh, L"bVisible")) +
            L" 親=" + (objectProperty(procMesh, L"AttachParent") == mesh ? std::wstring(L"プレビューのキャラ") : std::wstring(L"別")) +
            L" ライティングチャンネル=" + std::to_wstring(byteProp(mesh, L"LightingChannels")) + L"/" +
            std::to_wstring(byteProp(procMesh, L"LightingChannels")) +
            L" キャプチャ専用表示=" + std::to_wstring(boolProp(mesh, L"bVisibleInSceneCaptureOnly")) +
            L" カスタム深度=" + std::to_wstring(boolProp(mesh, L"bRenderCustomDepth")) + L"/" +
            std::to_wstring(boolProp(procMesh, L"bRenderCustomDepth")) + L" ステンシル=" +
            std::to_wstring(propertyPtr<int32_t>(mesh, L"CustomDepthStencilValue") ? *propertyPtr<int32_t>(mesh, L"CustomDepthStencilValue") : -1) +
            L" 撮影元=" + std::to_wstring(byteProp(capture, L"CaptureSource")));
    // カメラの表示フラグ(例: StaticMeshes=0 なら静的メッシュを描かない)
    if (auto* flags = propertyPtr<FakeTArray>(capture, L"ShowFlagSettings"); flags && flags->Data && flags->Num > 0 && flags->Num < 256) {
        std::wstring list;
        for (int32_t i = 0; i < flags->Num; ++i) {
            const auto* e = static_cast<const uint8_t*>(flags->Data) + static_cast<size_t>(i) * 24;  // FString(16) + bool(+pad)
            const auto* name = reinterpret_cast<const FakeTArray*>(e);
            std::wstring n = (name->Data && name->Num > 1 && name->Num < 128)
                                 ? std::wstring(static_cast<const wchar_t*>(name->Data), static_cast<size_t>(name->Num - 1))
                                 : L"?";
            list += n + L"=" + (e[16] ? L"1" : L"0") + L" ";
        }
        logInfo(L"[診断] プレビューのカメラの表示フラグ: " + list);
    } else {
        logInfo(L"[診断] プレビューのカメラの表示フラグ: なし");
    }
}

void VrmCharacter::hideOriginal(bool hide) {
    auto setFlags = [](UObject* c, bool mainPass, bool depthPass, bool castShadow) {
        bool ok = callBool(c, L"SetRenderInMainPass", L"bValue", mainPass);
        callBool(c, L"SetRenderInDepthPass", L"bValue", depthPass);
        if (!callBool(c, L"SetCastShadow", L"NewCastShadow", castShadow)) callBool(c, L"SetCastShadow", L"bInCastShadow", castShadow);
        return ok;
    };

    if (!hide) {
        for (auto& h : hidden_)
            if (UObject* o = h.object.Get()) setFlags(o, h.mainPass, h.depthPass, h.castShadow);
        hidden_.clear();
        if (UObject* mesh = mesh_.Get(); mesh && originalTickOption_ >= 0)
            if (auto* opt = propertyPtr<uint8_t>(mesh, L"VisibilityBasedAnimTickOption")) *opt = static_cast<uint8_t>(originalTickOption_);
        originalTickOption_ = -1;
        return;
    }

    UObject* mesh = mesh_.Get();
    UObject* pm = procMesh_.Get();
    if (!mesh) return;
    // 描画されなくても骨の計算を止めない
    if (auto* opt = propertyPtr<uint8_t>(mesh, L"VisibilityBasedAnimTickOption")) {
        if (originalTickOption_ < 0) originalTickOption_ = *opt;
        *opt = AlwaysTickPoseAndRefreshBones;
    }
    std::vector<UObject*> comps{mesh};
    collectDescendants(mesh, comps);
    UObject* owner = pawn_.Get();
    for (UObject* c : comps) {
        if (c == pm) continue;
        // 手に持った道具など、他のアクターの部品も体に取り付けられる。それらは隠さない
        UObject* compOwner = nullptr;
        Call go(c, L"GetOwner");
        if (!go.invoke() || !go.result(compOwner) || compOwner != owner) continue;
        bool known = false;
        for (auto& h : hidden_)
            if (h.object.Get() == c) known = true;
        if (known) {
            setFlags(c, false, false, false);  // ゲーム側が戻した場合に備えて毎回止め直す
            continue;
        }
        // 元の値を覚えてから止める。表示状態(Visibility)はゲームの管理に任せる
        Hidden h;
        h.mainPass = readBool(c, L"bRenderInMainPass", true);
        h.depthPass = readBool(c, L"bRenderInDepthPass", true);
        h.castShadow = readBool(c, L"CastShadow", true);
        if (setFlags(c, false, false, false)) {
            h.object = c;
            hidden_.push_back(h);
        }
    }
}

bool VrmCharacter::detectFirstPerson() {
    UObject* pawn = pawn_.Get();
    UObject* mesh = mesh_.Get();
    UObject* ctrl = objectProperty(pawn, L"Controller");
    UObject* cam = objectProperty(ctrl, L"PlayerCameraManager");
    if (!cam || !mesh) return false;
    Call cl(cam, L"GetCameraLocation");
    FVectorD camPos;
    if (!cl.invoke() || !cl.result(camPos)) return false;
    Call hs(mesh, L"GetSocketTransform");
    hs.set(L"InSocketName", FName(L"Head", FNAME_Add)).set(L"TransformSpace", RTS_World);
    FTransformD head;
    if (!hs.invoke() || !hs.result(head)) return false;
    double dx = camPos.X - head.Translation.X, dy = camPos.Y - head.Translation.Y, dz = camPos.Z - head.Translation.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz) < settings_.firstPersonDistance;
}

void VrmCharacter::updateBlink(float dt) {
    if (!settings_.autoBlink) return;
    static std::mt19937 rng{std::random_device{}()};
    constexpr float kClose = 0.06f, kOpen = 0.10f;
    if (blinkPhase_ < 0) {
        blinkTimer_ -= dt;
        if (blinkTimer_ > 0) return;
        blinkPhase_ = 0;
    }
    blinkPhase_ += dt;
    float w = blinkPhase_ < kClose ? blinkPhase_ / kClose : 1.0f - (blinkPhase_ - kClose) / kOpen;
    if (blinkPhase_ >= kClose + kOpen) {
        w = 0;
        blinkPhase_ = -1;
        blinkTimer_ = std::uniform_real_distribution<float>(2.0f, 6.0f)(rng);
    }
    avatar_->setExpression("blink", std::clamp(w, 0.0f, 1.0f));
}

void VrmCharacter::shutdown() {
    hideOriginal(false);
    if (UObject* pm = procMesh_.Get()) {
        Call d(pm, L"K2_DestroyComponent");
        d.set(L"Object", pm).invoke();
    }
    procMesh_ = static_cast<UObject*>(nullptr);
}

}  // namespace rvrm_mod
