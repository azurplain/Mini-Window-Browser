#include "InputGuard.h"

#include "CoreLogic.h"

#include <UIAutomation.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>
#include <optional>
#include <string>
#include <vector>

#pragma comment(lib, "uiautomationcore.lib")

namespace xiaochuang {

namespace {

std::optional<DWORD> ProcessIntegrityLevel(DWORD processId) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return std::nullopt;

    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
        CloseHandle(process);
        return std::nullopt;
    }

    DWORD byteCount = 0;
    const BOOL sizeQuery = GetTokenInformation(
        token, TokenIntegrityLevel, nullptr, 0, &byteCount);
    if (sizeQuery != FALSE || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        CloseHandle(token);
        CloseHandle(process);
        return std::nullopt;
    }
    std::vector<BYTE> buffer(byteCount);
    DWORD integrityLevel = 0;
    if (byteCount != 0 &&
        GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), byteCount, &byteCount)) {
        const auto* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buffer.data());
        if (label->Label.Sid && IsValidSid(label->Label.Sid)) {
            const PUCHAR count = GetSidSubAuthorityCount(label->Label.Sid);
            if (count && *count != 0) {
                const PDWORD level = GetSidSubAuthority(label->Label.Sid, *count - 1);
                if (level) integrityLevel = *level;
            }
        }
    }
    CloseHandle(token);
    CloseHandle(process);
    if (integrityLevel == 0) return std::nullopt;
    return integrityLevel;
}

bool CanInspectWithUiAutomation(DWORD foregroundProcessId) {
    const auto current = ProcessIntegrityLevel(GetCurrentProcessId());
    const auto foreground = ProcessIntegrityLevel(foregroundProcessId);
    // UI Automation calls across an elevation boundary are both unreliable and
    // capable of blocking the browser UI. Keep RegisterHotKey active there; the
    // system can still dispatch keyboard hotkeys even though our low-level hooks
    // cannot observe the elevated process.
    return current.has_value() && foreground.has_value() && *foreground <= *current;
}

} // namespace

InputGuard* InputGuard::instance_ = nullptr;

InputGuard::~InputGuard() {
    Stop();
}

bool InputGuard::Start(HWND applicationWindow, StateCallback callback) {
    Stop();
    applicationWindow_ = applicationWindow;
    callback_ = std::move(callback);
    instance_ = this;
    const HRESULT automationResult = CoCreateInstance(
        CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation_));
    if (FAILED(automationResult)) automation_ = nullptr;
    foregroundHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                      nullptr, WinEventProc, 0, 0,
                                      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    focusHook_ = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS,
                                 nullptr, WinEventProc, 0, 0,
                                 WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    Refresh();
    return foregroundHook_ != nullptr && focusHook_ != nullptr;
}

void InputGuard::Stop() {
    if (foregroundHook_) {
        UnhookWinEvent(foregroundHook_);
        foregroundHook_ = nullptr;
    }
    if (focusHook_) {
        UnhookWinEvent(focusHook_);
        focusHook_ = nullptr;
    }
    if (automation_) {
        automation_->Release();
        automation_ = nullptr;
    }
    callback_ = {};
    applicationWindow_ = nullptr;
    nativeEdit_ = nullptr;
    webTyping_ = false;
    typing_ = false;
    externalObservationArmed_ = false;
    if (instance_ == this) instance_ = nullptr;
}

void InputGuard::SetWebTyping(bool typing) {
    if (webTyping_ == typing) return;
    webTyping_ = typing;
    Refresh();
}

bool InputGuard::Refresh() {
    const bool next = webTyping_ || DetectSystemTextInput();
    if (next == typing_) return typing_;
    typing_ = next;
    if (callback_) callback_(typing_);
    return typing_;
}

void CALLBACK InputGuard::WinEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (instance_ && instance_->applicationWindow_) {
        instance_->externalObservationArmed_ = true;
        PostMessageW(instance_->applicationWindow_, kMessageInputFocusChanged, 0, 0);
    }
}

bool InputGuard::DetectSystemTextInput() const {
    if (!applicationWindow_) return false;
    if (nativeEdit_ && GetFocus() == nativeEdit_) return true;

    const HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    if (foreground == applicationWindow_ || IsChild(applicationWindow_, foreground)) {
        const HWND focused = GetFocus();
        if (focused == nativeEdit_) return true;
    }

    DWORD foregroundProcessId = 0;
    const DWORD threadId = GetWindowThreadProcessId(foreground, &foregroundProcessId);
    const bool foregroundIsApplication = foregroundProcessId == GetCurrentProcessId();
    if (!ShouldInspectTextInputProcess(foregroundIsApplication, externalObservationArmed_)) {
        // A launcher or editor may remain foreground while Windows blocks a new
        // process from stealing focus. Do not inherit that stale typing state.
        return false;
    }
    GUITHREADINFO info{sizeof(info)};
    if (GetGUIThreadInfo(threadId, &info)) {
        if (info.hwndCaret && IsWindowVisible(info.hwndCaret)) return true;
        if (info.hwndFocus) {
            wchar_t className[128]{};
            GetClassNameW(info.hwndFocus, className, static_cast<int>(std::size(className)));
            if (IsEditableClassName(className)) return true;
        }
    }
    // WebView inputs report their focus through the injected focusin/focusout bridge.
    // Avoid a synchronous UI Automation round-trip for ordinary pages in our process.
    if (foregroundIsApplication) return false;
    if (!CanInspectWithUiAutomation(foregroundProcessId)) return false;
    return DetectWithUiAutomation();
}

bool InputGuard::DetectWithUiAutomation() const {
    if (!automation_) return false;
    Microsoft::WRL::ComPtr<IUIAutomationElement> element;
    if (FAILED(automation_->GetFocusedElement(element.GetAddressOf())) || !element) return false;
    CONTROLTYPEID type = 0;
    if (FAILED(element->get_CurrentControlType(&type))) return false;
    const bool semanticTextControl = type == UIA_EditControlTypeId || type == UIA_ComboBoxControlTypeId;
    BOOL keyboardFocusable = FALSE;
    element->get_CurrentIsKeyboardFocusable(&keyboardFocusable);
    bool writableValuePattern = false;
    Microsoft::WRL::ComPtr<IUnknown> pattern;
    if (SUCCEEDED(element->GetCurrentPattern(UIA_ValuePatternId, pattern.GetAddressOf())) && pattern) {
        Microsoft::WRL::ComPtr<IUIAutomationValuePattern> valuePattern;
        if (SUCCEEDED(pattern.As(&valuePattern)) && valuePattern) {
            BOOL readOnly = TRUE;
            if (SUCCEEDED(valuePattern->get_CurrentIsReadOnly(&readOnly))) {
                writableValuePattern = readOnly == FALSE;
            }
        }
    }
    return IsLikelyAutomationTextInput(semanticTextControl, keyboardFocusable != FALSE,
                                       writableValuePattern);
}

} // namespace xiaochuang
