#pragma once

#include "AppModel.h"
#include "ConfigStore.h"
#include "HotkeyManager.h"
#include "InputGuard.h"
#include "ThemeManager.h"
#include "WindowModeController.h"

#include <WebView2.h>
#include <shellapi.h>
#include <wil/com.h>

#include <array>
#include <optional>
#include <string>
#include <unordered_map>

namespace xiaochuang {

class Application {
public:
    int Run(HINSTANCE instance, int showCommand);

private:
    enum ControlId : int {
        Address = 1001,
        Go = 1002,
        AddTab = 1003,
        Minimize = 1004,
        Maximize = 1005,
        Close = 1006,
        Star = 1007,
        Bookmarks = 1008,
        Presets = 1009,
        Settings = 1010,
        Tabs = 1011,

        SettingsRadius = 2001,
        SettingsSnap = 2002,
        SettingsStyle = 2003,
        SettingsOpacitySlider = 2004,
        SettingsOpacityEdit = 2005,
        SettingsAutoPause = 2006,
        SettingsTyping = 2007,
        SettingsTray = 2008,
        SettingsBackgroundMedia = 2009,
        SettingsHome = 2010,
        SettingsHoldRate = 2011,
        SettingsRenderMode = 2012,
        SettingsTheme = 2013,
        SettingsDiagnostics = 2014,
        SettingsAbout = 2015,
        SettingsRepository = 2016,
        SettingsAutoFitFullscreen = 2017,
        SettingsMaximizedTopDrag = 2018,
        SettingsHotkeyBase = 2100,

        BookmarkList = 3001,
        BookmarkTitle = 3002,
        BookmarkUrl = 3003,
        BookmarkSave = 3004,
        BookmarkOpen = 3005,
        BookmarkDelete = 3006,

        PresetList = 4001,
        PresetName = 4002,
        PresetApply = 4003,
        PresetAdd = 4004,
        PresetSaveName = 4005,
        PresetDelete = 4006,
        PresetReset = 4007,
    };

    static constexpr UINT kTrayMessage = WM_APP + 60;
    static constexpr UINT kShowRegistrationErrors = WM_APP + 61;
    static constexpr UINT kCloseTabMessage = WM_APP + 62;
    static constexpr UINT kShowExistingInstance = WM_APP + 63;
    static constexpr UINT_PTR kInputFallbackTimerId = 4403;
    static constexpr UINT_PTR kSettingsSaveTimerId = 4404;
    static constexpr int kTrayIconId = 1;

    static LRESULT CALLBACK MainWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK SettingsWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK BookmarkWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK PresetWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK AddressProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK TabSubclassProc(HWND window, UINT message, WPARAM wParam,
                                            LPARAM lParam, UINT_PTR subclassId,
                                            DWORD_PTR referenceData);

    bool Initialize(HINSTANCE instance, int showCommand);
    void Shutdown();
    void RegisterWindowClasses();
    void CreateMainControls();
    void LayoutMainControls();
    void ShowChrome(bool visible);
    void ApplyThemeToAllWindows();
    int Scale(int value) const;
    void UpdateDpi(UINT dpi);
    LRESULT HandleMainMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void PaintMainWindow();
    void DrawOwnerItem(const DRAWITEMSTRUCT& item);
    LRESULT HitTest(LPARAM lParam) const;

    void InitializeWebView();
    void ConfigureWebViewEvents();
    void FinishInitialNavigation();
    void ResizeWebView();
    void ExecuteScript(const std::wstring& script);
    void ExecuteMediaAction(HotkeyAction action, HotkeyGesture gesture);
    void RequestMediaDiagnostics();
    void HandleWebMessage(const std::wstring& message);
    void HandleWebFullscreenChanged();
    void HandleProcessFailure(COREWEBVIEW2_PROCESS_FAILED_KIND kind);

    void CreateNewTab(const std::wstring& url, const std::wstring& title, bool activate);
    void CloseTab(int index);
    void CloseOtherTabs(int index);
    void CloseAllTabs();
    void SwitchTab(int index);
    void UpdateCurrentTabTitle(const std::wstring& title);
    void UpdateCurrentTabUrl(const std::wstring& url);
    void RebuildTabControl();
    void HandleTabMouseDown(LPARAM lParam);
    void ShowTabContextMenu(POINT screenPoint);
    std::wstring SuggestedTitleForUrl(const std::wstring& url) const;
    std::wstring CurrentUrl() const;
    std::wstring CurrentTitle() const;

    void NavigateAddressBar();
    void UpdateBookmarkStar();
    void ToggleCurrentBookmark();
    void ShowBookmarkMenu();
    void NavigateTo(const std::wstring& url, bool concealUntilContent = false);
    void ToggleHidden();
    void SetHidden(bool hidden);
    void ToggleImmersion();
    void MinimizeWindow();
    void RefreshHotkeys();
    void ShowHotkeyErrors();

    void AddTrayIcon();
    void RemoveTrayIcon();
    void RefreshTrayMode();
    void ShowTrayMenu();
    void RestoreFromUserAction();

    void ShowSettingsWindow();
    void CreateSettingsControls(HWND window);
    void ReadSettingsControls();
    void RefreshSettingsControls();
    void RefreshOpacityControls();
    void BeginHotkeyCapture(HotkeyAction action);
    bool HandleHotkeyCaptureKey(UINT virtualKey);
    void ApplyCapturedHotkey(UINT virtualKey, UINT modifiers);
    void CancelHotkeyCapture();
    LRESULT HandleSettingsMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    void ShowBookmarkWindow();
    void CreateBookmarkControls(HWND window);
    void RebuildBookmarkList();
    void LoadSelectedBookmark();
    LRESULT HandleBookmarkMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    void ShowPresetWindow();
    void CreatePresetControls(HWND window);
    void RebuildPresetList();
    void LoadSelectedPreset();
    void AddCurrentPreset();
    void ApplySelectedPreset();
    bool NormalizeAndValidatePresetName(std::wstring& name, int ignoredIndex) const;
    void ResetToDefaults();
    LRESULT HandlePresetMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    void SaveConfiguration();
    void ApplyPreset(const Preset& preset);
    Preset CapturePreset(const std::wstring& name) const;
    void SetControlFont(HWND control, HFONT font = nullptr) const;
    HWND CreateThemedButton(HWND parent, const wchar_t* text, int id) const;
    HWND CreateLabel(HWND parent, const wchar_t* text) const;
    void CenterOwnedWindow(HWND window, int width, int height) const;
    void ShowError(const wchar_t* title, const wchar_t* message, HRESULT result = S_OK) const;

    HINSTANCE instance_ = nullptr;
    HWND mainWindow_ = nullptr;
    HWND addressEdit_ = nullptr;
    WNDPROC oldAddressProc_ = nullptr;
    HWND tabControl_ = nullptr;
    HWND goButton_ = nullptr;
    HWND addTabButton_ = nullptr;
    HWND minimizeButton_ = nullptr;
    HWND maximizeButton_ = nullptr;
    HWND closeButton_ = nullptr;
    HWND starButton_ = nullptr;
    HWND bookmarkButton_ = nullptr;
    HWND presetButton_ = nullptr;
    HWND settingsButton_ = nullptr;
    HWND settingsWindow_ = nullptr;
    HWND bookmarkWindow_ = nullptr;
    HWND presetWindow_ = nullptr;
    HFONT uiFont_ = nullptr;
    HFONT titleFont_ = nullptr;
    HICON largeIcon_ = nullptr;
    HICON smallIcon_ = nullptr;
    UINT dpi_ = 96;
    int tabRowHeight_ = 40;
    int navigationRowHeight_ = 44;
    int chromeHeight_ = 84;
    bool shuttingDown_ = false;
    bool navigatingFromTabSwitch_ = false;
    bool webViewReady_ = false;
    bool webTyping_ = false;
    bool trayIconAdded_ = false;
    std::wstring webViewVersion_;
    std::unordered_map<std::wstring, std::wstring> knownPageTitles_;
    NOTIFYICONDATAW trayData_{};

    AppState state_;
    ConfigStore config_;
    ThemeManager theme_;
    HotkeyManager hotkeys_;
    InputGuard inputGuard_;
    WindowModeController windowModes_;

    wil::com_ptr<ICoreWebView2Environment> webViewEnvironment_;
    wil::com_ptr<ICoreWebView2Controller> webViewController_;
    wil::com_ptr<ICoreWebView2> webView_;
    EventRegistrationToken navigationStartingToken_{};
    EventRegistrationToken navigationCompletedToken_{};
    EventRegistrationToken contentLoadingToken_{};
    EventRegistrationToken sourceChangedToken_{};
    EventRegistrationToken titleChangedToken_{};
    EventRegistrationToken webMessageToken_{};
    EventRegistrationToken fullscreenToken_{};
    EventRegistrationToken newWindowToken_{};
    EventRegistrationToken processFailedToken_{};

    struct SettingsControls {
        HWND radius = nullptr;
        HWND snap = nullptr;
        HWND style = nullptr;
        HWND opacitySlider = nullptr;
        HWND opacityEdit = nullptr;
        HWND opacityLabel = nullptr;
        HWND opacityPercent = nullptr;
        HWND autoPause = nullptr;
        HWND autoFitFullscreen = nullptr;
        HWND maximizedTopDrag = nullptr;
        HWND typing = nullptr;
        HWND tray = nullptr;
        HWND backgroundMedia = nullptr;
        HWND home = nullptr;
        HWND holdRate = nullptr;
        HWND renderMode = nullptr;
        HWND theme = nullptr;
        HWND diagnostics = nullptr;
        HWND about = nullptr;
        HWND repository = nullptr;
        std::array<HWND, kHotkeyCount> hotkeys{};
    } settingsControls_;
    std::optional<HotkeyAction> captureAction_;
    bool updatingSettingsControls_ = false;
    bool waitingForTabContent_ = false;
    UINT64 waitingNavigationId_ = 0;
    int settingsScrollPosition_ = 0;

    struct BookmarkControls {
        HWND list = nullptr;
        HWND title = nullptr;
        HWND url = nullptr;
    } bookmarkControls_;
    int selectedBookmark_ = -1;

    struct PresetControls {
        HWND list = nullptr;
        HWND name = nullptr;
    } presetControls_;
    int selectedPreset_ = -1;

    HANDLE singleInstanceMutex_ = nullptr;
};

} // namespace xiaochuang
