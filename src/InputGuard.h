#pragma once

#include <windows.h>

#include <functional>

struct IUIAutomation;

namespace xiaochuang {

constexpr UINT kMessageInputFocusChanged = WM_APP + 41;

class InputGuard {
public:
    using StateCallback = std::function<void(bool)>;

    InputGuard() = default;
    ~InputGuard();
    InputGuard(const InputGuard&) = delete;
    InputGuard& operator=(const InputGuard&) = delete;

    bool Start(HWND applicationWindow, StateCallback callback);
    void Stop();
    void SetNativeEdit(HWND edit) noexcept { nativeEdit_ = edit; }
    void SetWebTyping(bool typing);
    bool Refresh();
    bool IsTyping() const noexcept { return typing_; }

private:
    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND window,
                                      LONG objectId, LONG childId, DWORD threadId, DWORD time);
    bool DetectSystemTextInput() const;
    bool DetectWithUiAutomation() const;

    static InputGuard* instance_;
    HWND applicationWindow_ = nullptr;
    HWND nativeEdit_ = nullptr;
    HWINEVENTHOOK foregroundHook_ = nullptr;
    HWINEVENTHOOK focusHook_ = nullptr;
    IUIAutomation* automation_ = nullptr;
    StateCallback callback_;
    bool webTyping_ = false;
    bool typing_ = false;
    bool externalObservationArmed_ = false;
};

} // namespace xiaochuang
