// マルチプレイで「誰がどの VRM を使っているか」を伝える。
//
// ゲームの通信(EOS)には一切触れず、Steam のリッチプレゼンス(フレンドに見える状態表示)に
//   rvrm_model  = 使っている VRM ファイルの中身から計算した ID
//   rvrm_player = ゲーム内のプレイヤー名(相手の画面のキャラクターと対応付けるため)
// を書く。相手の MOD はフレンドのうち同じゲームを遊んでいる人の値を読み、自分の models フォルダに
// 同じ ID のファイルがあればそのキャラクターに表示する。モデルのファイル自体は送らない。
// steam_api64.dll はゲームが読み込んで初期化済みのものを使う(MOD からは初期化しない)。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rvrm_mod {

class SteamPresence {
public:
    // steam_api64.dll が読み込まれていなければ false(何もしない)
    bool init();
    bool ready() const { return friends_ != nullptr; }

    void publish(const std::string& modelId, const std::string& playerName);

    struct Peer {
        uint64_t steamId = 0;
        std::string playerName;  // rvrm_player(ゲーム内の名前)
        std::string modelId;     // rvrm_model
    };
    // 同じゲーム(appId)を遊んでいるフレンドのうち、MOD の情報を出している人
    std::vector<Peer> peers(uint32_t appId);
    // フレンドのリッチプレゼンスを取り直す(数秒おきに呼ぶ)
    void refresh(uint32_t appId);

private:
    using GetFriendsFn = void* (*)();
    using SetRichPresenceFn = bool (*)(void*, const char*, const char*);
    using GetFriendCountFn = int (*)(void*, int);
    using GetFriendByIndexFn = uint64_t (*)(void*, int, int);
    using GetFriendRichPresenceFn = const char* (*)(void*, uint64_t, const char*);
    using RequestFriendRichPresenceFn = void (*)(void*, uint64_t);
    using GetFriendGamePlayedFn = bool (*)(void*, uint64_t, void*);

    bool playingApp(uint64_t friendId, uint32_t appId);

    void* friends_ = nullptr;
    SetRichPresenceFn setRichPresence_ = nullptr;
    GetFriendCountFn getFriendCount_ = nullptr;
    GetFriendByIndexFn getFriendByIndex_ = nullptr;
    GetFriendRichPresenceFn getFriendRichPresence_ = nullptr;
    RequestFriendRichPresenceFn requestFriendRichPresence_ = nullptr;
    GetFriendGamePlayedFn getFriendGamePlayed_ = nullptr;
    std::string lastModel_, lastName_;
};

// VRM ファイルの中身から ID を作る(FNV-1a 64bit、16 桁の 16 進数)
std::string modelIdOf(const void* data, size_t size);

}  // namespace rvrm_mod
