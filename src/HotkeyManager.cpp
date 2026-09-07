#include "HotkeyManager.h"

#include "ConfigStore.h"
#include "CoreLogic.h"

#include <cstddef>

namespace xiaochuang {

namespace {
constexpr wchar_t kRawInputWindowClass[] = L"XiaoChuangRawInputV141";
constexpr UINT kUpdateRawInputRegistration = WM_APP + 1;
}

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
    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandleW(nullptr), 0);
    rawInputReadyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (rawInputReadyEvent_) {
        rawInputThread_ = CreateThread(nullptr, 0, RawInputThreadProc, this, 0, &rawInputThreadId_);
        if (rawInputThread_) {
            WaitForSingleObject(rawInputReadyEvent_, 500);
        }
    }
    Refresh(false, false, false);
    return rawInputWindow_.load() != nullptr && keyboardHook_ != nullptr;
}

void HotkeyManager::Stop() {
    CancelActiveGesture();
    if (window_) {
        for (const HotkeyBinding& binding : bindings_ ? *bindings_ : DefaultHotkeys()) {
            UnregisterHotKey(window_, binding.id);
        }
    }
    if (keyboardHook_) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
    if (rawInputThread_) {
        if (const HWND rawWindow = rawInputWindow_.load()) PostMessageW(rawWindow, WM_CLOSE, 0, 0);
        else if (rawInputThreadId_) PostThreadMessageW(rawInputThreadId_, WM_QUIT, 0, 0);
        WaitForSingleObject(rawInputThread_, 5000);
        CloseHandle(rawInputThread_);
        rawInputThread_ = nullptr;
    }
    if (rawInputReadyEvent_) {
        CloseHandle(rawInputReadyEvent_);
        rawInputReadyEvent_ = nullptr;
    }
    rawInputThreadId_ = 0;
    rawInputWindow_.store(nullptr);
    rawMouseRegistered_.store(false);
    if (instance_ == this) instance_ = nullptr;
    captureCallback_ = {};
    callback_ = {};
    bindings_ = nullptr;
    window_ = nullptr;
    mouseDown_.fill(false);
    keyboardDown_.fill(false);
    lastWindowActionAt_.fill(0);
    suppressEscapeForFocusedWebView_.store(false);
}

void HotkeyManager::Refresh(bool hidden, bool backgroundMediaHotkeys, bool inputSuppressed) {
    hidden_ = hidden;
    backgroundMediaHotkeys_ = backgroundMediaHotkeys;
    inputSuppressed_ = inputSuppressed;
    registrationErrors_.clear();
    if (!window_ || !bindings_) return;

    CancelActiveGesture();
    // A foreground transition can make the low-level key-up arrive after the
    // registration set changes. Do not let that stale state block the next
    // global seek press when focus is outside the browser.
    keyboardDown_.fill(false);
    mouseDown_.fill(false);
    for (const HotkeyBinding& binding : *bindings_) {
        UnregisterHotKey(window_, binding.id);
    }
    for (size_t index = 0; index < kHotkeyCount; ++index) {
        const HotkeyAction action = static_cast<HotkeyAction>(index);
        const HotkeyBinding& binding = (*bindings_)[index];
        if (inputSuppressed_) continue;
        if (!binding.enabled || !IsActionActive(action) || IsMouseKey(binding.virtualKey)) continue;
        if (!RegisterHotKey(window_, binding.id, binding.modifiers | MOD_NOREPEAT, binding.virtualKey)) {
            registrationErrors_.push_back(binding.displayName + L"（" + HotkeyDisplayText(binding) + L"）");
        }
    }
    UpdateRawMouseRegistration();
}

bool HotkeyManager::HandleHotkeyMessage(int identifier) {
    const auto action = FindById(identifier);
    if (!action || !IsActionActive(*action)) return false;
    const HotkeyBinding& binding = (*bindings_)[HotkeyIndex(*action)];
    if (!binding.enabled) return false;
    if (*action == HotkeyAction::SeekBackward || *action == HotkeyAction::SeekForward) {
        // When focus is outside the browser, the low-level hook owns both the
        // key-down and key-up messages. Ignore a duplicate WM_HOTKEY so it
        // cannot replace that message-tracked gesture with an async-state one.
        const bool hookGesturePending = pending_ && pending_->releaseByMessage &&
            !pending_->mouse && pending_->virtualKey == binding.virtualKey;
        if (hookGesturePending ||
            (binding.virtualKey < keyboardDown_.size() && keyboardDown_[binding.virtualKey])) {
            return true;
        }
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
        if (mouseDown_[*stateIndex]) {
            if ((GetAsyncKeyState(static_cast<int>(virtualKey)) & 0x8000) != 0) return false;
            mouseDown_[*stateIndex] = false;
        }
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

void HotkeyManager::HandleRawInput(LPARAM rawInputHandle) const {
    RAWINPUT input{};
    UINT size = sizeof(input);
    const UINT copied = GetRawInputData(reinterpret_cast<HRAWINPUT>(rawInputHandle), RID_INPUT,
                                        &input, &size, sizeof(RAWINPUTHEADER));
    if (copied == static_cast<UINT>(-1) || input.header.dwType != RIM_TYPEMOUSE) return;
    const USHORT buttonFlags = input.data.mouse.usButtonFlags;
    if ((buttonFlags & (RI_MOUSE_BUTTON_3_DOWN | RI_MOUSE_BUTTON_3_UP |
                        RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP |
                        RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP)) == 0) {
        return;
    }
    const UINT modifiers = CurrentModifiers();
    for (const MouseButtonTransition& transition :
         DecodeRawMouseButtons(buttonFlags)) {
        const LPARAM packed = static_cast<LPARAM>((transition.down ? 1U : 0U) |
                                                   (modifiers << 16U));
        PostMessageW(window_, kMessageMouseHotkey, transition.virtualKey, packed);
    }
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
        BeginGesture(*action, virtualKey, false, true);
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
    if (!pending_->mouse && !pending_->releaseByMessage &&
        (GetAsyncKeyState(static_cast<int>(pending_->virtualKey)) & 0x8000) == 0) {
        ReleaseGesture();
        return;
    }
    if (pending_->releaseByMessage && ShouldForceReleaseMessageGesture(
            GetTickCount64() - pending_->startedAt,
            (GetAsyncKeyState(static_cast<int>(pending_->virtualKey)) & 0x8000) != 0)) {
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
    UpdateRawMouseRegistration();
}

void HotkeyManager::CancelMouseCapture() {
    captureCallback_ = {};
    UpdateRawMouseRegistration();
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
    const auto* keyboard = reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
    const UINT virtualKey = keyboard->vkCode;
    if (virtualKey == VK_ESCAPE && instance_->suppressEscapeForFocusedWebView_.load() &&
        foregroundProcess == GetCurrentProcessId() && !instance_->captureCallback_) {
        return 1;
    }
    if (foregroundProcess == GetCurrentProcessId()) {
        return CallNextHookEx(instance_->keyboardHook_, code, message, data);
    }

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
    if (instance_->keyboardDown_[virtualKey]) {
        const bool matchingGesture = instance_->pending_ && !instance_->pending_->mouse &&
            instance_->pending_->virtualKey == virtualKey;
        const bool asynchronousDown =
            (GetAsyncKeyState(static_cast<int>(virtualKey)) & 0x8000) != 0;
        if (!ShouldRecoverStaleKeyDown(true, matchingGesture, asynchronousDown)) return 1;
        instance_->keyboardDown_[virtualKey] = false;
    }

    const UINT modifiers = CurrentModifiers();
    if (!instance_->FindKeyboardAction(virtualKey, modifiers)) {
        return CallNextHookEx(instance_->keyboardHook_, code, message, data);
    }
    instance_->keyboardDown_[virtualKey] = true;
    const LPARAM packed = static_cast<LPARAM>(1U | (modifiers << 16U));
    PostMessageW(instance_->window_, kMessageKeyboardHotkey, virtualKey, packed);
    return 1;
}

DWORD WINAPI HotkeyManager::RawInputThreadProc(void* context) {
    auto* manager = static_cast<HotkeyManager*>(context);
    return manager ? manager->RunRawInputThread() : 0;
}

LRESULT CALLBACK HotkeyManager::RawInputWindowProc(HWND window, UINT message,
                                                    WPARAM wParam, LPARAM lParam) {
    auto* manager = reinterpret_cast<HotkeyManager*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        manager = static_cast<HotkeyManager*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(manager));
    }
    if (message == WM_INPUT && manager) {
        manager->HandleRawInput(lParam);
        return DefWindowProcW(window, message, wParam, lParam);
    }
    if (message == kUpdateRawInputRegistration && manager) {
        manager->SetRawMouseRegistration(wParam != 0);
        return 0;
    }
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

DWORD HotkeyManager::RunRawInputThread() {
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = RawInputWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kRawInputWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (rawInputReadyEvent_) SetEvent(rawInputReadyEvent_);
        return 1;
    }

    const HWND rawWindow = CreateWindowExW(0, kRawInputWindowClass, L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, windowClass.hInstance, this);
    rawInputWindow_.store(rawWindow);
    if (rawInputReadyEvent_) SetEvent(rawInputReadyEvent_);
    if (!rawWindow) {
        if (rawWindow) DestroyWindow(rawWindow);
        rawInputWindow_.store(nullptr);
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    SetRawMouseRegistration(false);
    rawInputWindow_.store(nullptr);
    return 0;
}

void HotkeyManager::UpdateRawMouseRegistration() {
    if (const HWND rawWindow = rawInputWindow_.load()) {
        PostMessageW(rawWindow, kUpdateRawInputRegistration,
                     NeedsRawMouseInput() ? TRUE : FALSE, 0);
    }
}

void HotkeyManager::SetRawMouseRegistration(bool enabled) {
    if (enabled == rawMouseRegistered_.load()) return;
    RAWINPUTDEVICE rawMouse{};
    rawMouse.usUsagePage = 0x01;
    rawMouse.usUsage = 0x02;
    rawMouse.dwFlags = enabled ? RIDEV_INPUTSINK : RIDEV_REMOVE;
    rawMouse.hwndTarget = enabled ? rawInputWindow_.load() : nullptr;
    const bool succeeded = RegisterRawInputDevices(&rawMouse, 1, sizeof(rawMouse)) == TRUE;
    if (succeeded) rawMouseRegistered_.store(enabled);
}

bool HotkeyManager::NeedsRawMouseInput() const {
    if (captureCallback_ || !bindings_) return static_cast<bool>(captureCallback_);
    for (size_t index = 0; index < kHotkeyCount; ++index) {
        const HotkeyAction action = static_cast<HotkeyAction>(index);
        const HotkeyBinding& binding = (*bindings_)[index];
        if (binding.enabled && IsActionActive(action) && IsMouseKey(binding.virtualKey)) return true;
    }
    return false;
}

bool HotkeyManager::IsActionActive(HotkeyAction action) const {
    if (inputSuppressed_) return false;
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
        if (binding.enabled && IsActionActive(action) && binding.virtualKey == virtualKey &&
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
        if (binding.enabled && IsActionActive(action) && binding.virtualKey == virtualKey &&
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
        const size_t index = action == HotkeyAction::Immersion ? 0U : 1U;
        if (lastWindowActionAt_[index] != 0 && now - lastWindowActionAt_[index] < 250) return;
        lastWindowActionAt_[index] = now;
    }
    callback_(action, HotkeyGesture::Trigger);
}

void HotkeyManager::BeginGesture(HotkeyAction action, UINT virtualKey, bool mouse,
                                 bool releaseByMessage) {
    CancelActiveGesture();
    pending_ = PendingGesture{action, virtualKey, mouse, releaseByMessage, false, GetTickCount64()};
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
