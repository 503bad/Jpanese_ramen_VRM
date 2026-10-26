// UE4SS のログ(ue4ss\UE4SS.log とコンソール)へ出力する。行頭は [RamenVRM]。
#pragma once

#include <string>

namespace rvrm_mod {

void logInfo(const std::wstring& msg);
void logWarn(const std::wstring& msg);
void logError(const std::wstring& msg);

}  // namespace rvrm_mod
