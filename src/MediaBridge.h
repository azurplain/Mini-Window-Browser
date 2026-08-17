#pragma once

#include <string>

namespace xiaochuang {

enum class MediaCommand {
    TogglePlay,
    SeekBackward,
    SeekForward,
    HoldBackward,
    HoldForward,
    StopHold,
    Previous,
    Next,
};

class MediaBridge {
public:
    static const wchar_t* BootstrapScript();
    static std::wstring CommandScript(MediaCommand command, int holdRate = 3);
    static std::wstring DiagnosticScript();
};

} // namespace xiaochuang
