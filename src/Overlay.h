#pragma once

#include "Priority.h"

inline constexpr UINT kRefreshOverlay = WM_APP + 3;

enum class Intensity { low, normal, crazy };

class Overlay {
public:
    ~Overlay();

    bool Create(HINSTANCE instance, HWND controller);
    HWND WindowAt(POINT point);
    void Hide();
    void DrawIdle(POINT cursor);
    void DrawStrike(POINT impact, float elapsedMs, BoostResult result,
                    Intensity intensity, bool reducedMotion);

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void Draw(POINT anchor, float elapsedMs, BoostResult result,
              Intensity intensity, bool reducedMotion);
    bool ResizeSurface(int width, int height);
    void ReleaseSurface();

    HWND window_{};
    HMONITOR monitor_{};
    HDC surface_{};
    HBITMAP bitmap_{};
    HGDIOBJ originalBitmap_{};
    BYTE* pixels_{};
    int width_{};
    int height_{};
};
