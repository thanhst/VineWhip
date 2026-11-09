#include "Overlay.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>

namespace {

using namespace Gdiplus;

const Color kInk(255, 42, 23, 38);
const Color kLeather(255, 190, 83, 54);
const Color kHighlight(255, 255, 184, 108);
const Color kGold(255, 255, 224, 94);

struct Pose {
    std::array<PointF, 7> cord;
    PointF handleEnd;
};

const Pose kIdle{{PointF(-10, 15), {-30, -15}, {-66, -24}, {-81, -5},
                 {-98, 16}, {-78, 34}, {-58, 25}}, {6, 37}};
const Pose kWindup{{PointF(-46, 22), {-62, -22}, {-144, -72}, {-145, -28},
                   {-147, 4}, {-119, -7}, {-118, -32}}, {-23, 43}};
const Pose kStrike{{PointF(-106, 47), {-92, 22}, {-71, 13}, {-52, 15},
                   {-33, 17}, {-12, 9}, {0, 0}}, {-132, 62}};

float Unit(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float EaseOut(float progress)
{
    const float remaining = 1.0f - Unit(progress);
    return 1.0f - remaining * remaining * remaining;
}

PointF Mix(PointF a, PointF b, float progress)
{
    return {std::lerp(a.X, b.X, progress), std::lerp(a.Y, b.Y, progress)};
}

Pose WhipPose(float ms)
{
    if (ms < 0.0f) {
        return kIdle;
    }
    const Pose* start = &kIdle;
    const Pose* end = &kWindup;
    float progress = Unit(ms / 100.0f);
    progress *= progress;
    float tipArc = 0.0f;
    if (ms >= 220.0f) {
        start = &kStrike;
        end = &kIdle;
        const float t = Unit((ms - 220.0f) / 280.0f);
        progress = EaseOut(t) + std::sin(t * 12.0f) * t * (1.0f - t) * 0.35f;
    } else if (ms >= 100.0f) {
        start = &kWindup;
        end = &kStrike;
        progress = EaseOut((ms - 100.0f) / 120.0f);
        tipArc = -32.0f * std::sin(std::numbers::pi_v<float> * progress);
    }

    Pose pose{};
    for (size_t index = 0; index < pose.cord.size(); ++index) {
        pose.cord[index] = Mix(start->cord[index], end->cord[index], progress);
    }
    pose.cord[4].Y += tipArc * 0.25f;
    pose.cord[5].Y += tipArc * 0.65f;
    pose.cord[6].Y += tipArc;
    pose.handleEnd = Mix(start->handleEnd, end->handleEnd, progress);
    return pose;
}

Color Fade(Color color, float opacity)
{
    return Color(static_cast<BYTE>(color.GetAlpha() * Unit(opacity)),
        color.GetRed(), color.GetGreen(), color.GetBlue());
}

void RoundedPen(Pen& pen)
{
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
}

void DrawWhip(Graphics& graphics, const Pose& pose, float opacity, bool trail)
{
    const auto& p = pose.cord;
    GraphicsPath cord;
    cord.AddBezier(p[0], p[1], p[2], p[3]);
    cord.AddBezier(p[3], p[4], p[5], p[6]);

    if (trail) {
        Pen streak(Fade(kHighlight, opacity), 5.0f);
        RoundedPen(streak);
        graphics.DrawPath(&streak, &cord);
        return;
    }

    Pen outline(Fade(kInk, opacity), 7.0f);
    Pen leather(Fade(kLeather, opacity), 3.8f);
    Pen tip(Fade(kGold, opacity), 1.6f);
    RoundedPen(outline);
    RoundedPen(leather);
    RoundedPen(tip);
    graphics.DrawPath(&outline, &cord);
    graphics.DrawPath(&leather, &cord);
    graphics.DrawBezier(&tip, p[3], p[4], p[5], p[6]);

    outline.SetWidth(13.0f);
    leather.SetWidth(8.0f);
    graphics.DrawLine(&outline, p[0], pose.handleEnd);
    graphics.DrawLine(&leather, p[0], pose.handleEnd);
    for (float t : {0.15f, 0.40f, 0.65f, 0.88f}) {
        const PointF point = Mix(p[0], pose.handleEnd, t);
        graphics.DrawLine(&tip, point.X - 2.0f, point.Y, point.X + 1.5f, point.Y - 2.0f);
    }
}

void DrawBurst(Graphics& graphics, float ms, int sparkCount)
{
    const float age = ms - 220.0f;
    const float opacity = 1.0f - Unit(age / 265.0f);
    if (age < 100.0f) {
        const float radius = 10.0f + 27.0f * EaseOut(age / 100.0f);
        std::array<PointF, 18> star;
        for (size_t i = 0; i < star.size(); ++i) {
            const float angle = static_cast<float>(i) * std::numbers::pi_v<float> / 9.0f;
            const float r = radius * (i % 2 == 0 ? 1.0f : 0.55f);
            star[i] = {r * std::cos(angle), r * std::sin(angle)};
        }
        const float flash = 1.0f - Unit(age / 100.0f);
        SolidBrush fill(Fade(kGold, flash));
        Pen edge(Fade(kInk, flash), 2.5f);
        RoundedPen(edge);
        graphics.FillPolygon(&fill, star.data(), static_cast<int>(star.size()));
        graphics.DrawPolygon(&edge, star.data(), static_cast<int>(star.size()));
    }

    Pen spark(Fade(kGold, opacity), 2.8f);
    RoundedPen(spark);
    for (int i = 0; i < sparkCount; ++i) {
        const float angle = static_cast<float>(i) * 2.39996f;
        const float range = 30.0f + 10.0f * static_cast<float>(i % 5);
        const float distance = 12.0f + range * EaseOut(age / 280.0f);
        const float tail = distance - 5.0f - static_cast<float>(i % 3) * 2.0f;
        const float drop = 0.00024f * age * age;
        const float dx = std::cos(angle);
        const float dy = std::sin(angle);
        graphics.DrawLine(&spark, tail * dx, tail * dy + drop,
            distance * dx, distance * dy + drop);
    }
}

const wchar_t* StatusText(BoostResult result)
{
    switch (result) {
    case BoostResult::boosted: return L"+CPU PRIORITY";
    case BoostResult::alreadyBoosted: return L"ALREADY BOOSTED";
    case BoostResult::denied: return L"PRIORITY DENIED";
    case BoostResult::restorePending: return L"RESTORE PENDING";
    default: return L"PRIORITY UNCHANGED";
    }
}

void DrawCaption(Graphics& graphics, const wchar_t* text, PointF center,
                 float size, Color fill, float opacity)
{
    FontFamily font(L"Segoe UI");
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    GraphicsPath lettering;
    const RectF box(center.X - 120.0f, center.Y - 20.0f, 240.0f, 40.0f);
    lettering.AddString(text, -1, &font, FontStyleBold, size, box, &format);
    Pen outline(Fade(kInk, opacity), 4.0f);
    RoundedPen(outline);
    SolidBrush ink(Fade(fill, opacity));
    graphics.DrawPath(&outline, &lettering);
    graphics.FillPath(&ink, &lettering);
}

void DrawLightning(Graphics& graphics, PointF center, float opacity)
{
    const float x = center.X;
    const float y = center.Y;
    const PointF bolt[] = {{x + 3, y - 12}, {x - 8, y + 1}, {x - 1, y + 1},
        {x - 4, y + 12}, {x + 9, y - 3}, {x + 2, y - 3}};
    SolidBrush fill(Fade(kGold, opacity));
    Pen edge(Fade(kInk, opacity), 2.0f);
    RoundedPen(edge);
    graphics.FillPolygon(&fill, bolt, 6);
    graphics.DrawPolygon(&edge, bolt, 6);
}

} // namespace

Overlay::~Overlay()
{
    if (window_) {
        DestroyWindow(window_);
    }
    ReleaseSurface();
    if (surface_) {
        DeleteDC(surface_);
    }
}

bool Overlay::Create(HINSTANCE instance, HWND controller)
{
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"WhipCPU.Overlay";
    if (!RegisterClassW(&windowClass)) {
        return false;
    }
    window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, windowClass.lpszClassName, L"", WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, instance, controller);
    surface_ = CreateCompatibleDC(nullptr);
    return window_ && surface_;
}

LRESULT CALLBACK Overlay::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_NCCREATE:
        SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(reinterpret_cast<const CREATESTRUCTW*>(lParam)->lpCreateParams));
        break;
    case WM_DPICHANGED:
        PostMessageW(reinterpret_cast<HWND>(GetWindowLongPtrW(window, GWLP_USERDATA)),
            kRefreshOverlay, 0, 0);
        return 0;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_NCHITTEST: return HTTRANSPARENT;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void Overlay::Hide()
{
    if (window_ && IsWindowVisible(window_)) {
        ShowWindow(window_, SW_HIDE);
    }
}

HWND Overlay::WindowAt(POINT point)
{
    const HWND hit = WindowFromPoint(point);
    if (hit != window_) {
        return hit;
    }
    Hide();
    PostMessageW(reinterpret_cast<HWND>(GetWindowLongPtrW(window_, GWLP_USERDATA)),
        kRefreshOverlay, 0, 0);
    return WindowFromPoint(point);
}

void Overlay::DrawIdle(POINT cursor)
{
    Draw(cursor, -1.0f, BoostResult::unchanged, Intensity::normal, false);
}

void Overlay::DrawStrike(POINT impact, float elapsedMs, BoostResult result,
                         Intensity intensity, bool reducedMotion)
{
    Draw(impact, elapsedMs, result, intensity, reducedMotion);
}

void Overlay::Draw(POINT anchor, float ms, BoostResult result,
                   Intensity intensity, bool reducedMotion)
{
    const HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONULL);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info)) {
        Hide();
        return;
    }
    if (monitor_ != monitor) {
        Hide();
        if (!SetWindowPos(window_, nullptr, anchor.x, anchor.y, 1, 1,
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER)) {
            return;
        }
        monitor_ = monitor;
    }
    const UINT dpi = GetDpiForWindow(window_);
    const float scale = static_cast<float>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI) / 96.0f;
    const RECT bounds = info.rcMonitor;
    const bool idle = ms < 0.0f;
    const LONG marginX = std::lround((idle ? 105.0f : 250.0f) * scale);
    const LONG marginY = std::lround((idle ? 48.0f : 128.0f) * scale);
    SIZE size{std::min(bounds.right - bounds.left, 2 * marginX),
              std::min(bounds.bottom - bounds.top, 2 * marginY)};
    POINT topLeft{std::clamp(anchor.x - marginX, bounds.left, bounds.right - size.cx),
                  std::clamp(anchor.y - marginY, bounds.top, bounds.bottom - size.cy)};
    if (!ResizeSurface(static_cast<int>(size.cx), static_cast<int>(size.cy))) {
        Hide();
        return;
    }

    std::memset(pixels_, 0, static_cast<size_t>(width_) * height_ * 4);
    {
        Bitmap image(width_, height_, width_ * 4, PixelFormat32bppPARGB, pixels_);
        Graphics graphics(&image);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.TranslateTransform(static_cast<float>(anchor.x - topLeft.x),
            static_cast<float>(anchor.y - topLeft.y));
        graphics.ScaleTransform(scale, scale);

        if (!reducedMotion) {
            const GraphicsState state = graphics.Save();
            const float flipX = anchor.x - bounds.left < (idle ? 99.0f : 152.0f) * scale
                && bounds.right - anchor.x > anchor.x - bounds.left ? -1.0f : 1.0f;
            const float flipY = bounds.bottom - anchor.y < 70.0f * scale ? -1.0f : 1.0f;
            graphics.ScaleTransform(flipX, flipY);
            if (ms >= 112.0f && ms < 220.0f && intensity != Intensity::low) {
                DrawWhip(graphics, WhipPose(ms - 22.0f), 0.28f, true);
                if (intensity == Intensity::crazy) {
                    DrawWhip(graphics, WhipPose(ms - 39.0f), 0.18f, true);
                }
            }
            DrawWhip(graphics, WhipPose(ms), 1.0f, false);
            graphics.Restore(state);
        }

        if (!idle) {
            const float minX = static_cast<float>(bounds.left - anchor.x) / scale + 120.0f;
            const float maxX = static_cast<float>(bounds.right - anchor.x) / scale - 120.0f;
            const float captionX = minX <= maxX ? std::clamp(0.0f, minX, maxX)
                                                : (minX + maxX) / 2.0f;
            const float direction = anchor.y - bounds.top < 95.0f * scale ? 1.0f : -1.0f;
            if (!reducedMotion && ms >= 220.0f) {
                const int sparkCount = intensity == Intensity::crazy ? 20 :
                    intensity == Intensity::normal ? 11 : 5;
                DrawBurst(graphics, ms, sparkCount);
                if (ms < 320.0f) {
                    const float impact = Unit((ms - 220.0f) / 100.0f);
                    DrawCaption(graphics, L"CHÁT!",
                        {captionX, direction * (42.0f + 15.0f * EaseOut(impact))},
                        25.0f, kGold, 1.0f - impact * 0.48f);
                }
            }
            if (reducedMotion || ms >= 320.0f) {
                const float progress = Unit((ms - 320.0f) / 180.0f);
                const float opacity = reducedMotion ? 1.0f : 1.0f - progress * progress;
                const float y = direction * (57.0f + (reducedMotion ? 0.0f : 15.0f * EaseOut(progress)));
                const bool success = result == BoostResult::boosted || result == BoostResult::alreadyBoosted;
                const Color tone = success ? Color(255, 150, 255, 176) :
                    result == BoostResult::unchanged ? Color(255, 202, 222, 250) : Color(255, 156, 155);
                DrawCaption(graphics, StatusText(result), {captionX, y}, 18.0f, tone, opacity);
                if (success && !reducedMotion) {
                    DrawLightning(graphics, {captionX, y + 23.0f * direction}, opacity);
                }
            }
        }
        graphics.Flush(FlushIntentionSync);
    }

    POINT source{};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (UpdateLayeredWindow(window_, nullptr, &topLeft, &size, surface_, &source, 0, &blend, ULW_ALPHA)) {
        if (!IsWindowVisible(window_)) {
            ShowWindow(window_, SW_SHOWNOACTIVATE);
        }
    } else {
        Hide();
    }
}

bool Overlay::ResizeSurface(int width, int height)
{
    if (bitmap_ && width == width_ && height == height_) {
        return true;
    }
    ReleaseSurface();
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    bitmap_ = CreateDIBSection(surface_, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap_) {
        return false;
    }
    originalBitmap_ = SelectObject(surface_, bitmap_);
    if (!originalBitmap_ || originalBitmap_ == HGDI_ERROR) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
        return false;
    }
    pixels_ = static_cast<BYTE*>(bits);
    width_ = width;
    height_ = height;
    return true;
}

void Overlay::ReleaseSurface()
{
    if (bitmap_) {
        SelectObject(surface_, originalBitmap_);
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }
}
