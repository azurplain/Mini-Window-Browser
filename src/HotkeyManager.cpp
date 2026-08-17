#include "HotkeyManager.h"

#include "ConfigStore.h"
#include "CoreLogic.h"

#include <cstddef>
#include <vector>

namespace xiaochuang {

HotkeyManager* HotkeyManager::instance_ = nullptr;

HotkeyManager::~HotkeyManager() {
    Stop();
}

bool HotkeyManager::Start(HWND window, std::array<HotkeyBinding, kHotkeyCount>* bindings,
                          ActionCallback callback) {
    Stop();
    window_ = window;
    bindings_ = bindings;
    callback_ = std::move(callback);
    instance_ = this;
    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, GetModuleHandleW(nullptr), 0);
    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandleW(nullptr), 0);
    RAWINPUTDEVICE rawMouse{};
    rawMouse.usUsagePage = 0x01;
    rawMouse.usUsage = 0x02;
    rawMouse.dwFlags = RIDEV_INPUTSINK;
    rawMouse.hwndTarget = window_;
    rawMouseRegistered_ = RegisterRawInputDevices(&rawMouse, 1, sizeof(rawMouse)) == TRUE;
    Refresh(false, false, false);
    return (mouseHook_ != nullptr || rawMouseRegistered_) && keyboardHook_ != nullptr;
}

void HotkeyManager::Stop() {
    CancelActiveGesture();
    if (window_) {
        for (const HotkeyBinding& binding : bindings_ ? *bindings_ : DefaultHotkeys()) {
            UnregisterHotKey(window_, binding.id);
        }
    }
    if (mouseHook_) {
        UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
    }
    if (keyboardHook_) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
    if (rawMouseRegistered_) {
        RAWINPUTDEVICE rawMouse{};
        rawMouse.usUsagePage = 0x01;
        rawMouse.usUsage = 0x02;
        rawMouse.dwFlags = RIDEV_REMOVE;
        rawMouse.hwndTarget = nullptr;
        RegisterRawInputDevices(&rawMouse, 1, sizeof(rawMouse));
        rawMouseRegistered_ = false;
    }
    if (instance_ == this) instance_ = nullptr;
    captureCallback_ = {};
    callback_ = {};
    bindings_ = nullptr;
    window_ = nullptr;
    mouseDown_.fill(false);
    hookMouseCaptured_.fill(false);
    keyboardDown_.fill(false);
    lastWindowActionAt_ = 0;
}

void HotkeyManager::Refresh(bool hidden, bool backgroundMediaHotkeys, bool inputSuppressed) {
    hidden_ = hidden;
    backgroundMediaHotkeys_ = backgroundMediaHotkeys;
    inputSuppressed_ = inputSuppressed;
    registrationErrors_.clear();
    if (!window_ || !bindings_) return;

    CancelActiveGesture();
    for (const HotkeyBinding& binding : *bindings_) {
        UnregisterHotKey(window_, binding.id);
    }
    for (size_t index = 0; index < kHotkeyCount; ++index) {
        const HotkeyAction action = static_cast<HotkeyAction>(index);
        const HotkeyBinding& binding = (*bindings_)[index];
        if (inputSuppressed_ && action != HotkeyAction::ToggleHidden) continue;
        if (!IsActionActive(action) || IsMouseKey(binding.virtualKey)) continue;
        if (!RegisterHotKey(window_, binding.id, binding.modifiers | MOD_NOREPEAT, binding.virtualKey)) {
            registrationErrors_.push_back(binding.displayName + L"（" + HotkeyDisplayText(binding) + L"）");
        }
    }
}

bool HotkeyManager::HandleHotkeyMessage(int identifier) {
    const auto action = FindById(identifier);
    if (!action || !IsActionActive(*action)) return false;
    const HotkeyBinding& binding = (*bindings_)[HotkeyIndex(*action)];
    if (*action == HotkeyAction::SeekBackward || *action == HotkeyAction::SeekForward) {
        BeginGesture(*action, binding.virtualKey, false);
    } else {
        TriggerAction(*action);
    }
    return true;
}

bool HotkeyManager::HandleMouseMessage(WPARAM virtualKeyValue, LPARAM packedState) {
    const UINT virtualKey = static_cast<UINT>(virtualKeyValue);
    const bool down = (static_cast<UINT_PTR>(packedState) & 1U) != 0;
    const UINT modifiers = static_cast<UINT>((static_cast<UINT_PTR>(packedState) >> 16U) & 0xFFFFU);
    const auto stateIndex = MouseStateIndex(virtualKey);
    if (!stateIndex) return false;
    if (down) {
        if (mouseDown_[*stateIndex]) return false;
        mouseDown_[*stateIndex] = true;
    } else {
        if (!mouseDown_[*stateIndex]) return false;
        mouseDown_[*stateIndex] = false;
    }
    if (captureCallback_ && down) {
        CaptureCallback callback = std::move(captureCallback_);
        captureCallback_ = {};
        callback(virtualKey, modifiers);
        return true;
    }
    if (!down && pending_ && pending_->mouse && pending_->virtualKey == virtualKey) {
        ReleaseGesture();
        return true;
    }
    if (!down) return false;
    const auto action = FindMouseAction(virtualKey, modifiers);
    if (!action) return false;
    if (*action == HotkeyAction::SeekBackward || *action == HotkeyAction::SeekForward) {
        BeginGesture(*action, virtualKey, true);
    } else {
        TriggerAction(*action);
    }
    return true;
}

bool HotkeyManager::HandleRawInput(LPARAM rawInputHandle) {
    UINT size = 0;
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(rawInputHandle), RID_INPUT, nullptr,
                        &size, sizeof(RAWINPUTHEADER)) != 0 || size < sizeof(RAWINPUTHEADER)) {
        return false;
    }
    std::vector<std::byte> bytes(size);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(rawInputHandle), RID_INPUT, bytes.data(),
                        &size, sizeof(RAWINPUTHEADER)) != size) {
        return false;
    }
    const auto* input = reinterpret_cast<const RAWINPUT*>(bytes.data());
    if (input->header.dwType != RIM_TYPEMOUSE) return false;

    const UINT modifiers = CurrentModifiers();
    bool handled = false;
    for (const MouseButtonTransition& transition :
         DecodeRawMouseButtons(input->data.mouse.usButtonFlags)) {
        const LPARAM packed = static_cast<LPARAM>((transition.down ? 1U : 0U) |
                                                   (modifiers << 16U));
        handled = HandleMouseMessage(transition.virtualKey, packed) || handled;
    }
    return handled;
}

bool HotkeyManager::HandleKeyboardMessage(WPARAM virtualKeyValue, LPARAM packedState) {
    const UINT virtualKey = static_cast<UINT>(virtualKeyValue);
    const bool down = (static_cast<UINT_PTR>(packedState) & 1U) != 0;
    const UINT modifiers = static_cast<UINT>((static_cast<UINT_PTR>(packedState) >> 16U) & 0xFFFFU);
    if (!down && pending_ && !pending_->mouse && pending_->virtualKey == virtualKey) {
        ReleaseGesture();
        return true;
    }
    if (!down) return false;
    const auto action = FindKeyboardAction(virtualKey, modifiers);
    if (!action) return false;
    if (*action == HotkeyAction::SeekBackward || *action == HotkeyAction::SeekForward) {
        BeginGesture(*action, virtualKey, false);
    } else {
        TriggerAction(*action);
    }
    return true;
}

void HotkeyManager::Tick() {
    if (!pending_) {
        if (window_) KillTimer(window_, kHotkeyHoldTimerId);
        return;
    }
    if (!pending_->mouse && (GetAsyncKeyState(static_cast<int>(pending_->virtualKey)) & 0x8000) == 0) {
        ReleaseGesture();
        return;
    }
    if (!pending_->holding && GetTickCount64() - pending_->startedAt >= 400) {
        pending_->holding = true;
        if (callback_) callback_(pending_->action, HotkeyGesture::HoldStart);
    }
}

void HotkeyManager::CancelActiveGesture() {
    if (!pending_) return;
    if (pending_->holding && callback_) callback_(pending_->action, HotkeyGesture::HoldStop);
    pending_.reset();
    if (window_) KillTimer(window_, kHotkeyHoldTimerId);
}

void HotkeyManager::BeginMouseCapture(CaptureCallback callback) {
    CancelActiveGesture();
    captureCallback_ = std::move(callback);
}

void HotkeyManager::CancelMouseCapture() {
    captureCallback_ = {};
}

LRESULT CALLBACK HotkeyManager::MouseHookProc(int code, WPARAM message, LPARAM data) {
    if (code != HC_ACTION || !instance_ || !instance_->window_) {
        return CallNextHookEx(instance_ ? instance_->mouseHook_ : nullptr, code, message, data);
    }
    UINT virtualKey = 0;
    bool down = false;
    bool up = false;
    if (message == WM_MBUTTONDOWN || message == WM_NCMBUTTONDOWN) {
        virtualKey = VK_MBUTTON; down = true;
    } else if (message == WM_MBUTTONUP || message == WM_NCMBUTTONUP) {
        virtualKey = VK_MBUTTON; up = true;
    } else if (message == WM_XBUTTONDOWN || message == WM_NCXBUTTONDOWN ||
               message == WM_XBUTTONUP || message == WM_NCXBUTTONUP) {
        const auto* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
        virtualKey = HIWORD(mouse->mouseData) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
        down = message == WM_XBUTTONDOWN || message == WM_NCXBUTTONDOWN;
        up = !down;
    }
    if (virtualKey == 0 || (!down && !up)) {
        return CallNextHookEx(instance_->mouseHook_, code, message, data);
    }

    const auto stateIndex = MouseStateIndex(virtualKey);
    if (!stateIndex) return CallNextHookEx(instance_->mouseHook_, code, message, data);
    const UINT modifiers = CurrentModifiers();
    const bool capture = instance_->captureCallback_ && down;
    const bool actionMatch = down && instance_->FindMouseAction(virtualKey, modifiers).has_value();
    if (down && (capture || actionMatch)) {
        if (!instance_->hookMouseCaptured_[*stateIndex]) {
            instance_->hookMouseCaptured_[*stateIndex] = true;
            const LPARAM packed = static_cast<LPARAM>(1U | (modifiers << 16U));
            PostMessageW(instance_->window_, kMessageMouseHotkey, virtualKey, packed);
        }
        return 1;
    }
    if (up && instance_->hookMouseCaptured_[*stateIndex]) {
        instance_->hookMouseCaptured_[*stateIndex] = false;
        const LPARAM packed = static_cast<LPARAM>((down ? 1U : 0U) | (modifiers << 16U));
        PostMessageW(instance_->window_, kMessageMouseHotkey, virtualKey, packed);
        return 1;
    }
    return CallNextHookEx(instance_->mouseHook_, code, message, data);
}

LRESULT CALLBACK HotkeyManager::KeyboardHookProc(int code, WPARAM message, LPARAM data) {
    if (code != HC_ACTION || !instance_ || !instance_->window_ || !data) {
        return CallNextHookEx(instance_ ? instance_->keyboardHook_ : nullptr, code, message, data);
    }
    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
    if (!down && !up) return CallNextHookEx(instance_->keyboardHook_, code, message, data);

    // Let controls in this process receive ordinary keyboard messages (especially
    // the hotkey-capture buttons). RegisterHotKey remains the primary path there.
    DWORD foregroundProcess = 0;
    if (const HWND foreground = GetForegroundWindow()) {
        GetWindowThreadProcessId(foreground, &foregroundProcess);
    }
    if (foregroundProcess == GetCurrentProcessId()) {
        return CallNextHookEx(instance_->keyboardHook_, code, message, data);
    }

    const auto* keyboard = reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
    const UINT virtualKey = keyboard->vkCode;
    if (virtualKey >= instance_->keyboardDown_.size()) {
        return CallNextHookEx(instance_->keyboardHook_, code, message, data);
    }
    if (up) {
        if (!instance_->keyboardDown_[virtualKey]) {
            return CallNextHookEx(instance_->keyboardHook_, code, message, data);
        }
        instance_->keyboardDown_[virtualKey] = false;
        PostMessageW(instance_->window_, kMessageKeyboardHotkey, virtualKey, 0);
        return 1;
    }
    if (instance_->keyboardDown_[virtualKey]) return 1;

    const UINT modifiers = CurrentModifiers();
    if (!instance_->FindKeyboardAction(virtualKey, modifiers)) {
        return CallNextHookEx(instance_->keyboardHook_, code, message, data);
    }
    instance_->keyboardDown_[virtualKey] = true;
    const LPARAM packed = static_cast<LPARAM>(1U | (modifiers << 16U));
    PostMessageW(instance_->window_, kMessageKeyboardHotkey, virtualKey, packed);
    return 1;
}

bool HotkeyManager::IsActionActive(HotkeyAction action) const {
    if (inputSuppressed_ && action != HotkeyAction::ToggleHidden) return false;
    if (!hidden_) return true;
    if (action == HotkeyAction::ToggleHidden) return true;
    return backgroundMediaHotkeys_ && IsMediaAction(action);
}

std::optional<HotkeyAction> HotkeyManager::FindById(int identifier) const {
    if (!bindings_) return std::nullopt;
    for (size_t index = 0; index < kHotkeyCount; ++index) {
        if ((*bindings_)[index].id == identifier) return static_cast<HotkeyAction>(index);
    }
    return std::nullopt;
}

std::optional<HotkeyAction> HotkeyManager::FindMouseAction(UINT virtualKey, UINT modifiers) const {
    if (!bindings_) return std::nullopt;
    for (size_t index = 0; index < kHotkeyCount; ++index) {
        const HotkeyAction action = static_cast<HotkeyAction>(index);
        const HotkeyBinding& binding = (*bindings_)[index];
        if (IsActionActive(action) && binding.virtualKey == virtualKey &&
            binding.modifiers == modifiers && IsMouseKey(virtualKey)) {
            return action;
        }
    }
    return std::nullopt;
}

std::optional<HotkeyAction> HotkeyManager::FindKeyboardAction(UINT virtualKey, UINT modifiers) const {
    if (!bindings_) return std::nullopt;
    for (size_t index = 0; index < kHotkeyCount; ++index) {
        const HotkeyAction action = static_cast<HotkeyAction>(index);
        const HotkeyBinding& binding = (*bindings_)[index];
        if (IsActionActive(action) && binding.virtualKey == virtualKey &&
            binding.modifiers == modifiers && !IsMouseKey(virtualKey)) {
            return action;
        }
    }
    return std::nullopt;
}

void HotkeyManager::TriggerAction(HotkeyAction action) {
    if (!callback_) return;
    if (action == HotkeyAction::Immersion || action == HotkeyAction::ToggleHidden) {
        const ULONGLONG now = GetTickCount64();
        if (lastWindowActionAt_ != 0 && now - lastWindowActionAt_ < 250) return;
        lastWindowActionAt_ = now;
    }
    callback_(action, HotkeyGesture::Trigger);
}

void HotkeyManager::BeginGesture(HotkeyAction action, UINT virtualKey, bool mouse) {
    CancelActiveGesture();
    pending_ = PendingGesture{action, virtualKey, mouse, false, GetTickCount64()};
    SetTimer(window_, kHotkeyHoldTimerId, 20, nullptr);
}

void HotkeyManager::ReleaseGesture() {
    if (!pending_) return;
    const PendingGesture gesture = *pending_;
    pending_.reset();
    if (window_) KillTimer(window_, kHotkeyHoldTimerId);
    if (!callback_) return;
    callback_(gesture.action, gesture.holding ? HotkeyGesture::HoldStop : HotkeyGesture::Tap);
}

UINT HotkeyManager::CurrentModifiers() {
    UINT modifiers = 0;
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) modifiers |= MOD_CONTROL;
    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) modifiers |= MOD_SHIFT;
    if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) modifiers |= MOD_ALT;
    return modifiers;
}

bool HotkeyManager::IsMouseKey(UINT virtualKey) {
    return virtualKey == VK_MBUTTON || virtualKey == VK_XBUTTON1 || virtualKey == VK_XBUTTON2;
}

std::optional<size_t> HotkeyManager::MouseStateIndex(UINT virtualKey) {
    if (virtualKey == VK_MBUTTON) return 0;
    if (virtualKey == VK_XBUTTON1) return 1;
    if (virtualKey == VK_XBUTTON2) return 2;
    return std::nullopt;
}

} // namespace xiaochuang
