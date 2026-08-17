#pragma once

#include "AppModel.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xiaochuang {

enum class WindowEvent {
    ToggleMaximize,
    ToggleImmersion,
    Hide,
    Show,
};

struct WindowStateSnapshot {
    WindowMode visibleMode = WindowMode::Normal;
    bool hidden = false;
};

WindowStateSnapshot ReduceWindowState(WindowStateSnapshot state, WindowEvent event,
                                      ImmersionStyle immersionStyle);

enum class HoldGestureResult {
    None,
    Tap,
    HoldStart,
    HoldStop,
};

class HoldGestureState {
public:
    void Press(std::uint64_t nowMilliseconds);
    HoldGestureResult Tick(std::uint64_t nowMilliseconds);
    HoldGestureResult Release();
    void Cancel() noexcept;

private:
    bool pressed_ = false;
    bool holding_ = false;
    std::uint64_t pressedAt_ = 0;
};

enum class MediaSite {
    Bilibili,
    Douyin,
    YouTube,
    GenericHtml5,
};

MediaSite SelectMediaSite(std::wstring host);

struct VideoCandidate {
    bool playing = false;
    bool visible = true;
    double visibleArea = 0;
    std::uint64_t lastInteraction = 0;
};

struct MouseButtonTransition {
    UINT virtualKey = 0;
    bool down = false;
};

std::optional<size_t> SelectActiveVideo(const std::vector<VideoCandidate>& candidates);
std::vector<MouseButtonTransition> DecodeRawMouseButtons(std::uint16_t buttonFlags);
bool IsEditableClassName(std::wstring className);
bool IsLikelyAutomationTextInput(bool semanticTextControl, bool keyboardFocusable,
                                 bool writableValuePattern);
bool ShouldInspectTextInputProcess(bool foregroundIsApplication, bool externalObservationArmed);
bool IsHoleMaskColumnTransparent(int coordinate, int transparencyPercent);
std::wstring TrimWhitespace(std::wstring value);
bool HasPresetNameConflict(const std::vector<std::wstring>& names,
                           const std::wstring& candidate, size_t ignoredIndex = SIZE_MAX);

RECT CalculateRestoredDragRect(const RECT& maximizedRect, const RECT& normalRect,
                               POINT cursor, const RECT& workArea, int chromeHeight);
SIZE CalculateAspectFitWindowSize(int preferredWidth, int chromeHeight, double aspectRatio,
                                  int maximumWidth, int maximumHeight);

} // namespace xiaochuang
