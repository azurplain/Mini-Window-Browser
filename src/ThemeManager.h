#pragma once

#include "AppModel.h"

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <wrl/client.h>

namespace xiaochuang {

struct ThemePalette {
    COLORREF window = RGB(245, 246, 250);
    COLORREF chrome = RGB(237, 239, 246);
    COLORREF surface = RGB(255, 255, 255);
    COLORREF surfaceHover = RGB(228, 232, 245);
    COLORREF surfacePressed = RGB(210, 216, 237);
    COLORREF text = RGB(31, 35, 48);
    COLORREF secondaryText = RGB(92, 99, 122);
    COLORREF border = RGB(211, 215, 228);
    COLORREF accent = RGB(78, 87, 225);
    COLORREF accentHover = RGB(94, 102, 235);
    COLORREF danger = RGB(196, 43, 58);
};

class ThemeManager {
public:
    ThemeManager();
    ~ThemeManager();
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    bool SetMode(ThemeMode mode);
    bool RefreshSystemTheme();
    bool IsDark() const noexcept { return dark_; }
    const ThemePalette& Palette() const noexcept { return palette_; }

    void ApplyWindowTheme(HWND window) const;
    void DrawOwnerButton(const DRAWITEMSTRUCT& item, const std::wstring& text,
                         bool accent = false, bool danger = false,
                         COLORREF background = CLR_INVALID) const;
    void DrawTab(const DRAWITEMSTRUCT& item, const std::wstring& title, bool selected) const;
    void FillBackground(HDC dc, const RECT& rect, COLORREF color) const;
    HBRUSH WindowBrush() const noexcept { return windowBrush_; }
    HBRUSH SurfaceBrush() const noexcept { return surfaceBrush_; }
    HBRUSH ChromeBrush() const noexcept { return chromeBrush_; }

private:
    static bool SystemPrefersDark();
    void RebuildPalette();
    void RebuildBrushes();
    D2D1_COLOR_F ToD2D(COLORREF color) const;
    void DrawRoundedControl(HDC dc, const RECT& rect, COLORREF fill, COLORREF stroke,
                            const std::wstring& text, COLORREF textColor, float radius,
                            COLORREF background) const;

    ThemeMode mode_ = ThemeMode::System;
    bool dark_ = false;
    ThemePalette palette_;
    HBRUSH windowBrush_ = nullptr;
    HBRUSH surfaceBrush_ = nullptr;
    HBRUSH chromeBrush_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> smallTextFormat_;
};

} // namespace xiaochuang
