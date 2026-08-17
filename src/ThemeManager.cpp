#include "ThemeManager.h"

#include <dwmapi.h>

#include <algorithm>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")

namespace xiaochuang {

ThemeManager::ThemeManager() {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf());
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf()));
    if (writeFactory_) {
        writeFactory_->CreateTextFormat(L"Segoe UI Variable Text", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            14.0f, L"zh-CN", textFormat_.GetAddressOf());
        writeFactory_->CreateTextFormat(L"Segoe UI Variable Text", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            12.0f, L"zh-CN", smallTextFormat_.GetAddressOf());
        if (textFormat_) {
            textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (smallTextFormat_) {
            smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            smallTextFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
    dark_ = SystemPrefersDark();
    RebuildPalette();
    RebuildBrushes();
}

ThemeManager::~ThemeManager() {
    if (windowBrush_) DeleteObject(windowBrush_);
    if (surfaceBrush_) DeleteObject(surfaceBrush_);
    if (chromeBrush_) DeleteObject(chromeBrush_);
}

bool ThemeManager::SetMode(ThemeMode mode) {
    const bool previousDark = dark_;
    const ThemeMode previousMode = mode_;
    mode_ = mode;
    dark_ = mode == ThemeMode::Dark || (mode == ThemeMode::System && SystemPrefersDark());
    if (previousDark == dark_ && previousMode == mode_) return false;
    RebuildPalette();
    RebuildBrushes();
    return true;
}

bool ThemeManager::RefreshSystemTheme() {
    if (mode_ != ThemeMode::System) return false;
    const bool nextDark = SystemPrefersDark();
    if (nextDark == dark_) return false;
    dark_ = nextDark;
    RebuildPalette();
    RebuildBrushes();
    return true;
}

void ThemeManager::ApplyWindowTheme(HWND window) const {
    if (!window) return;
    const BOOL useDark = dark_ ? TRUE : FALSE;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
#ifdef DWMWA_BORDER_COLOR
    const COLORREF border = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(window, DWMWA_BORDER_COLOR, &border, sizeof(border));
#endif
}

void ThemeManager::DrawOwnerButton(const DRAWITEMSTRUCT& item, const std::wstring& text,
                                   bool accent, bool danger, COLORREF background) const {
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    COLORREF fill = palette_.surface;
    COLORREF stroke = palette_.border;
    COLORREF textColor = palette_.text;
    if (accent) {
        fill = selected ? palette_.accentHover : palette_.accent;
        stroke = fill;
        textColor = RGB(255, 255, 255);
    } else if (danger) {
        fill = selected ? RGB(220, 64, 80) : palette_.danger;
        stroke = fill;
        textColor = RGB(255, 255, 255);
    } else if (selected) {
        fill = palette_.surfacePressed;
    } else if ((item.itemState & ODS_HOTLIGHT) != 0) {
        fill = palette_.surfaceHover;
    }
    if (disabled) textColor = palette_.secondaryText;
    if (background == CLR_INVALID) background = palette_.window;
    DrawRoundedControl(item.hDC, item.rcItem, fill, stroke, text, textColor, 7.0f, background);
    if ((item.itemState & ODS_FOCUS) != 0) {
        RECT focus = item.rcItem;
        InflateRect(&focus, -3, -3);
        DrawFocusRect(item.hDC, &focus);
    }
}

void ThemeManager::DrawTab(const DRAWITEMSTRUCT& item, const std::wstring& title, bool selected) const {
    COLORREF fill = selected ? palette_.surface : palette_.chrome;
    if ((item.itemState & ODS_SELECTED) != 0) fill = palette_.surfacePressed;
    RECT rect = item.rcItem;
    InflateRect(&rect, -2, -2);
    DrawRoundedControl(item.hDC, rect, fill, selected ? palette_.accent : palette_.border,
                       L"", palette_.text, 8.0f, palette_.chrome);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, palette_.text);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int closeWidth = std::min(width,
        std::max(16, std::min(std::max(24, height), width / 2)));
    const bool showClose = selected || width >= 64;
    RECT titleRect = rect;
    titleRect.left += std::min(10, std::max(2, width / 8));
    if (showClose) titleRect.right -= closeWidth;
    DrawTextW(item.hDC, title.c_str(), static_cast<int>(title.size()), &titleRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (showClose) {
        RECT closeRect = rect;
        closeRect.left = closeRect.right - closeWidth;
        DrawTextW(item.hDC, L"×", 1, &closeRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

void ThemeManager::FillBackground(HDC dc, const RECT& rect, COLORREF color) const {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

bool ThemeManager::SystemPrefersDark() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS) {
        return value == 0;
    }
    return false;
}

void ThemeManager::RebuildPalette() {
    if (dark_) {
        palette_.window = RGB(19, 21, 29);
        palette_.chrome = RGB(26, 29, 40);
        palette_.surface = RGB(37, 41, 55);
        palette_.surfaceHover = RGB(48, 53, 70);
        palette_.surfacePressed = RGB(58, 63, 82);
        palette_.text = RGB(241, 243, 250);
        palette_.secondaryText = RGB(170, 176, 199);
        palette_.border = RGB(67, 72, 94);
        palette_.accent = RGB(100, 108, 255);
        palette_.accentHover = RGB(121, 128, 255);
        palette_.danger = RGB(207, 54, 70);
    } else {
        palette_ = ThemePalette{};
    }
}

void ThemeManager::RebuildBrushes() {
    if (windowBrush_) DeleteObject(windowBrush_);
    if (surfaceBrush_) DeleteObject(surfaceBrush_);
    if (chromeBrush_) DeleteObject(chromeBrush_);
    windowBrush_ = CreateSolidBrush(palette_.window);
    surfaceBrush_ = CreateSolidBrush(palette_.surface);
    chromeBrush_ = CreateSolidBrush(palette_.chrome);
}

D2D1_COLOR_F ThemeManager::ToD2D(COLORREF color) const {
    return D2D1::ColorF(GetRValue(color) / 255.0f, GetGValue(color) / 255.0f,
                        GetBValue(color) / 255.0f, 1.0f);
}

void ThemeManager::DrawRoundedControl(HDC dc, const RECT& rect, COLORREF fill, COLORREF stroke,
                                      const std::wstring& text, COLORREF textColor, float radius,
                                      COLORREF background) const {
    bool rendered = false;
    if (d2dFactory_) {
        D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target;
        if (SUCCEEDED(d2dFactory_->CreateDCRenderTarget(&properties, target.GetAddressOf())) && target &&
            SUCCEEDED(target->BindDC(dc, &rect))) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fillBrush;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> strokeBrush;
            if (SUCCEEDED(target->CreateSolidColorBrush(ToD2D(fill), fillBrush.GetAddressOf())) &&
                SUCCEEDED(target->CreateSolidColorBrush(ToD2D(stroke), strokeBrush.GetAddressOf()))) {
                const D2D1_RECT_F local = D2D1::RectF(0.5f, 0.5f,
                    static_cast<float>(rect.right - rect.left) - 0.5f,
                    static_cast<float>(rect.bottom - rect.top) - 0.5f);
                const D2D1_ROUNDED_RECT rounded{local, radius, radius};
                target->BeginDraw();
                target->Clear(ToD2D(background));
                target->FillRoundedRectangle(rounded, fillBrush.Get());
                target->DrawRoundedRectangle(rounded, strokeBrush.Get(), 1.0f);
                rendered = SUCCEEDED(target->EndDraw());
            }
        }
    }
    if (!rendered) {
        FillBackground(dc, rect, background);
        HBRUSH fillBrush = CreateSolidBrush(fill);
        HPEN strokePen = CreatePen(PS_SOLID, 1, stroke);
        HGDIOBJ oldBrush = SelectObject(dc, fillBrush);
        HGDIOBJ oldPen = SelectObject(dc, strokePen);
        const int diameter = std::max(2, static_cast<int>(radius * 2.0f));
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, diameter, diameter);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(strokePen);
        DeleteObject(fillBrush);
    }
    if (!text.empty()) {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, textColor);
        RECT copy = rect;
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &copy,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

} // namespace xiaochuang
