#include "custom_materials.hpp"

#include <cstring>
#include <vector>

#include <Unreal/UObjectGlobals.hpp>

#include "log.hpp"

namespace rvrm_mod {

using namespace RC::Unreal;

namespace {
// TSoftObjectPtr<UObject> の中身: FWeakObjectPtr(8) + FSoftObjectPath{ FTopLevelAssetPath{PackageName, AssetName}(16), SubPath(16) }
struct SoftObjectPtrRaw {
    int32_t weakIndex = 0, weakSerial = 0;
    FName packageName;
    FName assetName;
    FakeTArray subPath;
};
static_assert(sizeof(SoftObjectPtrRaw) == 40);
}  // namespace

UObject* CustomMaterials::load(const std::wstring& name) {
    const std::wstring package = L"/Game/RamenVRM/" + name;
    // 既に読み込まれていればそれを使う
    if (UObject* o = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, (package + L"." + name).c_str())) return o;

    UObject* lib = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"));
    if (!lib) return nullptr;
    SoftObjectPtrRaw raw;
    raw.packageName = FName(package.c_str(), FNAME_Add);
    raw.assetName = FName(name.c_str(), FNAME_Add);
    std::vector<uint8_t> bytes(sizeof raw);
    std::memcpy(bytes.data(), &raw, sizeof raw);
    Call c(lib, L"LoadAsset_Blocking");
    c.setRaw(L"Asset", bytes);
    UObject* result = nullptr;
    if (!c.invoke() || !c.result(result) || !result) {
        if (!warned_) {
            warned_ = true;
            logWarn(L"専用マテリアルを読み込めません(" + package + L")。ゲーム内のマテリアルで代用します" +
                    (c.error().empty() ? L"" : L": " + c.error()));
        }
        return nullptr;
    }
    if (!everLoaded_) logInfo(L"専用マテリアルを読み込みました: " + package);
    everLoaded_ = true;
    return result;
}

UObject* CustomMaterials::get(bool lit, rvrm::AlphaMode mode) {
    const int b = mode == rvrm::AlphaMode::Blend ? 2 : (mode == rvrm::AlphaMode::Mask ? 1 : 0);
    auto& slot = cache_[lit ? 0 : 1][b];
    if (UObject* o = slot.Get()) return o;
    static const wchar_t* kBlend[] = {L"Opaque", L"Masked", L"Translucent"};
    UObject* o = load(std::wstring(L"M_RVRM_") + (lit ? L"Lit_" : L"Unlit_") + kBlend[b]);
    if (o) slot = o;
    return o;
}

}  // namespace rvrm_mod
