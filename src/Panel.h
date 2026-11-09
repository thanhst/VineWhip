#pragma once

#include "AppList.h"
#include "Overlay.h"

#include <optional>

struct PanelSettings {
    bool whip;
    bool boost;
    bool animation;
    bool reducedMotion;
    bool restorePending;
    bool hotkeyRegistered;
    DWORD boostedPid;
    Intensity intensity;
};

struct PanelStrike {
    AppTarget target;
    std::optional<BoostResult> result;
    float elapsedMs;
};

class Panel {
public:
    static constexpr LONG width = 410;
    static constexpr LONG height = 422;
    static constexpr int visibleRows = 3;

    void Refresh(HWND window);
    void Scroll(HWND window, int command);
    void Wheel(HWND window, int delta);
    void Select(HWND window, int index);
    int Selection() const { return selection_; }
    int Count() const { return static_cast<int>(apps_.size()); }
    int HitTest(HWND window, POINT point) const;
    bool HitsBoostSwitch(HWND window, POINT point) const;
    const OpenApp* AppAt(int index) const;

    void PointAt(POINT point) { cursor_ = point; }
    void Leave() { cursor_.reset(); }
    void Paint(HWND window, HDC dc, const PanelSettings& settings, const PanelStrike* strike);

private:
    void SetScroll(HWND window, int first);

    std::vector<OpenApp> apps_;
    std::unique_ptr<Gdiplus::Bitmap> buffer_;
    std::optional<POINT> cursor_;
    int firstRow_{};
    int selection_{-1};
    int wheelRemainder_{};
};
