#pragma once

#include "AppModel.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace xiaochuang {

constexpr UINT kMessageMouseHotkey = WM_APP + 40;
constexpr UINT kMessageKeyboardHotkey = WM_APP + 42;
constexpr UINT_PTR kHotkeyHoldTimerId = 4401;

enum class HotkeyGesture {
    Trigger,
    Tap,
    HoldStart,
    HoldStop,
};

class HotkeyManager {
public:
    using ActionCallback = std::function<void(HotkeyAction, HotkeyGesture)>;
    using CaptureCallback = std::function<void(UINT, UINT)>;

    HotkeyManager() = default;
    ~HotkeyManager();
    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    bool Start(HWND window, std::array<HotkeyBinding, kHotkeyCount>* bindings,
               ActionCallback callback);
    void Stop();
    void Refresh(bool hidden, bool backgroundMediaHotkeys, bool inputSuppressed);
    bool HandleHotkeyMessage(int identifier);
    bool HandleMouseMessage(WPARAM virtualKey, LPARAM packedState);
    bool HandleRawInput(LPARAM rawInputHandle);
    bool HandleKeyboardMessage(WPARAM virtualKey, LPARAM packedState);
    void Tick();
    void CancelActiveGesture();

    void BeginMouseCapture(CaptureCallback callback);
    void CancelMouseCapture();
    bool IsCapturing() const noexcept { return static_cast<bool>(captureCallback_); }

    const std::vector<std::wstring>& RegistrationErrors() const noexcept { return registrationErrors_; }

private:
    struct PendingGesture {
        HotkeyAction action = HotkeyAction::SeekBackward;
        UINT virtualKey = 0;
        bool mouse = false;
        bool holding = false;
        ULONGLONG startedAt = 0;
    };

    static LRESULT CALLBACK MouseHookProc(int code, WPARAM message, LPARAM data);
    static LRESULT CALLBACK KeyboardHookProc(int code, WPARAM message, LPARAM data);
    bool IsActionActive(HotkeyAction action) const;
    std::optional<HotkeyAction> FindById(int identifier) const;
    std::optional<HotkeyAction> FindMouseAction(UINT virtualKey, UINT modifiers) const;
    std::optional<HotkeyAction> FindKeyboardAction(UINT virtualKey, UINT modifiers) const;
    void TriggerAction(HotkeyAction action);
    void BeginGesture(HotkeyAction action, UINT virtualKey, bool mouse);
    void ReleaseGesture();
    static UINT CurrentModifiers();
    static bool IsMouseKey(UINT virtualKey);
    static std::optional<size_t> MouseStateIndex(UINT virtualKey);

    static HotkeyManager* instance_;
    HWND window_ = nullptr;
    HHOOK mouseHook_ = nullptr;
    HHOOK keyboardHook_ = nullptr;
    bool rawMouseRegistered_ = false;
    std::array<HotkeyBinding, kHotkeyCount>* bindings_ = nullptr;
    ActionCallback callback_;
    CaptureCallback captureCallback_;
    std::optional<PendingGesture> pending_;
    std::vector<std::wstring> registrationErrors_;
    bool hidden_ = false;
    bool backgroundMediaHotkeys_ = false;
    bool inputSuppressed_ = false;
    std::array<bool, 3> mouseDown_{};
    std::array<bool, 3> hookMouseCaptured_{};
    std::array<bool, 256> keyboardDown_{};
    ULONGLONG lastWindowActionAt_ = 0;
};

} // namespace xiaochuang
