#pragma once

#include "AppModel.h"
#include <WebView2.h>
#include <wil/com.h>
#include <wrl.h>
#include <cmath>

namespace xiaochuang {

// Owns the zoom policy and event subscription for one WebView controller.
// Detach before closing/replacing the controller so callbacks cannot outlive us.
class WebViewZoom {
public:
    ~WebViewZoom() { Detach(); }
    WebViewZoom() = default;
    WebViewZoom(const WebViewZoom&) = delete;
    WebViewZoom& operator=(const WebViewZoom&) = delete;

    HRESULT Attach(ICoreWebView2Controller* controller, ICoreWebView2* view) {
        Detach();
        if (!controller || !view) return E_INVALIDARG;
        controller_ = controller;
        HRESULT result = view->get_Settings(&settings_);
        if (FAILED(result)) { Detach(); return result; }
        result = controller_->add_ZoomFactorChanged(
            Microsoft::WRL::Callback<ICoreWebView2ZoomFactorChangedEventHandler>(
                [this](ICoreWebView2Controller*, IUnknown*) -> HRESULT {
                    return Enforce();
                }).Get(), &token_);
        subscribed_ = SUCCEEDED(result);
        return result;
    }

    void Detach() {
        if (controller_ && subscribed_) controller_->remove_ZoomFactorChanged(token_);
        subscribed_ = false;
        settings_.reset();
        controller_.reset();
    }

    HRESULT Configure(const AppSettings& settings) {
        locked_ = settings.fixedWebZoomEnabled;
        factor_ = std::clamp(settings.fixedWebZoomPercent, 25, 500) / 100.0;
        if (!settings_ || !controller_) return S_FALSE;
        HRESULT result = settings_->put_IsZoomControlEnabled(locked_ ? FALSE : TRUE);
        if (FAILED(result)) return result;
        wil::com_ptr<ICoreWebView2Settings5> settings5;
        if (settings_.try_query_to(&settings5) && settings5) {
            result = settings5->put_IsPinchZoomEnabled(locked_ ? FALSE : TRUE);
            if (FAILED(result)) return result;
        }
        // Native zoom/pinch controls apply at the next top-level navigation.
        // Do not reload here: that would interrupt playback or unsaved forms.
        // Until navigation, the event guard still enforces a newly locked zoom.
        return Enforce();
    }

private:
    HRESULT Enforce() {
        if (!locked_ || !controller_ || applying_) return S_OK;
        double current = 1.0;
        HRESULT result = controller_->get_ZoomFactor(&current);
        if (FAILED(result) || std::abs(current - factor_) < 0.0001) return result;
        applying_ = true;
        result = controller_->put_ZoomFactor(factor_);
        applying_ = false;
        return result;
    }

    wil::com_ptr<ICoreWebView2Controller> controller_;
    wil::com_ptr<ICoreWebView2Settings> settings_;
    EventRegistrationToken token_{};
    bool subscribed_ = false;
    bool locked_ = false;
    bool applying_ = false;
    double factor_ = 1.0;
};

} // namespace xiaochuang
