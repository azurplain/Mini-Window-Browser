#pragma once

#include "AppModel.h"

#include <string>

namespace xiaochuang {

std::wstring BuildSystemDiagnostics(const AppState& state,
                                    const std::wstring& webViewVersion,
                                    WindowMode windowMode);
std::wstring DecodeExecuteScriptString(const std::wstring& value);
bool CopyTextToClipboard(HWND owner, const std::wstring& text);

} // namespace xiaochuang
