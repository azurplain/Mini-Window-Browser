#include "WindowModeController.h"

#include "CoreLogic.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#pragma comment(lib, "dwmapi.lib")

namespace xiaochuang {

namespace {
constexpr DWORD kNormalStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
constexpr DWORD kPopupStyle = WS_POPUP | WS_CLIPCHILDREN;
constexpr auto kDwmBorderColorAttribute = static_cast<DWMWINDOWATTRIBUTE>(34);
constexpr COLORREF kDwmColorNone = 0xFFFFFFFEU;
}

void WindowModeController::Initialize(HWND window, AppSettings* settings, RECT normalRect,
                                      bool maximized, int chromeHeight,
                                      VisibilityCallback chromeVisibility,
                                      LayoutCallback layoutCallback) {
    window_ = window;
    settings_ = settings;
    normalRect_ = EnsureVisible(normalRect);
    chromeHeight_ = chromeHeight;
    chromeVisibility_ = std::move(chromeVisibility);
    layoutCallback_ = std::move(layoutCallback);
    systemTray_ = settings ? settings->useSystemTray : false;
    visibleMode_ = maximized ? WindowMode::Maximized : WindowMode::Normal;
    ApplyVisibleMode(visibleMode_, true);
}

void WindowModeController::Shutdown() {
    if (window_) KillTimer(window_, kImmersionTimerId);
    window_ = nullptr;
    settings_ = nullptr;
    chromeVisibility_ = {};
    layoutCallback_ = {};
}

WindowMode WindowModeController::Mode() const noexcept {
    return hidden_ ? WindowMode::Hidden : visibleMode_;
}

void WindowModeController::ToggleMaximized() {
    if (!window_ || IsImmersive()) return;
    if (visibleMode_ == WindowMode::Maximized) {
        visibleMode_ = WindowMode::Normal;
        ApplyVisibleMode(visibleMode_, true);
    } else {
        UpdateNormalRectFromWindow();
        visibleMode_ = WindowMode::Maximized;
        ApplyVisibleMode(visibleMode_, true);
    }
}

void WindowModeController::ToggleImmersion() {
    SetImmersion(!IsImmersive());
}

void WindowModeController::SetImmersion(bool enabled) {
    if (!window_) return;
    // Web fullscreen is scoped to the WebView content area. Immersion only changes
    // the host window chrome/style, so keep the DOM fullscreen element alive.
    if (enabled == IsImmersive()) {
        if (enabled) RefreshVisualState();
        return;
    }
    if (enabled) {
        returnFromImmersion_ = visibleMode_;
        GetWindowRect(window_, &preImmersionRect_);
        visibleMode_ = settings_ && settings_->immersionStyle == ImmersionStyle::AutoHide
            ? WindowMode::ImmersionAutoHide : WindowMode::ImmersionHole;
        ApplyVisibleMode(visibleMode_, true);
    } else {
        visibleMode_ = returnFromImmersion_;
        if (visibleMode_ == WindowMode::Normal &&
            preImmersionRect_.right > preImmersionRect_.left) {
            normalRect_ = preImmersionRect_;
        }
        ApplyVisibleMode(visibleMode_, true);
    }
}

void WindowModeController::SetHidden(bool hidden) {
    if (!window_ || hidden_ == hidden) return;
    hidden_ = hidden;
    if (hidden_) {
        ShowWindow(window_, SW_HIDE);
    } else {
        ShowWindow(window_, IsIconic(window_) ? SW_RESTORE : SW_SHOWNOACTIVATE);
        ApplyVisibleMode(visibleMode_, false);
        ReassertTopmost();
    }
}

void WindowModeController::SetSystemTray(bool enabled) {
    systemTray_ = enabled;
    if (!window_ || IsImmersive()) return;
    ApplyVisibleMode(visibleMode_, false);
}

void WindowModeController::EnterWebFullscreen() {
    if (!window_ || webFullscreen_) return;
    webFullscreen_ = true;
}

void WindowModeController::LeaveWebFullscreen() {
    if (!window_ || !webFullscreen_) return;
    webFullscreen_ = false;
    if (!videoFitActive_) return;

    videoFitActive_ = false;
    normalRect_ = EnsureVisible(preVideoFitRect_);
    if (visibleMode_ == WindowMode::Normal && !hidden_) {
        ApplyVisibleMode(visibleMode_, true);
    }
}

void WindowModeController::FitWebFullscreenAspect(double aspectRatio) {
    if (!window_ || !webFullscreen_ || hidden_ || visibleMode_ != WindowMode::Normal ||
        !std::isfinite(aspectRatio) || aspectRatio <= 0.1 || aspectRatio >= 10.0) return;

    RECT current{};
    if (!GetWindowRect(window_, &current)) return;
    if (!videoFitActive_) {
        preVideoFitRect_ = current;
        videoFitActive_ = true;
    }

    const RECT work = CurrentMonitorRect(true);
    const SIZE target = CalculateAspectFitWindowSize(
        current.right - current.left, chromeHeight_, aspectRatio,
        work.right - work.left, work.bottom - work.top);
    RECT desired{
        current.left + ((current.right - current.left) - target.cx) / 2,
        current.top + ((current.bottom - current.top) - target.cy) / 2,
        0,
        0};
    desired.right = desired.left + target.cx;
    desired.bottom = desired.top + target.cy;
    desired = EnsureVisible(desired);
    SetWindowPos(window_, HWND_TOPMOST, desired.left, desired.top,
                 desired.right - desired.left, desired.bottom - desired.top,
                 SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    if (layoutCallback_) layoutCallback_();
}

bool WindowModeController::RestoreMaximizedForDrag(POINT cursor) {
    if (!window_ || hidden_ || visibleMode_ != WindowMode::Maximized) return false;

    RECT maximizedRect{};
    if (!GetWindowRect(window_, &maximizedRect)) return false;
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    if (!GetMonitorInfoW(monitor, &monitorInfo)) return false;

    normalRect_ = CalculateRestoredDragRect(maximizedRect, normalRect_, cursor,
                                            monitorInfo.rcWork, chromeHeight_);
    visibleMode_ = WindowMode::Normal;
    ApplyVisibleMode(visibleMode_, true);
    return true;
}

void WindowModeController::ApplyPresetGeometry(const RECT& normalRect, bool maximized) {
    if (!window_) return;
    if (webFullscreen_) LeaveWebFullscreen();
    if (IsImmersive()) SetImmersion(false);
    normalRect_ = EnsureVisible(normalRect);
    visibleMode_ = maximized ? WindowMode::Maximized : WindowMode::Normal;
    ApplyVisibleMode(visibleMode_, true);
}

void WindowModeController::UpdateNormalRectFromWindow() {
    if (!window_ || visibleMode_ != WindowMode::Normal || hidden_ || IsIconic(window_)) return;
    RECT current{};
    if (GetWindowRect(window_, &current)) {
        normalRect_ = current;
        if (videoFitActive_) {
            // Once the user moves or snaps the auto-fitted video window, that new
            // position becomes authoritative and must survive leaving fullscreen.
            preVideoFitRect_ = current;
            videoFitActive_ = false;
        }
    }
}

void WindowModeController::OnImmersionTimer() {
    if (!window_ || !settings_ || !IsImmersive()) return;
    POINT cursor{};
    GetCursorPos(&cursor);
    if (visibleMode_ == WindowMode::ImmersionHole) {
        if (cursor.x == lastCursor_.x && cursor.y == lastCursor_.y) return;
        lastCursor_ = cursor;
        POINT client = cursor;
        ScreenToClient(window_, &client);
        RECT bounds{};
        GetClientRect(window_, &bounds);
        HRGN full = CreateRectRgn(0, 0, bounds.right, bounds.bottom);
        HRGN result = nullptr;
        if (settings_->holeRadius > 0) {
            const int radius = settings_->holeRadius;
            HRGN hole = CreateEllipticRgn(client.x - radius, client.y - radius,
                                          client.x + radius, client.y + radius);
            if (full && hole) {
                result = CreateRectRgn(0, 0, 0, 0);
                if (result) CombineRgn(result, full, hole, RGN_DIFF);
            }
            if (hole) DeleteObject(hole);
        }
        if (!result) result = full;
        else if (full) DeleteObject(full);
        if (!result) return;
        if (!SetWindowRgn(window_, result, FALSE)) DeleteObject(result);
        return;
    }

    CURSORINFO cursorInfo{sizeof(cursorInfo)};
    const bool cursorVisible = GetCursorInfo(&cursorInfo) &&
        (cursorInfo.flags & CURSOR_SHOWING) != 0;
    RECT windowRect{};
    GetWindowRect(window_, &windowRect);
    const bool candidate = cursorVisible && PtInRect(&windowRect, cursor) != FALSE;
    const ULONGLONG now = GetTickCount64();
    if (candidate != hoverCandidate_) {
        hoverCandidate_ = candidate;
        hoverCandidateSince_ = now;
    }
    if (hoverApplied_ != hoverCandidate_ && now - hoverCandidateSince_ >= 80) {
        hoverApplied_ = hoverCandidate_;
        SetLayeredWindowAttributes(window_, 0, hoverApplied_ ? 0 : ConfiguredAlpha(), LWA_ALPHA);
    }
}

void WindowModeController::ReassertTopmost() const {
    if (!window_ || hidden_) return;
    const HWND activePopup = GetLastActivePopup(window_);
    if (activePopup && activePopup != window_ && IsWindowVisible(activePopup)) return;
    const HWND foreground = GetForegroundWindow();
    if (foreground && foreground != window_ &&
        GetAncestor(foreground, GA_ROOTOWNER) == window_) return;
    SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
}

void WindowModeController::SnapMovingRect(RECT* movingRect, bool controlHeld) const {
    if (!window_ || !settings_ || !movingRect || controlHeld || settings_->snapThreshold <= 0 ||
        visibleMode_ != WindowMode::Normal) return;
    HMONITOR monitor = MonitorFromRect(movingRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) return;
    const RECT screen = info.rcMonitor;
    const int width = movingRect->right - movingRect->left;
    const int height = movingRect->bottom - movingRect->top;
    const int threshold = settings_->snapThreshold;
    if (std::abs(movingRect->left - screen.left) <= threshold) {
        movingRect->left = screen.left; movingRect->right = screen.left + width;
    } else if (std::abs(movingRect->right - screen.right) <= threshold) {
        movingRect->right = screen.right; movingRect->left = screen.right - width;
    }
    if (std::abs(movingRect->top - screen.top) <= threshold) {
        movingRect->top = screen.top; movingRect->bottom = screen.top + height;
    } else if (std::abs(movingRect->bottom - screen.bottom) <= threshold) {
        movingRect->bottom = screen.bottom; movingRect->top = screen.bottom - height;
    }
}

void WindowModeController::RefreshVisualState() {
    if (!window_) return;
    if (!IsImmersive()) return;
    const WindowMode desired = settings_ && settings_->immersionStyle == ImmersionStyle::AutoHide
        ? WindowMode::ImmersionAutoHide : WindowMode::ImmersionHole;
    if (desired != visibleMode_) {
        visibleMode_ = desired;
        ClearRegionAndOpacity();
        ApplyVisibleMode(visibleMode_, false);
        return;
    }
    if (visibleMode_ == WindowMode::ImmersionHole) {
        lastCursor_ = {LONG_MIN, LONG_MIN};
        SetLayeredWindowAttributes(window_, 0, ConfiguredAlpha(), LWA_ALPHA);
        OnImmersionTimer();
    } else {
        SetLayeredWindowAttributes(window_, 0, hoverApplied_ ? 0 : ConfiguredAlpha(), LWA_ALPHA);
    }
    ReassertTopmost();
}

void WindowModeController::RefreshFrameAppearance() const {
    if (!window_) return;
    const bool normalFrame = visibleMode_ == WindowMode::Normal && !IsImmersive();
    const DWMNCRENDERINGPOLICY nonClientPolicy = normalFrame
        ? DWMNCRP_ENABLED : DWMNCRP_DISABLED;
    DwmSetWindowAttribute(window_, DWMWA_NCRENDERING_POLICY,
                          &nonClientPolicy, sizeof(nonClientPolicy));
    const DWM_WINDOW_CORNER_PREFERENCE corners = normalFrame
        ? DWMWCP_ROUND : DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    DwmSetWindowAttribute(window_, kDwmBorderColorAttribute,
                          &kDwmColorNone, sizeof(kDwmColorNone));
}

void WindowModeController::ApplyVisibleMode(WindowMode mode, bool restoreGeometry) {
    if (!window_ || !settings_) return;
    KillTimer(window_, kImmersionTimerId);
    const bool immersive = mode == WindowMode::ImmersionHole || mode == WindowMode::ImmersionAutoHide;
    const DWORD baseExStyle = WS_EX_TOPMOST | (systemTray_ ? WS_EX_TOOLWINDOW : WS_EX_APPWINDOW);
    const DWORD style = mode == WindowMode::Normal ? kNormalStyle : kPopupStyle;
    const DWORD exStyle = immersive
        ? baseExStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE
        : baseExStyle;
    ApplyStyle(style, exStyle);
    RefreshFrameAppearance();

    UINT positionFlags = SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOREDRAW;
    if (immersive) {
        if (restoreGeometry) {
            const RECT rect = preImmersionRect_;
            const int height = std::max(1, static_cast<int>(rect.bottom - rect.top) - chromeHeight_);
            SetWindowPos(window_, HWND_TOPMOST, rect.left, rect.top + chromeHeight_,
                         rect.right - rect.left, height,
                         positionFlags);
        } else {
            SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                         positionFlags | SWP_NOMOVE | SWP_NOSIZE);
        }
        if (chromeVisibility_) chromeVisibility_(false);
        lastCursor_ = {LONG_MIN, LONG_MIN};
        hoverApplied_ = hoverCandidate_ = false;
        hoverCandidateSince_ = GetTickCount64();
        SetLayeredWindowAttributes(window_, 0, ConfiguredAlpha(), LWA_ALPHA);
    } else {
        if (mode == WindowMode::Maximized) {
            if (restoreGeometry) {
                const RECT work = CurrentMonitorRect(true);
                SetWindowPos(window_, HWND_TOPMOST, work.left, work.top,
                             work.right - work.left, work.bottom - work.top,
                             positionFlags);
            } else {
                SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                             positionFlags | SWP_NOMOVE | SWP_NOSIZE);
            }
        } else {
            if (restoreGeometry) {
                const RECT target = normalRect_;
                SetWindowPos(window_, HWND_TOPMOST, target.left, target.top,
                             target.right - target.left, target.bottom - target.top,
                             positionFlags);
            } else {
                SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                             positionFlags | SWP_NOMOVE | SWP_NOSIZE);
            }
        }
        if (chromeVisibility_) chromeVisibility_(true);
        ClearRegionAndOpacity();
    }
    if (layoutCallback_) layoutCallback_();
    if (immersive) {
        OnImmersionTimer();
        SetTimer(window_, kImmersionTimerId, 33, nullptr);
    }
    RedrawWindow(window_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    ReassertTopmost();
}

void WindowModeController::ApplyStyle(DWORD style, DWORD exStyle) const {
    if ((GetWindowLongPtrW(window_, GWL_STYLE) & WS_VISIBLE) != 0) {
        style |= WS_VISIBLE;
    }
    SetWindowLongPtrW(window_, GWL_STYLE, static_cast<LONG_PTR>(style));
    SetWindowLongPtrW(window_, GWL_EXSTYLE, static_cast<LONG_PTR>(exStyle));
}

RECT WindowModeController::CurrentMonitorRect(bool workArea) const {
    HMONITOR monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    return workArea ? info.rcWork : info.rcMonitor;
}

BYTE WindowModeController::ConfiguredAlpha() const {
    if (!settings_) return 255;
    if (visibleMode_ == WindowMode::ImmersionHole) return 255;
    const int percent = settings_->autoHideOpacityPercent;
    return static_cast<BYTE>(std::clamp((percent * 255 + 50) / 100, 0, 255));
}

void WindowModeController::ClearRegionAndOpacity() const {
    if (!window_) return;
    SetWindowRgn(window_, nullptr, FALSE);
    SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
}

RECT WindowModeController::EnsureVisible(RECT rect) const {
    HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) return rect;
    const RECT work = info.rcWork;
    int width = std::clamp(static_cast<int>(rect.right - rect.left), 360,
                           static_cast<int>(work.right - work.left));
    int height = std::clamp(static_cast<int>(rect.bottom - rect.top), 240,
                            static_cast<int>(work.bottom - work.top));
    const int left = std::clamp(static_cast<int>(rect.left), static_cast<int>(work.left),
                                static_cast<int>(work.right) - width);
    const int top = std::clamp(static_cast<int>(rect.top), static_cast<int>(work.top),
                               static_cast<int>(work.bottom) - height);
    return RECT{left, top, left + width, top + height};
}

} // namespace xiaochuang
