#include "CoreLogic.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace xiaochuang {

WindowStateSnapshot ReduceWindowState(WindowStateSnapshot state, WindowEvent event,
                                      ImmersionStyle immersionStyle) {
    switch (event) {
    case WindowEvent::Hide:
        state.hidden = true;
        break;
    case WindowEvent::Show:
        state.hidden = false;
        break;
    case WindowEvent::ToggleMaximize:
        if (state.visibleMode == WindowMode::Normal) state.visibleMode = WindowMode::Maximized;
        else if (state.visibleMode == WindowMode::Maximized) state.visibleMode = WindowMode::Normal;
        break;
    case WindowEvent::ToggleImmersion:
        if (state.visibleMode == WindowMode::ImmersionHole ||
            state.visibleMode == WindowMode::ImmersionAutoHide) {
            state.visibleMode = WindowMode::Normal;
        } else {
            state.visibleMode = immersionStyle == ImmersionStyle::Hole
                ? WindowMode::ImmersionHole : WindowMode::ImmersionAutoHide;
        }
        break;
    }
    return state;
}

void HoldGestureState::Press(std::uint64_t nowMilliseconds) {
    pressed_ = true;
    holding_ = false;
    pressedAt_ = nowMilliseconds;
}

HoldGestureResult HoldGestureState::Tick(std::uint64_t nowMilliseconds) {
    if (!pressed_ || holding_ || nowMilliseconds - pressedAt_ < 400) return HoldGestureResult::None;
    holding_ = true;
    return HoldGestureResult::HoldStart;
}

HoldGestureResult HoldGestureState::Release() {
    if (!pressed_) return HoldGestureResult::None;
    const HoldGestureResult result = holding_ ? HoldGestureResult::HoldStop : HoldGestureResult::Tap;
    Cancel();
    return result;
}

void HoldGestureState::Cancel() noexcept {
    pressed_ = false;
    holding_ = false;
    pressedAt_ = 0;
}

MediaSite SelectMediaSite(std::wstring host) {
    std::transform(host.begin(), host.end(), host.begin(),
        [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    const auto matches = [&](const wchar_t* domain) {
        const std::wstring value(domain);
        return host == value || (host.size() > value.size() &&
            host.ends_with(L"." + value));
    };
    if (matches(L"bilibili.com")) return MediaSite::Bilibili;
    if (matches(L"douyin.com")) return MediaSite::Douyin;
    if (matches(L"youtube.com") || matches(L"youtu.be")) return MediaSite::YouTube;
    return MediaSite::GenericHtml5;
}

std::optional<size_t> SelectActiveVideo(const std::vector<VideoCandidate>& candidates) {
    std::optional<size_t> selected;
    long double selectedScore = -1;
    for (size_t index = 0; index < candidates.size(); ++index) {
        const VideoCandidate& candidate = candidates[index];
        if (!candidate.visible || candidate.visibleArea <= 0) continue;
        const long double score = static_cast<long double>(candidate.visibleArea) +
            (candidate.playing ? 1.0e12L : 0.0L) +
            static_cast<long double>(candidate.lastInteraction) * 0.001L;
        if (!selected || score > selectedScore) {
            selected = index;
            selectedScore = score;
        }
    }
    return selected;
}

std::vector<MouseButtonTransition> DecodeRawMouseButtons(std::uint16_t buttonFlags) {
    std::vector<MouseButtonTransition> transitions;
    if ((buttonFlags & RI_MOUSE_BUTTON_3_DOWN) != 0) transitions.push_back({VK_MBUTTON, true});
    if ((buttonFlags & RI_MOUSE_BUTTON_3_UP) != 0) transitions.push_back({VK_MBUTTON, false});
    if ((buttonFlags & RI_MOUSE_BUTTON_4_DOWN) != 0) transitions.push_back({VK_XBUTTON1, true});
    if ((buttonFlags & RI_MOUSE_BUTTON_4_UP) != 0) transitions.push_back({VK_XBUTTON1, false});
    if ((buttonFlags & RI_MOUSE_BUTTON_5_DOWN) != 0) transitions.push_back({VK_XBUTTON2, true});
    if ((buttonFlags & RI_MOUSE_BUTTON_5_UP) != 0) transitions.push_back({VK_XBUTTON2, false});
    return transitions;
}

bool IsEditableClassName(std::wstring className) {
    std::transform(className.begin(), className.end(), className.begin(),
        [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    return className.find(L"edit") != std::wstring::npos ||
        className.find(L"richedit") != std::wstring::npos ||
        className.find(L"scintilla") != std::wstring::npos ||
        className.find(L"textbox") != std::wstring::npos;
}

bool IsLikelyAutomationTextInput(bool semanticTextControl, bool keyboardFocusable,
                                 bool writableValuePattern) {
    // Cross-process UI Automation providers used by Electron/Chromium launchers
    // sometimes expose their entire render surface as either an Edit control or
    // a writable Value provider.  Neither signal alone means the user is typing.
    // Requiring all three preserves input protection for real edit controls while
    // avoiding the focus-dependent hotkey shutdown on custom application canvases.
    return semanticTextControl && keyboardFocusable && writableValuePattern;
}

bool ShouldInspectTextInputProcess(bool foregroundIsApplication, bool externalObservationArmed) {
    return foregroundIsApplication || externalObservationArmed;
}

bool IsHoleMaskColumnTransparent(int coordinate, int transparencyPercent) {
    transparencyPercent = std::clamp(transparencyPercent, 0, 100);
    if (transparencyPercent == 0) return false;
    if (transparencyPercent == 100) return true;
    const int normalized = ((coordinate % 100) + 100) % 100;
    return (normalized * 37) % 100 < transparencyPercent;
}

std::wstring TrimWhitespace(std::wstring value) {
    const auto first = std::find_if_not(value.begin(), value.end(),
        [](wchar_t character) { return std::iswspace(character) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(),
        [](wchar_t character) { return std::iswspace(character) != 0; }).base();
    if (first >= last) return {};
    return std::wstring(first, last);
}

bool HasPresetNameConflict(const std::vector<std::wstring>& names,
                           const std::wstring& candidate, size_t ignoredIndex) {
    std::wstring normalized = TrimWhitespace(candidate);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
        [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    if (normalized.empty()) return true;
    for (size_t index = 0; index < names.size(); ++index) {
        if (index == ignoredIndex) continue;
        std::wstring existing = TrimWhitespace(names[index]);
        std::transform(existing.begin(), existing.end(), existing.begin(),
            [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
        if (existing == normalized) return true;
    }
    return false;
}

RECT CalculateRestoredDragRect(const RECT& maximizedRect, const RECT& normalRect,
                               POINT cursor, const RECT& workArea, int chromeHeight) {
    const int workWidth = std::max(1, static_cast<int>(workArea.right - workArea.left));
    const int workHeight = std::max(1, static_cast<int>(workArea.bottom - workArea.top));
    const int width = std::clamp(static_cast<int>(normalRect.right - normalRect.left), 1, workWidth);
    const int height = std::clamp(static_cast<int>(normalRect.bottom - normalRect.top), 1, workHeight);
    const int maximizedWidth = std::max(1, static_cast<int>(maximizedRect.right - maximizedRect.left));
    const double horizontalRatio = std::clamp(
        static_cast<double>(cursor.x - maximizedRect.left) / maximizedWidth, 0.0, 1.0);
    int left = cursor.x - static_cast<int>(std::lround(width * horizontalRatio));
    const int captionOffset = std::clamp(static_cast<int>(cursor.y - maximizedRect.top), 0,
        std::max(0, std::min(chromeHeight - 1, height - 1)));
    int top = cursor.y - captionOffset;
    left = std::clamp(left, static_cast<int>(workArea.left),
                      static_cast<int>(workArea.right) - width);
    top = std::clamp(top, static_cast<int>(workArea.top),
                     static_cast<int>(workArea.bottom) - height);
    return RECT{left, top, left + width, top + height};
}

SIZE CalculateAspectFitWindowSize(int preferredWidth, int chromeHeight, double aspectRatio,
                                  int maximumWidth, int maximumHeight) {
    maximumWidth = std::max(1, maximumWidth);
    maximumHeight = std::max(1, maximumHeight);
    if (!std::isfinite(aspectRatio) || aspectRatio <= 0.05 || aspectRatio >= 20.0) {
        return SIZE{std::clamp(preferredWidth, 1, maximumWidth), maximumHeight};
    }
    int width = std::clamp(preferredWidth, 1, maximumWidth);
    int height = chromeHeight + static_cast<int>(std::lround(width / aspectRatio));
    if (height > maximumHeight) {
        height = maximumHeight;
        width = std::min(maximumWidth, std::max(1,
            static_cast<int>(std::lround(std::max(1, height - chromeHeight) * aspectRatio))));
    }
    return SIZE{width, std::clamp(height, 1, maximumHeight)};
}

} // namespace xiaochuang
