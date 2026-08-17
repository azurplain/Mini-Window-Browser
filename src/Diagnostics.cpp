#include "Diagnostics.h"

#include <windows.h>

#include <cstring>
#include <sstream>
#include <vector>

namespace xiaochuang {
namespace {

std::wstring HagsState() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LONG result = RegGetValueW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"HwSchMode",
        RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (result != ERROR_SUCCESS) return L"系统默认/未显式设置";
    if (value == 2) return L"已开启";
    if (value == 1) return L"已关闭";
    return L"系统默认（值=" + std::to_wstring(value) + L"）";
}

const wchar_t* WindowModeText(WindowMode mode) {
    switch (mode) {
    case WindowMode::Normal: return L"Normal";
    case WindowMode::Maximized: return L"Maximized";
    case WindowMode::ImmersionHole: return L"ImmersionHole";
    case WindowMode::ImmersionAutoHide: return L"ImmersionAutoHide";
    case WindowMode::Hidden: return L"Hidden";
    }
    return L"Unknown";
}

} // namespace

std::wstring BuildSystemDiagnostics(const AppState& state,
                                    const std::wstring& webViewVersion,
                                    WindowMode windowMode) {
    OSVERSIONINFOEXW version{sizeof(version)};
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
                GetProcAddress(ntdll, "RtlGetVersion"))) {
            rtlGetVersion(reinterpret_cast<OSVERSIONINFOW*>(&version));
        }
    }
    std::wostringstream output;
    output << L"Mini Window Browser 1.4.0\r\n"
           << L"Windows: " << version.dwMajorVersion << L'.' << version.dwMinorVersion
           << L" build " << version.dwBuildNumber << L"\r\n"
           << L"HAGS: " << HagsState() << L"\r\n"
           << L"WebView2 Runtime: " << (webViewVersion.empty() ? L"尚未初始化" : webViewVersion) << L"\r\n"
           << L"RenderMode: " << (state.settings.renderMode == RenderMode::SoftwareCompatibility
                ? L"Software compatibility (--disable-gpu)" : L"Automatic GPU") << L"\r\n"
           << L"WindowMode: " << WindowModeText(windowMode) << L"\r\n"
           << L"Hole mode: fully transparent cursor hole (fixed)\r\n"
           << L"Auto-hide opacity: " << state.settings.autoHideOpacityPercent << L"%\r\n"
           << L"Hold rate: " << state.settings.holdPlaybackRate << L"x\r\n"
           << L"Background media hotkeys: "
           << (state.settings.backgroundMediaHotkeys ? L"enabled" : L"disabled") << L"\r\n";
    return output.str();
}

std::wstring DecodeExecuteScriptString(const std::wstring& value) {
    if (value.size() < 2 || value.front() != L'"' || value.back() != L'"') return value;
    std::wstring decoded;
    decoded.reserve(value.size());
    for (size_t index = 1; index + 1 < value.size(); ++index) {
        wchar_t current = value[index];
        if (current != L'\\' || index + 1 >= value.size() - 1) {
            decoded.push_back(current);
            continue;
        }
        const wchar_t escaped = value[++index];
        switch (escaped) {
        case L'n': decoded.push_back(L'\n'); break;
        case L'r': decoded.push_back(L'\r'); break;
        case L't': decoded.push_back(L'\t'); break;
        case L'\\': decoded.push_back(L'\\'); break;
        case L'"': decoded.push_back(L'"'); break;
        case L'u': {
            if (index + 4 < value.size()) {
                wchar_t digits[5]{value[index + 1], value[index + 2], value[index + 3], value[index + 4], 0};
                decoded.push_back(static_cast<wchar_t>(wcstoul(digits, nullptr, 16)));
                index += 4;
            }
            break;
        }
        default: decoded.push_back(escaped); break;
        }
    }
    return decoded;
}

bool CopyTextToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        CloseClipboard();
        return false;
    }
    void* destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    memcpy(destination, text.c_str(), bytes);
    GlobalUnlock(memory);
    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

} // namespace xiaochuang
