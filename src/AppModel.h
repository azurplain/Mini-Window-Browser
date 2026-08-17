#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace xiaochuang {

enum class WindowMode {
    Normal,
    Maximized,
    ImmersionHole,
    ImmersionAutoHide,
    Hidden,
};

enum class ImmersionStyle : int {
    Hole = 0,
    AutoHide = 1,
};

enum class RenderMode : int {
    AutomaticGpu = 0,
    SoftwareCompatibility = 1,
};

enum class ThemeMode : int {
    System = 0,
    Light = 1,
    Dark = 2,
};

enum class HotkeyAction : size_t {
    Immersion = 0,
    PlayPause,
    SeekBackward,
    SeekForward,
    Previous,
    Next,
    ToggleHidden,
    Count,
};

constexpr size_t kHotkeyCount = static_cast<size_t>(HotkeyAction::Count);

struct HotkeyBinding {
    int id = 0;
    UINT modifiers = 0;
    UINT virtualKey = 0;
    std::wstring displayName;
};

inline std::array<HotkeyBinding, kHotkeyCount> DefaultHotkeys() {
    return {{
        {101, 0, L'0', L"沉浸模式"},
        {102, 0, VK_OEM_3, L"播放/暂停"},
        {103, 0, L'5', L"快退"},
        {104, 0, L'6', L"快进"},
        {105, 0, L'7', L"上一集"},
        {106, 0, L'8', L"下一集"},
        {107, 0, L'9', L"隐藏/显示"},
    }};
}

struct AppSettings {
    int holeRadius = 400;
    int snapThreshold = 20;
    int holeOpacityPercent = 100;
    int autoHideOpacityPercent = 100;
    int holdPlaybackRate = 3;
    bool autoPauseOnHide = true;
    bool disableHotkeysOnTyping = true;
    bool useSystemTray = false;
    bool backgroundMediaHotkeys = false;
    bool autoFitVideoFullscreen = false;
    bool maximizedTopDragEnabled = false;
    ImmersionStyle immersionStyle = ImmersionStyle::Hole;
    RenderMode renderMode = RenderMode::AutomaticGpu;
    ThemeMode themeMode = ThemeMode::System;
    std::wstring homeUrl = L"https://www.bilibili.com";

    void Clamp() {
        holeRadius = std::clamp(holeRadius, 0, 4000);
        snapThreshold = std::clamp(snapThreshold, 0, 200);
        holeOpacityPercent = std::clamp(holeOpacityPercent, 0, 100);
        autoHideOpacityPercent = std::clamp(autoHideOpacityPercent, 0, 100);
        holdPlaybackRate = std::clamp(holdPlaybackRate, 2, 5);
        if (homeUrl.empty() || homeUrl == L"0") {
            homeUrl = L"https://www.bilibili.com";
        }
    }
};

struct TabData {
    std::wstring title = L"正在加载…";
    std::wstring url = L"https://www.bilibili.com";
};

struct Bookmark {
    std::wstring title;
    std::wstring url;
};

struct Preset {
    std::wstring name = L"预设";
    RECT normalRect{100, 100, 1100, 700};
    bool maximized = false;
    int holeRadius = 400;
    int snapThreshold = 20;
    int holeOpacityPercent = 100;
    int autoHideOpacityPercent = 100;
    int holdPlaybackRate = 3;
    bool autoPauseOnHide = true;
    bool disableHotkeysOnTyping = true;
    bool useSystemTray = false;
    bool backgroundMediaHotkeys = false;
    ImmersionStyle immersionStyle = ImmersionStyle::Hole;
    std::wstring homeUrl = L"https://www.bilibili.com";
    std::wstring currentUrl = L"https://www.bilibili.com";
    std::vector<TabData> tabs;
    int activeTab = 0;
    std::array<HotkeyBinding, kHotkeyCount> hotkeys = DefaultHotkeys();
};

struct SessionState {
    RECT normalRect{100, 100, 1100, 700};
    bool maximized = false;
    int activeTab = 0;
};

struct AppState {
    AppSettings settings;
    SessionState session;
    std::array<HotkeyBinding, kHotkeyCount> hotkeys = DefaultHotkeys();
    std::vector<TabData> tabs;
    std::vector<Bookmark> bookmarks;
    std::vector<Preset> presets;
    int currentTabIndex = 0;
    WindowMode windowMode = WindowMode::Normal;
    bool windowHidden = false;
    bool immersionActive = false;
};

inline size_t HotkeyIndex(HotkeyAction action) {
    return static_cast<size_t>(action);
}

inline bool IsMediaAction(HotkeyAction action) {
    return action == HotkeyAction::PlayPause ||
        action == HotkeyAction::SeekBackward ||
        action == HotkeyAction::SeekForward ||
        action == HotkeyAction::Previous ||
        action == HotkeyAction::Next;
}

} // namespace xiaochuang
