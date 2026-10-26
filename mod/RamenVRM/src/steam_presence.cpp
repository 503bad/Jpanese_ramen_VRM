#include "steam_presence.hpp"

#include <cstdio>

#define NOMINMAX
#include <windows.h>

#include "log.hpp"
#include "settings.hpp"

namespace rvrm_mod {

namespace {
constexpr int kFriendFlagImmediate = 0x04;  // 通常のフレンド

// steam_api の FriendGameInfo_t(Windows では 8 バイト境界)
#pragma pack(push, 8)
struct FriendGameInfo {
    uint64_t gameId;
    uint32_t gameIp;
    uint16_t gamePort;
    uint16_t queryPort;
    uint64_t lobbyId;
};
#pragma pack(pop)
}  // namespace

std::string modelIdOf(const void* data, size_t size) {
    uint64_t h = 1469598103934665603ULL;
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

bool SteamPresence::init() {
    HMODULE m = GetModuleHandleW(L"steam_api64.dll");
    if (!m) {
        logWarn(L"steam_api64.dll が読み込まれていません。マルチプレイでの VRM 共有は無効です");
        return false;
    }
    auto getFriends = reinterpret_cast<GetFriendsFn>(GetProcAddress(m, "SteamAPI_SteamFriends_v017"));
    setRichPresence_ = reinterpret_cast<SetRichPresenceFn>(GetProcAddress(m, "SteamAPI_ISteamFriends_SetRichPresence"));
    getFriendCount_ = reinterpret_cast<GetFriendCountFn>(GetProcAddress(m, "SteamAPI_ISteamFriends_GetFriendCount"));
    getFriendByIndex_ = reinterpret_cast<GetFriendByIndexFn>(GetProcAddress(m, "SteamAPI_ISteamFriends_GetFriendByIndex"));
    getFriendRichPresence_ =
        reinterpret_cast<GetFriendRichPresenceFn>(GetProcAddress(m, "SteamAPI_ISteamFriends_GetFriendRichPresence"));
    requestFriendRichPresence_ =
        reinterpret_cast<RequestFriendRichPresenceFn>(GetProcAddress(m, "SteamAPI_ISteamFriends_RequestFriendRichPresence"));
    getFriendGamePlayed_ = reinterpret_cast<GetFriendGamePlayedFn>(GetProcAddress(m, "SteamAPI_ISteamFriends_GetFriendGamePlayed"));
    if (!getFriends || !setRichPresence_ || !getFriendCount_ || !getFriendByIndex_ || !getFriendRichPresence_ ||
        !requestFriendRichPresence_ || !getFriendGamePlayed_) {
        logWarn(L"Steam の関数が見つかりません(版の違い)。マルチプレイでの VRM 共有は無効です");
        return false;
    }
    friends_ = getFriends();
    if (!friends_) {
        logWarn(L"Steam が初期化されていません。マルチプレイでの VRM 共有は無効です");
        return false;
    }
    logInfo(L"Steam のリッチプレゼンスで VRM の情報を共有します(フレンド同士)");
    return true;
}

void SteamPresence::publish(const std::string& modelId, const std::string& playerName) {
    if (!friends_ || (modelId == lastModel_ && playerName == lastName_)) return;
    lastModel_ = modelId;
    lastName_ = playerName;
    // 空文字を設定するとキーが消える(元の見た目を選んだとき)
    setRichPresence_(friends_, "rvrm_model", modelId.c_str());
    setRichPresence_(friends_, "rvrm_player", playerName.c_str());
}

bool SteamPresence::playingApp(uint64_t friendId, uint32_t appId) {
    FriendGameInfo info{};
    if (!getFriendGamePlayed_(friends_, friendId, &info)) return false;
    return static_cast<uint32_t>(info.gameId & 0xFFFFFF) == appId;  // CGameID の下位 24 ビットが AppID
}

void SteamPresence::refresh(uint32_t appId) {
    if (!friends_) return;
    int n = getFriendCount_(friends_, kFriendFlagImmediate);
    for (int i = 0; i < n && i < 2000; ++i) {
        uint64_t id = getFriendByIndex_(friends_, i, kFriendFlagImmediate);
        if (id && playingApp(id, appId)) requestFriendRichPresence_(friends_, id);
    }
}

std::vector<SteamPresence::Peer> SteamPresence::peers(uint32_t appId) {
    std::vector<Peer> out;
    if (!friends_) return out;
    int n = getFriendCount_(friends_, kFriendFlagImmediate);
    for (int i = 0; i < n && i < 2000; ++i) {
        uint64_t id = getFriendByIndex_(friends_, i, kFriendFlagImmediate);
        if (!id || !playingApp(id, appId)) continue;
        const char* model = getFriendRichPresence_(friends_, id, "rvrm_model");
        const char* name = getFriendRichPresence_(friends_, id, "rvrm_player");
        if (!model || !*model || !name || !*name) continue;
        out.push_back({id, name, model});
    }
    return out;
}

}  // namespace rvrm_mod
