// MOD に同梱する専用マテリアル(RamenVRM_P.pak。ue/scripts/create_materials.py で作成)を読み込む。
// 読めなければ material_finder(ゲーム内のマテリアルを借りる方式)に戻る。
#pragma once

#include <string>

#include <Unreal/FWeakObjectPtr.hpp>

#include "rvrm/model.h"
#include "ue_call.hpp"

namespace rvrm_mod {

class CustomMaterials {
public:
    // lit = ライティングあり / なし(アニメ調)。読めなければ nullptr
    UObject* get(bool lit, rvrm::AlphaMode mode);

    // 一度でも読み込めたか(ログ用)
    bool everLoaded() const { return everLoaded_; }

private:
    UObject* load(const std::wstring& name);

    RC::Unreal::FWeakObjectPtr cache_[2][3];
    bool everLoaded_ = false;
    bool warned_ = false;
};

}  // namespace rvrm_mod
