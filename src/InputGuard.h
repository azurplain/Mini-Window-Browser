#pragma once

#include <windows.h>

#include <functional>
#include <atomic>
#include <memory>
#include <string>

struct IUIAutomation;

namespace xiaochuang {

constexpr UINT kMessageInputFocusChanged = WM_APP + 41;
constexpr UINT kMessageInputGuardResult = WM_APP + 43;

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
    void SetEnabled(bool enabled);
    void SetWebTyping(bool typing);
    bool Refresh();
    void HandleAsyncResult(LPARAM resultPointer);
    bool ConsumePendingEvents();
    bool IsTyping() const noexcept { return typing_; }
    const std::wstring& StatusText() const noexcept { return statusText_; }

private:
    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND window,
                                      LONG objectId, LONG childId, DWORD threadId, DWORD time);
    struct WorkerState;
    struct AsyncResult;
    static DWORD WINAPI WorkerThreadProc(void* context);
    static bool DetectWithUiAutomation(IUIAutomation* automation,
                                       DWORD* elementProcessId, HWND* elementWindow);
    bool DetectSystemTextInput();
    void QueueAutomationCheck(HWND foreground, DWORD threadId, HWND focusedWindow);
    void ApplyTyping(bool typing);

    static InputGuard* instance_;
    HWND applicationWindow_ = nullptr;
    HWND nativeEdit_ = nullptr;
    HWINEVENTHOOK foregroundHook_ = nullptr;
    HWINEVENTHOOK focusHook_ = nullptr;
    HANDLE workerThread_ = nullptr;
    std::shared_ptr<WorkerState> workerState_;
    StateCallback callback_;
    bool enabled_ = true;
    bool webTyping_ = false;
    bool typing_ = false;
    std::atomic_bool externalObservationArmed_{false};
    std::atomic_bool focusMessagePending_{false};
    std::atomic_bool foregroundChangePending_{false};
    HWND lastQueuedForeground_ = nullptr;
    HWND lastQueuedFocus_ = nullptr;
    ULONGLONG lastQueuedAt_ = 0;
    HWND automationForeground_ = nullptr;
    DWORD automationThreadId_ = 0;
    HWND automationFocus_ = nullptr;
    bool automationTyping_ = false;
    ULONGLONG automationCompletedAt_ = 0;
    ULONGLONG latestRequestSequence_ = 0;
    ULONGLONG imeTrailingUntil_ = 0;
    std::wstring statusText_ = L"UI Automation 尚未运行";
};

} // namespace xiaochuang
