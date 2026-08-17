#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "Application.h"

#include "CoreLogic.h"
#include "Diagnostics.h"
#include "MediaBridge.h"
#include "../Resource.h"

#include <WebView2EnvironmentOptions.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <wrl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <sstream>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uxtheme.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::Make;

namespace xiaochuang {
namespace {

constexpr wchar_t kMainWindowClass[] = L"XiaoChuangMainWindowV14";
constexpr wchar_t kSettingsWindowClass[] = L"XiaoChuangSettingsV14";
constexpr wchar_t kBookmarkWindowClass[] = L"XiaoChuangBookmarksV14";
constexpr wchar_t kPresetWindowClass[] = L"XiaoChuangPresetsV14";
constexpr wchar_t kSingleInstanceName[] = L"MiniWindowBrowser-v1.4-SingleInstance";
constexpr UINT kCloseCurrentTabCommand = 51001;
constexpr UINT kCloseOtherTabsCommand = 51002;
constexpr UINT kCloseAllTabsCommand = 51003;

std::wstring WindowText(HWND window) {
    const int length = GetWindowTextLengthW(window);
    std::vector<wchar_t> buffer(static_cast<size_t>(std::max(length, 0)) + 1U, L'\0');
    GetWindowTextW(window, buffer.data(), static_cast<int>(buffer.size()));
    return buffer.data();
}

void SetCheck(HWND control, bool checked) {
    SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool IsChecked(HWND control) {
    return SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void SelectComboValue(HWND combo, int index) {
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
}

int ComboValue(HWND combo, int fallback = 0) {
    const LRESULT result = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    return result == CB_ERR ? fallback : static_cast<int>(result);
}

int ParseInteger(HWND edit, int fallback) {
    const std::wstring text = WindowText(edit);
    if (text.empty()) return fallback;
    wchar_t* end = nullptr;
    const long value = wcstol(text.c_str(), &end, 10);
    return end == text.c_str() ? fallback : static_cast<int>(value);
}

bool IsMouseVirtualKey(UINT virtualKey) {
    return virtualKey == VK_MBUTTON || virtualKey == VK_XBUTTON1 || virtualKey == VK_XBUTTON2;
}

std::wstring FormatTabTitle(const std::wstring& title) {
    return title.empty() ? L"正在加载…" : title;
}

bool IsPlaceholderTabTitle(const std::wstring& title) {
    return title.empty() || title == L"主页" || title == L"新标签页" || title == L"正在加载…";
}

BOOL CALLBACK ApplyControlTheme(HWND control, LPARAM useDarkValue) {
    const bool useDark = useDarkValue != 0;
    SetWindowTheme(control, useDark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    SendMessageW(control, WM_THEMECHANGED, 0, 0);
    InvalidateRect(control, nullptr, TRUE);
    return TRUE;
}

void ApplyThemeToWindowTree(HWND window, bool useDark) {
    if (!window || !IsWindow(window)) return;
    SetWindowTheme(window, useDark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    EnumChildWindows(window, ApplyControlTheme, useDark ? 1 : 0);
    SendMessageW(window, WM_THEMECHANGED, 0, 0);
    RedrawWindow(window, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

} // namespace

int Application::Run(HINSTANCE instance, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        ShowError(L"启动失败", L"无法初始化 COM。", comResult);
        return 1;
    }
    if (!Initialize(instance, showCommand)) {
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (captureAction_ && settingsWindow_ &&
            (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) &&
            (message.hwnd == settingsWindow_ || IsChild(settingsWindow_, message.hwnd))) {
            if (HandleHotkeyCaptureKey(static_cast<UINT>(message.wParam))) continue;
        }
        if (settingsWindow_ && IsDialogMessageW(settingsWindow_, &message)) continue;
        if (bookmarkWindow_ && IsDialogMessageW(bookmarkWindow_, &message)) continue;
        if (presetWindow_ && IsDialogMessageW(presetWindow_, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    Shutdown();
    if (SUCCEEDED(comResult)) CoUninitialize();
    return static_cast<int>(message.wParam);
}

bool Application::Initialize(HINSTANCE instance, int showCommand) {
    instance_ = instance;
    singleInstanceMutex_ = CreateMutexW(nullptr, TRUE, kSingleInstanceName);
    if (!singleInstanceMutex_) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kMainWindowClass, nullptr)) {
            PostMessageW(existing, kShowExistingInstance, 0, 0);
        }
        return false;
    }

    config_.Load(state_);
    for (const TabData& tab : state_.tabs) {
        if (!tab.url.empty() && !IsPlaceholderTabTitle(tab.title)) {
            knownPageTitles_[NormalizeComparableUrl(tab.url)] = tab.title;
        }
    }
    theme_.SetMode(state_.settings.themeMode);
    dpi_ = GetDpiForSystem();
    UpdateDpi(dpi_);

    INITCOMMONCONTROLSEX controls{sizeof(controls),
        ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    RegisterWindowClasses();

    largeIcon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_MY), IMAGE_ICON,
                                                48, 48, LR_DEFAULTCOLOR));
    smallIcon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_SMALL), IMAGE_ICON,
                                                24, 24, LR_DEFAULTCOLOR));
    const RECT initial = state_.session.normalRect;
    mainWindow_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, kMainWindowClass,
        L"小窗浏览器", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        initial.left, initial.top, initial.right - initial.left, initial.bottom - initial.top,
        nullptr, nullptr, instance_, this);
    if (!mainWindow_) {
        ShowError(L"启动失败", L"无法创建主窗口。", HRESULT_FROM_WIN32(GetLastError()));
        return false;
    }
    if (largeIcon_) SendMessageW(mainWindow_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon_));
    if (smallIcon_) SendMessageW(mainWindow_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon_));

    windowModes_.Initialize(mainWindow_, &state_.settings, state_.session.normalRect,
        state_.session.maximized, chromeHeight_,
        [this](bool visible) { ShowChrome(visible); },
        [this]() { LayoutMainControls(); });
    hotkeys_.Start(mainWindow_, &state_.hotkeys,
        [this](HotkeyAction action, HotkeyGesture gesture) { ExecuteMediaAction(action, gesture); });
    // Register defaults before the window is shown. InputGuard used to sample the
    // launcher/editor focus here and immediately unregister every hotkey.
    RefreshHotkeys();
    RefreshTrayMode();
    ApplyThemeToAllWindows();
    InitializeWebView();

    ShowWindow(mainWindow_, showCommand == SW_HIDE ? SW_SHOWNORMAL : showCommand);
    UpdateWindow(mainWindow_);
    inputGuard_.Start(mainWindow_, [this](bool) { RefreshHotkeys(); });
    inputGuard_.SetNativeEdit(addressEdit_);
    SetTimer(mainWindow_, kInputFallbackTimerId, 1000, nullptr);
    inputGuard_.Refresh();
    RefreshHotkeys();
    windowModes_.ReassertTopmost();
    return true;
}

void Application::Shutdown() {
    if (shuttingDown_) return;
    SaveConfiguration();
    shuttingDown_ = true;
    hotkeys_.Stop();
    inputGuard_.Stop();
    windowModes_.Shutdown();
    RemoveTrayIcon();
    if (webViewController_) {
        webViewController_->Close();
        webViewController_.reset();
    }
    webView_.reset();
    webViewEnvironment_.reset();
    if (uiFont_) { DeleteObject(uiFont_); uiFont_ = nullptr; }
    if (titleFont_) { DeleteObject(titleFont_); titleFont_ = nullptr; }
    if (largeIcon_) { DestroyIcon(largeIcon_); largeIcon_ = nullptr; }
    if (smallIcon_) { DestroyIcon(smallIcon_); smallIcon_ = nullptr; }
    if (singleInstanceMutex_) { CloseHandle(singleInstanceMutex_); singleInstanceMutex_ = nullptr; }
}

void Application::RegisterWindowClasses() {
    WNDCLASSEXW mainClass{sizeof(mainClass)};
    mainClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    mainClass.lpfnWndProc = MainWindowProc;
    mainClass.hInstance = instance_;
    mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    mainClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_MY));
    mainClass.hIconSm = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_SMALL));
    mainClass.lpszClassName = kMainWindowClass;
    RegisterClassExW(&mainClass);

    WNDCLASSEXW popupClass{sizeof(popupClass)};
    popupClass.style = CS_HREDRAW | CS_VREDRAW;
    popupClass.hInstance = instance_;
    popupClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    popupClass.hIcon = mainClass.hIcon;
    popupClass.hIconSm = mainClass.hIconSm;
    popupClass.lpszClassName = kSettingsWindowClass;
    popupClass.lpfnWndProc = SettingsWindowProc;
    RegisterClassExW(&popupClass);
    popupClass.lpszClassName = kBookmarkWindowClass;
    popupClass.lpfnWndProc = BookmarkWindowProc;
    RegisterClassExW(&popupClass);
    popupClass.lpszClassName = kPresetWindowClass;
    popupClass.lpfnWndProc = PresetWindowProc;
    RegisterClassExW(&popupClass);
}

void Application::CreateMainControls() {
    addressEdit_ = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, mainWindow_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Address)), instance_, nullptr);
    oldAddressProc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        addressEdit_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(AddressProc)));
    SetWindowLongPtrW(addressEdit_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    tabControl_ = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FIXEDWIDTH | TCS_OWNERDRAWFIXED | TCS_FOCUSNEVER,
        0, 0, 0, 0, mainWindow_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Tabs)), instance_, nullptr);
    SetWindowSubclass(tabControl_, TabSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    goButton_ = CreateThemedButton(mainWindow_, L"前往", Go);
    addTabButton_ = CreateThemedButton(mainWindow_, L"＋", AddTab);
    minimizeButton_ = CreateThemedButton(mainWindow_, L"—", Minimize);
    maximizeButton_ = CreateThemedButton(mainWindow_, L"□", Maximize);
    closeButton_ = CreateThemedButton(mainWindow_, L"×", Close);
    starButton_ = CreateThemedButton(mainWindow_, L"☆", Star);
    bookmarkButton_ = CreateThemedButton(mainWindow_, L"书签", Bookmarks);
    presetButton_ = CreateThemedButton(mainWindow_, L"预设", Presets);
    settingsButton_ = CreateThemedButton(mainWindow_, L"设置", Settings);

    const HWND controls[] = {addressEdit_, tabControl_, goButton_, addTabButton_, minimizeButton_,
        maximizeButton_, closeButton_, starButton_, bookmarkButton_, presetButton_, settingsButton_};
    for (HWND control : controls) SetControlFont(control);
    RebuildTabControl();
}

void Application::LayoutMainControls() {
    if (!mainWindow_) return;
    RECT client{};
    GetClientRect(mainWindow_, &client);
    const int width = client.right;
    const bool chromeVisible = IsWindowVisible(tabControl_) != FALSE;
    if (chromeVisible) {
        const int leftMargin = Scale(4);
        const int captionButtonWidth = Scale(36);
        const int buttonY = Scale(1);
        const int buttonHeight = tabRowHeight_ - Scale(2);
        int right = width;
        MoveWindow(closeButton_, right - captionButtonWidth, buttonY, captionButtonWidth, buttonHeight, TRUE);
        right -= captionButtonWidth;
        MoveWindow(maximizeButton_, right - captionButtonWidth, buttonY, captionButtonWidth, buttonHeight, TRUE);
        right -= captionButtonWidth;
        MoveWindow(minimizeButton_, right - captionButtonWidth, buttonY, captionButtonWidth, buttonHeight, TRUE);
        right -= captionButtonWidth;
        MoveWindow(addTabButton_, right - captionButtonWidth, buttonY,
                   captionButtonWidth, buttonHeight, TRUE);
        right -= captionButtonWidth;

        const int dragReserve = Scale(72);
        const int available = std::max(0, right - leftMargin);
        const int stripCapacity = std::max(0, available - dragReserve);
        const int tabCount = std::max(1, static_cast<int>(state_.tabs.size()));
        const int itemWidth = std::min(Scale(132),
            std::max(1, (stripCapacity - Scale(6)) / tabCount));
        const int tabStripWidth = std::min(stripCapacity, itemWidth * tabCount + Scale(6));
        MoveWindow(tabControl_, leftMargin, Scale(1), tabStripWidth,
                   tabRowHeight_ - Scale(1), TRUE);
        SendMessageW(tabControl_, TCM_SETITEMSIZE, 0,
                     MAKELPARAM(itemWidth, tabRowHeight_ - Scale(5)));

        const int navigationY = tabRowHeight_;
        const int navigationButtonHeight = navigationRowHeight_ - Scale(8);
        const int gap = Scale(5);
        const int margin = Scale(7);
        const int smallWidth = Scale(34);
        const int textWidth = Scale(52);
        int xRight = width - margin;
        auto placeRight = [&](HWND button, int buttonWidth) {
            xRight -= buttonWidth;
            MoveWindow(button, xRight, navigationY + Scale(4), buttonWidth, navigationButtonHeight, TRUE);
            xRight -= gap;
        };
        placeRight(settingsButton_, textWidth);
        placeRight(presetButton_, textWidth);
        placeRight(bookmarkButton_, textWidth);
        placeRight(starButton_, smallWidth);
        placeRight(goButton_, textWidth);
        const int addressWidth = std::max(Scale(120), xRight - margin);
        MoveWindow(addressEdit_, margin + Scale(9), navigationY + Scale(7),
                   std::max(Scale(80), addressWidth - gap - Scale(20)),
                   navigationRowHeight_ - Scale(14), TRUE);
    }
    ResizeWebView();
    InvalidateRect(mainWindow_, nullptr, FALSE);
}

void Application::ShowChrome(bool visible) {
    const HWND controls[] = {addressEdit_, tabControl_, goButton_, addTabButton_, minimizeButton_,
        maximizeButton_, closeButton_, starButton_, bookmarkButton_, presetButton_, settingsButton_};
    for (HWND control : controls) {
        if (control) ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
    }
}

void Application::ApplyThemeToAllWindows() {
    const HWND windows[] = {mainWindow_, settingsWindow_, bookmarkWindow_, presetWindow_};
    for (HWND window : windows) {
        if (!window || !IsWindow(window)) continue;
        theme_.ApplyWindowTheme(window);
        ApplyThemeToWindowTree(window, theme_.IsDark());
    }
    windowModes_.RefreshFrameAppearance();
}

int Application::Scale(int value) const {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

void Application::UpdateDpi(UINT dpi) {
    dpi_ = dpi ? dpi : 96;
    tabRowHeight_ = Scale(32);
    navigationRowHeight_ = Scale(36);
    chromeHeight_ = tabRowHeight_ + navigationRowHeight_;
    if (uiFont_) DeleteObject(uiFont_);
    if (titleFont_) DeleteObject(titleFont_);
    uiFont_ = CreateFontW(-Scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    titleFont_ = CreateFontW(-Scale(15), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
}

LRESULT CALLBACK Application::MainWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        application = static_cast<Application*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
        if (application) application->mainWindow_ = window;
    }
    return application ? application->HandleMainMessage(window, message, wParam, lParam)
                       : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::HandleMainMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCCALCSIZE:
        if (wParam) return 0;
        break;
    case WM_CREATE:
        CreateMainControls();
        return 0;
    case WM_NCHITTEST:
        return HitTest(lParam);
    case WM_NCPAINT:
        if (windowModes_.IsSeamlessMaximized()) return 0;
        break;
    case WM_NCACTIVATE:
        if (windowModes_.IsSeamlessMaximized()) return TRUE;
        break;
    case WM_NCLBUTTONDOWN:
        if (wParam == HTCAPTION && windowModes_.IsSeamlessMaximized() &&
            state_.settings.maximizedTopDragEnabled) {
            const POINT cursor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (windowModes_.RestoreMaximizedForDrag(cursor)) {
                SetWindowTextW(maximizeButton_, L"□");
                return DefWindowProcW(window, message, wParam, lParam);
            }
        }
        break;
    case WM_NCLBUTTONDBLCLK:
        if (wParam == HTCAPTION &&
            (!windowModes_.IsSeamlessMaximized() || state_.settings.maximizedTopDragEnabled)) {
            windowModes_.ToggleMaximized();
            SetWindowTextW(maximizeButton_, windowModes_.IsSeamlessMaximized() ? L"❐" : L"□");
            return 0;
        }
        break;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = {Scale(520), Scale(320)};
        return 0;
    }
    case WM_DPICHANGED: {
        UpdateDpi(HIWORD(wParam));
        windowModes_.SetChromeHeight(chromeHeight_);
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        const HWND controls[] = {addressEdit_, tabControl_, goButton_, addTabButton_, minimizeButton_,
            maximizeButton_, closeButton_, starButton_, bookmarkButton_, presetButton_, settingsButton_};
        for (HWND control : controls) SetControlFont(control);
        LayoutMainControls();
        return 0;
    }
    case WM_PAINT:
        PaintMainWindow();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DRAWITEM:
        DrawOwnerItem(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, theme_.Palette().text);
        SetBkColor(dc, theme_.Palette().surface);
        return reinterpret_cast<LRESULT>(theme_.SurfaceBrush());
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, theme_.Palette().text);
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(theme_.WindowBrush());
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == Go) NavigateAddressBar();
        else if (id == AddTab) CreateNewTab(state_.settings.homeUrl, L"新标签页", true);
        else if (id == Minimize) MinimizeWindow();
        else if (id == Maximize) { windowModes_.ToggleMaximized(); SetWindowTextW(maximizeButton_, windowModes_.IsSeamlessMaximized() ? L"❐" : L"□"); }
        else if (id == Close) SendMessageW(window, WM_CLOSE, 0, 0);
        else if (id == Star) ToggleCurrentBookmark();
        else if (id == Bookmarks) ShowBookmarkMenu();
        else if (id == Presets) ShowPresetWindow();
        else if (id == Settings) ShowSettingsWindow();
        else if (id == Address && (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS)) {
            PostMessageW(window, kMessageInputFocusChanged, 0, 0);
        }
        return 0;
    }
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header && header->idFrom == Tabs && header->code == TCN_SELCHANGE) {
            SwitchTab(TabCtrl_GetCurSel(tabControl_));
        }
        return 0;
    }
    case WM_MOVING:
        windowModes_.SnapMovingRect(reinterpret_cast<RECT*>(lParam),
            (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
        return TRUE;
    case WM_EXITSIZEMOVE:
        windowModes_.UpdateNormalRectFromWindow();
        SaveConfiguration();
        return 0;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED && state_.settings.useSystemTray) {
            SetHidden(true);
        } else {
            if (wParam != SIZE_MINIMIZED && !windowModes_.IsHidden() &&
                !waitingForTabContent_ && webViewController_) {
                webViewController_->put_IsVisible(TRUE);
            }
            LayoutMainControls();
        }
        return 0;
    case WM_WINDOWPOSCHANGED:
        LayoutMainControls();
        if (webViewController_) webViewController_->NotifyParentWindowPositionChanged();
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) {
            windowModes_.ReassertTopmost();
        }
        if (windowModes_.IsSeamlessMaximized()) {
            RedrawWindow(window, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0U) == SC_RESTORE && IsIconic(window)) {
            break;
        }
        if ((wParam & 0xFFF0U) == SC_MAXIMIZE ||
            ((wParam & 0xFFF0U) == SC_RESTORE && windowModes_.IsSeamlessMaximized())) {
            if (!windowModes_.IsImmersive()) {
                const bool wantsMaximized = (wParam & 0xFFF0U) == SC_MAXIMIZE;
                if (wantsMaximized != windowModes_.IsSeamlessMaximized()) windowModes_.ToggleMaximized();
                SetWindowTextW(maximizeButton_, windowModes_.IsSeamlessMaximized() ? L"❐" : L"□");
            }
            return 0;
        }
        if ((wParam & 0xFFF0U) == SC_MINIMIZE && state_.settings.useSystemTray) {
            SetHidden(true);
            return 0;
        }
        break;
    case WM_HOTKEY:
        hotkeys_.HandleHotkeyMessage(static_cast<int>(wParam));
        return 0;
    case kMessageMouseHotkey:
        hotkeys_.HandleMouseMessage(wParam, lParam);
        return 0;
    case WM_INPUT:
        hotkeys_.HandleRawInput(lParam);
        return DefWindowProcW(window, message, wParam, lParam);
    case kMessageKeyboardHotkey:
        hotkeys_.HandleKeyboardMessage(wParam, lParam);
        return 0;
    case kMessageInputFocusChanged:
        inputGuard_.Refresh();
        windowModes_.ReassertTopmost();
        return 0;
    case kCloseTabMessage:
        CloseTab(static_cast<int>(wParam));
        return 0;
    case kShowRegistrationErrors:
        ShowHotkeyErrors();
        return 0;
    case WM_TIMER:
        if (wParam == kHotkeyHoldTimerId) hotkeys_.Tick();
        else if (wParam == kImmersionTimerId) windowModes_.OnImmersionTimer();
        else if (wParam == kInputFallbackTimerId) inputGuard_.Refresh();
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && windowModes_.IsWebFullscreen()) {
            ExecuteScript(L"if(document.fullscreenElement) document.exitFullscreen();");
            windowModes_.LeaveWebFullscreen();
            return 0;
        }
        break;
    case WM_SETTINGCHANGE:
        if (theme_.RefreshSystemTheme()) ApplyThemeToAllWindows();
        return 0;
    case kTrayMessage:
    {
        const UINT trayEvent = trayData_.uVersion == NOTIFYICON_VERSION_4
            ? LOWORD(static_cast<DWORD_PTR>(lParam)) : static_cast<UINT>(lParam);
        if (trayEvent == WM_LBUTTONUP || trayEvent == WM_LBUTTONDBLCLK ||
            trayEvent == NIN_SELECT || trayEvent == NIN_KEYSELECT) {
            RestoreFromUserAction();
        } else if (trayEvent == WM_RBUTTONUP || trayEvent == WM_CONTEXTMENU) {
            ShowTrayMenu();
        }
        return 0;
    }
    case kShowExistingInstance:
        RestoreFromUserAction();
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, kInputFallbackTimerId);
        SaveConfiguration();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void Application::PaintMainWindow() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(mainWindow_, &paint);
    RECT client{};
    GetClientRect(mainWindow_, &client);
    theme_.FillBackground(dc, client, theme_.Palette().window);
    if (IsWindowVisible(tabControl_)) {
        RECT chrome{0, 0, client.right, chromeHeight_};
        theme_.FillBackground(dc, chrome, theme_.Palette().chrome);
        RECT navigation{0, tabRowHeight_, client.right, chromeHeight_};
        theme_.FillBackground(dc, navigation, theme_.Palette().window);
        RECT addressRect{};
        GetWindowRect(addressEdit_, &addressRect);
        MapWindowPoints(nullptr, mainWindow_, reinterpret_cast<POINT*>(&addressRect), 2);
        InflateRect(&addressRect, Scale(10), Scale(4));
        HPEN addressPen = CreatePen(PS_SOLID, 1, theme_.Palette().border);
        HGDIOBJ addressOldPen = SelectObject(dc, addressPen);
        HGDIOBJ oldBrush = SelectObject(dc, theme_.SurfaceBrush());
        RoundRect(dc, addressRect.left, addressRect.top, addressRect.right, addressRect.bottom,
                  Scale(14), Scale(14));
        SelectObject(dc, oldBrush);
        SelectObject(dc, addressOldPen);
        DeleteObject(addressPen);
        HPEN pen = CreatePen(PS_SOLID, 1, theme_.Palette().border);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, 0, chromeHeight_ - 1, nullptr);
        LineTo(dc, client.right, chromeHeight_ - 1);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
    if (waitingForTabContent_) {
        RECT loading{Scale(20), chromeHeight_ + Scale(20), client.right - Scale(20),
                     client.bottom - Scale(20)};
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, theme_.Palette().secondaryText);
        if (uiFont_) SelectObject(dc, uiFont_);
        DrawTextW(dc, L"正在切换标签页…", -1, &loading,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    EndPaint(mainWindow_, &paint);
}

void Application::DrawOwnerItem(const DRAWITEMSTRUCT& item) {
    HGDIOBJ oldFont = nullptr;
    if (uiFont_) oldFont = SelectObject(item.hDC, uiFont_);
    if (item.CtlID == Tabs) {
        const int index = static_cast<int>(item.itemID);
        const std::wstring title = index >= 0 && index < static_cast<int>(state_.tabs.size())
            ? FormatTabTitle(state_.tabs[static_cast<size_t>(index)].title) : L"新标签页";
        theme_.DrawTab(item, title, index == TabCtrl_GetCurSel(tabControl_));
        if (oldFont) SelectObject(item.hDC, oldFont);
        return;
    }
    wchar_t text[128]{};
    GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));
    const bool topChromeButton = GetParent(item.hwndItem) == mainWindow_ &&
        (item.CtlID == AddTab || item.CtlID == Minimize || item.CtlID == Maximize || item.CtlID == Close);
    theme_.DrawOwnerButton(item, text, item.CtlID == Go, item.CtlID == Close,
                           topChromeButton ? theme_.Palette().chrome : theme_.Palette().window);
    if (oldFont) SelectObject(item.hDC, oldFont);
}

LRESULT Application::HitTest(LPARAM lParam) const {
    if (windowModes_.IsImmersive()) return HTTRANSPARENT;
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(mainWindow_, &point);
    RECT client{};
    GetClientRect(mainWindow_, &client);
    if (!windowModes_.IsSeamlessMaximized()) {
        const int edge = Scale(7);
        const bool left = point.x < edge;
        const bool right = point.x >= client.right - edge;
        const bool top = point.y < edge;
        const bool bottom = point.y >= client.bottom - edge;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
    }
    if (point.y < tabRowHeight_) {
        RECT tabs{};
        RECT addButton{};
        GetWindowRect(tabControl_, &tabs);
        GetWindowRect(addTabButton_, &addButton);
        MapWindowPoints(nullptr, mainWindow_, reinterpret_cast<POINT*>(&tabs), 2);
        MapWindowPoints(nullptr, mainWindow_, reinterpret_cast<POINT*>(&addButton), 2);
        if ((point.x >= Scale(1) && point.x < tabs.left - Scale(2)) ||
            (point.x >= tabs.right + Scale(3) && point.x < addButton.left - Scale(3))) {
            return (!windowModes_.IsSeamlessMaximized() || state_.settings.maximizedTopDragEnabled)
                ? HTCAPTION : HTCLIENT;
        }
    }
    return HTCLIENT;
}

LRESULT CALLBACK Application::AddressProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!application || !application->oldAddressProc_) return DefWindowProcW(window, message, wParam, lParam);
    if (message == WM_KEYDOWN && wParam == VK_RETURN) {
        application->NavigateAddressBar();
        return 0;
    }
    if (message == WM_LBUTTONDBLCLK) {
        SetFocus(window);
        SendMessageW(window, EM_SETSEL, 0, -1);
        SendMessageW(window, EM_SCROLLCARET, 0, 0);
        return 0;
    }
    if (message == WM_SETFOCUS) {
        SendMessageW(window, EM_SETSEL, 0, -1);
        PostMessageW(application->mainWindow_, kMessageInputFocusChanged, 0, 0);
    } else if (message == WM_KILLFOCUS) {
        PostMessageW(application->mainWindow_, kMessageInputFocusChanged, 0, 0);
    }
    return CallWindowProcW(application->oldAddressProc_, window, message, wParam, lParam);
}

LRESULT CALLBACK Application::TabSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                               UINT_PTR subclassId, DWORD_PTR referenceData) {
    auto* application = reinterpret_cast<Application*>(referenceData);
    if (!application) return DefSubclassProc(window, message, wParam, lParam);
    if (message == WM_LBUTTONDOWN) {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        MapWindowPoints(window, application->mainWindow_, &point, 1);
        application->HandleTabMouseDown(MAKELPARAM(point.x, point.y));
        return 0;
    }
    if (message == WM_LBUTTONUP) return 0;
    if (message == WM_CONTEXTMENU) {
        POINT point{};
        if (lParam == static_cast<LPARAM>(-1)) {
            const int selected = TabCtrl_GetCurSel(window);
            RECT item{};
            if (selected >= 0 && TabCtrl_GetItemRect(window, selected, &item)) {
                point = {item.left + (item.right - item.left) / 2, item.bottom};
                ClientToScreen(window, &point);
            } else {
                GetCursorPos(&point);
            }
        } else {
            point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        }
        application->ShowTabContextMenu(point);
        return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, TabSubclassProc, subclassId);
    return DefSubclassProc(window, message, wParam, lParam);
}

void Application::InitializeWebView() {
    auto options = Make<CoreWebView2EnvironmentOptions>();
    if (!options) {
        ShowError(L"WebView2 初始化失败", L"无法创建 WebView2 环境选项。");
        return;
    }
    if (state_.settings.renderMode == RenderMode::SoftwareCompatibility) {
        options->put_AdditionalBrowserArguments(L"--disable-gpu");
    }
    const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT environmentResult, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(environmentResult) || !environment) {
                    ShowError(L"WebView2 初始化失败",
                              L"请安装或修复 Microsoft Edge WebView2 Runtime。", environmentResult);
                    return S_OK;
                }
                webViewEnvironment_ = environment;
                wil::unique_cotaskmem_string version;
                if (SUCCEEDED(environment->get_BrowserVersionString(&version)) && version) {
                    webViewVersion_ = version.get();
                }
                environment->CreateCoreWebView2Controller(
                    mainWindow_,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(controllerResult) || !controller) {
                                ShowError(L"WebView2 初始化失败", L"无法创建网页视图。", controllerResult);
                                return S_OK;
                            }
                            webViewController_ = controller;
                            if (FAILED(controller->get_CoreWebView2(&webView_)) || !webView_) {
                                ShowError(L"WebView2 初始化失败", L"无法取得网页视图接口。");
                                return S_OK;
                            }

                            wil::com_ptr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(webView_->get_Settings(&settings)) && settings) {
                                settings->put_IsScriptEnabled(TRUE);
                                settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                                settings->put_IsWebMessageEnabled(TRUE);
                                settings->put_AreDevToolsEnabled(TRUE);
                                settings->put_IsStatusBarEnabled(FALSE);
                            }
                            ConfigureWebViewEvents();
                            ResizeWebView();
                            webViewController_->put_IsVisible(TRUE);
                            webView_->AddScriptToExecuteOnDocumentCreated(
                                MediaBridge::BootstrapScript(),
                                Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                                    [this](HRESULT, PCWSTR) -> HRESULT {
                                        webViewReady_ = true;
                                        FinishInitialNavigation();
                                        return S_OK;
                                    }).Get());
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
    if (FAILED(result)) {
        ShowError(L"WebView2 初始化失败", L"无法启动 WebView2 环境。", result);
    }
}

void Application::ConfigureWebViewEvents() {
    if (!webView_) return;
    webView_->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                hotkeys_.CancelActiveGesture();
                inputGuard_.SetWebTyping(false);
                if (waitingForTabContent_ && args) {
                    args->get_NavigationId(&waitingNavigationId_);
                }
                return S_OK;
            }).Get(), &navigationStartingToken_);
    webView_->add_ContentLoading(
        Callback<ICoreWebView2ContentLoadingEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2ContentLoadingEventArgs* args) -> HRESULT {
                UINT64 navigationId = 0;
                if (args) args->get_NavigationId(&navigationId);
                if (!waitingForTabContent_ ||
                    (waitingNavigationId_ != 0 && navigationId != waitingNavigationId_)) return S_OK;
                waitingForTabContent_ = false;
                waitingNavigationId_ = 0;
                if (webViewController_ && !windowModes_.IsHidden()) {
                    webViewController_->put_IsVisible(TRUE);
                }
                InvalidateRect(mainWindow_, nullptr, FALSE);
                return S_OK;
            }).Get(), &contentLoadingToken_);
    webView_->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                UINT64 navigationId = 0;
                if (args) args->get_NavigationId(&navigationId);
                if (waitingForTabContent_ && waitingNavigationId_ != 0 &&
                    navigationId != waitingNavigationId_) return S_OK;
                if (waitingForTabContent_) {
                    waitingForTabContent_ = false;
                    waitingNavigationId_ = 0;
                    if (webViewController_ && !windowModes_.IsHidden()) {
                        webViewController_->put_IsVisible(TRUE);
                    }
                    InvalidateRect(mainWindow_, nullptr, FALSE);
                }
                wil::unique_cotaskmem_string source;
                if (webView_ && SUCCEEDED(webView_->get_Source(&source)) && source) {
                    UpdateCurrentTabUrl(source.get());
                }
                wil::unique_cotaskmem_string title;
                if (webView_ && SUCCEEDED(webView_->get_DocumentTitle(&title)) && title) {
                    UpdateCurrentTabTitle(title.get());
                }
                return S_OK;
            }).Get(), &navigationCompletedToken_);
    webView_->add_SourceChanged(
        Callback<ICoreWebView2SourceChangedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                if (waitingForTabContent_) return S_OK;
                wil::unique_cotaskmem_string source;
                if (webView_ && SUCCEEDED(webView_->get_Source(&source)) && source) {
                    UpdateCurrentTabUrl(source.get());
                }
                return S_OK;
            }).Get(), &sourceChangedToken_);
    webView_->add_DocumentTitleChanged(
        Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [this](ICoreWebView2*, IUnknown*) -> HRESULT {
                if (waitingForTabContent_) return S_OK;
                wil::unique_cotaskmem_string title;
                if (webView_ && SUCCEEDED(webView_->get_DocumentTitle(&title)) && title) {
                    UpdateCurrentTabTitle(title.get());
                }
                return S_OK;
            }).Get(), &titleChangedToken_);
    webView_->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                wil::unique_cotaskmem_string message;
                if (args && SUCCEEDED(args->TryGetWebMessageAsString(&message)) && message) {
                    HandleWebMessage(message.get());
                }
                return S_OK;
            }).Get(), &webMessageToken_);
    const HRESULT fullscreenRegistration = webView_->add_ContainsFullScreenElementChanged(
        Callback<ICoreWebView2ContainsFullScreenElementChangedEventHandler>(
            [this](ICoreWebView2*, IUnknown*) -> HRESULT {
                HandleWebFullscreenChanged();
                return S_OK;
            }).Get(), &fullscreenToken_);
    if (FAILED(fullscreenRegistration)) {
        ShowError(L"WebView2 全屏事件初始化失败",
                  L"网页仍可在小窗内容区全屏，但 Esc 恢复和自动比例调整可能不可用。",
                  fullscreenRegistration);
    }
    webView_->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                wil::unique_cotaskmem_string uri;
                if (args && SUCCEEDED(args->get_Uri(&uri)) && uri) {
                    args->put_Handled(TRUE);
                    CreateNewTab(uri.get(), L"新标签页", true);
                }
                return S_OK;
            }).Get(), &newWindowToken_);
    webView_->add_ProcessFailed(
        Callback<ICoreWebView2ProcessFailedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
                COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
                if (args) args->get_ProcessFailedKind(&kind);
                HandleProcessFailure(kind);
                return S_OK;
            }).Get(), &processFailedToken_);
}

void Application::FinishInitialNavigation() {
    if (!webView_) return;
    if (state_.tabs.empty()) {
        state_.tabs.push_back({SuggestedTitleForUrl(state_.settings.homeUrl), state_.settings.homeUrl});
    }
    state_.currentTabIndex = std::clamp(state_.currentTabIndex, 0,
        static_cast<int>(state_.tabs.size()) - 1);
    RebuildTabControl();
    NavigateTo(state_.tabs[static_cast<size_t>(state_.currentTabIndex)].url);
}

void Application::ResizeWebView() {
    if (!webViewController_ || !mainWindow_) return;
    RECT bounds{};
    GetClientRect(mainWindow_, &bounds);
    bounds.top = IsWindowVisible(tabControl_) ? chromeHeight_ : 0;
    if (bounds.bottom < bounds.top) bounds.bottom = bounds.top;
    webViewController_->put_Bounds(bounds);
    webViewController_->NotifyParentWindowPositionChanged();
}

void Application::ExecuteScript(const std::wstring& script) {
    if (webView_) webView_->ExecuteScript(script.c_str(), nullptr);
}

void Application::ExecuteMediaAction(HotkeyAction action, HotkeyGesture gesture) {
    // The boss key must remain available even when an edit control owns focus;
    // otherwise hiding from the address bar leaves no way to show the window.
    if (action == HotkeyAction::ToggleHidden && gesture == HotkeyGesture::Trigger) {
        ToggleHidden();
        return;
    }
    if (inputGuard_.IsTyping() && state_.settings.disableHotkeysOnTyping &&
        gesture != HotkeyGesture::HoldStop) return;
    if (action == HotkeyAction::Immersion && gesture == HotkeyGesture::Trigger) {
        if (!windowModes_.IsHidden()) ToggleImmersion();
        return;
    }
    if (!webView_) return;

    MediaCommand command = MediaCommand::TogglePlay;
    if (action == HotkeyAction::SeekBackward) {
        command = gesture == HotkeyGesture::HoldStart ? MediaCommand::HoldBackward
            : gesture == HotkeyGesture::HoldStop ? MediaCommand::StopHold : MediaCommand::SeekBackward;
    } else if (action == HotkeyAction::SeekForward) {
        command = gesture == HotkeyGesture::HoldStart ? MediaCommand::HoldForward
            : gesture == HotkeyGesture::HoldStop ? MediaCommand::StopHold : MediaCommand::SeekForward;
    } else if (action == HotkeyAction::Previous) command = MediaCommand::Previous;
    else if (action == HotkeyAction::Next) command = MediaCommand::Next;
    else if (action != HotkeyAction::PlayPause) return;
    ExecuteScript(MediaBridge::CommandScript(command, state_.settings.holdPlaybackRate));
}

void Application::RequestMediaDiagnostics() {
    std::wstring base = BuildSystemDiagnostics(state_, webViewVersion_, windowModes_.Mode());
    if (!webView_) {
        CopyTextToClipboard(mainWindow_, base);
        MessageBoxW(settingsWindow_ ? settingsWindow_ : mainWindow_,
                    (base + L"\n\n诊断信息已复制到剪贴板。").c_str(),
                    L"小窗浏览器诊断", MB_OK | MB_ICONINFORMATION);
        return;
    }
    webView_->ExecuteScript(MediaBridge::DiagnosticScript().c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [this, base = std::move(base)](HRESULT result, PCWSTR value) -> HRESULT {
                std::wstring report = base;
                report += L"\n活动媒体: ";
                report += SUCCEEDED(result) && value ? DecodeExecuteScriptString(value) : L"无法读取";
                CopyTextToClipboard(mainWindow_, report);
                MessageBoxW(settingsWindow_ ? settingsWindow_ : mainWindow_,
                            (report + L"\n\n诊断信息已复制到剪贴板。").c_str(),
                            L"小窗浏览器诊断", MB_OK | MB_ICONINFORMATION);
                return S_OK;
            }).Get());
}

void Application::HandleWebMessage(const std::wstring& message) {
    if (message == L"MWB_TYPING:1") {
        webTyping_ = true;
        inputGuard_.SetWebTyping(true);
    } else if (message == L"MWB_TYPING:0") {
        webTyping_ = false;
        inputGuard_.SetWebTyping(false);
    }
}

void Application::HandleWebFullscreenChanged() {
    if (!webView_) return;
    BOOL fullscreen = FALSE;
    if (SUCCEEDED(webView_->get_ContainsFullScreenElement(&fullscreen))) {
        if (!fullscreen) {
            windowModes_.LeaveWebFullscreen();
            return;
        }

        windowModes_.EnterWebFullscreen();
        if (!state_.settings.autoFitVideoFullscreen) return;
        static constexpr wchar_t script[] = LR"JS((()=>{
const root=document.fullscreenElement;if(!root)return 0;
let video=root.matches&&root.matches('video')?root:(root.querySelector?root.querySelector('video'):null);
if(!video){video=[...document.querySelectorAll('video')].filter(v=>{const r=v.getBoundingClientRect();return r.width>0&&r.height>0}).sort((a,b)=>{const x=a.getBoundingClientRect(),y=b.getBoundingClientRect();return y.width*y.height-x.width*x.height})[0]}
const width=(video&&(video.videoWidth||video.clientWidth))||root.clientWidth||0;
const height=(video&&(video.videoHeight||video.clientHeight))||root.clientHeight||0;
return width>0&&height>0?width/height:0})())JS";
        webView_->ExecuteScript(script,
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [this](HRESULT result, PCWSTR value) -> HRESULT {
                    if (FAILED(result) || !value) return S_OK;
                    wchar_t* end = nullptr;
                    const double aspectRatio = wcstod(value, &end);
                    if (end != value && std::isfinite(aspectRatio)) {
                        windowModes_.FitWebFullscreenAspect(aspectRatio);
                    }
                    return S_OK;
                }).Get());
    }
}

void Application::HandleProcessFailure(COREWEBVIEW2_PROCESS_FAILED_KIND kind) {
    hotkeys_.CancelActiveGesture();
    waitingForTabContent_ = false;
    waitingNavigationId_ = 0;
    if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED ||
        kind == COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED) {
        if (webView_) webView_->Reload();
        return;
    }
    MessageBoxW(mainWindow_, L"WebView2 浏览器进程意外退出，将尝试重建网页视图。",
                L"网页进程恢复", MB_OK | MB_ICONWARNING);
    if (webViewController_) webViewController_->Close();
    webView_.reset();
    webViewController_.reset();
    webViewEnvironment_.reset();
    webViewReady_ = false;
    InitializeWebView();
}

void Application::CreateNewTab(const std::wstring& url, const std::wstring& title, bool activate) {
    const std::wstring targetUrl = url.empty() ? state_.settings.homeUrl : url;
    const std::wstring targetTitle = IsPlaceholderTabTitle(title)
        ? SuggestedTitleForUrl(targetUrl) : title;
    state_.tabs.push_back({targetTitle, targetUrl});
    RebuildTabControl();
    if (activate) SwitchTab(static_cast<int>(state_.tabs.size()) - 1);
}

void Application::CloseTab(int index) {
    if (index < 0 || index >= static_cast<int>(state_.tabs.size())) return;
    const bool closingCurrent = index == state_.currentTabIndex;
    if (state_.tabs.size() == 1) {
        state_.tabs[0] = {SuggestedTitleForUrl(state_.settings.homeUrl), state_.settings.homeUrl};
        state_.currentTabIndex = 0;
    } else {
        state_.tabs.erase(state_.tabs.begin() + index);
        if (state_.currentTabIndex >= static_cast<int>(state_.tabs.size())) {
            state_.currentTabIndex = static_cast<int>(state_.tabs.size()) - 1;
        } else if (index < state_.currentTabIndex) {
            --state_.currentTabIndex;
        }
    }
    RebuildTabControl();
    if (closingCurrent) {
        SwitchTab(state_.currentTabIndex);
    } else {
        TabCtrl_SetCurSel(tabControl_, state_.currentTabIndex);
        SaveConfiguration();
    }
}

void Application::CloseOtherTabs(int index) {
    if (index < 0 || index >= static_cast<int>(state_.tabs.size())) return;
    const bool wasCurrent = index == state_.currentTabIndex;
    TabData kept = state_.tabs[static_cast<size_t>(index)];
    state_.tabs.assign(1, std::move(kept));
    state_.currentTabIndex = 0;
    RebuildTabControl();
    if (!wasCurrent) SwitchTab(0); else SaveConfiguration();
}

void Application::CloseAllTabs() {
    state_.tabs.assign(1, TabData{SuggestedTitleForUrl(state_.settings.homeUrl),
                                  state_.settings.homeUrl});
    state_.currentTabIndex = 0;
    RebuildTabControl();
    SwitchTab(0);
}

void Application::SwitchTab(int index) {
    if (index < 0 || index >= static_cast<int>(state_.tabs.size())) return;
    state_.currentTabIndex = index;
    TabCtrl_SetCurSel(tabControl_, index);
    const TabData& tab = state_.tabs[static_cast<size_t>(index)];
    SetWindowTextW(addressEdit_, tab.url.c_str());
    UpdateBookmarkStar();
    if (webViewReady_) {
        navigatingFromTabSwitch_ = true;
        NavigateTo(tab.url, true);
        navigatingFromTabSwitch_ = false;
    }
    SaveConfiguration();
}

void Application::UpdateCurrentTabTitle(const std::wstring& title) {
    if (state_.currentTabIndex < 0 || state_.currentTabIndex >= static_cast<int>(state_.tabs.size())) return;
    TabData& tab = state_.tabs[static_cast<size_t>(state_.currentTabIndex)];
    const std::wstring resolvedTitle = title.empty() ? SuggestedTitleForUrl(tab.url) : title;
    tab.title = resolvedTitle;
    if (!tab.url.empty() && !IsPlaceholderTabTitle(resolvedTitle)) {
        knownPageTitles_[NormalizeComparableUrl(tab.url)] = resolvedTitle;
    }
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    std::wstring display = FormatTabTitle(resolvedTitle);
    item.pszText = display.data();
    TabCtrl_SetItem(tabControl_, state_.currentTabIndex, &item);
    SetWindowTextW(mainWindow_, (resolvedTitle + L" - 小窗浏览器").c_str());
}

void Application::UpdateCurrentTabUrl(const std::wstring& url) {
    if (url.empty() || state_.currentTabIndex < 0 ||
        state_.currentTabIndex >= static_cast<int>(state_.tabs.size())) return;
    state_.tabs[static_cast<size_t>(state_.currentTabIndex)].url = url;
    SetWindowTextW(addressEdit_, url.c_str());
    UpdateBookmarkStar();
}

void Application::RebuildTabControl() {
    if (!tabControl_) return;
    TabCtrl_DeleteAllItems(tabControl_);
    for (size_t index = 0; index < state_.tabs.size(); ++index) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        std::wstring text = FormatTabTitle(state_.tabs[index].title);
        item.pszText = text.data();
        TabCtrl_InsertItem(tabControl_, static_cast<int>(index), &item);
    }
    if (!state_.tabs.empty()) {
        state_.currentTabIndex = std::clamp(state_.currentTabIndex, 0,
            static_cast<int>(state_.tabs.size()) - 1);
        TabCtrl_SetCurSel(tabControl_, state_.currentTabIndex);
    }
    LayoutMainControls();
}

std::wstring Application::SuggestedTitleForUrl(const std::wstring& url) const {
    const std::wstring key = NormalizeComparableUrl(url);
    const auto known = knownPageTitles_.find(key);
    if (known != knownPageTitles_.end() && !IsPlaceholderTabTitle(known->second)) {
        return known->second;
    }
    for (const TabData& tab : state_.tabs) {
        if (NormalizeComparableUrl(tab.url) == key && !IsPlaceholderTabTitle(tab.title)) {
            return tab.title;
        }
    }
    return L"正在加载…";
}

void Application::HandleTabMouseDown(LPARAM lParam) {
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    MapWindowPoints(mainWindow_, tabControl_, &point, 1);
    TCHITTESTINFO hit{};
    hit.pt = point;
    const int index = TabCtrl_HitTest(tabControl_, &hit);
    if (index < 0) return;
    RECT item{};
    if (!TabCtrl_GetItemRect(tabControl_, index, &item)) return;
    const int itemWidth = item.right - item.left;
    const int itemHeight = item.bottom - item.top;
    const int closeWidth = std::min(itemWidth,
        std::max(Scale(16), std::min(std::max(Scale(24), itemHeight), itemWidth / 2)));
    const bool showClose = index == state_.currentTabIndex || itemWidth >= Scale(64);
    if (showClose && point.x >= item.right - closeWidth) {
        PostMessageW(mainWindow_, kCloseTabMessage, static_cast<WPARAM>(index), 0);
    } else if (index != state_.currentTabIndex) {
        SwitchTab(index);
    }
}

void Application::ShowTabContextMenu(POINT screenPoint) {
    POINT clientPoint = screenPoint;
    ScreenToClient(tabControl_, &clientPoint);
    TCHITTESTINFO hit{};
    hit.pt = clientPoint;
    int index = TabCtrl_HitTest(tabControl_, &hit);
    if (index < 0) index = state_.currentTabIndex;

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, kCloseCurrentTabCommand, L"关闭当前标签页");
    AppendMenuW(menu, MF_STRING | (state_.tabs.size() <= 1 ? MF_GRAYED : 0),
                kCloseOtherTabsCommand, L"关闭其他标签页");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCloseAllTabsCommand, L"关闭全部标签页");
    SetForegroundWindow(mainWindow_);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        screenPoint.x, screenPoint.y, 0, mainWindow_, nullptr);
    DestroyMenu(menu);
    if (command == kCloseCurrentTabCommand) CloseTab(index);
    else if (command == kCloseOtherTabsCommand) CloseOtherTabs(index);
    else if (command == kCloseAllTabsCommand) CloseAllTabs();
    PostMessageW(mainWindow_, WM_NULL, 0, 0);
}

std::wstring Application::CurrentUrl() const {
    if (state_.currentTabIndex < 0 || state_.currentTabIndex >= static_cast<int>(state_.tabs.size())) return {};
    return state_.tabs[static_cast<size_t>(state_.currentTabIndex)].url;
}

std::wstring Application::CurrentTitle() const {
    if (state_.currentTabIndex < 0 || state_.currentTabIndex >= static_cast<int>(state_.tabs.size())) return {};
    return state_.tabs[static_cast<size_t>(state_.currentTabIndex)].title;
}

void Application::NavigateAddressBar() {
    NavigateTo(NormalizeInputUrl(WindowText(addressEdit_)));
    SetFocus(mainWindow_);
}

void Application::UpdateBookmarkStar() {
    const std::wstring target = NormalizeComparableUrl(CurrentUrl());
    const bool exists = std::any_of(state_.bookmarks.begin(), state_.bookmarks.end(),
        [&](const Bookmark& bookmark) { return NormalizeComparableUrl(bookmark.url) == target; });
    SetWindowTextW(starButton_, exists ? L"★" : L"☆");
    InvalidateRect(starButton_, nullptr, TRUE);
}

void Application::ToggleCurrentBookmark() {
    const std::wstring url = CurrentUrl();
    if (url.empty()) return;
    const std::wstring target = NormalizeComparableUrl(url);
    const auto iterator = std::find_if(state_.bookmarks.begin(), state_.bookmarks.end(),
        [&](const Bookmark& bookmark) { return NormalizeComparableUrl(bookmark.url) == target; });
    if (iterator == state_.bookmarks.end()) {
        state_.bookmarks.push_back({CurrentTitle().empty() ? url : CurrentTitle(), url});
    } else {
        state_.bookmarks.erase(iterator);
    }
    UpdateBookmarkStar();
    RebuildBookmarkList();
    SaveConfiguration();
}

void Application::ShowBookmarkMenu() {
    constexpr UINT kBookmarkCommandBase = 50000;
    constexpr UINT kManageBookmarksCommand = 52000;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    if (state_.bookmarks.empty()) {
        AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, L"暂无书签");
    } else {
        const std::wstring current = NormalizeComparableUrl(CurrentUrl());
        for (size_t index = 0; index < state_.bookmarks.size(); ++index) {
            std::wstring title = state_.bookmarks[index].title.empty()
                ? state_.bookmarks[index].url : state_.bookmarks[index].title;
            if (title.size() > 48) title = title.substr(0, 47) + L"…";
            size_t marker = 0;
            while ((marker = title.find(L'&', marker)) != std::wstring::npos) {
                title.insert(marker, 1, L'&');
                marker += 2;
            }
            UINT flags = MF_STRING;
            if (NormalizeComparableUrl(state_.bookmarks[index].url) == current) flags |= MF_CHECKED;
            AppendMenuW(menu, flags, kBookmarkCommandBase + static_cast<UINT>(index), title.c_str());
        }
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kManageBookmarksCommand, L"管理书签…");

    RECT button{};
    GetWindowRect(bookmarkButton_, &button);
    SetForegroundWindow(mainWindow_);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN |
        TPM_RIGHTBUTTON, button.left, button.bottom + Scale(2), 0, mainWindow_, nullptr);
    DestroyMenu(menu);
    if (command >= kBookmarkCommandBase && command < kBookmarkCommandBase + state_.bookmarks.size()) {
        NavigateTo(state_.bookmarks[static_cast<size_t>(command - kBookmarkCommandBase)].url);
    } else if (command == kManageBookmarksCommand) {
        ShowBookmarkWindow();
    }
}

void Application::NavigateTo(const std::wstring& url, bool concealUntilContent) {
    const std::wstring normalized = NormalizeInputUrl(url);
    UpdateCurrentTabUrl(normalized);
    if (webView_) {
        if (concealUntilContent) {
            if (windowModes_.IsWebFullscreen()) {
                ExecuteScript(L"if(document.fullscreenElement) document.exitFullscreen();");
                windowModes_.LeaveWebFullscreen();
            }
            webView_->Stop();
            waitingForTabContent_ = true;
            waitingNavigationId_ = 0;
            if (webViewController_) webViewController_->put_IsVisible(FALSE);
            InvalidateRect(mainWindow_, nullptr, TRUE);
            UpdateWindow(mainWindow_);
        }
        const HRESULT result = webView_->Navigate(normalized.c_str());
        if (FAILED(result)) {
            waitingForTabContent_ = false;
            waitingNavigationId_ = 0;
            if (webViewController_ && !windowModes_.IsHidden()) {
                webViewController_->put_IsVisible(TRUE);
            }
            ShowError(L"导航失败", L"无法打开该地址。", result);
        }
    }
}

void Application::ToggleHidden() {
    SetHidden(!windowModes_.IsHidden());
}

void Application::SetHidden(bool hidden) {
    hotkeys_.CancelActiveGesture();
    if (hidden && state_.settings.autoPauseOnHide) {
        ExecuteScript(L"(()=>{const v=document.querySelector('video');if(v&&!v.paused)v.pause();})()");
    }
    if (webViewController_) {
        const BOOL render = !hidden || state_.settings.backgroundMediaHotkeys;
        webViewController_->put_IsVisible(render && !waitingForTabContent_);
    }
    windowModes_.SetHidden(hidden);
    state_.windowHidden = hidden;
    if (!hidden) {
        ResizeWebView();
        if (webViewController_) {
            webViewController_->put_IsVisible(waitingForTabContent_ ? FALSE : TRUE);
            webViewController_->NotifyParentWindowPositionChanged();
        }
        RedrawWindow(mainWindow_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
    }
    RefreshHotkeys();
}

void Application::ToggleImmersion() {
    hotkeys_.CancelActiveGesture();
    windowModes_.ToggleImmersion();
    state_.immersionActive = windowModes_.IsImmersive();
    RefreshOpacityControls();
}

void Application::MinimizeWindow() {
    if (state_.settings.useSystemTray) SetHidden(true);
    else ShowWindow(mainWindow_, SW_MINIMIZE);
}

void Application::RefreshHotkeys() {
    const bool suppressed = captureAction_.has_value() ||
        (state_.settings.disableHotkeysOnTyping && inputGuard_.IsTyping());
    hotkeys_.Refresh(windowModes_.IsHidden(), state_.settings.backgroundMediaHotkeys, suppressed);
    if (!hotkeys_.RegistrationErrors().empty()) PostMessageW(mainWindow_, kShowRegistrationErrors, 0, 0);
}

void Application::ShowHotkeyErrors() {
    if (hotkeys_.RegistrationErrors().empty()) return;
    std::wstring message = L"以下全局热键已被其他程序占用：\n\n";
    for (const std::wstring& error : hotkeys_.RegistrationErrors()) message += L"• " + error + L"\n";
    message += L"\n可在“设置 → 快捷键”中重新指定。";
    MessageBoxW(settingsWindow_ ? settingsWindow_ : mainWindow_, message.c_str(),
                L"热键冲突", MB_OK | MB_ICONWARNING);
}

void Application::AddTrayIcon() {
    if (trayIconAdded_ || !mainWindow_) return;
    trayData_ = {};
    trayData_.cbSize = sizeof(trayData_);
    trayData_.hWnd = mainWindow_;
    trayData_.uID = kTrayIconId;
    trayData_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    trayData_.uCallbackMessage = kTrayMessage;
    trayData_.hIcon = smallIcon_ ? smallIcon_ : largeIcon_;
    wcscpy_s(trayData_.szTip, L"小窗浏览器 v1.4");
    trayIconAdded_ = Shell_NotifyIconW(NIM_ADD, &trayData_) != FALSE;
    if (trayIconAdded_) {
        trayData_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &trayData_);
    }
}

void Application::RemoveTrayIcon() {
    if (!trayIconAdded_) return;
    Shell_NotifyIconW(NIM_DELETE, &trayData_);
    trayIconAdded_ = false;
}

void Application::RefreshTrayMode() {
    windowModes_.SetSystemTray(state_.settings.useSystemTray);
    if (state_.settings.useSystemTray) AddTrayIcon(); else RemoveTrayIcon();
}

void Application::ShowTrayMenu() {
    POINT point{};
    GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, 1, L"打开小窗");
    AppendMenuW(menu, MF_STRING, 2, L"设置");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"退出程序");
    SetForegroundWindow(mainWindow_);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        point.x, point.y, 0, mainWindow_, nullptr);
    DestroyMenu(menu);
    if (command == 1) RestoreFromUserAction();
    else if (command == 2) { RestoreFromUserAction(); ShowSettingsWindow(); }
    else if (command == 3) SendMessageW(mainWindow_, WM_CLOSE, 0, 0);
    PostMessageW(mainWindow_, WM_NULL, 0, 0);
}

void Application::RestoreFromUserAction() {
    if (windowModes_.IsHidden()) SetHidden(false);
    if (IsIconic(mainWindow_)) ShowWindow(mainWindow_, SW_RESTORE);
    else ShowWindow(mainWindow_, SW_SHOW);
    SetWindowPos(mainWindow_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(mainWindow_);
    SetForegroundWindow(mainWindow_);
    if (webViewController_) {
        ResizeWebView();
        webViewController_->put_IsVisible(waitingForTabContent_ ? FALSE : TRUE);
        webViewController_->NotifyParentWindowPositionChanged();
    }
    RedrawWindow(mainWindow_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void Application::ShowSettingsWindow() {
    if (windowModes_.IsHidden()) SetHidden(false);
    if (!settingsWindow_) {
        settingsWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW, kSettingsWindowClass,
            L"设置 · 小窗浏览器", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VSCROLL,
            CW_USEDEFAULT, CW_USEDEFAULT, Scale(720), Scale(700), mainWindow_, nullptr, instance_, this);
        if (!settingsWindow_) return;
        CenterOwnedWindow(settingsWindow_, Scale(720), Scale(700));
    }
    RefreshSettingsControls();
    ShowWindow(settingsWindow_, SW_SHOWNORMAL);
    BringWindowToTop(settingsWindow_);
    SetForegroundWindow(settingsWindow_);
}

void Application::CreateSettingsControls(HWND window) {
    const int left = Scale(24);
    const int labelWidth = Scale(150);
    const int fieldX = left + labelWidth;
    const int fieldWidth = Scale(490);
    const int row = Scale(38);
    int y = Scale(18);
    auto heading = [&](const wchar_t* text) {
        HWND label = CreateLabel(window, text);
        MoveWindow(label, left, y, Scale(640), Scale(28), FALSE);
        SetControlFont(label, titleFont_);
        y += Scale(34);
    };
    const auto label = [&](const wchar_t* text, int atY) {
        HWND control = CreateLabel(window, text);
        MoveWindow(control, left, atY + Scale(5), labelWidth - Scale(10), Scale(24), FALSE);
        return control;
    };
    const auto checkbox = [&](const wchar_t* text, int id, int atY) {
        HWND control = CreateWindowExW(0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            fieldX, atY, fieldWidth, Scale(28), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        SetControlFont(control);
        return control;
    };
    const auto edit = [&](int id, int atY, int width) {
        HWND control = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
            fieldX, atY, width, Scale(28), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        SetControlFont(control);
        return control;
    };
    const auto combo = [&](int id, int atY, int width) {
        HWND control = CreateWindowExW(0, WC_COMBOBOXW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            fieldX, atY, width, Scale(220), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        SetControlFont(control);
        return control;
    };

    heading(L"常规");
    label(L"主页地址", y);
    settingsControls_.home = edit(SettingsHome, y, fieldWidth);
    y += row;
    settingsControls_.tray = checkbox(L"最小化时隐藏到系统托盘", SettingsTray, y);
    y += Scale(30);
    settingsControls_.autoPause = checkbox(L"隐藏窗口时自动暂停视频", SettingsAutoPause, y);
    y += Scale(30);
    settingsControls_.autoFitFullscreen = checkbox(
        L"网页视频全屏时自动按画面比例调整小窗大小", SettingsAutoFitFullscreen, y);
    y += Scale(30);
    settingsControls_.maximizedTopDrag = checkbox(
        L"小窗全屏时拖动顶部空白可还原并移动（关闭后仅按钮可退出）",
        SettingsMaximizedTopDrag, y);
    y += Scale(42);

    heading(L"沉浸");
    label(L"挖孔半径（像素）", y);
    settingsControls_.radius = edit(SettingsRadius, y, Scale(130));
    label(L"吸附距离", y + row);
    settingsControls_.snap = edit(SettingsSnap, y + row, Scale(130));
    y += row * 2;
    label(L"沉浸模式", y);
    settingsControls_.style = combo(SettingsStyle, y, Scale(230));
    SendMessageW(settingsControls_.style, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"鼠标圆形挖孔"));
    SendMessageW(settingsControls_.style, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"整体透明（移入隐藏）"));
    y += row;
    settingsControls_.opacityLabel = label(L"整体透明模式不透明度", y);
    settingsControls_.opacitySlider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
        fieldX, y, Scale(330), Scale(32), window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(SettingsOpacitySlider)), instance_, nullptr);
    SendMessageW(settingsControls_.opacitySlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(settingsControls_.opacitySlider, TBM_SETTICFREQ, 10, 0);
    settingsControls_.opacityEdit = edit(SettingsOpacityEdit, y, Scale(72));
    MoveWindow(settingsControls_.opacityEdit, fieldX + Scale(350), y, Scale(72), Scale(28), FALSE);
    settingsControls_.opacityPercent = CreateLabel(window, L"%");
    MoveWindow(settingsControls_.opacityPercent, fieldX + Scale(428), y + Scale(4), Scale(30), Scale(24), FALSE);
    y += row;
    settingsControls_.typing = checkbox(L"输入文字时暂停全部全局热键", SettingsTyping, y);
    y += Scale(30);
    settingsControls_.backgroundMedia = checkbox(L"窗口隐藏时仍允许全部媒体热键", SettingsBackgroundMedia, y);
    y += Scale(44);

    heading(L"快捷键（点击后按下新按键，可组合 Ctrl / Alt / Shift）");
    const auto defaults = DefaultHotkeys();
    for (size_t index = 0; index < kHotkeyCount; ++index) {
        label(defaults[index].displayName.c_str(), y);
        settingsControls_.hotkeys[index] = CreateThemedButton(
            window, L"", SettingsHotkeyBase + static_cast<int>(index));
        MoveWindow(settingsControls_.hotkeys[index], fieldX, y, Scale(270), Scale(29), FALSE);
        y += Scale(33);
    }
    y += Scale(12);

    heading(L"性能与外观");
    label(L"长按快进倍速", y);
    settingsControls_.holdRate = combo(SettingsHoldRate, y, Scale(150));
    for (int rate = 2; rate <= 5; ++rate) {
        const std::wstring text = std::to_wstring(rate) + L"×";
        SendMessageW(settingsControls_.holdRate, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
    y += row;
    label(L"网页渲染模式", y);
    settingsControls_.renderMode = combo(SettingsRenderMode, y, Scale(260));
    SendMessageW(settingsControls_.renderMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自动 GPU（推荐）"));
    SendMessageW(settingsControls_.renderMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"软件兼容模式（需重启）"));
    y += row;
    label(L"界面主题", y);
    settingsControls_.theme = combo(SettingsTheme, y, Scale(220));
    SendMessageW(settingsControls_.theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"跟随系统"));
    SendMessageW(settingsControls_.theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"浅色"));
    SendMessageW(settingsControls_.theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"深色"));
    y += Scale(44);
    settingsControls_.diagnostics = CreateThemedButton(window, L"复制诊断信息", SettingsDiagnostics);
    MoveWindow(settingsControls_.diagnostics, fieldX, y, Scale(190), Scale(32), FALSE);
    settingsControls_.about = CreateThemedButton(window, L"关于 v1.4", SettingsAbout);
    MoveWindow(settingsControls_.about, fieldX + Scale(202), y, Scale(130), Scale(32), FALSE);
    settingsControls_.repository = CreateThemedButton(window, L"GitHub 项目主页", SettingsRepository);
    MoveWindow(settingsControls_.repository, fieldX + Scale(344), y, Scale(160), Scale(32), FALSE);
    HWND restartNote = CreateLabel(window, L"渲染模式变更将在下次启动时生效。独占全屏游戏可能绕过 DWM，普通窗口无法覆盖。");
    MoveWindow(restartNote, left, y + Scale(44), Scale(650), Scale(48), FALSE);
    y += Scale(102);

    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
    info.nMin = 0;
    info.nMax = y;
    RECT client{};
    GetClientRect(window, &client);
    info.nPage = static_cast<UINT>(std::max(1L, client.bottom));
    info.nPos = 0;
    SetScrollInfo(window, SB_VERT, &info, TRUE);
}

void Application::ReadSettingsControls() {
    if (!settingsWindow_ || updatingSettingsControls_) return;
    AppSettings old = state_.settings;
    state_.settings.holeRadius = ParseInteger(settingsControls_.radius, old.holeRadius);
    state_.settings.snapThreshold = ParseInteger(settingsControls_.snap, old.snapThreshold);
    state_.settings.autoPauseOnHide = IsChecked(settingsControls_.autoPause);
    state_.settings.autoFitVideoFullscreen = IsChecked(settingsControls_.autoFitFullscreen);
    state_.settings.maximizedTopDragEnabled = IsChecked(settingsControls_.maximizedTopDrag);
    state_.settings.disableHotkeysOnTyping = IsChecked(settingsControls_.typing);
    state_.settings.useSystemTray = IsChecked(settingsControls_.tray);
    state_.settings.backgroundMediaHotkeys = IsChecked(settingsControls_.backgroundMedia);
    state_.settings.homeUrl = NormalizeInputUrl(WindowText(settingsControls_.home));
    state_.settings.immersionStyle = static_cast<ImmersionStyle>(ComboValue(settingsControls_.style));
    const int opacity = std::clamp(ParseInteger(settingsControls_.opacityEdit,
        static_cast<int>(SendMessageW(settingsControls_.opacitySlider, TBM_GETPOS, 0, 0))), 0, 100);
    state_.settings.autoHideOpacityPercent = opacity;
    state_.settings.holdPlaybackRate = ComboValue(settingsControls_.holdRate, old.holdPlaybackRate - 2) + 2;
    state_.settings.renderMode = static_cast<RenderMode>(ComboValue(settingsControls_.renderMode));
    state_.settings.themeMode = static_cast<ThemeMode>(ComboValue(settingsControls_.theme));
    state_.settings.Clamp();

    if (old.themeMode != state_.settings.themeMode) {
        theme_.SetMode(state_.settings.themeMode);
        ApplyThemeToAllWindows();
    }
    if (old.useSystemTray != state_.settings.useSystemTray) RefreshTrayMode();
    if (old.immersionStyle != state_.settings.immersionStyle ||
        old.holeOpacityPercent != state_.settings.holeOpacityPercent ||
        old.autoHideOpacityPercent != state_.settings.autoHideOpacityPercent ||
        old.holeRadius != state_.settings.holeRadius) {
        windowModes_.RefreshVisualState();
    }
    if (old.disableHotkeysOnTyping != state_.settings.disableHotkeysOnTyping ||
        old.backgroundMediaHotkeys != state_.settings.backgroundMediaHotkeys) {
        RefreshHotkeys();
    }
    SaveConfiguration();
}

void Application::RefreshSettingsControls() {
    if (!settingsWindow_) return;
    updatingSettingsControls_ = true;
    SetWindowTextW(settingsControls_.radius, std::to_wstring(state_.settings.holeRadius).c_str());
    SetWindowTextW(settingsControls_.snap, std::to_wstring(state_.settings.snapThreshold).c_str());
    SetWindowTextW(settingsControls_.home, state_.settings.homeUrl.c_str());
    SetCheck(settingsControls_.autoPause, state_.settings.autoPauseOnHide);
    SetCheck(settingsControls_.autoFitFullscreen, state_.settings.autoFitVideoFullscreen);
    SetCheck(settingsControls_.maximizedTopDrag, state_.settings.maximizedTopDragEnabled);
    SetCheck(settingsControls_.typing, state_.settings.disableHotkeysOnTyping);
    SetCheck(settingsControls_.tray, state_.settings.useSystemTray);
    SetCheck(settingsControls_.backgroundMedia, state_.settings.backgroundMediaHotkeys);
    SelectComboValue(settingsControls_.style, static_cast<int>(state_.settings.immersionStyle));
    SelectComboValue(settingsControls_.holdRate, state_.settings.holdPlaybackRate - 2);
    SelectComboValue(settingsControls_.renderMode, static_cast<int>(state_.settings.renderMode));
    SelectComboValue(settingsControls_.theme, static_cast<int>(state_.settings.themeMode));
    for (size_t index = 0; index < kHotkeyCount; ++index) {
        SetWindowTextW(settingsControls_.hotkeys[index], HotkeyDisplayText(state_.hotkeys[index]).c_str());
    }
    updatingSettingsControls_ = false;
    RefreshOpacityControls();
}

void Application::RefreshOpacityControls() {
    if (!settingsWindow_ || !settingsControls_.opacitySlider || updatingSettingsControls_) return;
    updatingSettingsControls_ = true;
    const bool visible = state_.settings.immersionStyle == ImmersionStyle::AutoHide;
    const int value = state_.settings.autoHideOpacityPercent;
    SendMessageW(settingsControls_.opacitySlider, TBM_SETPOS, TRUE, value);
    SetWindowTextW(settingsControls_.opacityEdit, std::to_wstring(value).c_str());
    const HWND opacityControls[] = {settingsControls_.opacityLabel, settingsControls_.opacitySlider,
                                    settingsControls_.opacityEdit, settingsControls_.opacityPercent};
    for (HWND control : opacityControls) {
        if (control) ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
    }
    updatingSettingsControls_ = false;
}

void Application::BeginHotkeyCapture(HotkeyAction action) {
    CancelHotkeyCapture();
    captureAction_ = action;
    HWND button = settingsControls_.hotkeys[HotkeyIndex(action)];
    SetWindowTextW(button, L"请按新按键…（Esc 取消）");
    SetFocus(button ? button : settingsWindow_);
    RefreshHotkeys();
    hotkeys_.BeginMouseCapture([this](UINT key, UINT modifiers) {
        ApplyCapturedHotkey(key, modifiers);
    });
}

bool Application::HandleHotkeyCaptureKey(UINT virtualKey) {
    if (!captureAction_) return false;
    if (virtualKey == VK_ESCAPE) {
        CancelHotkeyCapture();
        return true;
    }
    if (virtualKey == VK_CONTROL || virtualKey == VK_LCONTROL || virtualKey == VK_RCONTROL ||
        virtualKey == VK_SHIFT || virtualKey == VK_LSHIFT || virtualKey == VK_RSHIFT ||
        virtualKey == VK_MENU || virtualKey == VK_LMENU || virtualKey == VK_RMENU ||
        virtualKey == VK_LWIN || virtualKey == VK_RWIN) {
        return true;
    }
    UINT modifiers = 0;
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) modifiers |= MOD_CONTROL;
    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) modifiers |= MOD_SHIFT;
    if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) modifiers |= MOD_ALT;
    ApplyCapturedHotkey(virtualKey, modifiers);
    return true;
}

void Application::ApplyCapturedHotkey(UINT virtualKey, UINT modifiers) {
    if (!captureAction_) return;
    const size_t target = HotkeyIndex(*captureAction_);
    for (size_t index = 0; index < kHotkeyCount; ++index) {
        if (index != target && state_.hotkeys[index].virtualKey == virtualKey &&
            state_.hotkeys[index].modifiers == modifiers) {
            MessageBoxW(settingsWindow_, L"该组合已被另一项小窗功能使用。", L"快捷键重复",
                        MB_OK | MB_ICONWARNING);
            CancelHotkeyCapture();
            return;
        }
    }
    state_.hotkeys[target].virtualKey = virtualKey;
    state_.hotkeys[target].modifiers = modifiers;
    captureAction_.reset();
    hotkeys_.CancelMouseCapture();
    RefreshSettingsControls();
    inputGuard_.Refresh();
    RefreshHotkeys();
    SaveConfiguration();
}

void Application::CancelHotkeyCapture() {
    const bool wasCapturing = captureAction_.has_value();
    if (captureAction_) {
        const size_t index = HotkeyIndex(*captureAction_);
        SetWindowTextW(settingsControls_.hotkeys[index], HotkeyDisplayText(state_.hotkeys[index]).c_str());
    }
    captureAction_.reset();
    hotkeys_.CancelMouseCapture();
    if (wasCapturing) {
        inputGuard_.Refresh();
        RefreshHotkeys();
    }
}

LRESULT CALLBACK Application::SettingsWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        application = static_cast<Application*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
    }
    return application ? application->HandleSettingsMessage(window, message, wParam, lParam)
                       : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::HandleSettingsMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateSettingsControls(window);
        theme_.ApplyWindowTheme(window);
        ApplyThemeToWindowTree(window, theme_.IsDark());
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id >= SettingsHotkeyBase && id < SettingsHotkeyBase + static_cast<int>(kHotkeyCount)) {
            BeginHotkeyCapture(static_cast<HotkeyAction>(id - SettingsHotkeyBase));
            return 0;
        }
        if (id == SettingsDiagnostics) {
            RequestMediaDiagnostics();
            return 0;
        }
        if (id == SettingsAbout) {
            MessageBoxW(window,
                L"小窗浏览器 v1.4.0\n\n"
                L"专为单屏玩家打造的 Windows 画中画浏览器\n"
                L"C++20 / Win32 / WebView2 1.0.4078.44\n\n"
                L"https://github.com/azurplain/Mini-Window-Browser",
                L"关于小窗浏览器", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        if (id == SettingsRepository) {
            const HINSTANCE opened = ShellExecuteW(
                window, L"open", L"https://github.com/azurplain/Mini-Window-Browser",
                nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(opened) <= 32) {
                MessageBoxW(window, L"无法打开浏览器，请手动访问仓库地址。",
                            L"打开失败", MB_OK | MB_ICONWARNING);
            }
            return 0;
        }
        const int notification = HIWORD(wParam);
        if (id == SettingsStyle && notification == CBN_SELCHANGE) {
            state_.settings.immersionStyle = static_cast<ImmersionStyle>(ComboValue(settingsControls_.style));
            state_.settings.Clamp();
            RefreshOpacityControls();
            windowModes_.RefreshVisualState();
            SaveConfiguration();
        } else if (notification == BN_CLICKED || notification == CBN_SELCHANGE ||
            notification == EN_KILLFOCUS) {
            ReadSettingsControls();
        }
        return 0;
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == settingsControls_.opacitySlider) {
            const int value = static_cast<int>(SendMessageW(settingsControls_.opacitySlider, TBM_GETPOS, 0, 0));
            updatingSettingsControls_ = true;
            SetWindowTextW(settingsControls_.opacityEdit, std::to_wstring(value).c_str());
            updatingSettingsControls_ = false;
            state_.settings.autoHideOpacityPercent = value;
            windowModes_.RefreshVisualState();
            SetTimer(window, kSettingsSaveTimerId, 250, nullptr);
        }
        return 0;
    case WM_TIMER:
        if (wParam == kSettingsSaveTimerId) {
            KillTimer(window, kSettingsSaveTimerId);
            SaveConfiguration();
            return 0;
        }
        break;
    case WM_VSCROLL: {
        SCROLLINFO info{sizeof(info), SIF_ALL};
        GetScrollInfo(window, SB_VERT, &info);
        int next = info.nPos;
        switch (LOWORD(wParam)) {
        case SB_LINEUP: next -= Scale(30); break;
        case SB_LINEDOWN: next += Scale(30); break;
        case SB_PAGEUP: next -= static_cast<int>(info.nPage); break;
        case SB_PAGEDOWN: next += static_cast<int>(info.nPage); break;
        case SB_THUMBTRACK: next = info.nTrackPos; break;
        default: break;
        }
        next = std::clamp(next, info.nMin, std::max(info.nMin, info.nMax - static_cast<int>(info.nPage) + 1));
        if (next != info.nPos) {
            ScrollWindowEx(window, 0, info.nPos - next, nullptr, nullptr, nullptr, nullptr,
                           SW_INVALIDATE | SW_ERASE | SW_SCROLLCHILDREN);
            info.fMask = SIF_POS;
            info.nPos = next;
            SetScrollInfo(window, SB_VERT, &info, TRUE);
            settingsScrollPosition_ = next;
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
        SendMessageW(window, WM_VSCROLL,
            MAKEWPARAM(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? SB_LINEUP : SB_LINEDOWN, 0), 0);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (HandleHotkeyCaptureKey(static_cast<UINT>(wParam))) return 0;
        break;
    case WM_DRAWITEM:
        DrawOwnerItem(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) {
            RedrawWindow(mainWindow_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
            RedrawWindow(window, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
        break;
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, theme_.Palette().text);
        SetBkColor(dc, theme_.Palette().surface);
        return reinterpret_cast<LRESULT>(theme_.SurfaceBrush());
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, theme_.Palette().text);
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(theme_.WindowBrush());
    }
    case WM_ERASEBKGND: {
        RECT client{}; GetClientRect(window, &client);
        theme_.FillBackground(reinterpret_cast<HDC>(wParam), client, theme_.Palette().window);
        return 1;
    }
    case WM_CLOSE:
        KillTimer(window, kSettingsSaveTimerId);
        SaveConfiguration();
        CancelHotkeyCapture();
        ShowWindow(window, SW_HIDE);
        return 0;
    case WM_DESTROY:
        settingsWindow_ = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void Application::ShowBookmarkWindow() {
    if (!bookmarkWindow_) {
        bookmarkWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW, kBookmarkWindowClass,
            L"书签 · 小窗浏览器", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
            CW_USEDEFAULT, CW_USEDEFAULT, Scale(650), Scale(500), mainWindow_, nullptr, instance_, this);
        if (!bookmarkWindow_) return;
        CenterOwnedWindow(bookmarkWindow_, Scale(650), Scale(500));
    }
    RebuildBookmarkList();
    ShowWindow(bookmarkWindow_, SW_SHOWNORMAL);
    SetForegroundWindow(bookmarkWindow_);
}

void Application::CreateBookmarkControls(HWND window) {
    bookmarkControls_.list = CreateWindowExW(0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        Scale(18), Scale(18), Scale(240), Scale(400), window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(BookmarkList)), instance_, nullptr);
    HWND titleLabel = CreateLabel(window, L"标题");
    MoveWindow(titleLabel, Scale(282), Scale(24), Scale(70), Scale(24), FALSE);
    bookmarkControls_.title = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        Scale(282), Scale(52), Scale(330), Scale(30), window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(BookmarkTitle)), instance_, nullptr);
    HWND urlLabel = CreateLabel(window, L"网址");
    MoveWindow(urlLabel, Scale(282), Scale(98), Scale(70), Scale(24), FALSE);
    bookmarkControls_.url = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL,
        Scale(282), Scale(126), Scale(330), Scale(90), window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(BookmarkUrl)), instance_, nullptr);
    HWND save = CreateThemedButton(window, L"保存 / 新增", BookmarkSave);
    HWND open = CreateThemedButton(window, L"打开", BookmarkOpen);
    HWND remove = CreateThemedButton(window, L"删除", BookmarkDelete);
    MoveWindow(save, Scale(282), Scale(242), Scale(126), Scale(34), FALSE);
    MoveWindow(open, Scale(420), Scale(242), Scale(92), Scale(34), FALSE);
    MoveWindow(remove, Scale(520), Scale(242), Scale(92), Scale(34), FALSE);
    const HWND controls[] = {bookmarkControls_.list, bookmarkControls_.title,
        bookmarkControls_.url, titleLabel, urlLabel, save, open, remove};
    for (HWND control : controls) SetControlFont(control);
}

void Application::RebuildBookmarkList() {
    if (!bookmarkControls_.list) return;
    SendMessageW(bookmarkControls_.list, LB_RESETCONTENT, 0, 0);
    for (const Bookmark& bookmark : state_.bookmarks) {
        SendMessageW(bookmarkControls_.list, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(bookmark.title.c_str()));
    }
    if (selectedBookmark_ >= static_cast<int>(state_.bookmarks.size())) selectedBookmark_ = -1;
    if (selectedBookmark_ >= 0) SendMessageW(bookmarkControls_.list, LB_SETCURSEL, selectedBookmark_, 0);
}

void Application::LoadSelectedBookmark() {
    const LRESULT selected = SendMessageW(bookmarkControls_.list, LB_GETCURSEL, 0, 0);
    selectedBookmark_ = selected == LB_ERR ? -1 : static_cast<int>(selected);
    if (selectedBookmark_ < 0 || selectedBookmark_ >= static_cast<int>(state_.bookmarks.size())) return;
    const Bookmark& bookmark = state_.bookmarks[static_cast<size_t>(selectedBookmark_)];
    SetWindowTextW(bookmarkControls_.title, bookmark.title.c_str());
    SetWindowTextW(bookmarkControls_.url, bookmark.url.c_str());
}

LRESULT CALLBACK Application::BookmarkWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        application = static_cast<Application*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
    }
    return application ? application->HandleBookmarkMessage(window, message, wParam, lParam)
                       : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::HandleBookmarkMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateBookmarkControls(window);
        theme_.ApplyWindowTheme(window);
        ApplyThemeToWindowTree(window, theme_.IsDark());
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == BookmarkList && HIWORD(wParam) == LBN_SELCHANGE) LoadSelectedBookmark();
        else if (id == BookmarkList && HIWORD(wParam) == LBN_DBLCLK) {
            LoadSelectedBookmark();
            if (selectedBookmark_ >= 0) {
                NavigateTo(state_.bookmarks[static_cast<size_t>(selectedBookmark_)].url);
                ShowWindow(window, SW_HIDE);
            }
        } else if (id == BookmarkSave) {
            Bookmark value{WindowText(bookmarkControls_.title),
                           NormalizeInputUrl(WindowText(bookmarkControls_.url))};
            if (value.title.empty()) value.title = value.url;
            if (selectedBookmark_ >= 0 && selectedBookmark_ < static_cast<int>(state_.bookmarks.size())) {
                state_.bookmarks[static_cast<size_t>(selectedBookmark_)] = std::move(value);
            } else {
                state_.bookmarks.push_back(std::move(value));
                selectedBookmark_ = static_cast<int>(state_.bookmarks.size()) - 1;
            }
            RebuildBookmarkList(); UpdateBookmarkStar(); SaveConfiguration();
        } else if (id == BookmarkOpen && selectedBookmark_ >= 0 &&
                   selectedBookmark_ < static_cast<int>(state_.bookmarks.size())) {
            NavigateTo(state_.bookmarks[static_cast<size_t>(selectedBookmark_)].url);
            ShowWindow(window, SW_HIDE);
        } else if (id == BookmarkDelete && selectedBookmark_ >= 0 &&
                   selectedBookmark_ < static_cast<int>(state_.bookmarks.size())) {
            state_.bookmarks.erase(state_.bookmarks.begin() + selectedBookmark_);
            selectedBookmark_ = -1;
            SetWindowTextW(bookmarkControls_.title, L"");
            SetWindowTextW(bookmarkControls_.url, L"");
            RebuildBookmarkList(); UpdateBookmarkStar(); SaveConfiguration();
        }
        return 0;
    }
    case WM_SIZE: {
        RECT client{}; GetClientRect(window, &client);
        const int margin = Scale(18), split = Scale(250), rightX = Scale(282);
        MoveWindow(bookmarkControls_.list, margin, margin, split - margin,
                   std::max(Scale(120), static_cast<int>(client.bottom) - margin * 2), TRUE);
        MoveWindow(bookmarkControls_.title, rightX, Scale(52),
                   std::max(Scale(160), static_cast<int>(client.right) - rightX - margin), Scale(30), TRUE);
        MoveWindow(bookmarkControls_.url, rightX, Scale(126),
                   std::max(Scale(160), static_cast<int>(client.right) - rightX - margin), Scale(90), TRUE);
        return 0;
    }
    case WM_DRAWITEM:
        DrawOwnerItem(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam)); return TRUE;
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) {
            RedrawWindow(mainWindow_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
            RedrawWindow(window, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam); SetTextColor(dc, theme_.Palette().text);
        SetBkColor(dc, theme_.Palette().surface); return reinterpret_cast<LRESULT>(theme_.SurfaceBrush());
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam); SetTextColor(dc, theme_.Palette().text);
        SetBkMode(dc, TRANSPARENT); return reinterpret_cast<LRESULT>(theme_.WindowBrush());
    }
    case WM_ERASEBKGND: {
        RECT client{}; GetClientRect(window, &client);
        theme_.FillBackground(reinterpret_cast<HDC>(wParam), client, theme_.Palette().window); return 1;
    }
    case WM_CLOSE: ShowWindow(window, SW_HIDE); return 0;
    case WM_DESTROY: bookmarkWindow_ = nullptr; return 0;
    default: break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void Application::ShowPresetWindow() {
    if (!presetWindow_) {
        presetWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW, kPresetWindowClass,
            L"场景预设 · 小窗浏览器", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
            CW_USEDEFAULT, CW_USEDEFAULT, Scale(620), Scale(490), mainWindow_, nullptr, instance_, this);
        if (!presetWindow_) return;
        CenterOwnedWindow(presetWindow_, Scale(620), Scale(490));
    }
    RebuildPresetList();
    ShowWindow(presetWindow_, SW_SHOWNORMAL);
    SetForegroundWindow(presetWindow_);
}

void Application::CreatePresetControls(HWND window) {
    presetControls_.list = CreateWindowExW(0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        Scale(18), Scale(18), Scale(250), Scale(365), window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(PresetList)), instance_, nullptr);
    HWND label = CreateLabel(window, L"预设名称");
    MoveWindow(label, Scale(292), Scale(24), Scale(220), Scale(24), FALSE);
    presetControls_.name = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        Scale(292), Scale(54), Scale(290), Scale(30), window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(PresetName)), instance_, nullptr);
    HWND apply = CreateThemedButton(window, L"一键应用", PresetApply);
    HWND add = CreateThemedButton(window, L"保存当前场景", PresetAdd);
    HWND rename = CreateThemedButton(window, L"重命名", PresetSaveName);
    HWND remove = CreateThemedButton(window, L"删除", PresetDelete);
    HWND reset = CreateThemedButton(window, L"恢复默认设置", PresetReset);
    MoveWindow(apply, Scale(292), Scale(110), Scale(140), Scale(35), FALSE);
    MoveWindow(add, Scale(442), Scale(110), Scale(140), Scale(35), FALSE);
    MoveWindow(rename, Scale(292), Scale(158), Scale(140), Scale(35), FALSE);
    MoveWindow(remove, Scale(442), Scale(158), Scale(140), Scale(35), FALSE);
    MoveWindow(reset, Scale(292), Scale(220), Scale(180), Scale(35), FALSE);
    HWND note = CreateWindowExW(0, L"STATIC",
        L"预设保存窗口、沉浸参数、热键及全部标签页；主题和 GPU 模式保持全局。\r\n\r\n应用预设不会删除书签；会恢复保存时的标签页。",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_EDITCONTROL | SS_NOPREFIX,
        Scale(292), Scale(272), Scale(290), Scale(130), window, nullptr, instance_, nullptr);
    const HWND controls[] = {presetControls_.list, presetControls_.name, label, apply,
        add, rename, remove, reset, note};
    for (HWND control : controls) SetControlFont(control);
}

void Application::RebuildPresetList() {
    if (!presetControls_.list) return;
    SendMessageW(presetControls_.list, LB_RESETCONTENT, 0, 0);
    for (const Preset& preset : state_.presets) {
        SendMessageW(presetControls_.list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(preset.name.c_str()));
    }
    if (selectedPreset_ >= static_cast<int>(state_.presets.size())) selectedPreset_ = -1;
    if (selectedPreset_ >= 0) SendMessageW(presetControls_.list, LB_SETCURSEL, selectedPreset_, 0);
}

void Application::LoadSelectedPreset() {
    const LRESULT selected = SendMessageW(presetControls_.list, LB_GETCURSEL, 0, 0);
    selectedPreset_ = selected == LB_ERR ? -1 : static_cast<int>(selected);
    if (selectedPreset_ >= 0 && selectedPreset_ < static_cast<int>(state_.presets.size())) {
        SetWindowTextW(presetControls_.name, state_.presets[static_cast<size_t>(selectedPreset_)].name.c_str());
    }
}

void Application::AddCurrentPreset() {
    std::wstring name = WindowText(presetControls_.name);
    if (TrimWhitespace(name).empty()) {
        size_t suffix = state_.presets.size() + 1;
        std::vector<std::wstring> names;
        names.reserve(state_.presets.size());
        for (const Preset& preset : state_.presets) names.push_back(preset.name);
        do {
            name = L"场景 " + std::to_wstring(suffix++);
        } while (HasPresetNameConflict(names, name));
    } else if (!NormalizeAndValidatePresetName(name, -1)) {
        return;
    }
    state_.presets.push_back(CapturePreset(name));
    selectedPreset_ = static_cast<int>(state_.presets.size()) - 1;
    RebuildPresetList();
    SaveConfiguration();
}

void Application::ApplySelectedPreset() {
    if (selectedPreset_ < 0 || selectedPreset_ >= static_cast<int>(state_.presets.size())) return;
    ApplyPreset(state_.presets[static_cast<size_t>(selectedPreset_)]);
}

bool Application::NormalizeAndValidatePresetName(std::wstring& name, int ignoredIndex) const {
    name = TrimWhitespace(std::move(name));
    std::vector<std::wstring> names;
    names.reserve(state_.presets.size());
    for (const Preset& preset : state_.presets) names.push_back(preset.name);
    const size_t ignored = ignoredIndex < 0 ? static_cast<size_t>(-1)
        : static_cast<size_t>(ignoredIndex);
    if (name.empty()) {
        MessageBoxW(presetWindow_, L"预设名称不能为空。", L"名称无效", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (HasPresetNameConflict(names, name, ignored)) {
        MessageBoxW(presetWindow_, L"已存在同名预设，请换一个名称。", L"名称重复", MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;
}

void Application::ResetToDefaults() {
    if (MessageBoxW(presetWindow_, L"恢复默认窗口与功能设置？书签和标签页不会删除。",
                    L"恢复默认", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    const ThemeMode retainedTheme = state_.settings.themeMode;
    const RenderMode retainedRender = state_.settings.renderMode;
    state_.settings = AppSettings{};
    state_.settings.themeMode = retainedTheme;
    state_.settings.renderMode = retainedRender;
    state_.hotkeys = DefaultHotkeys();
    RECT rect{100, 100, 1100, 700};
    windowModes_.ApplyPresetGeometry(rect, false);
    RefreshTrayMode(); RefreshHotkeys(); RefreshSettingsControls(); SaveConfiguration();
}

LRESULT CALLBACK Application::PresetWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        application = static_cast<Application*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
    }
    return application ? application->HandlePresetMessage(window, message, wParam, lParam)
                       : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::HandlePresetMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreatePresetControls(window);
        theme_.ApplyWindowTheme(window);
        ApplyThemeToWindowTree(window, theme_.IsDark());
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == PresetList && HIWORD(wParam) == LBN_SELCHANGE) LoadSelectedPreset();
        else if (id == PresetList && HIWORD(wParam) == LBN_DBLCLK) { LoadSelectedPreset(); ApplySelectedPreset(); }
        else if (id == PresetApply) ApplySelectedPreset();
        else if (id == PresetAdd) AddCurrentPreset();
        else if (id == PresetSaveName && selectedPreset_ >= 0 &&
                 selectedPreset_ < static_cast<int>(state_.presets.size())) {
            std::wstring name = WindowText(presetControls_.name);
            if (NormalizeAndValidatePresetName(name, selectedPreset_)) {
                state_.presets[static_cast<size_t>(selectedPreset_)].name = std::move(name);
                RebuildPresetList(); SaveConfiguration();
            }
        } else if (id == PresetDelete && selectedPreset_ >= 0 &&
                   selectedPreset_ < static_cast<int>(state_.presets.size())) {
            state_.presets.erase(state_.presets.begin() + selectedPreset_);
            selectedPreset_ = -1; SetWindowTextW(presetControls_.name, L"");
            RebuildPresetList(); SaveConfiguration();
        } else if (id == PresetReset) ResetToDefaults();
        return 0;
    }
    case WM_DRAWITEM: DrawOwnerItem(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam)); return TRUE;
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) {
            RedrawWindow(mainWindow_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
            RedrawWindow(window, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam); SetTextColor(dc, theme_.Palette().text);
        SetBkColor(dc, theme_.Palette().surface); return reinterpret_cast<LRESULT>(theme_.SurfaceBrush());
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam); SetTextColor(dc, theme_.Palette().text);
        SetBkMode(dc, TRANSPARENT); return reinterpret_cast<LRESULT>(theme_.WindowBrush());
    }
    case WM_ERASEBKGND: {
        RECT client{}; GetClientRect(window, &client);
        theme_.FillBackground(reinterpret_cast<HDC>(wParam), client, theme_.Palette().window); return 1;
    }
    case WM_CLOSE: ShowWindow(window, SW_HIDE); return 0;
    case WM_DESTROY: presetWindow_ = nullptr; return 0;
    default: break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void Application::SaveConfiguration() {
    if (!mainWindow_ || shuttingDown_) return;
    windowModes_.UpdateNormalRectFromWindow();
    state_.session.normalRect = windowModes_.NormalRect();
    state_.session.maximized = windowModes_.IsSeamlessMaximized();
    state_.session.activeTab = state_.currentTabIndex;
    config_.Save(state_, state_.session.normalRect, state_.session.maximized);
}

void Application::ApplyPreset(const Preset& preset) {
    const ThemeMode retainedTheme = state_.settings.themeMode;
    const RenderMode retainedRender = state_.settings.renderMode;
    state_.settings.holeRadius = preset.holeRadius;
    state_.settings.snapThreshold = preset.snapThreshold;
    state_.settings.holeOpacityPercent = preset.holeOpacityPercent;
    state_.settings.autoHideOpacityPercent = preset.autoHideOpacityPercent;
    state_.settings.holdPlaybackRate = preset.holdPlaybackRate;
    state_.settings.autoPauseOnHide = preset.autoPauseOnHide;
    state_.settings.disableHotkeysOnTyping = preset.disableHotkeysOnTyping;
    state_.settings.useSystemTray = preset.useSystemTray;
    state_.settings.backgroundMediaHotkeys = preset.backgroundMediaHotkeys;
    state_.settings.immersionStyle = preset.immersionStyle;
    state_.settings.homeUrl = preset.homeUrl;
    state_.settings.themeMode = retainedTheme;
    state_.settings.renderMode = retainedRender;
    state_.settings.Clamp();
    state_.hotkeys = preset.hotkeys;
    windowModes_.ApplyPresetGeometry(preset.normalRect, preset.maximized);
    if (!preset.tabs.empty()) {
        state_.tabs = preset.tabs;
        for (TabData& tab : state_.tabs) {
            if (tab.url.empty()) tab.url = state_.settings.homeUrl;
            if (IsPlaceholderTabTitle(tab.title)) tab.title = SuggestedTitleForUrl(tab.url);
        }
        state_.currentTabIndex = std::clamp(preset.activeTab, 0,
            static_cast<int>(state_.tabs.size()) - 1);
        RebuildTabControl();
        SwitchTab(state_.currentTabIndex);
    } else if (!preset.currentUrl.empty()) {
        NavigateTo(preset.currentUrl);
    }
    RefreshTrayMode(); RefreshHotkeys(); RefreshSettingsControls(); SaveConfiguration();
}

Preset Application::CapturePreset(const std::wstring& name) const {
    Preset preset;
    preset.name = name;
    preset.normalRect = windowModes_.NormalRect();
    preset.maximized = windowModes_.IsSeamlessMaximized();
    preset.holeRadius = state_.settings.holeRadius;
    preset.snapThreshold = state_.settings.snapThreshold;
    preset.holeOpacityPercent = state_.settings.holeOpacityPercent;
    preset.autoHideOpacityPercent = state_.settings.autoHideOpacityPercent;
    preset.holdPlaybackRate = state_.settings.holdPlaybackRate;
    preset.autoPauseOnHide = state_.settings.autoPauseOnHide;
    preset.disableHotkeysOnTyping = state_.settings.disableHotkeysOnTyping;
    preset.useSystemTray = state_.settings.useSystemTray;
    preset.backgroundMediaHotkeys = state_.settings.backgroundMediaHotkeys;
    preset.immersionStyle = state_.settings.immersionStyle;
    preset.homeUrl = state_.settings.homeUrl;
    preset.currentUrl = CurrentUrl();
    preset.tabs = state_.tabs;
    preset.activeTab = state_.currentTabIndex;
    preset.hotkeys = state_.hotkeys;
    return preset;
}

void Application::SetControlFont(HWND control, HFONT font) const {
    if (control) SendMessageW(control, WM_SETFONT,
        reinterpret_cast<WPARAM>(font ? font : uiFont_), TRUE);
}

HWND Application::CreateThemedButton(HWND parent, const wchar_t* text, int id) const {
    HWND button = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    SetControlFont(button);
    return button;
}

HWND Application::CreateLabel(HWND parent, const wchar_t* text) const {
    HWND label = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, parent, nullptr, instance_, nullptr);
    SetControlFont(label);
    return label;
}

void Application::CenterOwnedWindow(HWND window, int width, int height) const {
    RECT owner{};
    GetWindowRect(mainWindow_, &owner);
    HMONITOR monitor = MonitorFromWindow(mainWindow_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    width = std::min(width, static_cast<int>(info.rcWork.right - info.rcWork.left) - Scale(24));
    height = std::min(height, static_cast<int>(info.rcWork.bottom - info.rcWork.top) - Scale(24));
    int x = owner.left + ((owner.right - owner.left) - width) / 2;
    int y = owner.top + ((owner.bottom - owner.top) - height) / 2;
    const int workLeft = static_cast<int>(info.rcWork.left);
    const int workTop = static_cast<int>(info.rcWork.top);
    const int workRight = static_cast<int>(info.rcWork.right);
    const int workBottom = static_cast<int>(info.rcWork.bottom);
    x = std::clamp(x, workLeft, std::max(workLeft, workRight - width));
    y = std::clamp(y, workTop, std::max(workTop, workBottom - height));
    SetWindowPos(window, HWND_TOP, x, y, width, height, SWP_NOACTIVATE);
}

void Application::ShowError(const wchar_t* title, const wchar_t* message, HRESULT result) const {
    std::wstring text = message ? message : L"发生未知错误。";
    if (FAILED(result)) {
        wchar_t system[512]{};
        const DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        if (FormatMessageW(flags, nullptr, static_cast<DWORD>(result),
                           MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), system,
                           static_cast<DWORD>(std::size(system)), nullptr) > 0) {
            text += L"\n\n";
            text += system;
        }
        wchar_t code[24]{};
        swprintf_s(code, L"0x%08X", static_cast<unsigned int>(result));
        text += L"\n错误代码：";
        text += code;
    }
    MessageBoxW(mainWindow_, text.c_str(), title, MB_OK | MB_ICONERROR);
}

} // namespace xiaochuang
