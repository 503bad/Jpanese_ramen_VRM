// VRM の表示に使うマテリアルを、ゲーム内に読み込まれているものから探す。
//
// エンジンの UI 用マテリアル(アンリット)は、このゲームの自動露出の下では暗く(ほぼ黒く)見えてしまう。
// そこで、ライティングの効く通常のマテリアル(Surface / DefaultLit)のうち、ベースカラー用の
// テクスチャパラメータを持つものを実行時に探して使う。
// パラメータ名は一覧を取れないので、よくある名前を MID の K2_GetTextureParameterValue で順に試す。
#pragma once

#include <string>
#include <vector>

#include <Unreal/FWeakObjectPtr.hpp>

#include "rvrm/model.h"
#include "ue_call.hpp"

namespace rvrm_mod {

struct DisplayMaterial {
    // 親にする UMaterial。ゲーム側が後で解放することがあるので弱参照で持ち、使う直前に確認する
    RC::Unreal::FWeakObjectPtr material;
    std::wstring path;
    std::wstring textureParam;    // ベースカラーのテクスチャパラメータ名
    uint8_t blendMode = 0;        // 0 Opaque / 1 Masked / 2 Translucent
    bool twoSided = false;
    bool lit = true;
    int score = 0;
    // ゲーム内の派生マテリアル(MaterialInstanceConstant)が上書きしているパラメータ名。
    // マテリアル本体からは一覧を取れないので、これで「どんな入力があるか」を推定する
    bool paramsKnown = false;
    std::vector<std::wstring> textureParams;
    std::vector<std::wstring> scalarParams;
};

class MaterialFinder {
public:
    // config.ini で指定されたマテリアル(空なら自動)。worldContext は MID 作成用
    void discover(UObject* worldContext, const std::wstring& overridePath, const std::wstring& overrideParam);
    // 探索済みで、まだ使える候補が残っているか(マップ移動で全部解放されたら探し直す)
    bool ready() const { return done_ && anyAlive(); }
    bool anyAlive() const;

    // VRM のアルファモードに合うものを返す(無ければ近いもの、それも無ければ nullptr)。
    // 返したものの material.Get() は、このフレーム内では有効
    const DisplayMaterial* pick(rvrm::AlphaMode mode) const;

    // MID の余計な入力(ノーマルマップ・汚れ・ディテールなど)を打ち消す
    void neutralize(UObject* mid, const DisplayMaterial& d) const;

private:
    std::vector<DisplayMaterial> candidates_;
    bool done_ = false;
};

}  // namespace rvrm_mod
