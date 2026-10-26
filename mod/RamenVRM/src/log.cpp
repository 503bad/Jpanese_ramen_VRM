#include "log.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

namespace rvrm_mod {

using namespace RC;

void logInfo(const std::wstring& msg) { Output::send<LogLevel::Normal>(STR("[RamenVRM] {}\n"), msg); }
void logWarn(const std::wstring& msg) { Output::send<LogLevel::Warning>(STR("[RamenVRM] {}\n"), msg); }
void logError(const std::wstring& msg) { Output::send<LogLevel::Error>(STR("[RamenVRM] {}\n"), msg); }

}  // namespace rvrm_mod
