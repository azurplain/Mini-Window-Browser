#include "InputGuard.h"

#include "CoreLogic.h"

#include <UIAutomation.h>
#include <wrl/client.h>

#include <array>
#include <algorithm>
#include <cwctype>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <vector>

#pragma comment(lib, "uiautomationcore.lib")

namespace xiaochuang {

namespace {

bool IsImeClassName(std::wstring className) {
    std::transform(className.begin(), className.end(), className.begin(),
        [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    return className.find(L"ime") != std::wstring::npos ||
        className.find(L"candidate") != std::wstring::npos ||
        className.find(L"cicero") != std::wstring::npos;
}

std::optional<DWORD> ProcessIntegrityLevel(DWORD processId) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return std::nullopt;

    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
        CloseHandle(process);
        return std::nullopt;
    }

    DWORD byteCount = 0;
    const BOOL sizeQuery = GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &byteCount);
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
    return current.has_value() && foreground.has_value() && *foreground <= *current;
}

} // namespace

struct InputGuard::WorkerState {
    struct Request {
        HWND foreground = nullptr;
        HWND focusedWindow = nullptr;
        DWORD threadId = 0;
        ULONGLONG sequence = 0;
    };

    ~WorkerState() {
        if (requestEvent) CloseHandle(requestEvent);
        if (stopEvent) CloseHandle(stopEvent);
    }

    std::mutex mutex;
    HANDLE stopEvent = nullptr;
    HANDLE requestEvent = nullptr;
    HWND applicationWindow = nullptr;
    Request request;
    bool stopping = false;
};

struct InputGuard::AsyncResult {
    HWND foreground = nullptr;
    HWND focusedWindow = nullptr;
    DWORD threadId = 0;
    ULONGLONG sequence = 0;
    DWORD elapsedMilliseconds = 0;
    DWORD automationProcessId = 0;
    HWND automationWindow = nullptr;
    bool typing = false;
    bool automationAvailable = false;
};

InputGuard* InputGuard::instance_ = nullptr;

InputGuard::~InputGuard() {
    Stop();
}

bool InputGuard::Start(HWND applicationWindow, StateCallback callback) {
    Stop();
    applicationWindow_ = applicationWindow;
    callback_ = std::move(callback);
    enabled_ = true;
    instance_ = this;

    auto state = std::make_shared<WorkerState>();
    state->applicationWindow = applicationWindow;
    state->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    state->requestEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (state->stopEvent && state->requestEvent) {
        auto* threadContext = new (std::nothrow) std::shared_ptr<WorkerState>(state);
        if (threadContext) {
            workerThread_ = CreateThread(nullptr, 0, WorkerThreadProc, threadContext, 0, nullptr);
            if (!workerThread_) delete threadContext;
        }
    }
    if (workerThread_) workerState_ = std::move(state);

    foregroundHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                      nullptr, WinEventProc, 0, 0,
                                      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    focusHook_ = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS,
                                 nullptr, WinEventProc, 0, 0,
                                 WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    Refresh();
    return foregroundHook_ != nullptr && focusHook_ != nullptr && workerThread_ != nullptr;
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
    if (workerState_) {
        {
            const std::lock_guard lock(workerState_->mutex);
            workerState_->stopping = true;
            workerState_->applicationWindow = nullptr;
        }
        SetEvent(workerState_->stopEvent);
        SetEvent(workerState_->requestEvent);
    }
    if (workerThread_) {
        // A broken provider may never return from a UIA call. The worker owns all
        // of its state, so application shutdown never waits indefinitely.
        WaitForSingleObject(workerThread_, 200);
        CloseHandle(workerThread_);
        workerThread_ = nullptr;
    }
    workerState_.reset();
    callback_ = {};
    applicationWindow_ = nullptr;
    nativeEdit_ = nullptr;
    webTyping_ = false;
    typing_ = false;
    externalObservationArmed_ = false;
    focusMessagePending_.store(false);
    foregroundChangePending_.store(false);
    lastQueuedForeground_ = nullptr;
    lastQueuedFocus_ = nullptr;
    automationForeground_ = nullptr;
    automationFocus_ = nullptr;
    latestRequestSequence_ = 0;
    imeTrailingUntil_ = 0;
    statusText_ = L"UI Automation 尚未运行";
    if (instance_ == this) instance_ = nullptr;
}

void InputGuard::SetEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    if (!enabled_) {
        ApplyTyping(false);
    } else {
        Refresh();
    }
}

void InputGuard::SetWebTyping(bool typing) {
    if (webTyping_ == typing) return;
    webTyping_ = typing;
    Refresh();
}

bool InputGuard::Refresh() {
    if (!enabled_) {
        ApplyTyping(false);
        return false;
    }
    DWORD foregroundProcessId = 0;
    if (const HWND foreground = GetForegroundWindow()) {
        GetWindowThreadProcessId(foreground, &foregroundProcessId);
    }
    const bool applicationForeground = foregroundProcessId == GetCurrentProcessId();
    bool systemTyping = DetectSystemTextInput();
    if (!systemTyping && GetTickCount64() < imeTrailingUntil_) systemTyping = true;
    const bool next = ShouldApplyWebTypingGuard(webTyping_, applicationForeground) || systemTyping;
    ApplyTyping(next);
    return typing_;
}

void InputGuard::HandleAsyncResult(LPARAM resultPointer) {
    std::unique_ptr<AsyncResult> result(reinterpret_cast<AsyncResult*>(resultPointer));
    if (!result || !enabled_ || result->sequence < latestRequestSequence_) return;

    DWORD currentProcessId = 0;
    const HWND foreground = GetForegroundWindow();
    const DWORD threadId = foreground
        ? GetWindowThreadProcessId(foreground, &currentProcessId) : 0;
    if (foreground != result->foreground || threadId != result->threadId) return;

    GUITHREADINFO info{sizeof(info)};
    const HWND focusedWindow = GetGUIThreadInfo(threadId, &info) ? info.hwndFocus : nullptr;
    if (result->focusedWindow && focusedWindow && result->focusedWindow != focusedWindow) return;

    // Some providers briefly return an element that belongs to the previously
    // focused application. Never let that unrelated result suppress the
    // current foreground application's hotkeys.
    if (result->typing) {
        const HWND foregroundRoot = foreground ? GetAncestor(foreground, GA_ROOT) : nullptr;
        const HWND automationRoot = result->automationWindow
            ? GetAncestor(result->automationWindow, GA_ROOT) : nullptr;
        const bool matchingWindow = foregroundRoot && automationRoot == foregroundRoot;
        const bool matchingProcess = result->automationProcessId != 0 &&
            result->automationProcessId == currentProcessId;
        if (!matchingWindow && !matchingProcess) return;
    }

    automationForeground_ = result->foreground;
    automationThreadId_ = result->threadId;
    automationFocus_ = result->focusedWindow;
    automationTyping_ = result->typing;
    automationCompletedAt_ = GetTickCount64();
    statusText_ = result->automationAvailable ?
        (L"UI Automation " + std::to_wstring(result->elapsedMilliseconds) + L" ms，结果=" +
         (result->typing ? L"输入中" : L"非输入")) :
        L"UI Automation 不可用";
    Refresh();
}

bool InputGuard::ConsumePendingEvents() {
    focusMessagePending_.store(false);
    return foregroundChangePending_.exchange(false);
}

void CALLBACK InputGuard::WinEventProc(HWINEVENTHOOK, DWORD event, HWND, LONG, LONG, DWORD, DWORD) {
    if (instance_ && instance_->applicationWindow_) {
        instance_->externalObservationArmed_ = true;
        if (event == EVENT_SYSTEM_FOREGROUND) instance_->foregroundChangePending_.store(true);
        if (!instance_->focusMessagePending_.exchange(true) &&
            !PostMessageW(instance_->applicationWindow_, kMessageInputFocusChanged, 0, 0)) {
            instance_->focusMessagePending_.store(false);
        }
    }
}

bool InputGuard::DetectSystemTextInput() {
    if (!applicationWindow_) return false;

    const HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    DWORD foregroundProcessId = 0;
    const DWORD threadId = GetWindowThreadProcessId(foreground, &foregroundProcessId);
    const bool foregroundIsApplication = foregroundProcessId == GetCurrentProcessId();
    if (foregroundIsApplication) {
        const HWND focusedWindow = GetFocus();
        if (!focusedWindow) return false;
        wchar_t className[128]{};
        GetClassNameW(focusedWindow, className, static_cast<int>(std::size(className)));
        return focusedWindow == nativeEdit_ || IsEditableClassName(className);
    }
    if (!ShouldInspectTextInputProcess(false, externalObservationArmed_)) return false;

    GUITHREADINFO info{sizeof(info)};
    const bool hasThreadInfo = GetGUIThreadInfo(threadId, &info) != FALSE;
    const HWND focusedWindow = hasThreadInfo ? info.hwndFocus : nullptr;
    if (focusedWindow) {
        wchar_t className[128]{};
        GetClassNameW(focusedWindow, className, static_cast<int>(std::size(className)));
        if (IsImeClassName(className)) {
            imeTrailingUntil_ = GetTickCount64() + 1500;
            return true;
        }
        if (IsEditableClassName(className)) return true;
    }
    if (hasThreadInfo && info.hwndCaret && info.hwndFocus == info.hwndCaret &&
        IsWindowVisible(info.hwndCaret)) {
        RECT client{};
        if (GetClientRect(info.hwndCaret, &client) &&
            IsUsableTextCaret(info.rcCaret, client, true)) return true;
    }

    if (CanInspectWithUiAutomation(foregroundProcessId)) {
        QueueAutomationCheck(foreground, threadId, focusedWindow);
    }
    const ULONGLONG now = GetTickCount64();
    return automationForeground_ == foreground && automationThreadId_ == threadId &&
        (!automationFocus_ || !focusedWindow || automationFocus_ == focusedWindow) &&
        now - automationCompletedAt_ <= 3000 && automationTyping_;
}

void InputGuard::QueueAutomationCheck(HWND foreground, DWORD threadId, HWND focusedWindow) {
    if (!workerState_) return;
    const ULONGLONG now = GetTickCount64();
    if (lastQueuedForeground_ == foreground && lastQueuedFocus_ == focusedWindow &&
        now - lastQueuedAt_ < 2000) return;
    lastQueuedForeground_ = foreground;
    lastQueuedFocus_ = focusedWindow;
    lastQueuedAt_ = now;

    {
        const std::lock_guard lock(workerState_->mutex);
        if (workerState_->stopping) return;
        workerState_->request.foreground = foreground;
        workerState_->request.focusedWindow = focusedWindow;
        workerState_->request.threadId = threadId;
        workerState_->request.sequence = ++latestRequestSequence_;
    }
    SetEvent(workerState_->requestEvent);
}

void InputGuard::ApplyTyping(bool typing) {
    if (typing_ == typing) return;
    typing_ = typing;
    if (callback_) callback_(typing_);
}

DWORD WINAPI InputGuard::WorkerThreadProc(void* context) {
    std::unique_ptr<std::shared_ptr<WorkerState>> holder(
        static_cast<std::shared_ptr<WorkerState>*>(context));
    if (!holder || !*holder) return 1;
    const std::shared_ptr<WorkerState> state = std::move(*holder);

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Microsoft::WRL::ComPtr<IUIAutomation> automation;
    if (SUCCEEDED(comResult)) {
        const HRESULT automationResult = CoCreateInstance(
            CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(automation.GetAddressOf()));
        if (FAILED(automationResult)) automation.Reset();
    }

    const HANDLE events[] = {state->stopEvent, state->requestEvent};
    for (;;) {
        const DWORD wait = WaitForMultipleObjects(static_cast<DWORD>(std::size(events)), events,
                                                  FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0) break;
        if (wait != WAIT_OBJECT_0 + 1) break;

        WorkerState::Request request;
        {
            const std::lock_guard lock(state->mutex);
            if (state->stopping) break;
            request = state->request;
            ResetEvent(state->requestEvent);
        }
        const ULONGLONG started = GetTickCount64();
        DWORD automationProcessId = 0;
        HWND automationWindow = nullptr;
        const bool typing = automation && DetectWithUiAutomation(
            automation.Get(), &automationProcessId, &automationWindow);
        auto result = std::make_unique<AsyncResult>();
        result->foreground = request.foreground;
        result->focusedWindow = request.focusedWindow;
        result->threadId = request.threadId;
        result->sequence = request.sequence;
        result->elapsedMilliseconds = static_cast<DWORD>(
            std::min<ULONGLONG>(GetTickCount64() - started, MAXDWORD));
        result->automationProcessId = automationProcessId;
        result->automationWindow = automationWindow;
        result->typing = typing;
        result->automationAvailable = automation != nullptr;

        HWND target = nullptr;
        {
            const std::lock_guard lock(state->mutex);
            if (!state->stopping) target = state->applicationWindow;
        }
        if (target && PostMessageW(target, kMessageInputGuardResult, 0,
                                   reinterpret_cast<LPARAM>(result.get()))) {
            result.release();
        }
    }
    automation.Reset();
    if (SUCCEEDED(comResult)) CoUninitialize();
    return 0;
}

bool InputGuard::DetectWithUiAutomation(IUIAutomation* automation,
                                        DWORD* elementProcessId, HWND* elementWindow) {
    if (elementProcessId) *elementProcessId = 0;
    if (elementWindow) *elementWindow = nullptr;
    if (!automation) return false;
    Microsoft::WRL::ComPtr<IUIAutomationElement> element;
    if (FAILED(automation->GetFocusedElement(element.GetAddressOf())) || !element) return false;
    int processId = 0;
    UIA_HWND nativeWindow = nullptr;
    const HRESULT processResult = element->get_CurrentProcessId(&processId);
    const HRESULT windowResult = element->get_CurrentNativeWindowHandle(&nativeWindow);
    if (FAILED(processResult)) processId = 0;
    if (FAILED(windowResult)) nativeWindow = nullptr;
    if (elementProcessId && processId > 0) *elementProcessId = static_cast<DWORD>(processId);
    if (elementWindow) *elementWindow = reinterpret_cast<HWND>(nativeWindow);
    CONTROLTYPEID type = 0;
    if (FAILED(element->get_CurrentControlType(&type))) return false;
    const bool semanticTextControl = type == UIA_EditControlTypeId ||
        type == UIA_ComboBoxControlTypeId;
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
    pattern.Reset();
    const bool textPatternAvailable =
        SUCCEEDED(element->GetCurrentPattern(UIA_TextPatternId, pattern.GetAddressOf())) && pattern;
    return IsLikelyAutomationTextInput(semanticTextControl, keyboardFocusable != FALSE,
                                       writableValuePattern, textPatternAvailable);
}

} // namespace xiaochuang
