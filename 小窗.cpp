#define NOMINMAX 
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm> 
#include <commctrl.h> 
#include <windowsx.h>
#include <wrl.h>
#include <wil/com.h>
#include <wil/resource.h>
#include "WebView2.h"
#include <shlobj.h> 
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
using namespace Microsoft::WRL;
/* 数据结构与常量 */
struct AppHotkey {
    int id;
    UINT fsModifiers;
    UINT vk;
    std::wstring name;
};
struct TabData {
    std::wstring title;
    std::wstring url;
};
struct Bookmark {
    std::wstring title;
    std::wstring url;
};
struct Preset {
    std::wstring name;
    int x, y, w, h;
    int holeRadius;
    int snapThreshold;
    bool autoPause;
    int immersionStyle;
    bool isFullscreen;
    bool disableHkOnTyping;
    bool useSystemTray;
    std::wstring homeUrl;
    std::wstring currentUrl;
    AppHotkey hkImmersion;
    AppHotkey hkPlayPause;
    AppHotkey hkBack;
    AppHotkey hkForward;
    AppHotkey hkPrevEp;
    AppHotkey hkNextEp;
    AppHotkey hkHideWin;
};
/* 布局尺寸：拖拽条12 + 标签栏28 + 导航栏30 = 顶栏总高70 */
const int DRAG_BAR_HEIGHT = 12;
const int TAB_BAR_HEIGHT = 28;
const int NAV_BAR_HEIGHT = 30;
const int TOTAL_TOP_HEIGHT = DRAG_BAR_HEIGHT + TAB_BAR_HEIGHT + NAV_BAR_HEIGHT;
const int TAB_WIDTH = 140;
const int BM_ROW_HEIGHT = 35;
const int BM_BTN_SIZE = 30;
const int BM_MAX_HEIGHT = 400;
const int PRESET_WIN_WIDTH = 400;
const int PRESET_WIN_HEIGHT = 500;
/* 全局状态 */
HWND g_hSettingsWin = NULL;
HINSTANCE g_hInst;
HWND g_hWnd;
HWND g_hEditUrl;
HWND g_hBtnGo;
HWND g_hBtnSet;
HWND g_hBtnMove;
HWND g_hTabCtrl;
HWND g_hBtnAddTab;
HWND g_hBtnCloseTab;
HWND g_hBtnCloseApp;
HWND g_hChkAutoPause;
HWND g_hEditHome;
HWND g_hBtnStar;
HWND g_hBtnBmList;
HWND g_hBmPopup = NULL;
HWND g_hBtnPreset;
HWND g_hPresetWin = NULL;
HWND g_hNameInputPopup = NULL;
std::wstring g_tempPresetName;
HWND g_hBtnFullscreen;
bool g_isFullscreen = false;
RECT g_preFullscreenRect = { 0 };
WNDPROC g_OldBtnProc;
WNDPROC g_OldEditProc;
HFONT g_hListFont = NULL;
HHOOK g_hMouseHook = NULL;
AppHotkey* g_currentSettingKey = nullptr;
wil::com_ptr<ICoreWebView2Controller> g_controller;
wil::com_ptr<ICoreWebView2> g_webview;
bool g_isImmersionMode = false;
bool g_wasMouseOverImmersion = false;
bool g_isWindowHidden = false;
bool g_areHotkeysDisabled = false;
bool g_autoPauseOnHide = true;
bool g_disableHkOnTyping = true;
bool g_useSystemTray = false;
NOTIFYICONDATA g_nid = { 0 };
int g_holeRadius = 400;
int g_snapThreshold = 20;
int g_immersionStyle = 0; // 0=挖孔穿透, 1=整体透明
std::wstring g_homeUrl = L"https://www.bilibili.com";
std::vector<TabData> g_tabs;
std::vector<Bookmark> g_bookmarks;
std::vector<Preset> g_presets;
int g_currentTabIndex = 0;
bool g_isNavigatingFromTabSwitch = false;
int g_winX = 100; int g_winY = 100; int g_winW = 1000; int g_winH = 600;
TCHAR g_iniPath[MAX_PATH] = { 0 };
/* 控件 ID */
#define IDC_ADDRESS_BAR   9001
#define IDC_GO_BUTTON     9002
#define IDC_SET_BUTTON    9003
#define IDC_MOVE_BUTTON   9004
#define IDC_TAB_CTRL      9005
#define IDC_ADD_TAB       9006
#define IDC_CLOSE_TAB     9007
#define IDC_BTN_CLOSE_APP 9008
#define IDC_BTN_STAR      9009 
#define IDC_BTN_BMLIST    9010 
#define IDC_BTN_PRESET    9011 
#define IDC_BTN_FULLSCREEN 9012
#define IDC_BM_ROW_BASE     20000
#define IDC_PRESET_ROW_BASE 30000
#define IDC_PRESET_ADD      39999
#define IDC_PRESET_RESET    39996 
#define IDC_NAME_EDIT       39998
#define IDC_NAME_OK         39997
#define IDC_SET_RADIUS    9101
#define IDC_SET_SNAP      9102
#define IDC_CHK_AUTOPAUSE 9103
#define IDC_EDIT_HOME     9104 
#define IDC_CHK_IMMSTYLE  9105
#define IDC_CHK_TYPING    9106
#define IDC_CHK_TRAY      9107
#define IDC_BTN_HK1       9111
#define IDC_BTN_HK2       9112
#define IDC_BTN_HK3       9113
#define IDC_BTN_HK4       9114
#define IDC_BTN_HK5       9115
#define IDC_BTN_HK6       9116
#define IDC_BTN_HK7       9117
#define WM_TRAYICON       (WM_USER + 1)
/* 默认热键绑定 */
AppHotkey g_hkImmersion = { 101, 0, '0', L"沉浸模式" };
AppHotkey g_hkPlayPause = { 102, 0, VK_OEM_3, L"暂停/播放" };
AppHotkey g_hkBack = { 103, 0, '5', L"后退" };
AppHotkey g_hkForward = { 104, 0, '6', L"快进" };
AppHotkey g_hkPrevEp = { 105, 0, '7', L"上一集" };
AppHotkey g_hkNextEp = { 106, 0, '8', L"下一集" };
AppHotkey g_hkHideWin = { 107, 0, '9', L"隐藏/显示" };
/* 前向声明 */
void OpenSettingsWindow(HWND hParent);
void ShowBookmarkList(HWND hParent);
void ShowPresetList(HWND hParent);
void ShowNameInputWindow(HWND hParent);
void UpdateStarIcon();
void SaveBookmarks();
void LoadBookmarks();
void SavePresets();
void LoadPresets();
void CaptureAndAddPreset(const std::wstring& name);
void ApplyPreset(int index);
void RestoreDefaultSettings();
void AddTrayIcon();
void RemoveTrayIcon();
void UpdateTaskbarVisibility();
bool IsCurrentUrlBookmarked();
void ToggleCurrentBookmark();
void CheckAndRestartAsAdmin(HINSTANCE hInstance);
void InitConfigPath();
std::wstring NormalizeInputUrl(const std::wstring& input);
std::wstring GetHotkeyString(UINT mod, UINT vk);
void SaveConfig();
void LoadConfig();
void UpdateHotkeysState(HWND hWnd);
void UnregisterAllHotkeys(HWND hWnd);
void ExecuteScript(const std::wstring& script);
bool IsUserTyping();
bool IsMouseVK(UINT vk);
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
void UpdateImmersionHole();
void ToggleFullscreen();
void ResizeLayout(HWND hWnd);
void InitWebView2(HWND hWnd);
void ShowErrorBox(HWND hWnd, LPCWSTR title, LPCWSTR msg, HRESULT hr = S_OK);
std::wstring FormatTabTitle(const std::wstring& rawTitle);
void CreateNewTab(LPCWSTR url, LPCWSTR title, bool switchToNew);
void CloseTab(int index);
void CloseCurrentTab();
void SwitchToTab(int index);
void UpdateTabTitle(int index, LPCWSTR title);
void HandleTabClick(LPARAM lParam);
LRESULT CALLBACK DragButtonProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK EditWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK SettingsWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK BookmarkWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK PresetWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK NameInputWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
/* 仅在可见性需要变化时才调用 ShowWindow，避免闪烁 */
inline void SetControlVisible(HWND hCtrl, bool show) {
    BOOL visible = IsWindowVisible(hCtrl);
    if (show && !visible) ShowWindow(hCtrl, SW_SHOW);
    else if (!show && visible) ShowWindow(hCtrl, SW_HIDE);
}
void ShowErrorBox(HWND hWnd, LPCWSTR title, LPCWSTR msg, HRESULT hr) {
    WCHAR buffer[1024];
    if (hr != S_OK) {
        swprintf_s(buffer, 1024, L"%s\n\n错误代码 (HRESULT): 0x%08X", msg, (unsigned int)hr);
    }
    else {
        wcscpy_s(buffer, 1024, msg);
    }
    MessageBox(hWnd, buffer, title, MB_ICONERROR | MB_OK);
}
/* 启动时请求管理员权限，保证全屏游戏中热键可用 */
void CheckAndRestartAsAdmin(HINSTANCE hInstance) {
    if (IsUserAnAdmin()) return;
    TCHAR szFileName[MAX_PATH];
    GetModuleFileName(NULL, szFileName, MAX_PATH);
    SHELLEXECUTEINFO sei = { sizeof(sei) };
    sei.lpVerb = _T("runas");
    sei.lpFile = szFileName;
    sei.hwnd = NULL;
    sei.nShow = SW_NORMAL;
    if (!ShellExecuteEx(&sei)) {
        ShowErrorBox(NULL, L"权限警告", L"未能获取管理员权限！\n\n为了在全屏游戏中正常使用热键，\n建议右键选择\u201C以管理员身份运行\u201D。");
    }
    else {
        exit(0);
    }
}
void InitConfigPath() {
    GetModuleFileName(NULL, g_iniPath, MAX_PATH);
    PathRemoveFileSpec(g_iniPath);
    PathAppend(g_iniPath, _T("config.ini"));
}
/* 地址栏输入智能处理：有协议头直接用，含点号补 https://，否则走必应搜索 */
std::wstring NormalizeInputUrl(const std::wstring& input) {
    if (input.empty()) return L"https://www.bilibili.com";
    if (input.find(L"://") != std::wstring::npos) return input;
    if (input.find(L' ') == std::wstring::npos && input.find(L'.') != std::wstring::npos) {
        return L"https://" + input;
    }
    std::wstring encoded;
    for (wchar_t ch : input) {
        if ((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') ||
            (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_' || ch == L'.' || ch == L'~') {
            encoded += ch;
        }
        else {
            char mb[8] = { 0 };
            int mbLen = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, mb, sizeof(mb), NULL, NULL);
            for (int j = 0; j < mbLen; ++j) {
                WCHAR hex[8];
                swprintf_s(hex, L"%%%02X", (unsigned char)mb[j]);
                encoded += hex;
            }
        }
    }
    return L"https://www.bing.com/search?q=" + encoded;
}
std::wstring GetHotkeyString(UINT mod, UINT vk) {
    std::wstring str = L"";
    if (mod & MOD_CONTROL) str += L"Ctrl + ";
    if (mod & MOD_ALT) str += L"Alt + ";
    if (mod & MOD_SHIFT) str += L"Shift + ";
    WCHAR keyName[32];
    unsigned int scanCode = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
    switch (vk) {
    case VK_LEFT: wcscpy_s(keyName, L"Left"); break;
    case VK_RIGHT: wcscpy_s(keyName, L"Right"); break;
    case VK_UP: wcscpy_s(keyName, L"Up"); break;
    case VK_DOWN: wcscpy_s(keyName, L"Down"); break;
    case VK_OEM_3: wcscpy_s(keyName, L"`"); break;
    case VK_MBUTTON: wcscpy_s(keyName, L"鼠标中键"); break;
    case VK_XBUTTON1: wcscpy_s(keyName, L"鼠标侧键1"); break;
    case VK_XBUTTON2: wcscpy_s(keyName, L"鼠标侧键2"); break;
    default: if (GetKeyNameText(scanCode << 16, keyName, 32) == 0) wsprintf(keyName, L"%c", vk);
    }
    str += keyName;
    return str;
}
/* 书签持久化：写入/读取 config.ini [Bookmarks] 段 */
void SaveBookmarks() {
    WritePrivateProfileSection(_T("Bookmarks"), NULL, g_iniPath);
    WCHAR buf[32];
    _itot_s((int)g_bookmarks.size(), buf, 10);
    WritePrivateProfileString(_T("Bookmarks"), _T("Count"), buf, g_iniPath);
    for (size_t i = 0; i < g_bookmarks.size(); ++i) {
        std::wstring keyTitle = L"Title" + std::to_wstring(i);
        std::wstring keyUrl = L"Url" + std::to_wstring(i);
        WritePrivateProfileString(_T("Bookmarks"), keyTitle.c_str(), g_bookmarks[i].title.c_str(), g_iniPath);
        WritePrivateProfileString(_T("Bookmarks"), keyUrl.c_str(), g_bookmarks[i].url.c_str(), g_iniPath);
    }
}
void LoadBookmarks() {
    g_bookmarks.clear();
    int count = GetPrivateProfileInt(_T("Bookmarks"), _T("Count"), 0, g_iniPath);
    for (int i = 0; i < count; ++i) {
        std::wstring keyTitle = L"Title" + std::to_wstring(i);
        std::wstring keyUrl = L"Url" + std::to_wstring(i);
        TCHAR title[2048], url[2048];
        GetPrivateProfileString(_T("Bookmarks"), keyTitle.c_str(), L"Bookmark", title, 2048, g_iniPath);
        GetPrivateProfileString(_T("Bookmarks"), keyUrl.c_str(), L"", url, 2048, g_iniPath);
        g_bookmarks.push_back({ title, url });
    }
}
/* 场景预设持久化：保存窗口位置、沉浸参数、当前URL等全部状态 */
void SavePresets() {
    WritePrivateProfileSection(_T("Presets"), NULL, g_iniPath);
    WCHAR buf[32];
    _itot_s((int)g_presets.size(), buf, 10);
    WritePrivateProfileString(_T("Presets"), _T("Count"), buf, g_iniPath);
    for (size_t i = 0; i < g_presets.size(); ++i) {
        std::wstring p = L"P" + std::to_wstring(i) + L"_";
        WritePrivateProfileString(_T("Presets"), (p + L"Name").c_str(), g_presets[i].name.c_str(), g_iniPath);
        WritePrivateProfileString(_T("Presets"), (p + L"HomeUrl").c_str(), g_presets[i].homeUrl.c_str(), g_iniPath);
        WritePrivateProfileString(_T("Presets"), (p + L"CurUrl").c_str(), g_presets[i].currentUrl.c_str(), g_iniPath);
        _itot_s(g_presets[i].x, buf, 10); WritePrivateProfileString(_T("Presets"), (p + L"X").c_str(), buf, g_iniPath);
        _itot_s(g_presets[i].y, buf, 10); WritePrivateProfileString(_T("Presets"), (p + L"Y").c_str(), buf, g_iniPath);
        _itot_s(g_presets[i].w, buf, 10); WritePrivateProfileString(_T("Presets"), (p + L"W").c_str(), buf, g_iniPath);
        _itot_s(g_presets[i].h, buf, 10); WritePrivateProfileString(_T("Presets"), (p + L"H").c_str(), buf, g_iniPath);
        _itot_s(g_presets[i].holeRadius, buf, 10); WritePrivateProfileString(_T("Presets"), (p + L"Rad").c_str(), buf, g_iniPath);
        _itot_s(g_presets[i].snapThreshold, buf, 10); WritePrivateProfileString(_T("Presets"), (p + L"Snap").c_str(), buf, g_iniPath);
        _itot_s(g_presets[i].autoPause ? 1 : 0, buf, 10); WritePrivateProfileString(_T("Presets"), (p + L"Pause").c_str(), buf, g_iniPath);
        _itot_s(g_presets[i].immersionStyle, buf, 10); WritePrivateProfileString(_T("Presets"), (p + L"ImmStyle").c_str(), buf, g_iniPath);
        _itot_s(g_presets[i].isFullscreen ? 1 : 0, buf, 10); WritePrivateProfileString(_T("Presets"), (p + L"Fullscreen").c_str(), buf, g_iniPath);
        _itot_s(g_presets[i].disableHkOnTyping ? 1 : 0, buf, 10); WritePrivateProfileString(_T("Presets"), (p + L"HkTyping").c_str(), buf, g_iniPath);
        _itot_s(g_presets[i].useSystemTray ? 1 : 0, buf, 10); WritePrivateProfileString(_T("Presets"), (p + L"SysTray").c_str(), buf, g_iniPath);
        auto SavePHk = [&](const std::wstring& suffix, const AppHotkey& hk) {
            WCHAR hkBuf[32]; wsprintf(hkBuf, L"%u,%u", hk.fsModifiers, hk.vk);
            WritePrivateProfileString(_T("Presets"), (p + suffix).c_str(), hkBuf, g_iniPath);
        };
        SavePHk(L"HkImm", g_presets[i].hkImmersion);
        SavePHk(L"HkPlay", g_presets[i].hkPlayPause);
        SavePHk(L"HkBack", g_presets[i].hkBack);
        SavePHk(L"HkFwd", g_presets[i].hkForward);
        SavePHk(L"HkPrev", g_presets[i].hkPrevEp);
        SavePHk(L"HkNext", g_presets[i].hkNextEp);
        SavePHk(L"HkHide", g_presets[i].hkHideWin);
    }
}
void LoadPresets() {
    g_presets.clear();
    int count = GetPrivateProfileInt(_T("Presets"), _T("Count"), 0, g_iniPath);
    for (int i = 0; i < count; ++i) {
        Preset p;
        std::wstring pre = L"P" + std::to_wstring(i) + L"_";
        TCHAR buf[2048];
        GetPrivateProfileString(_T("Presets"), (pre + L"Name").c_str(), L"Preset", buf, 2048, g_iniPath); p.name = buf;
        GetPrivateProfileString(_T("Presets"), (pre + L"HomeUrl").c_str(), L"", buf, 2048, g_iniPath); p.homeUrl = buf;
        GetPrivateProfileString(_T("Presets"), (pre + L"CurUrl").c_str(), L"", buf, 2048, g_iniPath); p.currentUrl = buf;
        p.x = GetPrivateProfileInt(_T("Presets"), (pre + L"X").c_str(), 100, g_iniPath);
        p.y = GetPrivateProfileInt(_T("Presets"), (pre + L"Y").c_str(), 100, g_iniPath);
        p.w = GetPrivateProfileInt(_T("Presets"), (pre + L"W").c_str(), 1000, g_iniPath);
        p.h = GetPrivateProfileInt(_T("Presets"), (pre + L"H").c_str(), 600, g_iniPath);
        p.holeRadius = GetPrivateProfileInt(_T("Presets"), (pre + L"Rad").c_str(), 400, g_iniPath);
        p.snapThreshold = GetPrivateProfileInt(_T("Presets"), (pre + L"Snap").c_str(), 20, g_iniPath);
        p.autoPause = GetPrivateProfileInt(_T("Presets"), (pre + L"Pause").c_str(), 1, g_iniPath) != 0;
        p.immersionStyle = GetPrivateProfileInt(_T("Presets"), (pre + L"ImmStyle").c_str(), 0, g_iniPath);
        p.isFullscreen = GetPrivateProfileInt(_T("Presets"), (pre + L"Fullscreen").c_str(), 0, g_iniPath) != 0;
        p.disableHkOnTyping = GetPrivateProfileInt(_T("Presets"), (pre + L"HkTyping").c_str(), 1, g_iniPath) != 0;
        p.useSystemTray = GetPrivateProfileInt(_T("Presets"), (pre + L"SysTray").c_str(), 0, g_iniPath) != 0;
        auto LoadPHk = [&](const std::wstring& suffix, AppHotkey& hk, UINT defMod, UINT defVk) {
            WCHAR hkBuf[32]; GetPrivateProfileString(_T("Presets"), (pre + suffix).c_str(), L"", hkBuf, 32, g_iniPath);
            if (wcslen(hkBuf) > 0) swscanf_s(hkBuf, L"%u,%u", &hk.fsModifiers, &hk.vk);
            else { hk.fsModifiers = defMod; hk.vk = defVk; }
        };
        LoadPHk(L"HkImm", p.hkImmersion, 0, '0');
        LoadPHk(L"HkPlay", p.hkPlayPause, 0, VK_OEM_3);
        LoadPHk(L"HkBack", p.hkBack, 0, '5');
        LoadPHk(L"HkFwd", p.hkForward, 0, '6');
        LoadPHk(L"HkPrev", p.hkPrevEp, 0, '7');
        LoadPHk(L"HkNext", p.hkNextEp, 0, '8');
        LoadPHk(L"HkHide", p.hkHideWin, 0, '9');
        g_presets.push_back(p);
    }
}
/* 捕获当前窗口全部状态为新预设；全屏时保存全屏前的窗口位置 */
void CaptureAndAddPreset(const std::wstring& name) {
    Preset p;
    p.name = name;
    p.isFullscreen = g_isFullscreen;
    if (g_isFullscreen && g_preFullscreenRect.right > g_preFullscreenRect.left) {
        p.x = g_preFullscreenRect.left;
        p.y = g_preFullscreenRect.top;
        p.w = g_preFullscreenRect.right - g_preFullscreenRect.left;
        p.h = g_preFullscreenRect.bottom - g_preFullscreenRect.top;
    }
    else {
        RECT rc; GetWindowRect(g_hWnd, &rc);
        if (g_isImmersionMode) {
            if (rc.top >= TOTAL_TOP_HEIGHT) {
                rc.top -= TOTAL_TOP_HEIGHT;
            }
            else {
                rc.top = 0;
                rc.bottom += TOTAL_TOP_HEIGHT;
            }
        }
        p.x = rc.left;
        p.y = rc.top;
        p.w = rc.right - rc.left;
        p.h = rc.bottom - rc.top;
    }
    p.holeRadius = g_holeRadius;
    p.snapThreshold = g_snapThreshold;
    p.autoPause = g_autoPauseOnHide;
    p.immersionStyle = g_immersionStyle;
    p.disableHkOnTyping = g_disableHkOnTyping;
    p.useSystemTray = g_useSystemTray;
    p.homeUrl = g_homeUrl;
    if (g_currentTabIndex >= 0 && g_currentTabIndex < g_tabs.size()) {
        p.currentUrl = g_tabs[g_currentTabIndex].url;
    }
    else {
        p.currentUrl = g_homeUrl;
    }
    p.hkImmersion = g_hkImmersion;
    p.hkPlayPause = g_hkPlayPause;
    p.hkBack = g_hkBack;
    p.hkForward = g_hkForward;
    p.hkPrevEp = g_hkPrevEp;
    p.hkNextEp = g_hkNextEp;
    p.hkHideWin = g_hkHideWin;
    g_presets.push_back(p);
    SavePresets();
}
void RestoreDefaultSettings() {
    g_holeRadius = 400;
    g_snapThreshold = 20;
    g_autoPauseOnHide = true;
    g_immersionStyle = 0;
    g_disableHkOnTyping = true;
    g_homeUrl = L"https://www.bilibili.com";
    g_hkImmersion.fsModifiers = 0; g_hkImmersion.vk = '0';
    g_hkPlayPause.fsModifiers = 0; g_hkPlayPause.vk = VK_OEM_3;
    g_hkBack.fsModifiers = 0; g_hkBack.vk = '5';
    g_hkForward.fsModifiers = 0; g_hkForward.vk = '6';
    g_hkPrevEp.fsModifiers = 0; g_hkPrevEp.vk = '7';
    g_hkNextEp.fsModifiers = 0; g_hkNextEp.vk = '8';
    g_hkHideWin.fsModifiers = 0; g_hkHideWin.vk = '9';
    UpdateHotkeysState(g_hWnd);
    if (g_useSystemTray) {
        g_useSystemTray = false;
        UpdateTaskbarVisibility();
    }
    if (g_isImmersionMode) {
        SendMessage(g_hWnd, WM_HOTKEY, g_hkImmersion.id, 0);
    }
    if (g_isFullscreen) {
        g_isFullscreen = false;
        SetWindowText(g_hBtnFullscreen, L"□");
    }
    MoveWindow(g_hWnd, 100, 100, 1000, 600, TRUE);
    if (g_webview) {
        if (g_currentTabIndex >= 0 && g_currentTabIndex < g_tabs.size()) {
            g_tabs[g_currentTabIndex].url = g_homeUrl;
        }
        g_webview->Navigate(g_homeUrl.c_str());
    }
    UpdateStarIcon();
    SaveConfig();
}
/* 应用预设：还原窗口位置 → 退出沉浸 → 同步全屏 → 导航到预设URL */
void ApplyPreset(int index) {
    if (index < 0 || index >= g_presets.size()) return;
    const Preset& p = g_presets[index];
    g_holeRadius = p.holeRadius;
    g_snapThreshold = p.snapThreshold;
    g_autoPauseOnHide = p.autoPause;
    g_immersionStyle = p.immersionStyle;
    g_homeUrl = p.homeUrl;
    g_disableHkOnTyping = p.disableHkOnTyping;
    g_hkImmersion.fsModifiers = p.hkImmersion.fsModifiers;
    g_hkImmersion.vk = p.hkImmersion.vk;
    g_hkPlayPause.fsModifiers = p.hkPlayPause.fsModifiers;
    g_hkPlayPause.vk = p.hkPlayPause.vk;
    g_hkBack.fsModifiers = p.hkBack.fsModifiers;
    g_hkBack.vk = p.hkBack.vk;
    g_hkForward.fsModifiers = p.hkForward.fsModifiers;
    g_hkForward.vk = p.hkForward.vk;
    g_hkPrevEp.fsModifiers = p.hkPrevEp.fsModifiers;
    g_hkPrevEp.vk = p.hkPrevEp.vk;
    g_hkNextEp.fsModifiers = p.hkNextEp.fsModifiers;
    g_hkNextEp.vk = p.hkNextEp.vk;
    g_hkHideWin.fsModifiers = p.hkHideWin.fsModifiers;
    g_hkHideWin.vk = p.hkHideWin.vk;
    UpdateHotkeysState(g_hWnd);
    if (g_useSystemTray != p.useSystemTray) {
        g_useSystemTray = p.useSystemTray;
        UpdateTaskbarVisibility();
    }
    if (g_isImmersionMode) {
        g_isImmersionMode = false;
        SetWindowRgn(g_hWnd, NULL, TRUE);
                LONG_PTR exStyle = GetWindowLongPtr(g_hWnd, GWL_EXSTYLE);
                SetWindowLongPtr(g_hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_THICKFRAME);
        SetWindowLongPtr(g_hWnd, GWL_EXSTYLE, exStyle & ~(WS_EX_LAYERED | WS_EX_TRANSPARENT));
        ShowWindow(g_hEditUrl, SW_SHOW); ShowWindow(g_hBtnGo, SW_SHOW); ShowWindow(g_hBtnSet, SW_SHOW);
        ShowWindow(g_hTabCtrl, SW_SHOW); ShowWindow(g_hBtnAddTab, SW_SHOW); ShowWindow(g_hBtnCloseTab, SW_SHOW);
        ShowWindow(g_hBtnCloseApp, SW_SHOW); ShowWindow(g_hBtnStar, SW_SHOW); ShowWindow(g_hBtnBmList, SW_SHOW); ShowWindow(g_hBtnPreset, SW_SHOW);
        ShowWindow(g_hBtnFullscreen, SW_SHOW);
        ResizeLayout(g_hWnd);
    }
    MoveWindow(g_hWnd, p.x, p.y, p.w, p.h, TRUE);
    if (p.isFullscreen && !g_isFullscreen) {
        ToggleFullscreen();
    }
    else if (!p.isFullscreen && g_isFullscreen) {
        g_isFullscreen = false;
        SetWindowText(g_hBtnFullscreen, L"□");
    }
    if (g_webview && !p.currentUrl.empty()) {
        if (g_currentTabIndex >= 0 && g_currentTabIndex < g_tabs.size()) {
            g_tabs[g_currentTabIndex].url = p.currentUrl;
        }
        g_webview->Navigate(p.currentUrl.c_str());
        UpdateStarIcon();
    }
    if (g_hSettingsWin && IsWindow(g_hSettingsWin))
        PostMessage(g_hSettingsWin, WM_APP + 1, 0, 0);
    SaveConfig();
}
/* 书签匹配：去尾斜杠 + 统一小写后比对URL */
bool IsCurrentUrlBookmarked() {
    if (g_currentTabIndex < 0 || g_currentTabIndex >= g_tabs.size()) return false;
    std::wstring curUrl = g_tabs[g_currentTabIndex].url;
    auto Normalize = [](std::wstring u) {
        if (!u.empty() && u.back() == L'/') u.pop_back();
        for (auto& c : u) c = towlower(c);
        return u;
        };
    std::wstring cleanCur = Normalize(curUrl);
    for (const auto& bm : g_bookmarks) {
        if (Normalize(bm.url) == cleanCur) return true;
    }
    return false;
}
void UpdateStarIcon() {
    if (g_hBtnStar) {
        SetWindowText(g_hBtnStar, IsCurrentUrlBookmarked() ? L"★" : L"☆");
    }
}
void ToggleCurrentBookmark() {
    if (g_currentTabIndex < 0 || g_currentTabIndex >= g_tabs.size()) return;
    std::wstring curUrl = g_tabs[g_currentTabIndex].url;
    std::wstring curTitle = g_tabs[g_currentTabIndex].title;
    auto Normalize = [](std::wstring u) {
        if (!u.empty() && u.back() == L'/') u.pop_back();
        for (auto& c : u) c = towlower(c);
        return u;
        };
    std::wstring cleanCur = Normalize(curUrl);
    bool found = false;
    for (auto it = g_bookmarks.begin(); it != g_bookmarks.end(); ++it) {
        if (Normalize(it->url) == cleanCur) {
            g_bookmarks.erase(it);
            found = true;
            break;
        }
    }
    if (!found) {
        g_bookmarks.push_back({ curTitle, curUrl });
    }
    SaveBookmarks();
    UpdateStarIcon();
    if (g_hBmPopup && IsWindow(g_hBmPopup)) SendMessage(g_hBmPopup, WM_CLOSE, 0, 0);
}
/* 配置存储：Settings/Session/Hotkeys 三段写入 config.ini */
void SaveConfig() {
    TCHAR buffer[32];
    _itot_s(g_holeRadius, buffer, 10);
    WritePrivateProfileString(_T("Settings"), _T("HoleRadius"), buffer, g_iniPath);
    _itot_s(g_snapThreshold, buffer, 10);
    WritePrivateProfileString(_T("Settings"), _T("SnapThreshold"), buffer, g_iniPath);
    _itot_s(g_autoPauseOnHide ? 1 : 0, buffer, 10);
    WritePrivateProfileString(_T("Settings"), _T("AutoPause"), buffer, g_iniPath);
    _itot_s(g_immersionStyle, buffer, 10);
    WritePrivateProfileString(_T("Settings"), _T("ImmersionStyle"), buffer, g_iniPath);
    _itot_s(g_disableHkOnTyping ? 1 : 0, buffer, 10);
    WritePrivateProfileString(_T("Settings"), _T("DisableHkOnTyping"), buffer, g_iniPath);
    _itot_s(g_useSystemTray ? 1 : 0, buffer, 10);
    WritePrivateProfileString(_T("Settings"), _T("SystemTray"), buffer, g_iniPath);
    WritePrivateProfileString(_T("Settings"), _T("HomeUrl"), g_homeUrl.c_str(), g_iniPath);
    /* 先清除 Session 段中残留的旧标签页数据，再写入当前标签页 */
    WritePrivateProfileSection(_T("Session"), NULL, g_iniPath);
    _itot_s(g_currentTabIndex, buffer, 10);
    WritePrivateProfileString(_T("Session"), _T("ActiveTab"), buffer, g_iniPath);
    _itot_s((int)g_tabs.size(), buffer, 10);
    WritePrivateProfileString(_T("Session"), _T("TabCount"), buffer, g_iniPath);
    for (size_t i = 0; i < g_tabs.size(); ++i) {
        std::wstring keyUrl = L"TabUrl" + std::to_wstring(i);
        std::wstring keyTitle = L"TabTitle" + std::to_wstring(i);
        WritePrivateProfileString(_T("Session"), keyUrl.c_str(), g_tabs[i].url.c_str(), g_iniPath);
        WritePrivateProfileString(_T("Session"), keyTitle.c_str(), g_tabs[i].title.c_str(), g_iniPath);
    }
    /* 沉浸模式下保存窗口位置需补偿顶栏高度 */
    if (g_hWnd && !IsIconic(g_hWnd) && !g_isWindowHidden) {
        RECT rc; GetWindowRect(g_hWnd, &rc);
        if (g_isImmersionMode) {
            if (rc.top >= TOTAL_TOP_HEIGHT) {
                rc.top -= TOTAL_TOP_HEIGHT;
            }
            else {
                rc.top = 0;
                rc.bottom += TOTAL_TOP_HEIGHT;
            }
        }
        _itot_s(rc.left, buffer, 10); WritePrivateProfileString(_T("Session"), _T("WinX"), buffer, g_iniPath);
        _itot_s(rc.top, buffer, 10); WritePrivateProfileString(_T("Session"), _T("WinY"), buffer, g_iniPath);
        _itot_s(rc.right - rc.left, buffer, 10); WritePrivateProfileString(_T("Session"), _T("WinW"), buffer, g_iniPath);
        _itot_s(rc.bottom - rc.top, buffer, 10); WritePrivateProfileString(_T("Session"), _T("WinH"), buffer, g_iniPath);
    }
    /* 保存全屏状态及全屏前的窗口位置，以便重启后正确恢复 */
    _itot_s(g_isFullscreen ? 1 : 0, buffer, 10);
    WritePrivateProfileString(_T("Session"), _T("Fullscreen"), buffer, g_iniPath);
    if (g_isFullscreen && g_preFullscreenRect.right > g_preFullscreenRect.left) {
        _itot_s(g_preFullscreenRect.left, buffer, 10); WritePrivateProfileString(_T("Session"), _T("PreFsX"), buffer, g_iniPath);
        _itot_s(g_preFullscreenRect.top, buffer, 10); WritePrivateProfileString(_T("Session"), _T("PreFsY"), buffer, g_iniPath);
        _itot_s(g_preFullscreenRect.right - g_preFullscreenRect.left, buffer, 10); WritePrivateProfileString(_T("Session"), _T("PreFsW"), buffer, g_iniPath);
        _itot_s(g_preFullscreenRect.bottom - g_preFullscreenRect.top, buffer, 10); WritePrivateProfileString(_T("Session"), _T("PreFsH"), buffer, g_iniPath);
    }
    SaveBookmarks();
    SavePresets();
    auto SaveKey = [](LPCWSTR key, AppHotkey& hk) {
        WCHAR buf[32]; wsprintf(buf, L"%u,%u", hk.fsModifiers, hk.vk);
        WritePrivateProfileString(_T("Hotkeys"), key, buf, g_iniPath);
        };
    SaveKey(L"Immersion", g_hkImmersion);
    SaveKey(L"PlayPause", g_hkPlayPause);
    SaveKey(L"Back", g_hkBack);
    SaveKey(L"Forward", g_hkForward);
    SaveKey(L"PrevEp", g_hkPrevEp);
    SaveKey(L"NextEp", g_hkNextEp);
    SaveKey(L"HideWin", g_hkHideWin);
}
void LoadConfig() {
    g_holeRadius = GetPrivateProfileInt(_T("Settings"), _T("HoleRadius"), 400, g_iniPath);
    g_snapThreshold = GetPrivateProfileInt(_T("Settings"), _T("SnapThreshold"), 20, g_iniPath);
    g_autoPauseOnHide = GetPrivateProfileInt(_T("Settings"), _T("AutoPause"), 1, g_iniPath) != 0;
    g_immersionStyle = GetPrivateProfileInt(_T("Settings"), _T("ImmersionStyle"), 0, g_iniPath);
    g_disableHkOnTyping = GetPrivateProfileInt(_T("Settings"), _T("DisableHkOnTyping"), 1, g_iniPath) != 0;
    g_useSystemTray = GetPrivateProfileInt(_T("Settings"), _T("SystemTray"), 0, g_iniPath) != 0;
    TCHAR homeBuf[2048];
    GetPrivateProfileString(_T("Settings"), _T("HomeUrl"), L"https://www.bilibili.com", homeBuf, 2048, g_iniPath);
    g_homeUrl = homeBuf;
    if (g_homeUrl.empty()) { g_homeUrl = L"https://www.bilibili.com"; }
    g_winX = GetPrivateProfileInt(_T("Session"), _T("WinX"), 100, g_iniPath);
    g_winY = GetPrivateProfileInt(_T("Session"), _T("WinY"), 100, g_iniPath);
    g_winW = GetPrivateProfileInt(_T("Session"), _T("WinW"), 1000, g_iniPath);
    g_winH = GetPrivateProfileInt(_T("Session"), _T("WinH"), 600, g_iniPath);
    g_isFullscreen = GetPrivateProfileInt(_T("Session"), _T("Fullscreen"), 0, g_iniPath) != 0;
    if (g_isFullscreen) {
        int pfX = GetPrivateProfileInt(_T("Session"), _T("PreFsX"), 100, g_iniPath);
        int pfY = GetPrivateProfileInt(_T("Session"), _T("PreFsY"), 100, g_iniPath);
        int pfW = GetPrivateProfileInt(_T("Session"), _T("PreFsW"), 1000, g_iniPath);
        int pfH = GetPrivateProfileInt(_T("Session"), _T("PreFsH"), 600, g_iniPath);
        g_preFullscreenRect = { pfX, pfY, pfX + pfW, pfY + pfH };
    }
    LoadBookmarks();
    LoadPresets();
    auto LoadKey = [](LPCWSTR key, AppHotkey& hk, UINT defMod, UINT defVk) {
        WCHAR buf[32]; GetPrivateProfileString(_T("Hotkeys"), key, L"", buf, 32, g_iniPath);
        if (wcslen(buf) > 0) swscanf_s(buf, L"%u,%u", &hk.fsModifiers, &hk.vk);
        else { hk.fsModifiers = defMod; hk.vk = defVk; }
        };
    LoadKey(L"Immersion", g_hkImmersion, 0, '0');
    LoadKey(L"PlayPause", g_hkPlayPause, 0, VK_OEM_3);
    LoadKey(L"Back", g_hkBack, 0, '5');
    LoadKey(L"Forward", g_hkForward, 0, '6');
    LoadKey(L"PrevEp", g_hkPrevEp, 0, '7');
    LoadKey(L"NextEp", g_hkNextEp, 0, '8');
    LoadKey(L"HideWin", g_hkHideWin, 0, '9');
}
/* 全局热键管理：隐藏时只保留显示键，可见时注册全部 */
void UnregisterAllHotkeys(HWND hWnd) {
    UnregisterHotKey(hWnd, g_hkImmersion.id);
    UnregisterHotKey(hWnd, g_hkHideWin.id);
    UnregisterHotKey(hWnd, g_hkPlayPause.id);
    UnregisterHotKey(hWnd, g_hkBack.id);
    UnregisterHotKey(hWnd, g_hkForward.id);
    UnregisterHotKey(hWnd, g_hkPrevEp.id);
    UnregisterHotKey(hWnd, g_hkNextEp.id);
}
bool IsMouseVK(UINT vk) {
    return vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
}
void UpdateHotkeysState(HWND hWnd) {
    UnregisterAllHotkeys(hWnd);
    auto RegHK = [&](AppHotkey& hk) {
        if (!IsMouseVK(hk.vk))
            RegisterHotKey(hWnd, hk.id, hk.fsModifiers, hk.vk);
    };
    if (g_isWindowHidden) {
        RegHK(g_hkHideWin);
        return;
    }
    RegHK(g_hkImmersion);
    RegHK(g_hkHideWin);
    RegHK(g_hkPlayPause);
    RegHK(g_hkBack);
    RegHK(g_hkForward);
    RegHK(g_hkPrevEp);
    RegHK(g_hkNextEp);
}
void ExecuteScript(const std::wstring& script) {
    if (g_webview) g_webview->ExecuteScript(script.c_str(), nullptr);
}
/* 通过 GetCursorInfo 检测 IDC_IBEAM 光标判断焦点是否在地址栏/输入框 */
bool IsUserTyping() {
    CURSORINFO ci = { 0 };
    ci.cbSize = sizeof(CURSORINFO);
    if (GetCursorInfo(&ci)) {
        static HCURSOR hCursorIBeam = LoadCursor(NULL, IDC_IBEAM);
        if (ci.flags == CURSOR_SHOWING && ci.hCursor == hCursorIBeam) return true;
    }
    return false;
}
/* 低级鼠标钩子：将鼠标中键/侧键映射为热键触发，同时支持设置窗口录入
   注意：钩子回调中禁止执行耗时操作（文件I/O等），否则会触发超时被系统移除 */
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        UINT vk = 0;
        if (wParam == WM_MBUTTONDOWN || wParam == WM_NCMBUTTONDOWN) {
            vk = VK_MBUTTON;
        } else if (wParam == WM_XBUTTONDOWN || wParam == WM_NCXBUTTONDOWN) {
            MSLLHOOKSTRUCT* pMouse = (MSLLHOOKSTRUCT*)lParam;
            WORD xButton = HIWORD(pMouse->mouseData);
            if (xButton == XBUTTON1) vk = VK_XBUTTON1;
            else if (xButton == XBUTTON2) vk = VK_XBUTTON2;
        }
        if (vk != 0) {
            if (g_currentSettingKey) {
                UINT mods = 0;
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
                if (GetAsyncKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
                g_currentSettingKey->vk = vk;
                g_currentSettingKey->fsModifiers = mods;
                g_currentSettingKey = nullptr;
                PostMessage(g_hWnd, WM_APP + 2, 0, 0);
                if (g_hSettingsWin && IsWindow(g_hSettingsWin))
                    PostMessage(g_hSettingsWin, WM_APP + 1, 0, 0);
                return 1;
            }
            if (g_areHotkeysDisabled)
                return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
            UINT mods = 0;
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
            if (GetAsyncKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
            if (g_isWindowHidden) {
                if (IsMouseVK(g_hkHideWin.vk) && g_hkHideWin.vk == vk && g_hkHideWin.fsModifiers == mods) {
                    PostMessage(g_hWnd, WM_HOTKEY, g_hkHideWin.id, 0);
                    return 1;
                }
            } else {
                AppHotkey* hotkeys[] = { &g_hkImmersion, &g_hkPlayPause, &g_hkBack, &g_hkForward, &g_hkPrevEp, &g_hkNextEp, &g_hkHideWin };
                for (auto* hk : hotkeys) {
                    if (IsMouseVK(hk->vk) && hk->vk == vk && hk->fsModifiers == mods) {
                        PostMessage(g_hWnd, WM_HOTKEY, hk->id, 0);
                        return 1;
                    }
                }
            }
        }
    }
    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}
/* 系统托盘图标管理 */
void AddTrayIcon() {
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = g_hWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"小窗浏览器");
    Shell_NotifyIcon(NIM_ADD, &g_nid);
}
void RemoveTrayIcon() {
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
}
void UpdateTaskbarVisibility() {
    LONG_PTR exStyle = GetWindowLongPtr(g_hWnd, GWL_EXSTYLE);
    if (g_useSystemTray) {
        SetWindowLongPtr(g_hWnd, GWL_EXSTYLE, exStyle | WS_EX_TOOLWINDOW);
        AddTrayIcon();
    }
    else {
        SetWindowLongPtr(g_hWnd, GWL_EXSTYLE, exStyle & ~WS_EX_TOOLWINDOW);
        RemoveTrayIcon();
    }
    if (!g_isWindowHidden) {
        ShowWindow(g_hWnd, SW_HIDE);
        ShowWindow(g_hWnd, SW_SHOW);
    }
}
/* 拖拽按钮子类化：左键按下时模拟标题栏拖动 */
LRESULT CALLBACK DragButtonProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_LBUTTONDOWN) {
        ReleaseCapture();
        SendMessage(GetParent(hWnd), WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    }
    return CallWindowProc(g_OldBtnProc, hWnd, uMsg, wParam, lParam);
}
/* 地址栏子类化：回车导航、单击全选 */
LRESULT CALLBACK EditWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
        SendMessage(GetParent(hWnd), WM_COMMAND, IDC_GO_BUTTON, 0);
        return 0;
    }
    if (uMsg == WM_CHAR && wParam == L'\r') {
        return 0;
    }
    if (uMsg == WM_LBUTTONDOWN) {
        if (GetFocus() != hWnd) {
            SetFocus(hWnd);
            SendMessage(hWnd, EM_SETSEL, 0, -1);
            return 0;
        }
    }
    return CallWindowProc(g_OldEditProc, hWnd, uMsg, wParam, lParam);
}
/* 预设命名弹窗：含重名检测 */
LRESULT CALLBACK NameInputWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static HWND hEdit;
    switch (message) {
    case WM_CREATE:
        CreateWindow(L"STATIC", L"请输入预设名称:", WS_CHILD | WS_VISIBLE, 10, 10, 200, 20, hWnd, NULL, g_hInst, NULL);
        hEdit = CreateWindow(L"EDIT", L"我的预设", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 10, 35, 210, 25, hWnd, (HMENU)IDC_NAME_EDIT, g_hInst, NULL);
        SendMessage(hEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        CreateWindow(L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 60, 70, 100, 30, hWnd, (HMENU)IDC_NAME_OK, g_hInst, NULL);
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_NAME_OK) {
            WCHAR buf[256];
            GetWindowText(hEdit, buf, 256);
            std::wstring newName = buf;
            if (newName.empty()) newName = L"未命名预设";
            bool exists = false;
            for (const auto& p : g_presets) {
                if (p.name == newName) { exists = true; break; }
            }
            if (exists) {
                MessageBox(hWnd, L"该预设名称已存在，请换一个名字。", L"提示", MB_ICONWARNING | MB_OK);
                return 0;
            }
            g_tempPresetName = newName;
            CaptureAndAddPreset(g_tempPresetName);
            DestroyWindow(hWnd);
            if (g_hPresetWin) DestroyWindow(g_hPresetWin);
            ShowPresetList(g_hWnd);
        }
        break;
    case WM_DESTROY:
        g_hNameInputPopup = NULL;
        EnableWindow(GetParent(hWnd), TRUE);
        break;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;
    default: return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
void ShowNameInputWindow(HWND hParent) {
    if (g_hNameInputPopup) return;
    static bool s_classRegistered = false;
    if (!s_classRegistered) {
        WNDCLASS wc = { 0 };
        wc.lpfnWndProc = NameInputWndProc;
        wc.hInstance = g_hInst;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"XiaoChuangNameInput";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClass(&wc);
        s_classRegistered = true;
    }
    HWND hOwner = g_hPresetWin ? g_hPresetWin : hParent;
    RECT rc; GetWindowRect(hOwner, &rc);
    int popW = 250, popH = 150;
    int x = rc.left + (rc.right - rc.left) / 2 - popW / 2;
    int y = rc.top + (rc.bottom - rc.top) / 2 - popH / 2;
    HMONITOR hMon = MonitorFromWindow(hOwner, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfo(hMon, &mi)) {
        if (x < mi.rcWork.left) x = mi.rcWork.left;
        if (y < mi.rcWork.top) y = mi.rcWork.top;
        if (x + popW > mi.rcWork.right) x = mi.rcWork.right - popW;
        if (y + popH > mi.rcWork.bottom) y = mi.rcWork.bottom - popH;
    }
    g_hNameInputPopup = CreateWindowEx(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, L"XiaoChuangNameInput", L"新建预设",
        WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU, x, y, popW, popH, hOwner, NULL, g_hInst, NULL);
    EnableWindow(hOwner, FALSE);
}
/* 预设管理窗口：列表含应用/重命名/删除，支持滚轮和还原默认 */
LRESULT CALLBACK PresetWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static int scrollY = 0;
    static int contentHeight = 0;
    static bool s_presetClosing = false;
    switch (message) {
    case WM_CREATE: {
        SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE, 0, 0, 0 };
        SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
        scrollY = 0;
        RECT rcClient; GetClientRect(hWnd, &rcClient);
        int winW = rcClient.right - rcClient.left;
        HWND hBtnAdd = CreateWindow(L"BUTTON", L"+ 新建预设", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, winW, BM_ROW_HEIGHT, hWnd, (HMENU)IDC_PRESET_ADD, g_hInst, NULL);
        SendMessage(hBtnAdd, WM_SETFONT, (WPARAM)g_hListFont, TRUE);
        int startY = BM_ROW_HEIGHT;
        for (size_t i = 0; i < g_presets.size(); ++i) {
            int y = startY + (int)i * BM_ROW_HEIGHT;
            int rowIdBase = IDC_PRESET_ROW_BASE + (int)i * 10;
            HWND hBtnApply = CreateWindow(L"BUTTON", L"➜", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, y, BM_BTN_SIZE, BM_ROW_HEIGHT, hWnd, (HMENU)(rowIdBase + 0), g_hInst, NULL);
            SendMessage(hBtnApply, WM_SETFONT, (WPARAM)g_hListFont, TRUE);
            int editW = winW - BM_BTN_SIZE * 2 - 25;
            if (editW < 50) editW = 50;
            HWND hName = CreateWindow(L"EDIT", g_presets[i].name.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                BM_BTN_SIZE, y, editW, BM_ROW_HEIGHT, hWnd, (HMENU)(rowIdBase + 1), g_hInst, NULL);
            SendMessage(hName, WM_SETFONT, (WPARAM)g_hListFont, TRUE);
            HWND hDel = CreateWindow(L"BUTTON", L"🗑", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                BM_BTN_SIZE + editW, y, BM_BTN_SIZE, BM_ROW_HEIGHT, hWnd, (HMENU)(rowIdBase + 2), g_hInst, NULL);
            SendMessage(hDel, WM_SETFONT, (WPARAM)g_hListFont, TRUE);
        }
        int listHeight = startY + (int)g_presets.size() * BM_ROW_HEIGHT;
        HWND hBtnReset = CreateWindow(L"BUTTON", L"↺ 还原默认状态", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, listHeight, winW, BM_ROW_HEIGHT, hWnd, (HMENU)IDC_PRESET_RESET, g_hInst, NULL);
        SendMessage(hBtnReset, WM_SETFONT, (WPARAM)g_hListFont, TRUE);
        contentHeight = listHeight + BM_ROW_HEIGHT;
        break;
    }
    case WM_SIZE: {
        RECT rc; GetClientRect(hWnd, &rc);
        SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE, 0, std::max(0, contentHeight - 1), (UINT)rc.bottom };
        SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
        break;
    }
    case WM_VSCROLL: {
        SCROLLINFO si = { sizeof(si), SIF_ALL };
        GetScrollInfo(hWnd, SB_VERT, &si);
        int oldY = si.nPos;
        switch (LOWORD(wParam)) {
        case SB_TOP: si.nPos = si.nMin; break;
        case SB_BOTTOM: si.nPos = si.nMax; break;
        case SB_LINEUP: si.nPos -= BM_ROW_HEIGHT; break;
        case SB_LINEDOWN: si.nPos += BM_ROW_HEIGHT; break;
        case SB_PAGEUP: si.nPos -= si.nPage; break;
        case SB_PAGEDOWN: si.nPos += si.nPage; break;
        case SB_THUMBTRACK: si.nPos = si.nTrackPos; break;
        }
        si.nPos = std::max(si.nMin, std::min(si.nPos, si.nMax - (int)si.nPage + 1));
        if (si.nMax < (int)si.nPage) si.nPos = 0;
        SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
        ScrollWindow(hWnd, 0, oldY - si.nPos, NULL, NULL);
        scrollY = si.nPos;
        return 0;
    }
    case WM_MOUSEWHEEL: {
        SCROLLINFO si = { sizeof(si), SIF_ALL };
        GetScrollInfo(hWnd, SB_VERT, &si);
        int oldY = si.nPos;
        si.nPos -= (GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA) * BM_ROW_HEIGHT;
        si.nPos = std::max(si.nMin, std::min(si.nPos, si.nMax - (int)si.nPage + 1));
        if (si.nMax < (int)si.nPage) si.nPos = 0;
        SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
        ScrollWindow(hWnd, 0, oldY - si.nPos, NULL, NULL);
        scrollY = si.nPos;
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (id == IDC_PRESET_ADD) {
            ShowNameInputWindow(g_hPresetWin);
        }
        else if (id == IDC_PRESET_RESET) {
            RestoreDefaultSettings();
            DestroyWindow(hWnd);
        }
        else if (id >= IDC_PRESET_ROW_BASE && code == EN_CHANGE && !s_presetClosing) {
            int idx = (id - IDC_PRESET_ROW_BASE) / 10;
            if (idx >= 0 && idx < g_presets.size()) {
                WCHAR buf[256];
                GetWindowText((HWND)lParam, buf, 256);
                g_presets[idx].name = buf;
            }
        }
        else if (id >= IDC_PRESET_ROW_BASE && code == BN_CLICKED) {
            int idx = (id - IDC_PRESET_ROW_BASE) / 10;
            int type = (id - IDC_PRESET_ROW_BASE) % 10;
            if (idx >= 0 && idx < g_presets.size()) {
                if (type == 0) {
                    ApplyPreset(idx);
                    DestroyWindow(hWnd);
                }
                else if (type == 2) {
                    g_presets.erase(g_presets.begin() + idx);
                    SavePresets();
                    DestroyWindow(hWnd);
                    ShowPresetList(g_hWnd);
                }
            }
        }
        break;
    }
    case WM_DESTROY:
        s_presetClosing = true;
        SavePresets();
        g_hPresetWin = NULL;
        break;
    case WM_NCDESTROY:
        s_presetClosing = false;
        break;
    default: return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
void ShowPresetList(HWND hParent) {
    if (g_hPresetWin && IsWindow(g_hPresetWin)) {
        SetForegroundWindow(g_hPresetWin);
        return;
    }
    RECT rcMain; GetWindowRect(hParent, &rcMain);
    int mainW = rcMain.right - rcMain.left;
    int mainH = rcMain.bottom - rcMain.top;
    int x = rcMain.left + (mainW - PRESET_WIN_WIDTH) / 2;
    int y = rcMain.top + (mainH - PRESET_WIN_HEIGHT) / 2;
    HMONITOR hMon = MonitorFromWindow(hParent, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfo(hMon, &mi)) {
        if (x < mi.rcWork.left) x = mi.rcWork.left;
        if (y < mi.rcWork.top) y = mi.rcWork.top;
        if (x + PRESET_WIN_WIDTH > mi.rcWork.right) x = mi.rcWork.right - PRESET_WIN_WIDTH;
        if (y + PRESET_WIN_HEIGHT > mi.rcWork.bottom) y = mi.rcWork.bottom - PRESET_WIN_HEIGHT;
    }
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = PresetWndProc;
    wc.hInstance = g_hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"XiaoChuangPresets";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    static bool s_classRegistered = false;
    if (!s_classRegistered) {
        RegisterClass(&wc);
        s_classRegistered = true;
    }
    g_hPresetWin = CreateWindow(L"XiaoChuangPresets", L"场景预设管理",
        WS_VISIBLE | WS_CAPTION | WS_SYSMENU | WS_POPUPWINDOW | WS_DLGFRAME,
        x, y, PRESET_WIN_WIDTH, PRESET_WIN_HEIGHT, hParent, NULL, g_hInst, NULL);
}
/* 标签页管理：TCS_OWNERDRAWFIXED 自绘，标签右侧 20px 为关闭区域，标题超长末尾拼接省略号 */
std::wstring FormatTabTitle(const std::wstring& rawTitle) {
    std::wstring out = rawTitle;
    const size_t MAX_LEN = 10;
    if (out.length() > MAX_LEN) {
        out = out.substr(0, MAX_LEN) + L"..";
    }
    return out;
}
void CreateNewTab(LPCWSTR url, LPCWSTR title, bool switchToNew) {
    TabData newTab;
    std::wstring safeUrl = url;
    if (safeUrl.empty()) safeUrl = g_homeUrl;
    safeUrl = NormalizeInputUrl(safeUrl);
    newTab.url = safeUrl;
    newTab.title = title;
    g_tabs.push_back(newTab);
    TCITEM tie;
    tie.mask = TCIF_TEXT;
    std::wstring displayTitle = FormatTabTitle(title);
    tie.pszText = (LPWSTR)displayTitle.c_str();
    TabCtrl_InsertItem(g_hTabCtrl, g_tabs.size() - 1, &tie);
    if (switchToNew) {
        SwitchToTab(g_tabs.size() - 1);
        g_isNavigatingFromTabSwitch = false;
        UpdateTabTitle(g_tabs.size() - 1, L"加载中...");
        UpdateStarIcon();
    }
}
void CloseTab(int index) {
    if (index < 0 || index >= g_tabs.size()) return;
    if (g_tabs.size() == 1) {
        std::wstring home = NormalizeInputUrl(g_homeUrl);
        g_currentTabIndex = 0;
        g_tabs[0].url = home;
        g_tabs[0].title = L"主页";
        UpdateTabTitle(0, L"主页");
        if (g_webview) {
            g_isNavigatingFromTabSwitch = true;
            g_webview->Navigate(home.c_str());
        }
        SetWindowText(g_hEditUrl, home.c_str());
        UpdateStarIcon();
        return;
    }
    g_tabs.erase(g_tabs.begin() + index);
    TabCtrl_DeleteItem(g_hTabCtrl, index);
    if (index < g_currentTabIndex) {
        g_currentTabIndex--;
    }
    else if (g_currentTabIndex >= (int)g_tabs.size()) {
        g_currentTabIndex = (int)g_tabs.size() - 1;
    }
    SwitchToTab(g_currentTabIndex);
}
void CloseCurrentTab() {
    CloseTab(g_currentTabIndex);
}
void SwitchToTab(int index) {
    if (index < 0 || index >= g_tabs.size()) return;
    g_currentTabIndex = index;
    TabCtrl_SetCurSel(g_hTabCtrl, index);
    UpdateTabTitle(index, g_tabs[index].title.c_str());
    UpdateStarIcon();
    if (g_webview) {
        g_isNavigatingFromTabSwitch = true;
        g_webview->Navigate(g_tabs[index].url.c_str());
        SetWindowText(g_hEditUrl, g_tabs[index].url.c_str());
    }
}
void UpdateTabTitle(int index, LPCWSTR title) {
    if (index < 0 || index >= g_tabs.size()) return;
    g_tabs[index].title = title;
    std::wstring displayTitle = FormatTabTitle(title);
    TCITEM tie;
    tie.mask = TCIF_TEXT;
    tie.pszText = (LPWSTR)displayTitle.c_str();
    TabCtrl_SetItem(g_hTabCtrl, index, &tie);
    InvalidateRect(g_hTabCtrl, NULL, TRUE);
    UpdateWindow(g_hTabCtrl);
}
void HandleTabClick(LPARAM lParam) {
    LPNMHDR pnm = (LPNMHDR)lParam;
    if (pnm->code == NM_CLICK && pnm->idFrom == IDC_TAB_CTRL) {
        POINT pt; GetCursorPos(&pt);
        ScreenToClient(g_hTabCtrl, &pt);
        TCHITTESTINFO hti;
        hti.pt = pt;
        int idx = TabCtrl_HitTest(g_hTabCtrl, &hti);
        if (idx != -1) {
            RECT rc;
            TabCtrl_GetItemRect(g_hTabCtrl, idx, &rc);
            if (pt.x > rc.right - 25) {
                CloseTab(idx);
            }
            else {
                SwitchToTab(idx);
            }
        }
    }
}
/* 书签下拉列表：行内编辑URL/标题，失焦自动关闭 */
LRESULT CALLBACK BookmarkWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static int scrollY = 0;
    static int contentHeight = 0;
    static bool s_bmClosing = false;
    switch (message) {
    case WM_CREATE: {
        SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE, 0, 0, 0 };
        SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
        scrollY = 0;
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int winW = rcClient.right - rcClient.left;
        for (size_t i = 0; i < g_bookmarks.size(); ++i) {
            int y = (int)i * BM_ROW_HEIGHT;
            int rowIdBase = IDC_BM_ROW_BASE + (int)i * 10;
            HWND hBtnGo = CreateWindow(L"BUTTON", L"➜", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, y, BM_BTN_SIZE, BM_ROW_HEIGHT, hWnd, (HMENU)(rowIdBase + 0), g_hInst, NULL);
            SendMessage(hBtnGo, WM_SETFONT, (WPARAM)g_hListFont, TRUE);
            int availableW = winW - BM_BTN_SIZE * 2 - 25;
            if (availableW < 100) availableW = 100;
            int urlW = availableW * 2 / 3;
            int titleW = availableW - urlW;
            HWND hUrl = CreateWindow(L"EDIT", g_bookmarks[i].url.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                BM_BTN_SIZE, y, urlW, BM_ROW_HEIGHT, hWnd, (HMENU)(rowIdBase + 2), g_hInst, NULL);
            SendMessage(hUrl, WM_SETFONT, (WPARAM)g_hListFont, TRUE);
            HWND hTitle = CreateWindow(L"EDIT", g_bookmarks[i].title.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                BM_BTN_SIZE + urlW, y, titleW, BM_ROW_HEIGHT, hWnd, (HMENU)(rowIdBase + 1), g_hInst, NULL);
            SendMessage(hTitle, WM_SETFONT, (WPARAM)g_hListFont, TRUE);
            HWND hDel = CreateWindow(L"BUTTON", L"🗑", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                BM_BTN_SIZE + urlW + titleW, y, BM_BTN_SIZE, BM_ROW_HEIGHT, hWnd, (HMENU)(rowIdBase + 3), g_hInst, NULL);
            SendMessage(hDel, WM_SETFONT, (WPARAM)g_hListFont, TRUE);
        }
        contentHeight = (int)g_bookmarks.size() * BM_ROW_HEIGHT;
        break;
    }
    case WM_SIZE: {
        RECT rc; GetClientRect(hWnd, &rc);
        SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE, 0, std::max(0, contentHeight - 1), (UINT)rc.bottom };
        SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
        break;
    }
    case WM_VSCROLL: {
        SCROLLINFO si = { sizeof(si), SIF_ALL };
        GetScrollInfo(hWnd, SB_VERT, &si);
        int oldY = si.nPos;
        switch (LOWORD(wParam)) {
        case SB_TOP: si.nPos = si.nMin; break;
        case SB_BOTTOM: si.nPos = si.nMax; break;
        case SB_LINEUP: si.nPos -= BM_ROW_HEIGHT; break;
        case SB_LINEDOWN: si.nPos += BM_ROW_HEIGHT; break;
        case SB_PAGEUP: si.nPos -= si.nPage; break;
        case SB_PAGEDOWN: si.nPos += si.nPage; break;
        case SB_THUMBTRACK: si.nPos = si.nTrackPos; break;
        }
        si.nPos = std::max(si.nMin, std::min(si.nPos, si.nMax - (int)si.nPage + 1));
        if (si.nMax < (int)si.nPage) si.nPos = 0;
        SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
        ScrollWindow(hWnd, 0, oldY - si.nPos, NULL, NULL);
        scrollY = si.nPos;
        return 0;
    }
    case WM_MOUSEWHEEL: {
        SCROLLINFO si = { sizeof(si), SIF_ALL };
        GetScrollInfo(hWnd, SB_VERT, &si);
        int oldY = si.nPos;
        si.nPos -= (GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA) * BM_ROW_HEIGHT;
        si.nPos = std::max(si.nMin, std::min(si.nPos, si.nMax - (int)si.nPage + 1));
        if (si.nMax < (int)si.nPage) si.nPos = 0;
        SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
        ScrollWindow(hWnd, 0, oldY - si.nPos, NULL, NULL);
        scrollY = si.nPos;
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (id >= IDC_BM_ROW_BASE && code == EN_CHANGE && !s_bmClosing) {
            int idx = (id - IDC_BM_ROW_BASE) / 10;
            int type = (id - IDC_BM_ROW_BASE) % 10;
            if (idx >= 0 && idx < g_bookmarks.size()) {
                WCHAR buf[2048];
                GetWindowText((HWND)lParam, buf, 2048);
                if (type == 1) g_bookmarks[idx].title = buf;
                if (type == 2) g_bookmarks[idx].url = buf;
            }
        }
        else if (id >= IDC_BM_ROW_BASE && code == BN_CLICKED) {
            int idx = (id - IDC_BM_ROW_BASE) / 10;
            int type = (id - IDC_BM_ROW_BASE) % 10;
            if (idx >= 0 && idx < g_bookmarks.size()) {
                if (type == 0) {
                    if (g_webview) g_webview->Navigate(g_bookmarks[idx].url.c_str());
                    DestroyWindow(hWnd);
                }
                else if (type == 3) {
                    g_bookmarks.erase(g_bookmarks.begin() + idx);
                    SaveBookmarks();
                    UpdateStarIcon();
                    DestroyWindow(hWnd);
                    ShowBookmarkList(g_hWnd);
                }
            }
        }
        break;
    }
    case WM_DESTROY:
        s_bmClosing = true;
        SaveBookmarks();
        g_hBmPopup = NULL;
        break;
    case WM_NCDESTROY:
        s_bmClosing = false;
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            DestroyWindow(hWnd);
        }
        break;
    default: return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
void ShowBookmarkList(HWND hParent) {
    if (g_hBmPopup && IsWindow(g_hBmPopup)) {
        DestroyWindow(g_hBmPopup);
        return;
    }
    RECT rcMain;
    GetClientRect(hParent, &rcMain);
    int mainWinWidth = rcMain.right - rcMain.left;
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = BookmarkWndProc;
    wc.hInstance = g_hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"XiaoChuangBookmarks";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    static bool s_classRegistered = false;
    if (!s_classRegistered) {
        RegisterClass(&wc);
        s_classRegistered = true;
    }
    POINT pt = { rcMain.left, TOTAL_TOP_HEIGHT };
    ClientToScreen(hParent, &pt);
    int height = (int)g_bookmarks.size() * BM_ROW_HEIGHT;
    if (height > BM_MAX_HEIGHT) height = BM_MAX_HEIGHT;
    if (height < 50) height = 50;
    g_hBmPopup = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"XiaoChuangBookmarks", L"",
        WS_POPUP | WS_VISIBLE | WS_BORDER | WS_VSCROLL,
        pt.x, pt.y, mainWinWidth, height, hParent, NULL, g_hInst, NULL);
    ShowWindow(g_hBmPopup, SW_SHOW);
    SetFocus(g_hBmPopup);
}
/* 设置窗口：沉浸参数、自动暂停、热键绑定（点击按钮后按键或鼠标按键录入） */
LRESULT CALLBACK SettingsWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static HWND hRadiusEdit, hSnapEdit, hChkImmStyle, hBtnHk1, hBtnHk2, hBtnHk3, hBtnHk4, hBtnHk5, hBtnHk6, hBtnHk7;
    static bool s_isClosing = false;
    /* 取消当前热键绑定模式并恢复按钮文本 */
    auto CancelBinding = [&]() {
        if (!g_currentSettingKey) return;
        HWND hBtn = NULL;
        if (g_currentSettingKey == &g_hkImmersion) hBtn = hBtnHk1;
        else if (g_currentSettingKey == &g_hkPlayPause) hBtn = hBtnHk2;
        else if (g_currentSettingKey == &g_hkBack) hBtn = hBtnHk3;
        else if (g_currentSettingKey == &g_hkForward) hBtn = hBtnHk4;
        else if (g_currentSettingKey == &g_hkPrevEp) hBtn = hBtnHk5;
        else if (g_currentSettingKey == &g_hkNextEp) hBtn = hBtnHk6;
        else if (g_currentSettingKey == &g_hkHideWin) hBtn = hBtnHk7;
        if (hBtn) SetWindowText(hBtn, GetHotkeyString(g_currentSettingKey->fsModifiers, g_currentSettingKey->vk).c_str());
        g_currentSettingKey = nullptr;
    };
    switch (message) {
    case WM_CREATE: {
        RECT rcClient; GetClientRect(hDlg, &rcClient);
        int cw = rcClient.right - rcClient.left;
        int margin = 20;
        int contentW = cw - margin * 2;
        int y = 20;
        CreateWindow(L"STATIC", L"挖孔半径(px):", WS_CHILD | WS_VISIBLE, margin, y, 100, 20, hDlg, NULL, g_hInst, NULL);
        hRadiusEdit = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 130, y, 80, 20, hDlg, (HMENU)IDC_SET_RADIUS, g_hInst, NULL);
        SetWindowText(hRadiusEdit, std::to_wstring(g_holeRadius).c_str());
        y += 30;
        CreateWindow(L"STATIC", L"吸附距离(px):", WS_CHILD | WS_VISIBLE, margin, y, 100, 20, hDlg, NULL, g_hInst, NULL);
        hSnapEdit = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 130, y, 80, 20, hDlg, (HMENU)IDC_SET_SNAP, g_hInst, NULL);
        SetWindowText(hSnapEdit, std::to_wstring(g_snapThreshold).c_str());
        y += 35;
        g_hChkAutoPause = CreateWindow(L"BUTTON", L"老板键隐藏时自动暂停视频", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, margin, y, contentW, 20, hDlg, (HMENU)IDC_CHK_AUTOPAUSE, g_hInst, NULL);
        SendMessage(g_hChkAutoPause, BM_SETCHECK, g_autoPauseOnHide ? BST_CHECKED : BST_UNCHECKED, 0);
        y += 25;
        hChkImmStyle = CreateWindow(L"BUTTON", L"沉浸模式使用整体透明（替代挖孔）", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, margin, y, contentW, 20, hDlg, (HMENU)IDC_CHK_IMMSTYLE, g_hInst, NULL);
        SendMessage(hChkImmStyle, BM_SETCHECK, g_immersionStyle ? BST_CHECKED : BST_UNCHECKED, 0);
        y += 25;
        CreateWindow(L"BUTTON", L"输入时自动禁用全局热键", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, margin, y, contentW, 20, hDlg, (HMENU)IDC_CHK_TYPING, g_hInst, NULL);
        SendMessage(GetDlgItem(hDlg, IDC_CHK_TYPING), BM_SETCHECK, g_disableHkOnTyping ? BST_CHECKED : BST_UNCHECKED, 0);
        y += 25;
        CreateWindow(L"BUTTON", L"隐藏任务栏图标（最小化到托盘）", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, margin, y, contentW, 20, hDlg, (HMENU)IDC_CHK_TRAY, g_hInst, NULL);
        SendMessage(GetDlgItem(hDlg, IDC_CHK_TRAY), BM_SETCHECK, g_useSystemTray ? BST_CHECKED : BST_UNCHECKED, 0);
        y += 30;
        CreateWindow(L"STATIC", L"默认主页:", WS_CHILD | WS_VISIBLE, margin, y, 80, 20, hDlg, NULL, g_hInst, NULL);
        g_hEditHome = CreateWindow(L"EDIT", g_homeUrl.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 100, y, cw - 100 - margin, 20, hDlg, (HMENU)IDC_EDIT_HOME, g_hInst, NULL);
        y += 35;
        int btnX = 150;
        int btnW = cw - btnX - margin;
        auto CreateKeyBtn = [&](LPCWSTR title, int id) {
            CreateWindow(L"STATIC", title, WS_CHILD | WS_VISIBLE, margin, y, 120, 20, hDlg, NULL, g_hInst, NULL);
            HWND h = CreateWindow(L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, btnX, y, btnW, 25, hDlg, (HMENU)id, g_hInst, NULL);
            y += 40;
            return h;
            };
        hBtnHk1 = CreateKeyBtn(L"沉浸模式开关:", IDC_BTN_HK1);
        hBtnHk2 = CreateKeyBtn(L"暂停/继续:", IDC_BTN_HK2);
        hBtnHk3 = CreateKeyBtn(L"后退:", IDC_BTN_HK3);
        hBtnHk4 = CreateKeyBtn(L"快进:", IDC_BTN_HK4);
        hBtnHk5 = CreateKeyBtn(L"上一集:", IDC_BTN_HK5);
        hBtnHk6 = CreateKeyBtn(L"下一集:", IDC_BTN_HK6);
        hBtnHk7 = CreateKeyBtn(L"隐藏/显示:", IDC_BTN_HK7);
        SetWindowText(hBtnHk1, GetHotkeyString(g_hkImmersion.fsModifiers, g_hkImmersion.vk).c_str());
        SetWindowText(hBtnHk2, GetHotkeyString(g_hkPlayPause.fsModifiers, g_hkPlayPause.vk).c_str());
        SetWindowText(hBtnHk3, GetHotkeyString(g_hkBack.fsModifiers, g_hkBack.vk).c_str());
        SetWindowText(hBtnHk4, GetHotkeyString(g_hkForward.fsModifiers, g_hkForward.vk).c_str());
        SetWindowText(hBtnHk5, GetHotkeyString(g_hkPrevEp.fsModifiers, g_hkPrevEp.vk).c_str());
        SetWindowText(hBtnHk6, GetHotkeyString(g_hkNextEp.fsModifiers, g_hkNextEp.vk).c_str());
        SetWindowText(hBtnHk7, GetHotkeyString(g_hkHideWin.fsModifiers, g_hkHideWin.vk).c_str());
        CreateWindow(L"STATIC", L"提示: 鼠标移动到顶部白条中间显现窗口移动键。窗口吸附后按住Ctrl移动可脱离吸附。", WS_CHILD | WS_VISIBLE, margin, y, contentW, 40, hDlg, NULL, g_hInst, NULL);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (HIWORD(wParam) == EN_CHANGE && !s_isClosing) {
            WCHAR buf[2048];
            if (id == IDC_SET_RADIUS) { GetWindowText(hRadiusEdit, buf, 10); g_holeRadius = _wtoi(buf); if (g_holeRadius < 0) g_holeRadius = 0; }
            else if (id == IDC_SET_SNAP) { GetWindowText(hSnapEdit, buf, 10); g_snapThreshold = _wtoi(buf); }
        }
        else if (id == IDC_CHK_AUTOPAUSE) {
            g_autoPauseOnHide = (SendMessage(g_hChkAutoPause, BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        else if (id == IDC_CHK_IMMSTYLE) {
            g_immersionStyle = (SendMessage(hChkImmStyle, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
            SaveConfig();
        }
        else if (id == IDC_CHK_TYPING) {
            g_disableHkOnTyping = (SendMessage(GetDlgItem(hDlg, IDC_CHK_TYPING), BM_GETCHECK, 0, 0) == BST_CHECKED);
            SaveConfig();
        }
        else if (id == IDC_CHK_TRAY) {
            g_useSystemTray = (SendMessage(GetDlgItem(hDlg, IDC_CHK_TRAY), BM_GETCHECK, 0, 0) == BST_CHECKED);
            UpdateTaskbarVisibility();
            SaveConfig();
        }
        else if (id == IDC_BTN_HK1) { CancelBinding(); g_currentSettingKey = &g_hkImmersion; SetWindowText(hBtnHk1, L"按下新键..."); SetFocus(hDlg); }
        else if (id == IDC_BTN_HK2) { CancelBinding(); g_currentSettingKey = &g_hkPlayPause; SetWindowText(hBtnHk2, L"按下新键..."); SetFocus(hDlg); }
        else if (id == IDC_BTN_HK3) { CancelBinding(); g_currentSettingKey = &g_hkBack; SetWindowText(hBtnHk3, L"按下新键..."); SetFocus(hDlg); }
        else if (id == IDC_BTN_HK4) { CancelBinding(); g_currentSettingKey = &g_hkForward; SetWindowText(hBtnHk4, L"按下新键..."); SetFocus(hDlg); }
        else if (id == IDC_BTN_HK5) { CancelBinding(); g_currentSettingKey = &g_hkPrevEp; SetWindowText(hBtnHk5, L"按下新键..."); SetFocus(hDlg); }
        else if (id == IDC_BTN_HK6) { CancelBinding(); g_currentSettingKey = &g_hkNextEp; SetWindowText(hBtnHk6, L"按下新键..."); SetFocus(hDlg); }
        else if (id == IDC_BTN_HK7) { CancelBinding(); g_currentSettingKey = &g_hkHideWin; SetWindowText(hBtnHk7, L"按下新键..."); SetFocus(hDlg); }
        break;
    }
    case WM_KEYDOWN: case WM_SYSKEYDOWN: {
        if (g_currentSettingKey) {
            UINT vk = (UINT)wParam;
            if (vk == VK_ESCAPE) { CancelBinding(); break; }
            if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_TAB) break;
            UINT mods = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
            if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
            if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
            g_currentSettingKey->vk = vk; g_currentSettingKey->fsModifiers = mods;
            HWND hBtn = NULL;
            if (g_currentSettingKey == &g_hkImmersion) hBtn = hBtnHk1;
            else if (g_currentSettingKey == &g_hkPlayPause) hBtn = hBtnHk2;
            else if (g_currentSettingKey == &g_hkBack) hBtn = hBtnHk3;
            else if (g_currentSettingKey == &g_hkForward) hBtn = hBtnHk4;
            else if (g_currentSettingKey == &g_hkPrevEp) hBtn = hBtnHk5;
            else if (g_currentSettingKey == &g_hkNextEp) hBtn = hBtnHk6;
            else if (g_currentSettingKey == &g_hkHideWin) hBtn = hBtnHk7;
            SetWindowText(hBtn, GetHotkeyString(mods, vk).c_str());
            UpdateHotkeysState(g_hWnd);
            SaveConfig();
            g_currentSettingKey = nullptr;
        }
        break;
    }
    case WM_APP + 1: {
        SetWindowText(hBtnHk1, GetHotkeyString(g_hkImmersion.fsModifiers, g_hkImmersion.vk).c_str());
        SetWindowText(hBtnHk2, GetHotkeyString(g_hkPlayPause.fsModifiers, g_hkPlayPause.vk).c_str());
        SetWindowText(hBtnHk3, GetHotkeyString(g_hkBack.fsModifiers, g_hkBack.vk).c_str());
        SetWindowText(hBtnHk4, GetHotkeyString(g_hkForward.fsModifiers, g_hkForward.vk).c_str());
        SetWindowText(hBtnHk5, GetHotkeyString(g_hkPrevEp.fsModifiers, g_hkPrevEp.vk).c_str());
        SetWindowText(hBtnHk6, GetHotkeyString(g_hkNextEp.fsModifiers, g_hkNextEp.vk).c_str());
        SetWindowText(hBtnHk7, GetHotkeyString(g_hkHideWin.fsModifiers, g_hkHideWin.vk).c_str());
        break;
    }
    case WM_CLOSE: {
        CancelBinding();
        /* 关闭前读取默认主页编辑框内容 */
        if (g_hEditHome && IsWindow(g_hEditHome)) {
            WCHAR buf[2048];
            GetWindowText(g_hEditHome, buf, 2048);
            g_homeUrl = buf;
            if (g_homeUrl.empty()) g_homeUrl = L"https://www.bilibili.com";
        }
        /* 设置关闭标志，阻止 DestroyWindow 销毁子编辑框时
           EN_CHANGE 将空文本写入全局变量 */
        s_isClosing = true;
        EnableWindow(g_hWnd, TRUE);
        DestroyWindow(hDlg);
        g_hSettingsWin = NULL;
        g_hEditHome = NULL;
        s_isClosing = false;
        SaveConfig();
        return 0;
    }
    case WM_DESTROY:
        s_isClosing = true;
        g_hEditHome = NULL;
        break;
    }
    return DefWindowProc(hDlg, message, wParam, lParam);
}
void OpenSettingsWindow(HWND hParent) {
    if (g_hSettingsWin && IsWindow(g_hSettingsWin)) {
        SetForegroundWindow(g_hSettingsWin);
        return;
    }
    WNDCLASS wc = { 0 }; wc.lpfnWndProc = SettingsWndProc; wc.hInstance = g_hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.lpszClassName = L"XiaoChuangSettings";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    static bool s_classRegistered = false;
    if (!s_classRegistered) {
        RegisterClass(&wc);
        s_classRegistered = true;
    }
    int contentH = 20 + 30 + 35 + 25 + 25 + 25 + 30 + 35 + (40 * 7) + 40 + 20;
    RECT rcAdj = { 0, 0, 420, contentH };
    AdjustWindowRectEx(&rcAdj, WS_SYSMENU | WS_CAPTION, FALSE, WS_EX_DLGMODALFRAME);
    int winW = rcAdj.right - rcAdj.left;
    int winH = rcAdj.bottom - rcAdj.top;
    RECT rcMain; GetWindowRect(hParent, &rcMain);
    int x = rcMain.left + ((rcMain.right - rcMain.left) - winW) / 2;
    int y = rcMain.top + ((rcMain.bottom - rcMain.top) - winH) / 2;
    HMONITOR hMon = MonitorFromWindow(hParent, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfo(hMon, &mi)) {
        if (x < mi.rcWork.left) x = mi.rcWork.left;
        if (y < mi.rcWork.top) y = mi.rcWork.top;
        if (x + winW > mi.rcWork.right) x = mi.rcWork.right - winW;
        if (y + winH > mi.rcWork.bottom) y = mi.rcWork.bottom - winH;
    }
    g_hSettingsWin = CreateWindowEx(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"XiaoChuangSettings", L"小窗设置",
        WS_VISIBLE | WS_SYSMENU | WS_CAPTION, x, y, winW, winH, hParent, NULL, g_hInst, NULL);
}
/* 沉浸模式通过 SetWindowPos 定位，禁止顶栏改变 WebView 视口
   挖孔模式：跟随鼠标创建椭圆区域差集实现穿透
   整体透明模式：鼠标进出窗口区域时切换 alpha 值 */
void UpdateImmersionHole() {
    if (!g_hWnd) return;
    if (!g_isImmersionMode) {
        SetWindowRgn(g_hWnd, NULL, TRUE);
        return;
    }
    POINT pt; GetCursorPos(&pt);
    if (g_immersionStyle == 1) {
        RECT rcWnd; GetWindowRect(g_hWnd, &rcWnd);
        bool mouseOver = PtInRect(&rcWnd, pt);
        if (mouseOver != g_wasMouseOverImmersion) {
            SetLayeredWindowAttributes(g_hWnd, 0, mouseOver ? 0 : 255, LWA_ALPHA);
            g_wasMouseOverImmersion = mouseOver;
        }
    }
    else {
        if (g_holeRadius <= 0) {
            SetWindowRgn(g_hWnd, NULL, TRUE);
            return;
        }
        static POINT s_lastCursorPos = { LONG_MIN, LONG_MIN };
        if (pt.x == s_lastCursorPos.x && pt.y == s_lastCursorPos.y) return;
        s_lastCursorPos = pt;
        POINT ptClient = pt; ScreenToClient(g_hWnd, &ptClient);
        RECT rcClient; GetClientRect(g_hWnd, &rcClient);
        HRGN hRgnFull = CreateRectRgn(0, 0, rcClient.right, rcClient.bottom);
        HRGN hRgnHole = CreateEllipticRgn(ptClient.x - g_holeRadius, ptClient.y - g_holeRadius, ptClient.x + g_holeRadius, ptClient.y + g_holeRadius);
        HRGN hRgnResult = CreateRectRgn(0, 0, 0, 0);
        CombineRgn(hRgnResult, hRgnFull, hRgnHole, RGN_DIFF);
        SetWindowRgn(g_hWnd, hRgnResult, TRUE);
        DeleteObject(hRgnFull); DeleteObject(hRgnHole);
    }
}
/* 全屏切换：使用工作区(rcWork)而非整屏，沉浸模式下禁用 */
void ToggleFullscreen() {
    if (g_isImmersionMode) return;
    HMONITOR hMon = MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);
    if (!g_isFullscreen) {
        GetWindowRect(g_hWnd, &g_preFullscreenRect);
        RECT& m = mi.rcWork;
        SetWindowPos(g_hWnd, NULL, m.left, m.top, m.right - m.left, m.bottom - m.top,
            SWP_NOZORDER | SWP_FRAMECHANGED);
        g_isFullscreen = true;
    }
    else {
        SetWindowPos(g_hWnd, NULL, g_preFullscreenRect.left, g_preFullscreenRect.top,
            g_preFullscreenRect.right - g_preFullscreenRect.left,
            g_preFullscreenRect.bottom - g_preFullscreenRect.top,
            SWP_NOZORDER | SWP_FRAMECHANGED);
        g_isFullscreen = false;
    }
    SetWindowText(g_hBtnFullscreen, g_isFullscreen ? L"❐" : L"□");
}
/* 布局计算：导航栏按钮顺序 [地址栏][☆][⌵][Go][预设][设置] */
void ResizeLayout(HWND hWnd) {
    RECT rc; GetClientRect(hWnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    if (!g_isImmersionMode) {
        int moveBtnW = 60;
        if (g_hBtnMove) MoveWindow(g_hBtnMove, (width - moveBtnW) / 2, 0, moveBtnW, DRAG_BAR_HEIGHT, TRUE);
        int tabBtnW = 25;
        if (g_hBtnCloseApp) MoveWindow(g_hBtnCloseApp, width - tabBtnW, DRAG_BAR_HEIGHT, tabBtnW, TAB_BAR_HEIGHT, TRUE);
        if (g_hBtnFullscreen) MoveWindow(g_hBtnFullscreen, width - tabBtnW * 2, DRAG_BAR_HEIGHT, tabBtnW, TAB_BAR_HEIGHT, TRUE);
        if (g_hBtnCloseTab) MoveWindow(g_hBtnCloseTab, width - tabBtnW * 3, DRAG_BAR_HEIGHT, tabBtnW, TAB_BAR_HEIGHT, TRUE);
        if (g_hBtnAddTab) MoveWindow(g_hBtnAddTab, width - tabBtnW * 4, DRAG_BAR_HEIGHT, tabBtnW, TAB_BAR_HEIGHT, TRUE);
        if (g_hTabCtrl) MoveWindow(g_hTabCtrl, 0, DRAG_BAR_HEIGHT, width - tabBtnW * 4, TAB_BAR_HEIGHT, TRUE);
        int btnW = 35;
        int setBtnW = 50;
        int padding = 5;
        int navY = DRAG_BAR_HEIGHT + TAB_BAR_HEIGHT;
        int navH = NAV_BAR_HEIGHT - 4;
        int rightTotal = setBtnW * 2 + btnW * 3 + padding * 6;
        int addrW = width - rightTotal - padding;
        if (g_hEditUrl) MoveWindow(g_hEditUrl, padding, navY, addrW, navH, TRUE);
        int x = padding + addrW + padding;
        if (g_hBtnStar) MoveWindow(g_hBtnStar, x, navY, btnW, navH, TRUE);
        x += btnW + padding;
        if (g_hBtnBmList) MoveWindow(g_hBtnBmList, x, navY, btnW, navH, TRUE);
        x += btnW + padding;
        if (g_hBtnGo) MoveWindow(g_hBtnGo, x, navY, btnW, navH, TRUE);
        x += btnW + padding;
        if (g_hBtnPreset) MoveWindow(g_hBtnPreset, x, navY, setBtnW, navH, TRUE);
        x += setBtnW + padding;
        if (g_hBtnSet) MoveWindow(g_hBtnSet, x, navY, setBtnW, navH, TRUE);
    }
    if (g_controller) {
        RECT webViewBounds;
        if (g_isImmersionMode) {
            webViewBounds = { 0, 0, width, height };
        }
        else {
            webViewBounds = { 0, TOTAL_TOP_HEIGHT, width, height };
        }
        g_controller->put_Bounds(webViewBounds);
        wil::com_ptr<ICoreWebView2Controller2> controller2;
        g_controller->QueryInterface(IID_PPV_ARGS(&controller2));
        if (controller2) {
            COREWEBVIEW2_COLOR color = { 255, 0, 0, 0 };
            controller2->put_DefaultBackgroundColor(color);
        }
    }
}
/* WebView2 初始化：注册新窗口拦截、导航/标题/地址变更事件，恢复上次会话标签 */
void InitWebView2(HWND hWnd) {
    CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hWnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result)) {
                    ShowErrorBox(hWnd, L"初始化错误", L"无法创建 WebView2 环境。\n请确保已安装 'Microsoft Edge WebView2 Runtime'。", result);
                    return result;
                }
                env->CreateCoreWebView2Controller(hWnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [hWnd](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result)) {
                                ShowErrorBox(hWnd, L"初始化错误", L"无法创建 WebView2 控制器。", result);
                                return result;
                            }
                            g_controller = controller;
                            g_controller->get_CoreWebView2(g_webview.put());
                            wil::com_ptr<ICoreWebView2Settings> settings;
                            g_webview->get_Settings(&settings);
                            if (settings) {
                                settings->put_IsScriptEnabled(TRUE);
                                settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                                settings->put_IsWebMessageEnabled(TRUE);
                            }
                            g_webview->add_NewWindowRequested(
                                Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                        wil::unique_cotaskmem_string uri;
                                        args->get_Uri(&uri);
                                        args->put_Handled(TRUE);
                                        std::wstring targetUrl = uri.get();
                                        if (targetUrl.empty()) targetUrl = g_homeUrl;
                                        CreateNewTab(targetUrl.c_str(), L"New Tab", true);
                                        return S_OK;
                                    }).Get(), nullptr);
                            g_webview->add_NavigationStarting(
                                Callback<ICoreWebView2NavigationStartingEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                        if (!g_isNavigatingFromTabSwitch) {
                                            UpdateTabTitle(g_currentTabIndex, L"加载中...");
                                        }
                                        return S_OK;
                                    }).Get(), nullptr);
                            g_webview->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                        g_isNavigatingFromTabSwitch = false;
                                        wil::unique_cotaskmem_string title;
                                        sender->get_DocumentTitle(&title);
                                        if (title.get()) {
                                            UpdateTabTitle(g_currentTabIndex, title.get());
                                        }
                                        UpdateStarIcon();
                                        return S_OK;
                                    }).Get(), nullptr);
                            g_webview->add_DocumentTitleChanged(
                                Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                                    [](ICoreWebView2* sender, IUnknown* args) -> HRESULT {
                                        wil::unique_cotaskmem_string title;
                                        sender->get_DocumentTitle(&title);
                                        if (title.get()) {
                                            UpdateTabTitle(g_currentTabIndex, title.get());
                                        }
                                        return S_OK;
                                    }).Get(), nullptr);
                            /* 地址变化时(含重定向)立即同步书签星标状态 */
                            g_webview->add_SourceChanged(
                                Callback<ICoreWebView2SourceChangedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs* args) -> HRESULT {
                                        wil::unique_cotaskmem_string uri; sender->get_Source(&uri);
                                        if (uri.get()) {
                                            SetWindowText(g_hEditUrl, uri.get());
                                            if (!g_isNavigatingFromTabSwitch && g_currentTabIndex >= 0 && g_currentTabIndex < g_tabs.size()) {
                                                g_tabs[g_currentTabIndex].url = uri.get();
                                            }
                                        }
                                        UpdateStarIcon();
                                        return S_OK;
                                    }).Get(), nullptr);
                            int savedTabCount = GetPrivateProfileInt(_T("Session"), _T("TabCount"), 0, g_iniPath);
                            int savedActiveTab = GetPrivateProfileInt(_T("Session"), _T("ActiveTab"), 0, g_iniPath);
                            if (savedTabCount > 0) {
                                for (int i = 0; i < savedTabCount; ++i) {
                                    TCHAR keyUrl[32], keyTitle[32];
                                    wsprintf(keyUrl, L"TabUrl%d", i);
                                    wsprintf(keyTitle, L"TabTitle%d", i);
                                    TCHAR urlBuf[2048], titleBuf[256];
                                    GetPrivateProfileString(_T("Session"), keyUrl, g_homeUrl.c_str(), urlBuf, 2048, g_iniPath);
                                    GetPrivateProfileString(_T("Session"), keyTitle, L"New Tab", titleBuf, 256, g_iniPath);
                                    CreateNewTab(urlBuf, titleBuf, false);
                                }
                                if (savedActiveTab >= (int)g_tabs.size()) savedActiveTab = (int)g_tabs.size() - 1;
                                if (savedActiveTab < 0) savedActiveTab = 0;
                                SwitchToTab(savedActiveTab);
                            }
                            else {
                                CreateNewTab(g_homeUrl.c_str(), L"Home", true);
                            }
                            ResizeLayout(hWnd);
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
}
/* 主窗口消息处理 */
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        INITCOMMONCONTROLSEX icex;
        icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icex.dwICC = ICC_TAB_CLASSES;
        InitCommonControlsEx(&icex);
        g_hEditUrl = CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_ADDRESS_BAR, g_hInst, NULL);
        SendMessage(g_hEditUrl, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        g_OldEditProc = (WNDPROC)SetWindowLongPtr(g_hEditUrl, GWLP_WNDPROC, (LONG_PTR)EditWndProc);
        g_hBtnStar = CreateWindow(_T("BUTTON"), _T("☆"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_STAR, g_hInst, NULL);
        SendMessage(g_hBtnStar, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        g_hBtnBmList = CreateWindow(_T("BUTTON"), _T("⌵"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_BMLIST, g_hInst, NULL);
        SendMessage(g_hBtnBmList, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        g_hBtnGo = CreateWindow(_T("BUTTON"), _T("Go"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hWnd, (HMENU)IDC_GO_BUTTON, g_hInst, NULL);
        SendMessage(g_hBtnGo, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        g_hBtnPreset = CreateWindow(_T("BUTTON"), _T("预设"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_PRESET, g_hInst, NULL);
        SendMessage(g_hBtnPreset, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        g_hBtnSet = CreateWindow(_T("BUTTON"), _T("设置"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hWnd, (HMENU)IDC_SET_BUTTON, g_hInst, NULL);
        SendMessage(g_hBtnSet, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        g_hBtnMove = CreateWindow(_T("BUTTON"), _T("✥"), WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, hWnd, (HMENU)IDC_MOVE_BUTTON, g_hInst, NULL);
        SendMessage(g_hBtnMove, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        g_OldBtnProc = (WNDPROC)SetWindowLongPtr(g_hBtnMove, GWLP_WNDPROC, (LONG_PTR)DragButtonProc);
        g_hTabCtrl = CreateWindow(WC_TABCONTROL, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FIXEDWIDTH | TCS_OWNERDRAWFIXED, 0, 0, 0, 0, hWnd, (HMENU)IDC_TAB_CTRL, g_hInst, NULL);
        SendMessage(g_hTabCtrl, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        SendMessage(g_hTabCtrl, TCM_SETITEMSIZE, 0, MAKELPARAM(TAB_WIDTH, 26));
        g_hBtnAddTab = CreateWindow(_T("BUTTON"), _T("+"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hWnd, (HMENU)IDC_ADD_TAB, g_hInst, NULL);
        SendMessage(g_hBtnAddTab, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        g_hBtnCloseTab = CreateWindow(_T("BUTTON"), _T("-"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hWnd, (HMENU)IDC_CLOSE_TAB, g_hInst, NULL);
        SendMessage(g_hBtnCloseTab, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        g_hBtnFullscreen = CreateWindow(_T("BUTTON"), _T("□"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_FULLSCREEN, g_hInst, NULL);
        SendMessage(g_hBtnFullscreen, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        g_hBtnCloseApp = CreateWindow(_T("BUTTON"), _T("×"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hWnd, (HMENU)IDC_BTN_CLOSE_APP, g_hInst, NULL);
        SendMessage(g_hBtnCloseApp, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        SetTimer(hWnd, 1, 16, NULL);
        InitWebView2(hWnd);
        UpdateHotkeysState(hWnd);
        break;
        /* 自绘标签页：TCS_OWNERDRAWFIXED，右侧20px为关闭按钮区域 */
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pDis = (LPDRAWITEMSTRUCT)lParam;
        if (pDis->CtlID == IDC_TAB_CTRL) {
            int tabIndex = pDis->itemID;
            std::wstring title = (tabIndex < g_tabs.size()) ? g_tabs[tabIndex].title : L"New Tab";
            bool isSelected = (tabIndex == TabCtrl_GetCurSel(g_hTabCtrl));
            HBRUSH hBrush = CreateSolidBrush(isSelected ? GetSysColor(COLOR_WINDOW) : GetSysColor(COLOR_3DFACE));
            FillRect(pDis->hDC, &pDis->rcItem, hBrush);
            DeleteObject(hBrush);
            SetBkMode(pDis->hDC, TRANSPARENT);
            RECT rcClose = pDis->rcItem;
            rcClose.left = rcClose.right - 20;
            DrawText(pDis->hDC, L"×", -1, &rcClose, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            RECT rcTitle = pDis->rcItem;
            rcTitle.right -= 20;
            rcTitle.left += 5;
            DrawText(pDis->hDC, title.c_str(), -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            return TRUE;
        }
        break;
    }
                    /* 沉浸模式下返回 HTTRANSPARENT 实现鼠标穿透 */
    case WM_NCHITTEST: {
        if (g_isImmersionMode) return HTTRANSPARENT;
        LRESULT hit = DefWindowProc(hWnd, message, wParam, lParam);
        if (hit == HTCLIENT) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);
            if (pt.y < TOTAL_TOP_HEIGHT) return HTCAPTION;
            RECT rc; GetClientRect(hWnd, &rc);
            if (pt.x > rc.right - 10 && pt.y > rc.bottom - 10) return HTBOTTOMRIGHT;
        }
        return hit;
    }
    case WM_EXITSIZEMOVE: {
        if (g_isFullscreen) {
            HMONITOR hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfo(hMon, &mi);
            RECT rc; GetWindowRect(hWnd, &rc);
            if (rc.left != mi.rcWork.left || rc.top != mi.rcWork.top ||
                (rc.right - rc.left) != (mi.rcWork.right - mi.rcWork.left) ||
                (rc.bottom - rc.top) != (mi.rcWork.bottom - mi.rcWork.top)) {
                g_isFullscreen = false;
                SetWindowText(g_hBtnFullscreen, L"□");
            }
        }
        break;
    }
                        /* 窗口边缘吸附：按住Ctrl可临时脱离 */
    case WM_MOVING: {
        if (g_snapThreshold <= 0 || (GetAsyncKeyState(VK_CONTROL) & 0x8000)) break;
        LPRECT pRect = (LPRECT)lParam;
        HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfo(hMonitor, &mi)) {
            RECT rcScreen = mi.rcMonitor;
            int w = pRect->right - pRect->left;
            int h = pRect->bottom - pRect->top;
            if (abs(pRect->left - rcScreen.left) <= g_snapThreshold) { pRect->left = rcScreen.left; pRect->right = pRect->left + w; }
            else if (abs(pRect->right - rcScreen.right) <= g_snapThreshold) { pRect->right = rcScreen.right; pRect->left = pRect->right - w; }
            if (abs(pRect->top - rcScreen.top) <= g_snapThreshold) { pRect->top = rcScreen.top; pRect->bottom = pRect->top + h; }
            else if (abs(pRect->bottom - rcScreen.bottom) <= g_snapThreshold) { pRect->bottom = rcScreen.bottom; pRect->top = pRect->bottom - h; }
        }
        return TRUE;
    }
    case WM_NOTIFY: {
        HandleTabClick(lParam);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_GO_BUTTON) {
            int len = GetWindowTextLength(g_hEditUrl);
            if (len > 0) {
                std::vector<wchar_t> buf(len + 1);
                GetWindowText(g_hEditUrl, &buf[0], len + 1);
                std::wstring url = NormalizeInputUrl(&buf[0]);
                if (g_webview) g_webview->Navigate(url.c_str());
            }
            SetFocus(hWnd);
        }
        else if (LOWORD(wParam) == IDC_SET_BUTTON) OpenSettingsWindow(hWnd);
        else if (LOWORD(wParam) == IDC_ADD_TAB) CreateNewTab(g_homeUrl.c_str(), L"New Tab", true);
        else if (LOWORD(wParam) == IDC_CLOSE_TAB) ShowWindow(hWnd, SW_MINIMIZE);
        else if (LOWORD(wParam) == IDC_BTN_CLOSE_APP) SendMessage(hWnd, WM_CLOSE, 0, 0);
        else if (LOWORD(wParam) == IDC_BTN_FULLSCREEN) ToggleFullscreen();
        else if (LOWORD(wParam) == IDC_BTN_STAR) ToggleCurrentBookmark();
        else if (LOWORD(wParam) == IDC_BTN_BMLIST) ShowBookmarkList(hWnd);
        else if (LOWORD(wParam) == IDC_BTN_PRESET) ShowPresetList(hWnd);
        break;
    case WM_WINDOWPOSCHANGED: ResizeLayout(hWnd); break;
        /* 16ms定时器：输入检测禁用热键 + 沉浸模式挖孔刷新 + 拖拽按钮显隐 */
    case WM_TIMER:
        if (wParam == 1) {
            bool currentlyTyping = g_disableHkOnTyping && (IsUserTyping() || (GetFocus() == g_hEditUrl));
            if (currentlyTyping && !g_areHotkeysDisabled) {
                UnregisterAllHotkeys(hWnd);
                g_areHotkeysDisabled = true;
            }
            else if (!currentlyTyping && g_areHotkeysDisabled) {
                UpdateHotkeysState(hWnd);
                g_areHotkeysDisabled = false;
            }
            if (g_isImmersionMode) {
                UpdateImmersionHole();
                SetControlVisible(g_hBtnMove, false);
                SetControlVisible(g_hTabCtrl, false);
                SetControlVisible(g_hBtnAddTab, false);
                SetControlVisible(g_hBtnCloseTab, false);
                SetControlVisible(g_hBtnCloseApp, false);
                SetControlVisible(g_hBtnStar, false);
                SetControlVisible(g_hBtnBmList, false);
                SetControlVisible(g_hBtnPreset, false);
                SetControlVisible(g_hBtnFullscreen, false);
            }
            else {
                POINT pt; GetCursorPos(&pt); ScreenToClient(hWnd, &pt);
                RECT rc; GetClientRect(hWnd, &rc);
                bool inZone = (pt.x > (rc.right - rc.left) / 2 - 50 && pt.x < (rc.right - rc.left) / 2 + 50 && pt.y >= 0 && pt.y < 20);
                SetControlVisible(g_hBtnMove, inZone);
                SetControlVisible(g_hTabCtrl, true);
                SetControlVisible(g_hBtnAddTab, true);
                SetControlVisible(g_hBtnCloseTab, true);
                SetControlVisible(g_hBtnCloseApp, true);
                SetControlVisible(g_hBtnStar, true);
                SetControlVisible(g_hBtnBmList, true);
                SetControlVisible(g_hBtnPreset, true);
                SetControlVisible(g_hBtnFullscreen, true);
            }
        }
        break;
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, 1, g_isWindowHidden ? L"显示窗口" : L"隐藏窗口");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, 2, L"退出");
            SetForegroundWindow(g_hWnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, g_hWnd, NULL);
            DestroyMenu(hMenu);
            if (cmd == 1) SendMessage(g_hWnd, WM_HOTKEY, g_hkHideWin.id, 0);
            else if (cmd == 2) {
                if (g_isWindowHidden) { g_isWindowHidden = false; ShowWindow(g_hWnd, SW_SHOW); }
                SendMessage(g_hWnd, WM_CLOSE, 0, 0);
            }
        }
        else if (lParam == WM_LBUTTONDBLCLK) {
            SendMessage(g_hWnd, WM_HOTKEY, g_hkHideWin.id, 0);
        }
        break;
        /* 全局热键处理：老板键隐藏时根据 g_autoPauseOnHide 决定是否执行 JS 暂停视频 */
    case WM_HOTKEY: {
        if (IsUserTyping() || (GetFocus() == g_hEditUrl)) break;
        if (wParam == g_hkHideWin.id) {
            g_isWindowHidden = !g_isWindowHidden;
            if (g_controller) {
                g_controller->put_IsVisible(!g_isWindowHidden);
            }
            ShowWindow(hWnd, g_isWindowHidden ? SW_HIDE : SW_SHOW);
            if (g_isWindowHidden && g_autoPauseOnHide) {
                ExecuteScript(L"document.querySelector('video').pause();");
            }
            UpdateHotkeysState(hWnd);
            break;
        }
        /* 沉浸模式切换：进入时隐藏顶栏+添加WS_EX_LAYERED|WS_EX_TRANSPARENT，退出时还原 */
        if (wParam == g_hkImmersion.id) {
            g_isImmersionMode = !g_isImmersionMode;
            UpdateHotkeysState(hWnd);
            RECT rc; GetWindowRect(hWnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            LONG_PTR exStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);
            if (g_isImmersionMode) {
                g_wasMouseOverImmersion = false;
                ShowWindow(g_hEditUrl, SW_HIDE); ShowWindow(g_hBtnGo, SW_HIDE); ShowWindow(g_hBtnSet, SW_HIDE); ShowWindow(g_hBtnMove, SW_HIDE);
                ShowWindow(g_hTabCtrl, SW_HIDE); ShowWindow(g_hBtnAddTab, SW_HIDE); ShowWindow(g_hBtnCloseTab, SW_HIDE);
                ShowWindow(g_hBtnCloseApp, SW_HIDE);
                ShowWindow(g_hBtnStar, SW_HIDE);
                ShowWindow(g_hBtnBmList, SW_HIDE);
                ShowWindow(g_hBtnPreset, SW_HIDE);
                ShowWindow(g_hBtnFullscreen, SW_HIDE);
                SetWindowLongPtr(hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
                SetWindowLongPtr(hWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT);
                SetLayeredWindowAttributes(hWnd, 0, 255, LWA_ALPHA);
                int newY = 0;
                if (abs(rc.top) < g_snapThreshold) {
                    newY = 0;
                }
                else {
                    newY = rc.top + TOTAL_TOP_HEIGHT;
                }
                SetWindowPos(hWnd, NULL, rc.left, newY, w, h - TOTAL_TOP_HEIGHT, SWP_NOZORDER | SWP_FRAMECHANGED);
            }
            else {
                SetWindowRgn(hWnd, NULL, TRUE);
                SetWindowLongPtr(hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_THICKFRAME);
                SetWindowLongPtr(hWnd, GWL_EXSTYLE, exStyle & ~(WS_EX_LAYERED | WS_EX_TRANSPARENT));
                int newY = rc.top;
                if (rc.top > 0) {
                    newY = rc.top - TOTAL_TOP_HEIGHT;
                }
                if (newY < 0) newY = 0;
                SetWindowPos(hWnd, NULL, rc.left, newY, w, h + TOTAL_TOP_HEIGHT, SWP_NOZORDER | SWP_FRAMECHANGED);
                ShowWindow(g_hEditUrl, SW_SHOW); ShowWindow(g_hBtnGo, SW_SHOW); ShowWindow(g_hBtnSet, SW_SHOW);
                ShowWindow(g_hTabCtrl, SW_SHOW); ShowWindow(g_hBtnAddTab, SW_SHOW); ShowWindow(g_hBtnCloseTab, SW_SHOW);
                ShowWindow(g_hBtnCloseApp, SW_SHOW);
                ShowWindow(g_hBtnStar, SW_SHOW);
                ShowWindow(g_hBtnBmList, SW_SHOW);
                ShowWindow(g_hBtnPreset, SW_SHOW);
                ShowWindow(g_hBtnFullscreen, SW_SHOW);
            }
            ResizeLayout(hWnd);
        }
        else if (wParam == g_hkPlayPause.id) ExecuteScript(L"document.querySelector('video').paused ? document.querySelector('video').play() : document.querySelector('video').pause();");
        else if (wParam == g_hkBack.id) ExecuteScript(L"document.querySelector('video').currentTime -= 5;");
        else if (wParam == g_hkForward.id) ExecuteScript(L"document.querySelector('video').currentTime += 5;");
        else if (wParam == g_hkPrevEp.id) ExecuteScript(
            L"var b = document.querySelector('.bpx-player-ctrl-prev');"
            L"if(b) b.click();"
            L"else { var e = new KeyboardEvent('keydown', {key:'ArrowUp', keyCode:38, code:'ArrowUp', bubbles:true}); document.dispatchEvent(e); }");
        else if (wParam == g_hkNextEp.id) ExecuteScript(
            L"var b = document.querySelector('.bpx-player-ctrl-next');"
            L"if(b) b.click();"
            L"else { var e = new KeyboardEvent('keydown', {key:'ArrowDown', keyCode:40, code:'ArrowDown', bubbles:true}); document.dispatchEvent(e); }");
        break;
    }
    case WM_APP + 2:
        UpdateHotkeysState(hWnd);
        SaveConfig();
        break;
    case WM_DESTROY:
        if (g_hMouseHook) { UnhookWindowsHookEx(g_hMouseHook); g_hMouseHook = NULL; }
        RemoveTrayIcon();
        SaveConfig();
        UnregisterAllHotkeys(hWnd);
        KillTimer(hWnd, 1);
        SetWindowLongPtr(g_hBtnMove, GWLP_WNDPROC, (LONG_PTR)g_OldBtnProc);
        if (g_hListFont) { DeleteObject(g_hListFont); g_hListFont = NULL; }
        PostQuitMessage(0);
        break;
    default: return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
/* 程序入口：提权 → 单实例检测 → 加载配置 → 创建置顶无边框窗口 → 消息循环 */
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CheckAndRestartAsAdmin(hInstance);
    /* 单实例互斥锁：若已有实例运行则激活其窗口并退出 */
    HANDLE hMutex = CreateMutex(NULL, TRUE, _T("XiaoChuangBrowser_SingleInstance"));
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExist = FindWindow(_T("XiaoChuangClass"), NULL);
        if (hExist) {
            ShowWindow(hExist, SW_SHOW);
            SetForegroundWindow(hExist);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_hInst = hInstance;
    InitConfigPath(); LoadConfig();
    g_hListFont = CreateFont(26, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    WNDCLASSEX wcex = { sizeof(WNDCLASSEX) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = _T("XiaoChuangClass");
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassEx(&wcex);
    DWORD exStyle = WS_EX_TOPMOST;
    if (g_useSystemTray) exStyle |= WS_EX_TOOLWINDOW;
    g_hWnd = CreateWindowEx(exStyle, _T("XiaoChuangClass"), _T("小窗"), WS_POPUP | WS_VISIBLE | WS_THICKFRAME,
        g_winX, g_winY, g_winW, g_winH, nullptr, nullptr, hInstance, nullptr);
    if (!g_hWnd) {
        ShowErrorBox(NULL, L"启动失败", L"无法创建应用程序窗口。");
        return FALSE;
    }
    if (g_useSystemTray) AddTrayIcon();
    if (g_isFullscreen) SetWindowText(g_hBtnFullscreen, L"❐");
    g_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, g_hInst, 0);
    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    CoUninitialize();
    return (int)msg.wParam;
}
