#include "ConfigStore.h"

#include <shlwapi.h>

#include <cwchar>
#include <sstream>

#pragma comment(lib, "shlwapi.lib")

namespace xiaochuang {
namespace {

constexpr wchar_t kSettings[] = L"Settings";
constexpr wchar_t kSession[] = L"Session";
constexpr wchar_t kHotkeys[] = L"Hotkeys";
constexpr wchar_t kBookmarks[] = L"Bookmarks";
constexpr wchar_t kPresets[] = L"Presets";

std::wstring ReadString(const std::filesystem::path& path, const wchar_t* section,
                        const std::wstring& key, const std::wstring& fallback = L"") {
    std::vector<wchar_t> buffer(4096, L'\0');
    GetPrivateProfileStringW(section, key.c_str(), fallback.c_str(), buffer.data(),
                             static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

int ReadInt(const std::filesystem::path& path, const wchar_t* section,
            const std::wstring& key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(section, key.c_str(), fallback, path.c_str()));
}

void WriteString(const std::filesystem::path& path, const wchar_t* section,
                 const std::wstring& key, const std::wstring& value) {
    WritePrivateProfileStringW(section, key.c_str(), value.c_str(), path.c_str());
}

void WriteInt(const std::filesystem::path& path, const wchar_t* section,
              const std::wstring& key, int value) {
    WriteString(path, section, key, std::to_wstring(value));
}

void ReadHotkey(const std::filesystem::path& path, const wchar_t* section,
                const std::wstring& key, HotkeyBinding& binding) {
    const std::wstring encoded = ReadString(path, section, key);
    if (encoded.empty()) {
        return;
    }
    unsigned int modifiers = 0;
    unsigned int virtualKey = 0;
    unsigned int enabled = 1;
    const int fields = swscanf_s(encoded.c_str(), L"%u,%u,%u", &modifiers, &virtualKey, &enabled);
    if (fields >= 2) {
        constexpr UINT kAllowedModifiers = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;
        const bool validModifiers = (modifiers & ~kAllowedModifiers) == 0;
        const bool validVirtualKey = virtualKey > 0 && virtualKey < 256 &&
            virtualKey != VK_CONTROL && virtualKey != VK_SHIFT && virtualKey != VK_MENU &&
            virtualKey != VK_LWIN && virtualKey != VK_RWIN;
        if (validModifiers && validVirtualKey) {
            binding.modifiers = modifiers;
            binding.virtualKey = virtualKey;
            if (fields >= 3) binding.enabled = enabled != 0;
        }
    }
}

void WriteHotkey(const std::filesystem::path& path, const wchar_t* section,
                 const std::wstring& key, const HotkeyBinding& binding) {
    WriteString(path, section, key,
                std::to_wstring(binding.modifiers) + L"," +
                std::to_wstring(binding.virtualKey) + L"," +
                std::to_wstring(binding.enabled ? 1 : 0));
}

const std::array<const wchar_t*, kHotkeyCount> kHotkeyKeys = {
    L"Immersion", L"PlayPause", L"Back", L"Forward", L"PrevEp", L"NextEp", L"HideWin"
};

RECT MakeRect(int x, int y, int width, int height) {
    width = std::max(width, 160);
    height = std::max(height, 120);
    return RECT{x, y, x + width, y + height};
}

} // namespace

ConfigStore::ConfigStore(std::filesystem::path overridePath) {
    if (!overridePath.empty()) {
        path_ = std::move(overridePath);
        return;
    }
    std::vector<wchar_t> modulePath(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(),
                                            static_cast<DWORD>(modulePath.size()));
    std::filesystem::path executable(length > 0 ? modulePath.data() : L".");
    path_ = executable.parent_path() / L"config.ini";
}

bool ConfigStore::Load(AppState& state) const {
    state.settings.holeRadius = ReadInt(path_, kSettings, L"HoleRadius", 400);
    state.settings.snapThreshold = ReadInt(path_, kSettings, L"SnapThreshold", 20);
    state.settings.holeOpacityPercent = ReadInt(path_, kSettings, L"HoleOpacityPercent", 100);
    state.settings.autoHideOpacityPercent = ReadInt(path_, kSettings, L"AutoHideOpacityPercent", 100);
    state.settings.holdPlaybackRate = ReadInt(path_, kSettings, L"HoldPlaybackRate", 3);
    state.settings.autoPauseOnHide = ReadInt(path_, kSettings, L"AutoPause", 1) != 0;
    state.settings.disableHotkeysOnTyping = ReadInt(path_, kSettings, L"DisableHkOnTyping", 1) != 0;
    state.settings.useSystemTray = ReadInt(path_, kSettings, L"SystemTray", 0) != 0;
    state.settings.backgroundMediaHotkeys = ReadInt(path_, kSettings, L"BackgroundMediaHotkeys", 0) != 0;
    state.settings.autoFitVideoFullscreen = ReadInt(path_, kSettings, L"AutoFitVideoFullscreen", 0) != 0;
    state.settings.lockVideoFullscreenAspect = ReadInt(path_, kSettings, L"LockVideoFullscreenAspect", 0) != 0;
    state.settings.maximizedTopDragEnabled = ReadInt(path_, kSettings, L"MaximizedTopDragEnabled", 0) != 0;
    state.settings.immersionStyle = static_cast<ImmersionStyle>(
        std::clamp(ReadInt(path_, kSettings, L"ImmersionStyle", 0), 0, 1));
    const std::wstring renderMode = ReadString(path_, kSettings, L"RenderMode", L"Auto");
    state.settings.renderMode = _wcsicmp(renderMode.c_str(), L"Software") == 0
        ? RenderMode::SoftwareCompatibility : RenderMode::AutomaticGpu;
    const std::wstring theme = ReadString(path_, kSettings, L"Theme", L"System");
    if (_wcsicmp(theme.c_str(), L"Light") == 0) state.settings.themeMode = ThemeMode::Light;
    else if (_wcsicmp(theme.c_str(), L"Dark") == 0) state.settings.themeMode = ThemeMode::Dark;
    else state.settings.themeMode = ThemeMode::System;
    state.settings.homeUrl = ReadString(path_, kSettings, L"HomeUrl", L"https://www.bilibili.com");
    state.settings.Clamp();

    state.session.normalRect = MakeRect(
        ReadInt(path_, kSession, L"WinX", 100), ReadInt(path_, kSession, L"WinY", 100),
        ReadInt(path_, kSession, L"WinW", 1000), ReadInt(path_, kSession, L"WinH", 600));
    state.session.maximized = ReadInt(path_, kSession, L"Fullscreen", 0) != 0;
    state.session.activeTab = ReadInt(path_, kSession, L"ActiveTab", 0);

    const int tabCount = std::clamp(ReadInt(path_, kSession, L"TabCount", 0), 0, 100);
    state.tabs.clear();
    for (int index = 0; index < tabCount; ++index) {
        TabData tab;
        tab.url = ReadString(path_, kSession, L"TabUrl" + std::to_wstring(index), state.settings.homeUrl);
        tab.title = ReadString(path_, kSession, L"TabTitle" + std::to_wstring(index), L"正在加载…");
        if (tab.url.empty() || tab.url == L"0") tab.url = state.settings.homeUrl;
        state.tabs.push_back(std::move(tab));
    }
    if (state.tabs.empty()) {
        state.tabs.push_back({L"正在加载…", state.settings.homeUrl});
    }
    state.currentTabIndex = std::clamp(state.session.activeTab, 0, static_cast<int>(state.tabs.size()) - 1);

    for (size_t index = 0; index < kHotkeyCount; ++index) {
        ReadHotkey(path_, kHotkeys, kHotkeyKeys[index], state.hotkeys[index]);
    }

    state.bookmarks.clear();
    const int bookmarkCount = std::clamp(ReadInt(path_, kBookmarks, L"Count", 0), 0, 1000);
    for (int index = 0; index < bookmarkCount; ++index) {
        Bookmark bookmark;
        bookmark.title = ReadString(path_, kBookmarks, L"Title" + std::to_wstring(index), L"书签");
        bookmark.url = ReadString(path_, kBookmarks, L"Url" + std::to_wstring(index));
        if (!bookmark.url.empty()) state.bookmarks.push_back(std::move(bookmark));
    }

    state.presets.clear();
    const int presetCount = std::clamp(ReadInt(path_, kPresets, L"Count", 0), 0, 200);
    for (int index = 0; index < presetCount; ++index) {
        Preset preset;
        const std::wstring prefix = L"P" + std::to_wstring(index) + L"_";
        preset.name = ReadString(path_, kPresets, prefix + L"Name", L"预设");
        preset.normalRect = MakeRect(
            ReadInt(path_, kPresets, prefix + L"X", 100), ReadInt(path_, kPresets, prefix + L"Y", 100),
            ReadInt(path_, kPresets, prefix + L"W", 1000), ReadInt(path_, kPresets, prefix + L"H", 600));
        preset.maximized = ReadInt(path_, kPresets, prefix + L"Fullscreen", 0) != 0;
        preset.holeRadius = ReadInt(path_, kPresets, prefix + L"Rad", 400);
        preset.snapThreshold = ReadInt(path_, kPresets, prefix + L"Snap", 20);
        preset.holeOpacityPercent = ReadInt(path_, kPresets, prefix + L"HoleOpacity", 100);
        preset.autoHideOpacityPercent = ReadInt(path_, kPresets, prefix + L"AutoHideOpacity", 100);
        preset.holdPlaybackRate = ReadInt(path_, kPresets, prefix + L"HoldRate", 3);
        preset.autoPauseOnHide = ReadInt(path_, kPresets, prefix + L"Pause", 1) != 0;
        preset.disableHotkeysOnTyping = ReadInt(path_, kPresets, prefix + L"HkTyping", 1) != 0;
        preset.useSystemTray = ReadInt(path_, kPresets, prefix + L"SysTray", 0) != 0;
        preset.backgroundMediaHotkeys = ReadInt(path_, kPresets, prefix + L"BackgroundMedia", 0) != 0;
        preset.autoFitVideoFullscreen = ReadInt(path_, kPresets, prefix + L"AutoFitVideo", 0) != 0;
        preset.lockVideoFullscreenAspect = ReadInt(path_, kPresets, prefix + L"LockVideoAspect", 0) != 0;
        preset.maximizedTopDragEnabled = ReadInt(path_, kPresets, prefix + L"MaxTopDrag", 0) != 0;
        preset.immersionStyle = static_cast<ImmersionStyle>(
            std::clamp(ReadInt(path_, kPresets, prefix + L"ImmStyle", 0), 0, 1));
        preset.homeUrl = ReadString(path_, kPresets, prefix + L"HomeUrl", state.settings.homeUrl);
        preset.currentUrl = ReadString(path_, kPresets, prefix + L"CurUrl", preset.homeUrl);
        const int presetTabCount = std::clamp(ReadInt(path_, kPresets, prefix + L"TabCount", 0), 0, 100);
        for (int tabIndex = 0; tabIndex < presetTabCount; ++tabIndex) {
            const std::wstring tabPrefix = prefix + L"Tab" + std::to_wstring(tabIndex) + L"_";
            TabData tab;
            tab.url = ReadString(path_, kPresets, tabPrefix + L"Url");
            tab.title = ReadString(path_, kPresets, tabPrefix + L"Title", L"正在加载…");
            if (!tab.url.empty()) preset.tabs.push_back(std::move(tab));
        }
        if (preset.tabs.empty()) {
            preset.tabs.push_back({ReadString(path_, kPresets, prefix + L"CurTitle", L"正在加载…"),
                                   preset.currentUrl});
        }
        preset.activeTab = std::clamp(ReadInt(path_, kPresets, prefix + L"ActiveTab", 0), 0,
            static_cast<int>(preset.tabs.size()) - 1);
        preset.holeRadius = std::clamp(preset.holeRadius, 0, 4000);
        preset.snapThreshold = std::clamp(preset.snapThreshold, 0, 200);
        preset.holeOpacityPercent = std::clamp(preset.holeOpacityPercent, 0, 100);
        preset.autoHideOpacityPercent = std::clamp(preset.autoHideOpacityPercent, 0, 100);
        preset.holdPlaybackRate = std::clamp(preset.holdPlaybackRate, 2, 5);
        for (size_t hotkeyIndex = 0; hotkeyIndex < kHotkeyCount; ++hotkeyIndex) {
            preset.hotkeys[hotkeyIndex] = state.hotkeys[hotkeyIndex];
            const std::wstring legacySuffixes[kHotkeyCount] = {
                L"HkImm", L"HkPlay", L"HkBack", L"HkFwd", L"HkPrev", L"HkNext", L"HkHide"
            };
            ReadHotkey(path_, kPresets, prefix + legacySuffixes[hotkeyIndex], preset.hotkeys[hotkeyIndex]);
        }
        state.presets.push_back(std::move(preset));
    }
    return true;
}

bool ConfigStore::Save(const AppState& state, const RECT& normalRect, bool maximized) const {
    std::filesystem::path transactionPath = path_;
    transactionPath += L".tmp";
    DeleteFileW(transactionPath.c_str());
    if (GetFileAttributesW(path_.c_str()) != INVALID_FILE_ATTRIBUTES &&
        !CopyFileW(path_.c_str(), transactionPath.c_str(), FALSE)) {
        return false;
    }
    const AppSettings& settings = state.settings;
    WriteInt(transactionPath, kSettings, L"HoleRadius", settings.holeRadius);
    WriteInt(transactionPath, kSettings, L"SnapThreshold", settings.snapThreshold);
    WriteInt(transactionPath, kSettings, L"HoleOpacityPercent", settings.holeOpacityPercent);
    WriteInt(transactionPath, kSettings, L"AutoHideOpacityPercent", settings.autoHideOpacityPercent);
    WriteInt(transactionPath, kSettings, L"HoldPlaybackRate", settings.holdPlaybackRate);
    WriteInt(transactionPath, kSettings, L"AutoPause", settings.autoPauseOnHide ? 1 : 0);
    WriteInt(transactionPath, kSettings, L"DisableHkOnTyping", settings.disableHotkeysOnTyping ? 1 : 0);
    WriteInt(transactionPath, kSettings, L"SystemTray", settings.useSystemTray ? 1 : 0);
    WriteInt(transactionPath, kSettings, L"BackgroundMediaHotkeys", settings.backgroundMediaHotkeys ? 1 : 0);
    WriteInt(transactionPath, kSettings, L"AutoFitVideoFullscreen", settings.autoFitVideoFullscreen ? 1 : 0);
    WriteInt(transactionPath, kSettings, L"LockVideoFullscreenAspect", settings.lockVideoFullscreenAspect ? 1 : 0);
    WriteInt(transactionPath, kSettings, L"MaximizedTopDragEnabled", settings.maximizedTopDragEnabled ? 1 : 0);
    WriteInt(transactionPath, kSettings, L"ImmersionStyle", static_cast<int>(settings.immersionStyle));
    WriteString(transactionPath, kSettings, L"RenderMode",
                settings.renderMode == RenderMode::SoftwareCompatibility ? L"Software" : L"Auto");
    const wchar_t* theme = L"System";
    if (settings.themeMode == ThemeMode::Light) theme = L"Light";
    else if (settings.themeMode == ThemeMode::Dark) theme = L"Dark";
    WriteString(transactionPath, kSettings, L"Theme", theme);
    WriteString(transactionPath, kSettings, L"HomeUrl", settings.homeUrl);

    WritePrivateProfileSectionW(kSession, nullptr, transactionPath.c_str());
    WriteInt(transactionPath, kSession, L"WinX", normalRect.left);
    WriteInt(transactionPath, kSession, L"WinY", normalRect.top);
    WriteInt(transactionPath, kSession, L"WinW", normalRect.right - normalRect.left);
    WriteInt(transactionPath, kSession, L"WinH", normalRect.bottom - normalRect.top);
    WriteInt(transactionPath, kSession, L"Fullscreen", maximized ? 1 : 0);
    WriteInt(transactionPath, kSession, L"ActiveTab", state.currentTabIndex);
    WriteInt(transactionPath, kSession, L"TabCount", static_cast<int>(state.tabs.size()));
    for (size_t index = 0; index < state.tabs.size(); ++index) {
        WriteString(transactionPath, kSession, L"TabUrl" + std::to_wstring(index), state.tabs[index].url);
        WriteString(transactionPath, kSession, L"TabTitle" + std::to_wstring(index), state.tabs[index].title);
    }

    WritePrivateProfileSectionW(kHotkeys, nullptr, transactionPath.c_str());
    for (size_t index = 0; index < kHotkeyCount; ++index) {
        WriteHotkey(transactionPath, kHotkeys, kHotkeyKeys[index], state.hotkeys[index]);
    }

    WritePrivateProfileSectionW(kBookmarks, nullptr, transactionPath.c_str());
    WriteInt(transactionPath, kBookmarks, L"Count", static_cast<int>(state.bookmarks.size()));
    for (size_t index = 0; index < state.bookmarks.size(); ++index) {
        WriteString(transactionPath, kBookmarks, L"Title" + std::to_wstring(index), state.bookmarks[index].title);
        WriteString(transactionPath, kBookmarks, L"Url" + std::to_wstring(index), state.bookmarks[index].url);
    }

    WritePrivateProfileSectionW(kPresets, nullptr, transactionPath.c_str());
    WriteInt(transactionPath, kPresets, L"Count", static_cast<int>(state.presets.size()));
    const std::array<const wchar_t*, kHotkeyCount> suffixes = {
        L"HkImm", L"HkPlay", L"HkBack", L"HkFwd", L"HkPrev", L"HkNext", L"HkHide"
    };
    for (size_t index = 0; index < state.presets.size(); ++index) {
        const Preset& preset = state.presets[index];
        const std::wstring prefix = L"P" + std::to_wstring(index) + L"_";
        WriteString(transactionPath, kPresets, prefix + L"Name", preset.name);
        WriteString(transactionPath, kPresets, prefix + L"HomeUrl", preset.homeUrl);
        WriteString(transactionPath, kPresets, prefix + L"CurUrl", preset.currentUrl);
        const std::wstring currentTitle = !preset.tabs.empty()
            ? preset.tabs[static_cast<size_t>(std::clamp(
                preset.activeTab, 0, static_cast<int>(preset.tabs.size()) - 1))].title
            : L"正在加载…";
        WriteString(transactionPath, kPresets, prefix + L"CurTitle", currentTitle);
        WriteInt(transactionPath, kPresets, prefix + L"TabCount", static_cast<int>(preset.tabs.size()));
        WriteInt(transactionPath, kPresets, prefix + L"ActiveTab", preset.activeTab);
        for (size_t tabIndex = 0; tabIndex < preset.tabs.size(); ++tabIndex) {
            const std::wstring tabPrefix = prefix + L"Tab" + std::to_wstring(tabIndex) + L"_";
            WriteString(transactionPath, kPresets, tabPrefix + L"Url", preset.tabs[tabIndex].url);
            WriteString(transactionPath, kPresets, tabPrefix + L"Title", preset.tabs[tabIndex].title);
        }
        WriteInt(transactionPath, kPresets, prefix + L"X", preset.normalRect.left);
        WriteInt(transactionPath, kPresets, prefix + L"Y", preset.normalRect.top);
        WriteInt(transactionPath, kPresets, prefix + L"W", preset.normalRect.right - preset.normalRect.left);
        WriteInt(transactionPath, kPresets, prefix + L"H", preset.normalRect.bottom - preset.normalRect.top);
        WriteInt(transactionPath, kPresets, prefix + L"Fullscreen", preset.maximized ? 1 : 0);
        WriteInt(transactionPath, kPresets, prefix + L"Rad", preset.holeRadius);
        WriteInt(transactionPath, kPresets, prefix + L"Snap", preset.snapThreshold);
        WriteInt(transactionPath, kPresets, prefix + L"HoleOpacity", preset.holeOpacityPercent);
        WriteInt(transactionPath, kPresets, prefix + L"AutoHideOpacity", preset.autoHideOpacityPercent);
        WriteInt(transactionPath, kPresets, prefix + L"HoldRate", preset.holdPlaybackRate);
        WriteInt(transactionPath, kPresets, prefix + L"Pause", preset.autoPauseOnHide ? 1 : 0);
        WriteInt(transactionPath, kPresets, prefix + L"HkTyping", preset.disableHotkeysOnTyping ? 1 : 0);
        WriteInt(transactionPath, kPresets, prefix + L"SysTray", preset.useSystemTray ? 1 : 0);
        WriteInt(transactionPath, kPresets, prefix + L"BackgroundMedia", preset.backgroundMediaHotkeys ? 1 : 0);
        WriteInt(transactionPath, kPresets, prefix + L"AutoFitVideo", preset.autoFitVideoFullscreen ? 1 : 0);
        WriteInt(transactionPath, kPresets, prefix + L"LockVideoAspect", preset.lockVideoFullscreenAspect ? 1 : 0);
        WriteInt(transactionPath, kPresets, prefix + L"MaxTopDrag", preset.maximizedTopDragEnabled ? 1 : 0);
        WriteInt(transactionPath, kPresets, prefix + L"ImmStyle", static_cast<int>(preset.immersionStyle));
        for (size_t hotkeyIndex = 0; hotkeyIndex < kHotkeyCount; ++hotkeyIndex) {
            WriteHotkey(transactionPath, kPresets, prefix + suffixes[hotkeyIndex], preset.hotkeys[hotkeyIndex]);
        }
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, transactionPath.c_str());
    std::error_code error;
    if (!std::filesystem::exists(transactionPath, error) || error) return false;
    const DWORD replaceFlags = REPLACEFILE_WRITE_THROUGH;
    if (ReplaceFileW(path_.c_str(), transactionPath.c_str(), nullptr,
                     replaceFlags, nullptr, nullptr)) return true;
    if (MoveFileExW(transactionPath.c_str(), path_.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    DeleteFileW(transactionPath.c_str());
    return false;
}

std::wstring NormalizeInputUrl(const std::wstring& input) {
    if (input.empty()) return L"https://www.bilibili.com";
    if (input.find(L"://") != std::wstring::npos) return input;
    if (input.find(L' ') == std::wstring::npos && input.find(L'.') != std::wstring::npos) {
        return L"https://" + input;
    }
    std::wostringstream encoded;
    encoded << L"https://www.bing.com/search?q=";
    for (const wchar_t character : input) {
        if ((character >= L'A' && character <= L'Z') ||
            (character >= L'a' && character <= L'z') ||
            (character >= L'0' && character <= L'9') ||
            character == L'-' || character == L'_' || character == L'.' || character == L'~') {
            encoded << character;
            continue;
        }
        char utf8[8]{};
        const int length = WideCharToMultiByte(CP_UTF8, 0, &character, 1, utf8,
                                                static_cast<int>(std::size(utf8)), nullptr, nullptr);
        for (int index = 0; index < length; ++index) {
            wchar_t escaped[4]{};
            swprintf_s(escaped, L"%%%02X", static_cast<unsigned char>(utf8[index]));
            encoded << escaped;
        }
    }
    return encoded.str();
}

std::wstring NormalizeComparableUrl(std::wstring url) {
    while (url.size() > 1 && url.back() == L'/') url.pop_back();
    std::transform(url.begin(), url.end(), url.begin(), towlower);
    return url;
}

std::wstring HotkeyDisplayText(const HotkeyBinding& binding) {
    std::wstring text;
    if ((binding.modifiers & MOD_CONTROL) != 0) text += L"Ctrl + ";
    if ((binding.modifiers & MOD_ALT) != 0) text += L"Alt + ";
    if ((binding.modifiers & MOD_SHIFT) != 0) text += L"Shift + ";
    switch (binding.virtualKey) {
    case VK_LEFT: text += L"Left"; break;
    case VK_RIGHT: text += L"Right"; break;
    case VK_UP: text += L"Up"; break;
    case VK_DOWN: text += L"Down"; break;
    case VK_OEM_3: text += L"`"; break;
    case VK_MBUTTON: text += L"鼠标中键"; break;
    case VK_XBUTTON1: text += L"鼠标侧键1"; break;
    case VK_XBUTTON2: text += L"鼠标侧键2"; break;
    default: {
        wchar_t name[64]{};
        const UINT scanCode = MapVirtualKeyW(binding.virtualKey, MAPVK_VK_TO_VSC);
        if (GetKeyNameTextW(static_cast<LONG>(scanCode << 16U), name,
                            static_cast<int>(std::size(name))) > 0) {
            text += name;
        } else if (binding.virtualKey >= 32 && binding.virtualKey <= 126) {
            text.push_back(static_cast<wchar_t>(binding.virtualKey));
        } else {
            text += L"VK " + std::to_wstring(binding.virtualKey);
        }
        break;
    }
    }
    return text;
}

} // namespace xiaochuang
