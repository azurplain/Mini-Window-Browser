// ===========================================================================
// 项目名称：小窗 (LiteBrowser) - v1.1 Dev (BugFix)
// 作者：Gemini & User
// 更新日志：
//   - [修复] 修复老板键隐藏窗口时，WebView2 画面残留/位置不同步的 Bug (Ghost Window)。
//   - [优化] 显式控制 WebView2 的 IsVisible 属性，确保隐藏彻底。
// ===========================================================================

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
#include <commctrl.h> 

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

// =============================================================
//  数据结构与常量
// =============================================================

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

const int DRAG_BAR_HEIGHT = 12;
const int TAB_BAR_HEIGHT = 28;
const int NAV_BAR_HEIGHT = 30;
const int TOTAL_TOP_HEIGHT = DRAG_BAR_HEIGHT + TAB_BAR_HEIGHT + NAV_BAR_HEIGHT;
const int TAB_WIDTH = 140;

// =============================================================
//  全局变量
// =============================================================

HINSTANCE g_hInst;
HWND g_hWnd;
HWND g_hEditUrl;
HWND g_hBtnGo;
HWND g_hBtnSet;
HWND g_hBtnMove;
HWND g_hTabCtrl;
HWND g_hBtnAddTab;
HWND g_hBtnCloseTab;
HWND g_hChkAutoPause;
HWND g_hEditHome;

WNDPROC g_OldBtnProc;

wil::com_ptr<ICoreWebView2Controller> g_controller;
wil::com_ptr<ICoreWebView2> g_webview;

// --- 状态标志 ---
bool g_isImmersionMode = false;
bool g_isWindowHidden = false;
bool g_areHotkeysDisabled = false;
bool g_autoPauseOnHide = true;

// --- 配置参数 ---
int g_holeRadius = 400;
int g_snapThreshold = 20;
std::wstring g_homeUrl = L"https://www.bilibili.com";

// --- 标签页状态 ---
std::vector<TabData> g_tabs;
int g_currentTabIndex = 0;
bool g_isNavigatingFromTabSwitch = false;

// --- 窗口位置记忆 ---
int g_winX = 100; int g_winY = 100; int g_winW = 1000; int g_winH = 600;

TCHAR g_iniPath[MAX_PATH] = { 0 };

// --- 控件 ID ---
#define IDC_ADDRESS_BAR 9001
#define IDC_GO_BUTTON   9002
#define IDC_SET_BUTTON  9003
#define IDC_MOVE_BUTTON 9004
#define IDC_TAB_CTRL    9005
#define IDC_ADD_TAB     9006
#define IDC_CLOSE_TAB   9007

#define IDC_SET_RADIUS    9101
#define IDC_SET_SNAP      9102
#define IDC_CHK_AUTOPAUSE 9103
#define IDC_EDIT_HOME     9104 
#define IDC_BTN_HK1       9111
#define IDC_BTN_HK2       9112
#define IDC_BTN_HK3       9113
#define IDC_BTN_HK4       9114
#define IDC_BTN_HK5       9115
#define IDC_BTN_HK6       9116
#define IDC_BTN_HK7       9117

// --- 默认热键 ---
AppHotkey g_hkImmersion = { 101, 0, '0', L"沉浸模式" };
AppHotkey g_hkPlayPause = { 102, 0, VK_OEM_3, L"暂停/播放" };
AppHotkey g_hkBack = { 103, 0, '5', L"后退" };
AppHotkey g_hkForward = { 104, 0, '6', L"快进" };
AppHotkey g_hkPrevEp = { 105, 0, '7', L"上一集" };
AppHotkey g_hkNextEp = { 106, 0, '8', L"下一集" };
AppHotkey g_hkHideWin = { 107, 0, '9', L"隐藏/显示" };

// =============================================================
//  前向声明
// =============================================================
void OpenSettingsWindow(HWND hParent);
void CheckAndRestartAsAdmin(HINSTANCE hInstance);
void InitConfigPath();
std::wstring GetHotkeyString(UINT mod, UINT vk);
void SaveConfig();
void LoadConfig();
void UpdateHotkeysState(HWND hWnd);
void UnregisterAllHotkeys(HWND hWnd);
void ExecuteScript(const std::wstring& script);
bool IsUserTyping();
void UpdateImmersionHole();
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
LRESULT CALLBACK SettingsWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// =============================================================
//  核心逻辑实现
// =============================================================

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
        ShowErrorBox(NULL, L"权限警告", L"未能获取管理员权限！\n\n为了在全屏游戏中正常使用热键，\n建议右键选择“以管理员身份运行”。");
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
    default: if (GetKeyNameText(scanCode << 16, keyName, 32) == 0) wsprintf(keyName, L"%c", vk);
    }
    str += keyName;
    return str;
}

void SaveConfig() {
    TCHAR buffer[32];
    _itot_s(g_holeRadius, buffer, 10);
    WritePrivateProfileString(_T("Settings"), _T("HoleRadius"), buffer, g_iniPath);
    _itot_s(g_snapThreshold, buffer, 10);
    WritePrivateProfileString(_T("Settings"), _T("SnapThreshold"), buffer, g_iniPath);
    _itot_s(g_autoPauseOnHide ? 1 : 0, buffer, 10);
    WritePrivateProfileString(_T("Settings"), _T("AutoPause"), buffer, g_iniPath);
    WritePrivateProfileString(_T("Settings"), _T("HomeUrl"), g_homeUrl.c_str(), g_iniPath);

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

    if (g_hWnd && !IsIconic(g_hWnd) && !g_isWindowHidden) {
        RECT rc; GetWindowRect(g_hWnd, &rc);
        if (g_isImmersionMode) {
            if (rc.top >= TOTAL_TOP_HEIGHT) rc.top -= TOTAL_TOP_HEIGHT;
            else rc.top = 0;
            rc.bottom += TOTAL_TOP_HEIGHT;
        }
        _itot_s(rc.left, buffer, 10); WritePrivateProfileString(_T("Session"), _T("WinX"), buffer, g_iniPath);
        _itot_s(rc.top, buffer, 10); WritePrivateProfileString(_T("Session"), _T("WinY"), buffer, g_iniPath);
        _itot_s(rc.right - rc.left, buffer, 10); WritePrivateProfileString(_T("Session"), _T("WinW"), buffer, g_iniPath);
        _itot_s(rc.bottom - rc.top, buffer, 10); WritePrivateProfileString(_T("Session"), _T("WinH"), buffer, g_iniPath);
    }

    auto SaveKey = [](LPCWSTR key, AppHotkey& hk) {
        WCHAR buf[32]; wsprintf(buf, L"%d,%d", hk.fsModifiers, hk.vk);
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
    InitConfigPath();
    g_holeRadius = GetPrivateProfileInt(_T("Settings"), _T("HoleRadius"), 400, g_iniPath);
    g_snapThreshold = GetPrivateProfileInt(_T("Settings"), _T("SnapThreshold"), 20, g_iniPath);
    g_autoPauseOnHide = GetPrivateProfileInt(_T("Settings"), _T("AutoPause"), 1, g_iniPath) != 0;

    TCHAR homeBuf[2048];
    GetPrivateProfileString(_T("Settings"), _T("HomeUrl"), L"https://www.bilibili.com", homeBuf, 2048, g_iniPath);
    g_homeUrl = homeBuf;

    g_winX = GetPrivateProfileInt(_T("Session"), _T("WinX"), 100, g_iniPath);
    g_winY = GetPrivateProfileInt(_T("Session"), _T("WinY"), 100, g_iniPath);
    g_winW = GetPrivateProfileInt(_T("Session"), _T("WinW"), 1000, g_iniPath);
    g_winH = GetPrivateProfileInt(_T("Session"), _T("WinH"), 600, g_iniPath);

    auto LoadKey = [](LPCWSTR key, AppHotkey& hk, UINT defMod, UINT defVk) {
        WCHAR buf[32]; GetPrivateProfileString(_T("Hotkeys"), key, L"", buf, 32, g_iniPath);
        if (wcslen(buf) > 0) swscanf_s(buf, L"%d,%d", &hk.fsModifiers, &hk.vk);
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

void UnregisterAllHotkeys(HWND hWnd) {
    UnregisterHotKey(hWnd, g_hkImmersion.id);
    UnregisterHotKey(hWnd, g_hkHideWin.id);
    UnregisterHotKey(hWnd, g_hkPlayPause.id);
    UnregisterHotKey(hWnd, g_hkBack.id);
    UnregisterHotKey(hWnd, g_hkForward.id);
    UnregisterHotKey(hWnd, g_hkPrevEp.id);
    UnregisterHotKey(hWnd, g_hkNextEp.id);
}

void UpdateHotkeysState(HWND hWnd) {
    UnregisterAllHotkeys(hWnd);
    if (g_isWindowHidden) {
        RegisterHotKey(hWnd, g_hkHideWin.id, g_hkHideWin.fsModifiers, g_hkHideWin.vk);
        return;
    }
    RegisterHotKey(hWnd, g_hkImmersion.id, g_hkImmersion.fsModifiers, g_hkImmersion.vk);
    RegisterHotKey(hWnd, g_hkHideWin.id, g_hkHideWin.fsModifiers, g_hkHideWin.vk);
    RegisterHotKey(hWnd, g_hkPlayPause.id, g_hkPlayPause.fsModifiers, g_hkPlayPause.vk);
    RegisterHotKey(hWnd, g_hkBack.id, g_hkBack.fsModifiers, g_hkBack.vk);
    RegisterHotKey(hWnd, g_hkForward.id, g_hkForward.fsModifiers, g_hkForward.vk);
    RegisterHotKey(hWnd, g_hkPrevEp.id, g_hkPrevEp.fsModifiers, g_hkPrevEp.vk);
    RegisterHotKey(hWnd, g_hkNextEp.id, g_hkNextEp.fsModifiers, g_hkNextEp.vk);
}

void ExecuteScript(const std::wstring& script) {
    if (g_webview) g_webview->ExecuteScript(script.c_str(), nullptr);
}

bool IsUserTyping() {
    CURSORINFO ci = { 0 };
    ci.cbSize = sizeof(CURSORINFO);
    if (GetCursorInfo(&ci)) {
        static HCURSOR hCursorIBeam = LoadCursor(NULL, IDC_IBEAM);
        if (ci.flags == CURSOR_SHOWING && ci.hCursor == hCursorIBeam) return true;
    }
    return false;
}

LRESULT CALLBACK DragButtonProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_LBUTTONDOWN) {
        ReleaseCapture();
        SendMessage(GetParent(hWnd), WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    }
    return CallWindowProc(g_OldBtnProc, hWnd, uMsg, wParam, lParam);
}

// =============================================================
//  标签页管理逻辑
// =============================================================

std::wstring FormatTabTitle(const std::wstring& rawTitle) {
    std::wstring out = rawTitle;
    const size_t MAX_LEN = 10;
    if (out.length() > MAX_LEN) {
        out = out.substr(0, MAX_LEN) + L"..";
    }
    out += L"　×";
    return out;
}

void CreateNewTab(LPCWSTR url, LPCWSTR title, bool switchToNew) {
    TabData newTab;
    newTab.url = url;
    newTab.title = title;
    g_tabs.push_back(newTab);

    TCITEM tie;
    tie.mask = TCIF_TEXT;
    std::wstring displayTitle = FormatTabTitle(title);
    tie.pszText = (LPWSTR)displayTitle.c_str();
    TabCtrl_InsertItem(g_hTabCtrl, g_tabs.size() - 1, &tie);

    if (switchToNew) {
        SwitchToTab(g_tabs.size() - 1);
    }
}

void CloseTab(int index) {
    if (index < 0 || index >= g_tabs.size()) return;

    if (g_tabs.size() == 1) {
        g_tabs[0].url = g_homeUrl;
        g_tabs[0].title = L"New Tab";
        UpdateTabTitle(0, L"New Tab");
        SwitchToTab(0);
        return;
    }

    g_tabs.erase(g_tabs.begin() + index);
    TabCtrl_DeleteItem(g_hTabCtrl, index);

    if (g_currentTabIndex >= g_tabs.size()) {
        g_currentTabIndex = g_tabs.size() - 1;
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

// =============================================================
//  设置窗口逻辑
// =============================================================
AppHotkey* g_currentSettingKey = nullptr;

LRESULT CALLBACK SettingsWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static HWND hRadiusEdit, hSnapEdit, hBtnHk1, hBtnHk2, hBtnHk3, hBtnHk4, hBtnHk5, hBtnHk6, hBtnHk7;
    switch (message) {
    case WM_CREATE: {
        int y = 20;
        CreateWindow(L"STATIC", L"挖孔半径(px):", WS_CHILD | WS_VISIBLE, 20, y, 100, 20, hDlg, NULL, g_hInst, NULL);
        hRadiusEdit = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 130, y, 80, 20, hDlg, (HMENU)IDC_SET_RADIUS, g_hInst, NULL);
        SetWindowText(hRadiusEdit, std::to_wstring(g_holeRadius).c_str());
        y += 30;

        CreateWindow(L"STATIC", L"吸附距离(px):", WS_CHILD | WS_VISIBLE, 20, y, 100, 20, hDlg, NULL, g_hInst, NULL);
        hSnapEdit = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 130, y, 80, 20, hDlg, (HMENU)IDC_SET_SNAP, g_hInst, NULL);
        SetWindowText(hSnapEdit, std::to_wstring(g_snapThreshold).c_str());
        y += 35;

        g_hChkAutoPause = CreateWindow(L"BUTTON", L"老板键隐藏时自动暂停视频", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, y, 250, 20, hDlg, (HMENU)IDC_CHK_AUTOPAUSE, g_hInst, NULL);
        SendMessage(g_hChkAutoPause, BM_SETCHECK, g_autoPauseOnHide ? BST_CHECKED : BST_UNCHECKED, 0);
        y += 30;

        CreateWindow(L"STATIC", L"默认主页:", WS_CHILD | WS_VISIBLE, 20, y, 80, 20, hDlg, NULL, g_hInst, NULL);
        g_hEditHome = CreateWindow(L"EDIT", g_homeUrl.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 100, y, 200, 20, hDlg, (HMENU)IDC_EDIT_HOME, g_hInst, NULL);
        y += 35;

        auto CreateKeyBtn = [&](LPCWSTR title, int id) {
            CreateWindow(L"STATIC", title, WS_CHILD | WS_VISIBLE, 20, y, 120, 20, hDlg, NULL, g_hInst, NULL);
            HWND h = CreateWindow(L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 150, y, 150, 25, hDlg, (HMENU)id, g_hInst, NULL);
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

        CreateWindow(L"STATIC", L"提示: 打字(I型光标)时热键会自动失效。", WS_CHILD | WS_VISIBLE, 20, y, 300, 40, hDlg, NULL, g_hInst, NULL);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (HIWORD(wParam) == EN_CHANGE) {
            WCHAR buf[2048];
            if (id == IDC_SET_RADIUS) { GetWindowText(hRadiusEdit, buf, 10); g_holeRadius = _wtoi(buf); if (g_holeRadius < 10) g_holeRadius = 10; }
            else if (id == IDC_SET_SNAP) { GetWindowText(hSnapEdit, buf, 10); g_snapThreshold = _wtoi(buf); }
            else if (id == IDC_EDIT_HOME) {
                GetWindowText(g_hEditHome, buf, 2048);
                g_homeUrl = buf;
            }
        }
        else if (id == IDC_CHK_AUTOPAUSE) {
            g_autoPauseOnHide = (SendMessage(g_hChkAutoPause, BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        else if (id >= IDC_BTN_HK1 && id <= IDC_BTN_HK7) {
            if (id == IDC_BTN_HK1) g_currentSettingKey = &g_hkImmersion;
            else if (id == IDC_BTN_HK2) g_currentSettingKey = &g_hkPlayPause;
            else if (id == IDC_BTN_HK3) g_currentSettingKey = &g_hkBack;
            else if (id == IDC_BTN_HK4) g_currentSettingKey = &g_hkForward;
            else if (id == IDC_BTN_HK5) g_currentSettingKey = &g_hkPrevEp;
            else if (id == IDC_BTN_HK6) g_currentSettingKey = &g_hkNextEp;
            else if (id == IDC_BTN_HK7) g_currentSettingKey = &g_hkHideWin;
            SetWindowText((HWND)lParam, L"请按下按键...");
            SetFocus(hDlg);
        }
        break;
    }
    case WM_KEYDOWN: case WM_SYSKEYDOWN: {
        if (g_currentSettingKey) {
            UINT vk = (UINT)wParam;
            if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU) break;
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
    case WM_CLOSE: EnableWindow(g_hWnd, TRUE); DestroyWindow(hDlg); break;
    }
    return DefWindowProc(hDlg, message, wParam, lParam);
}

void OpenSettingsWindow(HWND hParent) {
    WNDCLASS wc = { 0 }; wc.lpfnWndProc = SettingsWndProc; wc.hInstance = g_hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.lpszClassName = L"XiaoChuangSettings";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); RegisterClass(&wc);
    HWND hSettings = CreateWindowEx(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"XiaoChuangSettings", L"小窗设置",
        WS_VISIBLE | WS_SYSMENU | WS_CAPTION, 200, 200, 350, 520, hParent, NULL, g_hInst, NULL);
    EnableWindow(hParent, FALSE);
}

// =============================================================
//  主窗口逻辑
// =============================================================

void UpdateImmersionHole() {
    if (!g_isImmersionMode || !g_hWnd) {
        SetWindowRgn(g_hWnd, NULL, TRUE);
        return;
    }
    POINT pt; GetCursorPos(&pt);
    POINT ptClient = pt; ScreenToClient(g_hWnd, &ptClient);
    RECT rcClient; GetClientRect(g_hWnd, &rcClient);
    HRGN hRgnFull = CreateRectRgn(0, 0, rcClient.right, rcClient.bottom);
    HRGN hRgnHole = CreateEllipticRgn(ptClient.x - g_holeRadius, ptClient.y - g_holeRadius, ptClient.x + g_holeRadius, ptClient.y + g_holeRadius);
    HRGN hRgnResult = CreateRectRgn(0, 0, 0, 0);
    CombineRgn(hRgnResult, hRgnFull, hRgnHole, RGN_DIFF);
    SetWindowRgn(g_hWnd, hRgnResult, TRUE);
    DeleteObject(hRgnFull); DeleteObject(hRgnHole);
}

void ResizeLayout(HWND hWnd) {
    RECT rc; GetClientRect(hWnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    if (!g_isImmersionMode) {
        int moveBtnW = 60;
        if (g_hBtnMove) MoveWindow(g_hBtnMove, (width - moveBtnW) / 2, 0, moveBtnW, DRAG_BAR_HEIGHT, TRUE);

        int tabBtnW = 25;
        if (g_hTabCtrl) MoveWindow(g_hTabCtrl, 0, DRAG_BAR_HEIGHT, width - tabBtnW * 2, TAB_BAR_HEIGHT, TRUE);
        if (g_hBtnAddTab) MoveWindow(g_hBtnAddTab, width - tabBtnW * 2, DRAG_BAR_HEIGHT, tabBtnW, TAB_BAR_HEIGHT, TRUE);
        if (g_hBtnCloseTab) MoveWindow(g_hBtnCloseTab, width - tabBtnW, DRAG_BAR_HEIGHT, tabBtnW, TAB_BAR_HEIGHT, TRUE);

        int btnWidth = 50; int setBtnWidth = 60; int padding = 5;
        int navY = DRAG_BAR_HEIGHT + TAB_BAR_HEIGHT;
        int navH = NAV_BAR_HEIGHT - 4;

        if (g_hEditUrl) MoveWindow(g_hEditUrl, padding, navY, width - btnWidth - setBtnWidth - padding * 4, navH, TRUE);
        if (g_hBtnGo) MoveWindow(g_hBtnGo, width - btnWidth - setBtnWidth - padding * 2, navY, btnWidth, navH, TRUE);
        if (g_hBtnSet) MoveWindow(g_hBtnSet, width - setBtnWidth - padding, navY, setBtnWidth, navH, TRUE);
    }

    if (g_controller) {
        RECT webViewBounds;
        if (g_isImmersionMode) {
            webViewBounds = { 0, 0, width, height };
        }
        else {
            // [修复] 铺满窗口底部，防止白边
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

                            g_webview->add_DocumentTitleChanged(
                                Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                                    [](ICoreWebView2* sender, IUnknown* args) -> HRESULT {
                                        wil::unique_cotaskmem_string title;
                                        sender->get_DocumentTitle(&title);
                                        UpdateTabTitle(g_currentTabIndex, title.get());
                                        return S_OK;
                                    }).Get(), nullptr);

                            g_webview->add_SourceChanged(
                                Callback<ICoreWebView2SourceChangedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs* args) -> HRESULT {
                                        wil::unique_cotaskmem_string uri; sender->get_Source(&uri);
                                        if (uri.get()) {
                                            SetWindowText(g_hEditUrl, uri.get());
                                            if (!g_isNavigatingFromTabSwitch && g_currentTabIndex >= 0 && g_currentTabIndex < g_tabs.size()) {
                                                g_tabs[g_currentTabIndex].url = uri.get();
                                            }
                                            g_isNavigatingFromTabSwitch = false;
                                        }
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

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        INITCOMMONCONTROLSEX icex;
        icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icex.dwICC = ICC_TAB_CLASSES;
        InitCommonControlsEx(&icex);

        g_hEditUrl = CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_ADDRESS_BAR, g_hInst, NULL);
        SendMessage(g_hEditUrl, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

        g_hBtnGo = CreateWindow(_T("BUTTON"), _T("Go"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hWnd, (HMENU)IDC_GO_BUTTON, g_hInst, NULL);
        SendMessage(g_hBtnGo, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

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

        SetTimer(hWnd, 1, 16, NULL);
        InitWebView2(hWnd);
        UpdateHotkeysState(hWnd);
        break;

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

    case WM_NCHITTEST: {
        if (g_isImmersionMode) return HTTRANSPARENT;

        LRESULT hit = DefWindowProc(hWnd, message, wParam, lParam);
        if (hit == HTCLIENT) {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) }; ScreenToClient(hWnd, &pt);
            if (pt.y < TOTAL_TOP_HEIGHT) return HTCAPTION;
            RECT rc; GetClientRect(hWnd, &rc);
            if (pt.x > rc.right - 10 && pt.y > rc.bottom - 10) return HTBOTTOMRIGHT;
        }
        return hit;
    }

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
                std::vector<wchar_t> buf(len + 1); GetWindowText(g_hEditUrl, &buf[0], len + 1);
                std::wstring url = &buf[0];
                if (url.find(L"://") == std::wstring::npos) url = L"https://" + url;
                if (g_webview) g_webview->Navigate(url.c_str());
            }
            SetFocus(hWnd);
        }
        else if (LOWORD(wParam) == IDC_SET_BUTTON) OpenSettingsWindow(hWnd);
        else if (LOWORD(wParam) == IDC_ADD_TAB) CreateNewTab(g_homeUrl.c_str(), L"New Tab", true);
        else if (LOWORD(wParam) == IDC_CLOSE_TAB) CloseCurrentTab();
        break;

    case WM_SIZE: ResizeLayout(hWnd); break;
    case WM_WINDOWPOSCHANGED: ResizeLayout(hWnd); break;

    case WM_TIMER:
        if (wParam == 1) {
            bool currentlyTyping = IsUserTyping() || (GetFocus() == g_hEditUrl);

            if (currentlyTyping && !g_areHotkeysDisabled) {
                UnregisterAllHotkeys(hWnd);
                g_areHotkeysDisabled = true;
            }
            else if (!currentlyTyping && g_areHotkeysDisabled) {
                UpdateHotkeysState(hWnd);
                g_areHotkeysDisabled = false;
            }

            if (g_isImmersionMode) UpdateImmersionHole();
            if (g_isImmersionMode) {
                if (IsWindowVisible(g_hBtnMove)) ShowWindow(g_hBtnMove, SW_HIDE);
                if (IsWindowVisible(g_hTabCtrl)) ShowWindow(g_hTabCtrl, SW_HIDE);
                if (IsWindowVisible(g_hBtnAddTab)) ShowWindow(g_hBtnAddTab, SW_HIDE);
                if (IsWindowVisible(g_hBtnCloseTab)) ShowWindow(g_hBtnCloseTab, SW_HIDE);
            }
            else {
                POINT pt; GetCursorPos(&pt); ScreenToClient(hWnd, &pt);
                RECT rc; GetClientRect(hWnd, &rc);
                bool inZone = (pt.x > (rc.right - rc.left) / 2 - 50 && pt.x < (rc.right - rc.left) / 2 + 50 && pt.y >= 0 && pt.y < 20);
                if (inZone && !IsWindowVisible(g_hBtnMove)) ShowWindow(g_hBtnMove, SW_SHOW);
                else if (!inZone && IsWindowVisible(g_hBtnMove)) ShowWindow(g_hBtnMove, SW_HIDE);

                if (!IsWindowVisible(g_hTabCtrl)) ShowWindow(g_hTabCtrl, SW_SHOW);
                if (!IsWindowVisible(g_hBtnAddTab)) ShowWindow(g_hBtnAddTab, SW_SHOW);
                if (!IsWindowVisible(g_hBtnCloseTab)) ShowWindow(g_hBtnCloseTab, SW_SHOW);
            }
        }
        break;

        // [核心修复] 老板键隐藏逻辑：显式控制 WebView 可见性，防止画面残留
    case WM_HOTKEY: {
        if (IsUserTyping() || (GetFocus() == g_hEditUrl)) break;

        if (wParam == g_hkHideWin.id) {
            g_isWindowHidden = !g_isWindowHidden;

            // 1. 显式开关渲染，解决Ghost Window问题
            if (g_controller) {
                g_controller->put_IsVisible(!g_isWindowHidden);
            }

            // 2. 隐藏/显示主窗口
            ShowWindow(hWnd, g_isWindowHidden ? SW_HIDE : SW_SHOW);

            // 3. 执行自动暂停
            if (g_isWindowHidden && g_autoPauseOnHide) {
                ExecuteScript(L"document.querySelector('video').pause();");
            }
            UpdateHotkeysState(hWnd);
            break;
        }

        if (wParam == g_hkImmersion.id) {
            g_isImmersionMode = !g_isImmersionMode;
            UpdateHotkeysState(hWnd);

            RECT rc; GetWindowRect(hWnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;

            LONG_PTR exStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);

            if (g_isImmersionMode) {
                ShowWindow(g_hEditUrl, SW_HIDE); ShowWindow(g_hBtnGo, SW_HIDE); ShowWindow(g_hBtnSet, SW_HIDE); ShowWindow(g_hBtnMove, SW_HIDE);
                ShowWindow(g_hTabCtrl, SW_HIDE); ShowWindow(g_hBtnAddTab, SW_HIDE); ShowWindow(g_hBtnCloseTab, SW_HIDE);

                SetWindowLong(hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
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
                SetWindowLong(hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_THICKFRAME);
                SetWindowLongPtr(hWnd, GWL_EXSTYLE, exStyle & ~(WS_EX_LAYERED | WS_EX_TRANSPARENT));

                int newY = rc.top;
                if (rc.top > 0) {
                    newY = rc.top - TOTAL_TOP_HEIGHT;
                }
                if (newY < 0) newY = 0;

                SetWindowPos(hWnd, NULL, rc.left, newY, w, h + TOTAL_TOP_HEIGHT, SWP_NOZORDER | SWP_FRAMECHANGED);
                ShowWindow(g_hEditUrl, SW_SHOW); ShowWindow(g_hBtnGo, SW_SHOW); ShowWindow(g_hBtnSet, SW_SHOW);
                ShowWindow(g_hTabCtrl, SW_SHOW); ShowWindow(g_hBtnAddTab, SW_SHOW); ShowWindow(g_hBtnCloseTab, SW_SHOW);
            }
            ResizeLayout(hWnd);
        }
        else if (wParam == g_hkPlayPause.id) ExecuteScript(L"document.querySelector('video').paused ? document.querySelector('video').play() : document.querySelector('video').pause();");
        else if (wParam == g_hkBack.id) ExecuteScript(L"document.querySelector('video').currentTime -= 5;");
        else if (wParam == g_hkForward.id) ExecuteScript(L"document.querySelector('video').currentTime += 5;");
        else if (wParam == g_hkPrevEp.id) ExecuteScript(L"var btn = document.querySelector('.bpx-player-ctrl-prev'); if(btn) btn.click();");
        else if (wParam == g_hkNextEp.id) ExecuteScript(L"var btn = document.querySelector('.bpx-player-ctrl-next'); if(btn) btn.click();");
        break;
    }

    case WM_DESTROY:
        SaveConfig();
        UnregisterAllHotkeys(hWnd);
        KillTimer(hWnd, 1);
        SetWindowLongPtr(g_hBtnMove, GWLP_WNDPROC, (LONG_PTR)g_OldBtnProc);
        PostQuitMessage(0);
        break;

    default: return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    CheckAndRestartAsAdmin(hInstance);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_hInst = hInstance;
    InitConfigPath(); LoadConfig();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEX wcex = { sizeof(WNDCLASSEX) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = _T("XiaoChuangClass");
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassEx(&wcex);

    g_hWnd = CreateWindowEx(WS_EX_TOPMOST, _T("XiaoChuangClass"), _T("小窗"), WS_POPUP | WS_VISIBLE | WS_THICKFRAME,
        g_winX, g_winY, g_winW, g_winH, nullptr, nullptr, hInstance, nullptr);

    if (!g_hWnd) {
        ShowErrorBox(NULL, L"启动失败", L"无法创建应用程序窗口。");
        return FALSE;
    }
    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    CoUninitialize();
    return (int)msg.wParam;
}