#include "material_finder.hpp"

#include <algorithm>
#include <cwctype>
#include <map>
#include <set>

#include <Unreal/UObjectGlobals.hpp>

#include "log.hpp"

namespace rvrm_mod {

using namespace RC::Unreal;

namespace {

// ベースカラーのテクスチャパラメータとしてよく使われる名前(先頭ほど優先)
const wchar_t* const kTextureParamNames[] = {
    L"BaseColor", L"Base Color", L"BaseColorTexture", L"BaseColor Texture", L"BaseColorMap", L"Base Color Map",
    L"Base_Color", L"T_BaseColor", L"Albedo", L"AlbedoTexture", L"Albedo Texture", L"Diffuse", L"DiffuseTexture",
    L"Diffuse Texture", L"DiffuseMap", L"Diffuse Map", L"BaseTexture", L"Base Texture", L"BaseMap", L"Base Map",
    L"Color", L"ColorTexture", L"Color Texture", L"MainTex", L"Main Texture", L"Texture", L"Tex", L"BC",
};

// 名前から見て、キャラクターの表示に向かないもの(特殊な用途のマスター)
const wchar_t* const kAvoid[] = {
    L"food", L"liquid", L"noodle", L"soup", L"vfx", L"/ui/", L"_ui_", L"sky", L"water", L"decal", L"glow",
    L"volumetric", L"impostor", L"tree", L"lines", L"debug", L"brush", L"crystal", L"parallax", L"/lf_", L"projection",
    L"preview", L"eye", L"teeth", L"hair", L"facial", L"lacrimal", L"skin", L"metahuman", L"widget", L"flyer",
    L"refraction", L"ribbon", L"poo", L"dither", L"translucent_master", L"transparent", L"outline", L"cards",
};

const wchar_t* const kPrefer[] = {L"simple", L"basic", L"basematerial", L"master01", L"mm_basic", L"m_base"};

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

bool readBool(UObject* obj, const wchar_t* name, bool def) {
    FProperty* p = obj ? obj->GetPropertyByNameInChain(name) : nullptr;
    auto* bp = p ? CastField<FBoolProperty>(p) : nullptr;
    return bp ? bp->GetPropertyValueInContainer(obj) : def;
}

uint8_t readByte(UObject* obj, const wchar_t* name, uint8_t def) {
    auto* p = propertyPtr<uint8_t>(obj, name);
    return p ? *p : def;
}

UObject* createMid(UObject* materialLib, UObject* worldContext, UObject* parent) {
    Call cm(materialLib, L"CreateDynamicMaterialInstance");
    cm.set(L"WorldContextObject", worldContext).set(L"Parent", parent);
    UObject* mid = nullptr;
    if (!cm.invoke() || !cm.result(mid)) return nullptr;
    return mid;
}

bool hasTextureParam(UObject* mid, const wchar_t* name) {
    Call g(mid, L"K2_GetTextureParameterValue");
    g.set(L"ParameterName", FName(name, FNAME_Add));
    UObject* tex = nullptr;
    return g.invoke() && g.result(tex) && tex != nullptr;
}

struct ParamSets {
    std::set<std::wstring> textures, scalars;
};

// TArray<F*ParameterValue> の各要素の先頭(FMaterialParameterInfo::Name)を読む
void readParamNames(UObject* mic, const wchar_t* arrayName, std::set<std::wstring>& out) {
    FProperty* p = mic->GetPropertyByNameInChain(arrayName);
    auto* ap = p ? CastField<FArrayProperty>(p) : nullptr;
    if (!ap || ap->GetSize() != sizeof(FakeTArray)) return;
    const auto* arr = reinterpret_cast<const FakeTArray*>(reinterpret_cast<uint8_t*>(mic) + p->GetOffset_Internal());
    const int32_t stride = ap->GetInner()->GetElementSize();
    if (!arr->Data || arr->Num <= 0 || arr->Num > 1024 || stride < static_cast<int32_t>(sizeof(FName))) return;
    for (int32_t i = 0; i < arr->Num; ++i) {
        const auto* name = reinterpret_cast<const FName*>(static_cast<const uint8_t*>(arr->Data) + static_cast<size_t>(i) * stride);
        std::wstring s = name->ToString();
        if (!s.empty() && s != L"None") out.insert(s);
    }
}

// 全 MIC について、親をたどった先の UMaterial ごとにパラメータ名を集める
std::map<UObject*, ParamSets> collectInstanceParams() {
    std::map<UObject*, ParamSets> result;
    std::vector<UObject*> mics;
    UObjectGlobals::FindAllOf(STR("MaterialInstanceConstant"), mics);
    for (UObject* mic : mics) {
        if (!mic) continue;
        UObject* base = mic;
        for (int depth = 0; depth < 8; ++depth) {
            if (!base->GetPropertyByNameInChain(L"Parent")) break;  // UMaterial には Parent が無い
            UObject* parent = objectProperty(base, L"Parent");
            if (!parent) {
                base = nullptr;
                break;
            }
            base = parent;
        }
        if (!base) continue;
        auto& sets = result[base];
        readParamNames(mic, L"TextureParameterValues", sets.textures);
        readParamNames(mic, L"ScalarParameterValues", sets.scalars);
    }
    return result;
}

bool contains(const std::wstring& lowerName, std::initializer_list<const wchar_t*> words) {
    for (auto* w : words)
        if (lowerName.find(w) != std::wstring::npos) return true;
    return false;
}

}  // namespace

void MaterialFinder::discover(UObject* worldContext, const std::wstring& overridePath, const std::wstring& overrideParam) {
    if (done_ && anyAlive()) return;
    done_ = true;
    candidates_.clear();
    UObject* materialLib = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetMaterialLibrary"));
    if (!materialLib) {
        logError(L"KismetMaterialLibrary が見つかりません");
        return;
    }

    std::vector<UObject*> materials;
    if (!overridePath.empty()) {
        if (UObject* m = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, overridePath.c_str())) materials.push_back(m);
        else logWarn(L"config.ini の material が見つかりません(読み込まれていない可能性): " + overridePath);
    }
    if (materials.empty()) UObjectGlobals::FindAllOf(STR("Material"), materials);
    auto instanceParams = collectInstanceParams();

    for (UObject* m : materials) {
        if (!m) continue;
        std::wstring path = m->GetFullName();
        // "Material /Game/...": クラス名を除いたパス
        if (auto sp = path.find(L' '); sp != std::wstring::npos) path = path.substr(sp + 1);
        std::wstring lp = lower(path);
        if (overridePath.empty()) {
            if (lp.rfind(L"/game/", 0) != 0) continue;
            if (readByte(m, L"MaterialDomain", 255) != 0) continue;  // Surface 以外
            if (readByte(m, L"ShadingModel", 255) != 1) continue;    // DefaultLit 以外
            bool avoid = false;
            for (auto* a : kAvoid)
                if (lp.find(a) != std::wstring::npos) avoid = true;
            if (avoid) continue;
        }
        uint8_t blend = readByte(m, L"BlendMode", 255);
        if (blend > 2) continue;

        UObject* mid = createMid(materialLib, worldContext, m);
        if (!mid) continue;
        std::wstring param;
        int nameRank = 0;
        if (!overrideParam.empty()) {
            if (hasTextureParam(mid, overrideParam.c_str())) param = overrideParam;
        } else {
            for (auto* n : kTextureParamNames) {
                if (hasTextureParam(mid, n)) {
                    param = n;
                    break;
                }
                ++nameRank;
            }
        }
        if (param.empty()) continue;

        DisplayMaterial d;
        d.material = m;
        d.path = path;
        d.textureParam = param;
        d.blendMode = blend;
        d.twoSided = readBool(m, L"TwoSided", false);
        d.score = (d.twoSided ? 100 : 0) - nameRank * 2;
        if (auto it = instanceParams.find(m); it != instanceParams.end()) {
            d.paramsKnown = true;
            d.textureParams.assign(it->second.textures.begin(), it->second.textures.end());
            d.scalarParams.assign(it->second.scalars.begin(), it->second.scalars.end());
            // ベースカラー以外のテクスチャ入力が多いほど、VRM の見た目に余計なものが重なる
            for (const auto& t : d.textureParams)
                if (t != param) d.score -= 15;
        } else {
            d.score -= 10;  // 入力が分からない
        }
        for (auto* p : kPrefer)
            if (lp.find(p) != std::wstring::npos) d.score += 20;
        if (lp.find(L"/men/") != std::wstring::npos) d.score += 10;  // ゲーム本体のマテリアルを優先
        candidates_.push_back(d);
    }

    std::sort(candidates_.begin(), candidates_.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    logInfo(L"表示用マテリアルの候補: " + std::to_wstring(candidates_.size()) + L" 個(調べたマテリアル " +
            std::to_wstring(materials.size()) + L" 個)");
    for (size_t i = 0; i < candidates_.size() && i < 30; ++i) {
        const auto& c = candidates_[i];
        static const wchar_t* kBlend[] = {L"Opaque", L"Masked", L"Translucent"};
        std::wstring tex, sc;
        for (const auto& t : c.textureParams) tex += (tex.empty() ? L"" : L", ") + t;
        for (const auto& t : c.scalarParams) sc += (sc.empty() ? L"" : L", ") + t;
        logInfo(L"  候補 " + std::to_wstring(c.score) + L" " + kBlend[c.blendMode] + (c.twoSided ? L" 両面 " : L" 片面 ") +
                c.path + L" [" + c.textureParam + L"]" +
                (c.paramsKnown ? L" テクスチャ{" + tex + L"} 数値{" + sc + L"}" : L" (入力不明)"));
    }
}

const DisplayMaterial* MaterialFinder::pick(rvrm::AlphaMode) const {
    // 半透明・切り抜きのマスターは、透明度をどのパラメータから取るか分からない(部品が丸ごと消えることがある)。
    // 見えなくなるよりは良いので、VRM のアルファモードによらず Opaque → Masked → Translucent の順で選ぶ
    for (uint8_t b : {uint8_t{0}, uint8_t{1}, uint8_t{2}})
        for (const auto& c : candidates_)
            if (c.blendMode == b && c.material.Get()) return &c;  // 解放済みのものは飛ばす
    return nullptr;
}

bool MaterialFinder::anyAlive() const {
    for (const auto& c : candidates_)
        if (c.material.Get()) return true;
    return false;
}

}  // namespace rvrm_mod

namespace rvrm_mod {

void MaterialFinder::neutralize(UObject* mid, const DisplayMaterial& d) const {
    auto tex = [](const wchar_t* path) { return UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path); };
    UObject* flatNormal = tex(STR("/Engine/EngineMaterials/DefaultNormal.DefaultNormal"));
    UObject* white = tex(STR("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
    UObject* black = tex(STR("/Engine/EngineResources/Black.Black"));

    for (const auto& t : d.textureParams) {
        if (t == d.textureParam) continue;
        std::wstring l = lower(t);
        UObject* value = nullptr;
        if (contains(l, {L"normal", L"bump"})) value = flatNormal;              // 凹凸なし
        else if (contains(l, {L"emiss", L"glow"})) value = black;              // 発光なし
        else if (contains(l, {L"orm", L"arm", L"rma", L"mra", L"metal"})) continue;  // 詰め合わせは触らない(下の数値で抑える)
        else value = white;                                                    // 汚れ・ディテール・AO・ラフネス等は白(影響なし/マット)
        if (!value) continue;
        Call st(mid, L"SetTextureParameterValue");
        st.set(L"ParameterName", FName(t.c_str(), FNAME_Add)).set(L"Value", value).invoke();
    }

    auto setScalar = [&](const std::wstring& name, float v) {
        Call ss(mid, L"SetScalarParameterValue");
        ss.set(L"ParameterName", FName(name.c_str(), FNAME_Add)).set(L"Value", v).invoke();
    };
    for (const auto& s : d.scalarParams) {
        std::wstring l = lower(s);
        if (contains(l, {L"tiling", L"tile", L"scale", L"uv", L"offset", L"rotation"})) continue;  // UV 系は触らない
        if (contains(l, {L"metal"})) setScalar(s, 0.0f);
        else if (contains(l, {L"rough"})) setScalar(s, 0.85f);
        else if (contains(l, {L"spec"})) setScalar(s, 0.3f);
        else if (contains(l, {L"normal", L"bump", L"detail", L"dirt", L"grunge", L"noise", L"overlay", L"wear", L"damage",
                              L"emiss", L"height", L"parallax", L"displace"}))
            setScalar(s, 0.0f);
    }
    // 名前が分からないマテリアルでも効くよう、よくある名前も設定しておく(無ければ何も起きない)
    setScalar(L"Roughness", 0.85f);
    setScalar(L"Metallic", 0.0f);
    setScalar(L"Specular", 0.3f);
    if (flatNormal)
        for (const wchar_t* n : {L"Normal", L"NormalMap", L"Normal Map", L"Normal_Map"}) {
            Call st(mid, L"SetTextureParameterValue");
            st.set(L"ParameterName", FName(n, FNAME_Add)).set(L"Value", flatNormal).invoke();
        }
}

}  // namespace rvrm_mod
