#include "rvrm/license.h"

namespace rvrm {

TransferDecision canTransferToOtherPlayers(const Meta& meta, VrmVersion version) {
    TransferDecision d;
    if (version == VrmVersion::Unknown) {
        d.reason = "VRM の利用許諾情報がありません";
        return d;
    }
    if (meta.allowRedistribution == 1) {
        d.allowed = true;
        d.reason = version == VrmVersion::Vrm0 ? "ライセンス " + meta.vrm0LicenseName + " により再配布が許可されています"
                                               : "meta.allowRedistribution = true";
        return d;
    }
    if (meta.allowRedistribution == 0) {
        d.reason = "このモデルは再配布が許可されていません(自分の画面にのみ表示されます)";
    } else {
        d.reason = "再配布の可否が独自ライセンスで指定されているため自動判定できません(自分の画面にのみ表示されます)";
    }
    return d;
}

}  // namespace rvrm
