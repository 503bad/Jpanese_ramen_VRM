// VRM の利用許諾(meta)に基づく判定。
// マルチプレイでモデルファイルを他プレイヤーへ送ることは「再配布」に当たり得るため、
// 明示的に許可されているモデル以外は送らない(保守的に判定する)。
#pragma once

#include <string>

#include "rvrm/model.h"

namespace rvrm {

struct TransferDecision {
    bool allowed = false;
    std::string reason;  // 日本語の説明(ログ・UI 表示用)
};

TransferDecision canTransferToOtherPlayers(const Meta& meta, VrmVersion version);

}  // namespace rvrm
