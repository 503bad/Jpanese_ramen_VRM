// 利用者の設定(ドキュメント\RamenVRM\config.ini)とフォルダの場所。
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rvrm_mod {

struct Settings {
    std::filesystem::path userDir;    // ドキュメント\RamenVRM
    std::filesystem::path modelsDir;  // ドキュメント\RamenVRM\models
    // VRM を探すフォルダ(先頭ほど優先)。install.bat の隣の models(インストール時に記録)→ ドキュメント\RamenVRM\models
    std::vector<std::filesystem::path> modelDirs;
    std::filesystem::path modelPath;  // 実際に使う VRM(見つからない・none なら空)
    bool modelNone = false;           // model = none(元の見た目)

    bool enabled = true;
    bool springBones = true;
    bool autoBlink = true;
    // 三角形の向きを反転する(config.ini の reverse_faces)。実機確認で「反転しない」が正しいと分かった
    bool flipWinding = false;
    // VRM で片面のマテリアルも両面表示する(裏向きの三角形を足す)
    bool twoSidedAll = false;
    // 一人称視点で頭を隠す
    bool hideHeadInFirstPerson = true;
    // カメラと頭の距離がこれ以下なら一人称とみなす(cm)
    float firstPersonDistance = 30.0f;
    // 表示用マテリアルの手動指定(空なら自動で探す)
    std::wstring materialPath;
    std::wstring materialTextureParam;
    // 専用マテリアルの表示方法(config.ini の shading = lit / unlit)
    bool shadingLit = true;
    // マルチプレイで Steam のフレンドと VRM の情報を共有する(config.ini の share_with_friends)
    bool shareWithFriends = true;

    std::vector<std::string> notes;  // 読み込み時の注意(ログ用)
};

// config.ini を読む(無ければ既定値で作る)
Settings loadSettings();

// modelDirs の .vrm(フォルダの優先順 → ファイル名順。同じファイル名は先のフォルダのものだけ)
std::vector<std::filesystem::path> listModels(const Settings& s);

// config.ini の model 行を書き換える(他の行はそのまま)。value は ファイル名 または "none"
void saveModelSetting(const Settings& s, const std::wstring& value);

// config.ini の任意の項目を書き換える(無ければ末尾に足す)
void saveSetting(const Settings& s, const std::string& key, const std::wstring& value);

std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& w);

}  // namespace rvrm_mod
