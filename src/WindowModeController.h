#pragma once

#include "AppModel.h"

#include <functional>

namespace xiaochuang {

constexpr UINT_PTR kImmersionTimerId = 4402;

class WindowModeController {
public:
    using VisibilityCallback = std::function<void(bool)>;
    using LayoutCallback = std::function<void()>;

    void Initialize(HWND window, AppSettings* settings, RECT normalRect, bool maximized,
                    int chromeHeight, VisibilityCallback chromeVisibility,
                    LayoutCallback layoutCallback);
    void Shutdown();

    WindowMode Mode() const noexcept;
    bool IsSeamlessMaximized() const noexcept { return visibleMode_ == WindowMode::Maximized; }
    bool IsImmersive() const noexcept {
        return visibleMode_ == WindowMode::ImmersionHole ||
            visibleMode_ == WindowMode::ImmersionAutoHide;
    }
    bool IsHidden() const noexcept { return hidden_; }
    bool IsWebFullscreen() const noexcept { return webFullscreen_; }
    const RECT& NormalRect() const noexcept { return normalRect_; }

    void ToggleMaximized();
    void ToggleImmersion();
    void SetImmersion(bool enabled);
    void SetHidden(bool hidden);
    void SetSystemTray(bool enabled);
    void SetChromeHeight(int chromeHeight) noexcept { chromeHeight_ = chromeHeight; }
    void EnterWebFullscreen();
    void LeaveWebFullscreen();
    void FitWebFullscreenAspect(double aspectRatio);
    bool RestoreMaximizedForDrag(POINT cursor);
    void ApplyPresetGeometry(const RECT& normalRect, bool maximized);
    void UpdateNormalRectFromWindow();
    void OnImmersionTimer();
    void ReassertTopmost() const;
    void SnapMovingRect(RECT* movingRect, bool controlHeld) const;
    void RefreshVisualState();
    void RefreshFrameAppearance() const;

private:
    void ApplyVisibleMode(WindowMode mode, bool restoreGeometry);
    void ApplyStyle(DWORD style, DWORD exStyle) const;
    RECT CurrentMonitorRect(bool workArea) const;
    BYTE ConfiguredAlpha() const;
    void ClearRegionAndOpacity() const;
    RECT EnsureVisible(RECT rect) const;

    HWND window_ = nullptr;
    AppSettings* settings_ = nullptr;
    VisibilityCallback chromeVisibility_;
    LayoutCallback layoutCallback_;
    RECT normalRect_{100, 100, 1100, 700};
    RECT preImmersionRect_{};
    RECT preVideoFitRect_{};
    WindowMode visibleMode_ = WindowMode::Normal;
    WindowMode returnFromImmersion_ = WindowMode::Normal;
    int chromeHeight_ = 82;
    bool hidden_ = false;
    bool systemTray_ = false;
    bool webFullscreen_ = false;
    bool videoFitActive_ = false;
    POINT lastCursor_{LONG_MIN, LONG_MIN};
    bool hoverCandidate_ = false;
    bool hoverApplied_ = false;
    ULONGLONG hoverCandidateSince_ = 0;
};

} // namespace xiaochuang
