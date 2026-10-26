// Ramen VRM MOD(UE4SS C++ MOD)のエントリポイント。
// エンジンの Tick(ゲームスレッド)ごとに、次のキャラクターへ VRM を適用する。
//   - 自分のプレイヤーキャラクター(選んだモデル)
//   - F5 / F6 で見た目を切り替え、F7 で表示方法(いつでも)
//   ※ ロッカーのカスタマイズ画面のプレビューには写せなかった(専用カメラの撮影対象に入らない)ので触らない
//   - マルチプレイの他プレイヤー(Steam のフレンドで、同じモデルのファイルを自分も持っている場合)
#include <algorithm>
#include <atomic>
#include <exception>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <set>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Input/KeyDef.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include "custom_materials.hpp"
#include "log.hpp"
#include "material_finder.hpp"
#include "rvrm/license.h"
#include "settings.hpp"
#include "steam_presence.hpp"
#include "ue_call.hpp"
#include "vrm_character.hpp"

using namespace RC;
using namespace RC::Unreal;

namespace rvrm_mod {

namespace fs = std::filesystem;

constexpr uint32_t kAppId = 4968640;  // Japanese Ramen Simulator

using AvatarFuture = std::future<std::unique_ptr<rvrm::Avatar>>;
using ModelIndex = std::map<std::string, fs::path>;  // モデル ID → ファイル

// 読み込み中に不要になったもの。future をその場で破棄すると完了まで待たされるので、終わるまで持っておく
std::vector<AvatarFuture>& abandonedLoads() {
    static std::vector<AvatarFuture> v;
    return v;
}

// VRM を被せる対象 1 つ分
struct Slot {
    RC::Unreal::FWeakObjectPtr target;
    fs::path model;  // 表示中(または読み込み中)のモデル
    AvatarFuture pending;
    std::unique_ptr<VrmCharacter> character;
    UObject* failedTarget = nullptr;  // 比較専用(参照はしない)

    void shutdown() {
        if (character) character->shutdown();
        character.reset();
    }
    void reset() {
        shutdown();
        failedTarget = nullptr;
        if (pending.valid()) abandonedLoads().push_back(std::move(pending));
    }
};

// FString の UPROPERTY を読む(エンジン側の文字列を参照するだけで、確保・解放はしない)
std::wstring readFString(UObject* obj, const wchar_t* name) {
    auto* s = propertyPtr<FakeTArray>(obj, name);
    if (!s || !s->Data || s->Num <= 1 || s->Num > 1024) return {};
    return std::wstring(static_cast<const wchar_t*>(s->Data), static_cast<size_t>(s->Num - 1));
}

class RamenVRM {
public:
    void start() {
        settings_ = loadSettings();
        for (const auto& n : settings_.notes) logInfo(widen(n));
        if (!settings_.enabled) {
            logInfo(L"config.ini で無効になっています(enabled = 0)");
            return;
        }
        models_ = listModels(settings_);
        for (const auto& d : settings_.modelDirs) logInfo(L"VRM を探すフォルダ: " + d.wstring());
        logInfo(L"見つかった VRM: " + std::to_wstring(models_.size()) + L" 個(F5 / F6 で切り替え、F7 で表示方法)");
        selection_ = -1;
        if (!settings_.modelNone)
            for (size_t i = 0; i < models_.size(); ++i)
                if (models_[i] == settings_.modelPath) selection_ = static_cast<int>(i);
        logInfo(L"選択中: " + selectionName());
        if (settings_.shareWithFriends) {
            steamOk_ = steam_.init();
            rebuildIndex();
        }
        active_ = true;
    }

    void tick(float dt) {
        if (!active_) return;
        try {
            tickImpl(dt);
        } catch (const std::exception& e) {
            logError(L"予期しないエラーのため MOD を停止します: " + widen(e.what()));
            stop();
        }
    }

    // BeginPlay(ゲームスレッド): ロッカーのカスタマイズ画面のプレビュー用キャラクターを見つける
    void onBeginPlay(UObject* actor) {
        if (!active_ || !actor) return;
        (void)actor;  // ロッカーのプレビューは対象外(上のコメント参照)
    }

    // キー入力(UE4SS の入力スレッドから呼ばれる)。処理はゲームスレッドの tick で行う
    void requestStep(int step) { pendingStep_ += step; }
    void requestShadingToggle() { pendingShading_ = true; }

    void stop() {
        player_.shutdown();
        for (auto& [k, s] : remote_) s.shutdown();
        remote_.clear();
        if (steamOk_) steam_.publish("", "");
        active_ = false;
    }

private:
    fs::path selectedModel() const {
        return (selection_ >= 0 && selection_ < static_cast<int>(models_.size())) ? models_[selection_] : fs::path();
    }

    std::wstring selectionName() const {
        auto p = selectedModel();
        return p.empty() ? L"元の見た目" : p.stem().wstring();
    }

    // models フォルダの全ファイルのモデル ID をワーカースレッドで計算する
    void rebuildIndex() {
        if (indexPending_.valid()) abandonedIndex_.push_back(std::move(indexPending_));
        auto files = models_;
        indexPending_ = std::async(std::launch::async, [files]() {
            ModelIndex idx;
            for (const auto& p : files) {
                std::ifstream f(p, std::ios::binary);
                std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                if (!bytes.empty()) idx[modelIdOf(bytes.data(), bytes.size())] = p;
            }
            return idx;
        });
    }

    std::shared_ptr<std::vector<char>> bytesFor(const fs::path& p) {
        auto it = fileCache_.find(p.wstring());
        if (it != fileCache_.end()) return it->second;
        std::ifstream f(p, std::ios::binary);
        auto bytes = std::make_shared<std::vector<char>>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (bytes->empty()) {
            logError(L"VRM を読めません: " + p.wstring());
            return nullptr;
        }
        if (fileCache_.size() >= 3) fileCache_.clear();  // 大きいファイルを溜め込まない
        return fileCache_[p.wstring()] = bytes;
    }

    // 読み込みはワーカースレッドで行う(数百 ms かかるため)
    bool startLoad(Slot& slot) {
        if (slot.model.empty()) return false;
        auto bytes = bytesFor(slot.model);
        if (!bytes) return false;
        rvrm::AvatarOptions opt;
        opt.convert.flipWinding = settings_.flipWinding;
        opt.enableSpringBones = settings_.springBones;
        slot.pending = std::async(std::launch::async, [bytes, opt]() -> std::unique_ptr<rvrm::Avatar> {
            auto a = std::make_unique<rvrm::Avatar>();
            std::string err;
            if (!a->loadFromMemory(bytes->data(), bytes->size(), opt, &err)) {
                logError(L"VRM の読み込みに失敗しました: " + widen(err));
                return nullptr;
            }
            return a;
        });
        return true;
    }

    UObject* gameViewport() {
        UObject* engine = engine_.Get();
        if (!engine) {
            engine = UObjectGlobals::FindFirstOf(STR("GameEngine"));  // 全走査は最初の 1 回だけ
            if (!engine) return nullptr;
            engine_ = engine;
        }
        return objectProperty(engine, L"GameViewport");
    }

    // Engine → GameViewport → GameInstance → LocalPlayers[0] → PlayerController → Pawn
    UObject* findLocalPlayer() {
        UObject* gi = objectProperty(gameViewport(), L"GameInstance");
        auto* players = propertyPtr<FakeTArray>(gi, L"LocalPlayers");
        if (!players || !players->Data || players->Num <= 0) return nullptr;
        UObject* localPlayer = static_cast<UObject* const*>(players->Data)[0];
        UObject* pc = objectProperty(localPlayer, L"PlayerController");
        UObject* pawn = objectProperty(pc, L"Pawn");
        if (!objectProperty(pawn, L"CharacterMeshComponent")) return nullptr;  // このゲームのプレイヤーキャラクターだけ
        return pawn;
    }

    // target に model(空なら元の見た目)を表示し続ける
    void driveSlot(Slot& slot, UObject* target, const fs::path& model, const wchar_t* meshProperty, bool firstPerson, float dt) {
        if (!target || model.empty()) {
            if (slot.character || slot.pending.valid()) slot.reset();
            slot.target = target;
            slot.model = model;
            return;
        }
        if (target != slot.target.Get() || model != slot.model) {
            slot.reset();
            slot.target = target;
            slot.model = model;
        }
        if (!slot.character && !slot.pending.valid() && target != slot.failedTarget) {
            if (!startLoad(slot)) {
                slot.failedTarget = target;
                return;
            }
        }
        if (slot.pending.valid() && slot.pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto avatar = slot.pending.get();
            if (!avatar) {
                slot.failedTarget = target;
                return;
            }
            if (!materials_.ready()) materials_.discover(target, settings_.materialPath, settings_.materialTextureParam);
            if (firstPerson) logMeta(avatar->model());
            slot.character =
                std::make_unique<VrmCharacter>(target, std::move(avatar), settings_, meshProperty, firstPerson, &materials_, &custom_);
        }
        if (slot.character && !slot.character->tick(dt)) {
            if (slot.character->failed()) slot.failedTarget = slot.character->pawn();
            slot.shutdown();
        }
    }

    // ---- マルチプレイ(Steam のフレンド同士)----

    void updateSharing(UObject* localPawn) {
        if (!steamOk_) return;
        if (indexPending_.valid() && indexPending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            index_ = indexPending_.get();
            logInfo(L"models フォルダのモデル ID を計算しました(" + std::to_wstring(index_.size()) + L" 個)");
        }
        // 自分の情報を公開: 選んでいるモデルの ID と、ゲーム内の名前
        std::string myId;
        for (const auto& [id, path] : index_)
            if (path == selectedModel()) myId = id;
        UObject* myState = objectProperty(localPawn, L"PlayerState");
        std::wstring myName = readFString(myState, L"PlayerNamePrivate");
        steam_.publish(myId, myId.empty() ? std::string() : narrow(myName));

        // フレンドの情報を集める(名前 → モデル ID)
        steam_.refresh(kAppId);
        peerModels_.clear();
        for (const auto& p : steam_.peers(kAppId)) peerModels_[widen(p.playerName)] = p.modelId;
    }

    // 他プレイヤー: GameState の PlayerArray から、自分以外のキャラクターを集める
    void driveRemotes(UObject* localPawn, float dt) {
        std::map<UObject*, fs::path> wanted;
        UObject* world = objectProperty(gameViewport(), L"World");
        UObject* gs = objectProperty(world, L"GameState");
        if (auto* arr = propertyPtr<FakeTArray>(gs, L"PlayerArray"); arr && arr->Data && arr->Num > 0 && arr->Num < 64) {
            for (int32_t i = 0; i < arr->Num; ++i) {
                UObject* ps = static_cast<UObject* const*>(arr->Data)[i];
                UObject* pawn = objectProperty(ps, L"PawnPrivate");
                if (!pawn || pawn == localPawn || !objectProperty(pawn, L"CharacterMeshComponent")) continue;
                auto it = peerModels_.find(readFString(ps, L"PlayerNamePrivate"));
                if (it == peerModels_.end()) continue;
                auto file = index_.find(it->second);
                if (file == index_.end()) {
                    if (missingLogged_.insert(it->second).second)
                        logInfo(L"他プレイヤーのモデル(ID " + widen(it->second) + L")は models フォルダにありません。元の見た目で表示します");
                    continue;
                }
                wanted[pawn] = file->second;
            }
        }
        // いなくなった人の後始末
        for (auto it = remote_.begin(); it != remote_.end();) {
            if (!wanted.count(it->first) || !it->second.target.Get()) {
                it->second.reset();
                it = remote_.erase(it);
            } else {
                ++it;
            }
        }
        for (const auto& [pawn, path] : wanted) driveSlot(remote_[pawn], pawn, path, L"CharacterMeshComponent", false, dt);
    }

    void tickImpl(float dt) {
        auto& ab = abandonedLoads();
        ab.erase(std::remove_if(ab.begin(), ab.end(),
                                [](AvatarFuture& f) { return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }),
                 ab.end());

        // 見た目の切り替え(いつでも)
        int step = pendingStep_.exchange(0);
        if (step != 0) changeSelection(step);
        if (pendingShading_.exchange(false)) {
            settings_.shadingLit = !settings_.shadingLit;
            saveSetting(settings_, "shading", settings_.shadingLit ? L"lit" : L"unlit");
            player_.reset();
            for (auto& [k, s] : remote_) s.reset();
            logInfo(std::wstring(L"表示方法を切り替えました: ") + (settings_.shadingLit ? L"ライティングあり" : L"アニメ調"));
        }

        // 自分のキャラクターは 1 秒ごとに確認(マップ移動・リスポーンで作り直されるため)
        searchTimer_ -= dt;
        if (searchTimer_ <= 0) {
            searchTimer_ = 1.0f;
            playerPawn_ = findLocalPlayer();
        }
        UObject* localPawn = playerPawn_.Get();
        driveSlot(player_, localPawn, selectedModel(), L"CharacterMeshComponent", true, dt);

        if (settings_.shareWithFriends) {
            shareTimer_ -= dt;
            if (shareTimer_ <= 0) {
                shareTimer_ = 3.0f;
                updateSharing(localPawn);
            }
            driveRemotes(localPawn, dt);
        }
    }

    void changeSelection(int step) {
        models_ = listModels(settings_);  // ゲーム中に追加されたファイルも拾う
        const int count = static_cast<int>(models_.size()) + 1;  // 先頭(-1)は元の見た目
        int idx = ((selection_ + 1 + step) % count + count) % count;
        selection_ = idx - 1;
        saveModelSetting(settings_, selection_ < 0 ? L"none" : models_[selection_].filename().wstring());
        if (settings_.shareWithFriends && steamOk_) rebuildIndex();
        logInfo(L"見た目を切り替えました: " + selectionName());
        shareTimer_ = 0;  // すぐ公開し直す
    }

    void logMeta(const rvrm::Model& m) {
        const auto& meta = m.meta;
        logInfo(L"モデル: " + widen(meta.name) + L" / 作者: " + widen(meta.authors) + L" / " +
                (m.version == rvrm::VrmVersion::Vrm1 ? L"VRM 1.0" : L"VRM 0.x"));
        for (const auto& w : m.warnings) logWarn(widen(w));
    }

    Settings settings_;
    std::vector<fs::path> models_;
    int selection_ = -1;  // -1 = 元の見た目
    std::map<std::wstring, std::shared_ptr<std::vector<char>>> fileCache_;
    bool active_ = false;
    float searchTimer_ = 0;
    std::atomic<int> pendingStep_{0};
    std::atomic<bool> pendingShading_{false};
    RC::Unreal::FWeakObjectPtr engine_, playerPawn_;
    MaterialFinder materials_;
    CustomMaterials custom_;
    Slot player_;

    // マルチプレイ
    SteamPresence steam_;
    bool steamOk_ = false;
    float shareTimer_ = 0;
    ModelIndex index_;
    std::future<ModelIndex> indexPending_;
    std::vector<std::future<ModelIndex>> abandonedIndex_;
    std::map<std::wstring, std::string> peerModels_;  // ゲーム内の名前 → モデル ID
    std::map<UObject*, Slot> remote_;                 // 他プレイヤーのキャラクター(キーは比較専用)
    std::set<std::string> missingLogged_;
};

}  // namespace rvrm_mod

class RamenVRMMod : public CppUserModBase {
public:
    RamenVRMMod() {
        ModName = STR("RamenVRM");
        ModVersion = STR("0.6.0");
        ModDescription = STR("VRM avatars for Japanese Ramen Simulator");
        ModAuthors = STR("ramen_vrm_mod");
    }

    ~RamenVRMMod() override = default;

    auto on_unreal_init() -> void override {
        if (initialized_) return;  // 再読み込み時に二重登録しない
        initialized_ = true;
        rvrm_mod::logInfo(L"Ramen VRM MOD 0.6.0 を読み込みました");
        impl_.start();
        Hook::RegisterEngineTickPostCallback([this](auto&, UEngine*, float dt, bool) { impl_.tick(dt); },
                                             {false, false, STR("RamenVRM"), STR("EngineTick")});
        Hook::RegisterBeginPlayPostCallback([this](auto&, AActor* actor) { impl_.onBeginPlay(reinterpret_cast<UObject*>(actor)); },
                                            {false, false, STR("RamenVRM"), STR("BeginPlay")});
        register_keydown_event(Input::Key::F5, [this]() { impl_.requestStep(-1); });
        register_keydown_event(Input::Key::F6, [this]() { impl_.requestStep(+1); });
        register_keydown_event(Input::Key::F7, [this]() { impl_.requestShadingToggle(); });
    }

private:
    rvrm_mod::RamenVRM impl_;
    bool initialized_ = false;
};

extern "C" {
__declspec(dllexport) CppUserModBase* start_mod() { return new RamenVRMMod(); }
__declspec(dllexport) void uninstall_mod(CppUserModBase* mod) { delete mod; }
}
