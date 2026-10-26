#include "settings.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#define NOMINMAX
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

namespace rvrm_mod {

namespace fs = std::filesystem;

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

namespace {

const char* kDefaultConfig =
    "; Ramen VRM MOD の設定(UTF-8)。変更はゲームの再起動後に反映されます。\r\n"
    "\r\n"
    "; 使う VRM のファイル名(models フォルダの中)。空なら最初に見つかった .vrm、none なら元の見た目\r\n"
    "; (ロッカーのカスタマイズ画面で F5 / F6 を押して選ぶと、ここが自動で書き換わります)\r\n"
    "model =\r\n"
    "\r\n"
    "; 1 = 有効 / 0 = 無効\r\n"
    "enabled = 1\r\n"
    "; 揺れもの(髪・スカートなど)\r\n"
    "spring_bones = 1\r\n"
    "; 自動まばたき\r\n"
    "auto_blink = 1\r\n"
    "; 表示方法: lit = 光の当たり方を反映 / unlit = アニメ調(影なし)。ロッカーで F7 を押しても切り替わる\r\n"
    "shading = lit\r\n"
    "; マルチプレイで Steam のフレンドと VRM の情報(モデル ID とゲーム内の名前)を共有する。\r\n"
    "; 相手も同じ VRM ファイルを models に入れていれば、お互いの VRM が見える(ファイル自体は送らない)\r\n"
    "share_with_friends = 1\r\n"
    "; 一人称視点で頭を隠す\r\n"
    "hide_head_in_first_person = 1\r\n"
    "\r\n"
    "; --- 表示がおかしいときの調整 ---\r\n"
    "; VRM で片面のマテリアルも両面表示する(1 にすると重くなる)\r\n"
    "double_sided_all = 0\r\n"
    "; ポリゴンが裏返って見えるときだけ 1 にする\r\n"
    "reverse_faces = 0\r\n"
    "; 表示に使うマテリアルを手動で指定する(空なら自動)。例: /Game/Folder/M_Example.M_Example\r\n"
    "material =\r\n"
    "; そのマテリアルのベースカラー用テクスチャパラメータ名\r\n"
    "material_texture =\r\n";

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool toBool(const std::string& v, bool def) {
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return def;
}

fs::path knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    fs::path r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = p;
    if (p) CoTaskMemFree(p);
    return r;
}

fs::path documentsDir() { return knownFolder(FOLDERID_Documents); }

// install.bat が記録した「配布物の models フォルダ」の場所(%LOCALAPPDATA%\RamenVRM\models_location.txt、UTF-8 1 行)
fs::path packageModelsDir() {
    std::ifstream in(knownFolder(FOLDERID_LocalAppData) / L"RamenVRM" / L"models_location.txt", std::ios::binary);
    std::string line;
    if (!std::getline(in, line)) return {};
    if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line = line.substr(3);
    line = trim(line);
    return line.empty() ? fs::path() : fs::path(widen(line));
}

}  // namespace

Settings loadSettings() {
    Settings s;
    s.userDir = documentsDir() / L"RamenVRM";
    s.modelsDir = s.userDir / L"models";
    std::error_code ec;
    fs::create_directories(s.modelsDir, ec);
    if (fs::path pkg = packageModelsDir(); !pkg.empty()) {
        if (fs::is_directory(pkg, ec)) s.modelDirs.push_back(pkg);
        else s.notes.push_back("install.bat の隣の models フォルダが見つかりません(配布物を移動・削除した場合は install.bat を実行し直してください): " + narrow(pkg.wstring()));
    }
    s.modelDirs.push_back(s.modelsDir);

    fs::path cfg = s.userDir / L"config.ini";
    if (!fs::exists(cfg, ec)) {
        std::ofstream o(cfg, std::ios::binary);
        o << "\xEF\xBB\xBF" << kDefaultConfig;
        s.notes.push_back("config.ini を作成しました: " + narrow(cfg.wstring()));
    }

    std::string model;
    std::ifstream in(cfg, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line = line.substr(3);  // BOM
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (key == "model") model = val;
        else if (key == "enabled") s.enabled = toBool(val, s.enabled);
        else if (key == "spring_bones") s.springBones = toBool(val, s.springBones);
        else if (key == "auto_blink") s.autoBlink = toBool(val, s.autoBlink);
        else if (key == "hide_head_in_first_person") s.hideHeadInFirstPerson = toBool(val, s.hideHeadInFirstPerson);
        else if (key == "reverse_faces") s.flipWinding = toBool(val, s.flipWinding);
        // 0.1.0 / 0.2.0 の flip_winding と two_sided は既定値が誤っていたので読まない(新しい名前の項目を使う)
        else if (key == "double_sided_all") s.twoSidedAll = toBool(val, s.twoSidedAll);
        else if (key == "material") s.materialPath = widen(val);
        else if (key == "shading") s.shadingLit = (val != "unlit");
        else if (key == "share_with_friends") s.shareWithFriends = toBool(val, s.shareWithFriends);
        else if (key == "material_texture") s.materialTextureParam = widen(val);
    }

    if (model == "none") {
        s.modelNone = true;
        return s;
    }
    if (!model.empty()) {
        for (const auto& dir : s.modelDirs) {
            fs::path p = dir / widen(model);
            if (fs::exists(p, ec)) {
                s.modelPath = p;
                break;
            }
        }
        if (s.modelPath.empty()) s.notes.push_back("config.ini の model に指定されたファイルが見つかりません: " + model);
    }
    if (s.modelPath.empty()) {
        auto found = listModels(s);
        if (!found.empty()) s.modelPath = found.front();
    }
    return s;
}

std::vector<fs::path> listModels(const Settings& s) {
    std::vector<fs::path> result;
    std::vector<std::wstring> names;
    for (const auto& dir : s.modelDirs) {
        std::vector<fs::path> found;
        std::error_code ec;
        for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            auto ext = it->path().extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (it->is_regular_file() && ext == L".vrm") found.push_back(it->path());
        }
        std::sort(found.begin(), found.end());
        for (const auto& p : found) {
            // 同じファイル名は先のフォルダのものを使う(config.ini の model はファイル名で保存するため)
            if (std::find(names.begin(), names.end(), p.filename().wstring()) != names.end()) continue;
            names.push_back(p.filename().wstring());
            result.push_back(p);
        }
    }
    return result;
}

void saveModelSetting(const Settings& s, const std::wstring& value) { saveSetting(s, "model", value); }

void saveSetting(const Settings& s, const std::string& key, const std::wstring& value) {
    fs::path cfg = s.userDir / L"config.ini";
    std::vector<std::string> lines;
    {
        std::ifstream in(cfg, std::ios::binary);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }
    const std::string newLine = key + " = " + narrow(value) + "\r";
    bool replaced = false;
    for (auto& l : lines) {
        bool bom = l.size() >= 3 && static_cast<unsigned char>(l[0]) == 0xEF;
        std::string t = trim(bom ? l.substr(3) : l);
        size_t eq = t.find('=');
        if (eq != std::string::npos && t[0] != ';' && trim(t.substr(0, eq)) == key) {
            l = (bom ? std::string("\xEF\xBB\xBF") : std::string()) + newLine;
            replaced = true;
            break;
        }
    }
    if (!replaced) lines.push_back(newLine);
    std::ofstream out(cfg, std::ios::binary | std::ios::trunc);
    for (const auto& l : lines) out << l << "\n";
}

}  // namespace rvrm_mod
