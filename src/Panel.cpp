#include "Panel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace {

using Gdiplus::Color;
using Gdiplus::Graphics;
using Gdiplus::PointF;
using Gdiplus::RectF;
using Gdiplus::SolidBrush;

constexpr int kListTop = 99;
constexpr int kRowHeight = 89;
constexpr int kListBottom = kListTop + kRowHeight * Panel::visibleRows;
constexpr int kGutter = 14;
const Color kBackground(23, 20, 35);
const Color kText(248, 242, 252);
const Color kMuted(189, 176, 204);
const Color kCoral(255, 177, 151);
const Color kMint(148, 238, 196);
const Color kInk(31, 23, 43);

float DpiScale(HWND window)
{
    const UINT dpi = GetDpiForWindow(window);
    return static_cast<float>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI) / 96.0f;
}

bool SameProcess(const AppTarget& left, const AppTarget& right)
{
    return left.pid == right.pid && left.created == right.created;
}

void Fill(Graphics& graphics, Color color, RectF bounds)
{
    SolidBrush brush(color);
    graphics.FillRectangle(&brush, bounds);
}

void Circle(Graphics& graphics, Color color, RectF bounds)
{
    SolidBrush brush(color);
    graphics.FillEllipse(&brush, bounds);
}

void RoundBox(Graphics& graphics, RectF rect, float radius, Color color)
{
    const float diameter = radius * 2.0f;
    Gdiplus::GraphicsPath path;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
    SolidBrush brush(color);
    graphics.FillPath(&brush, &path);
}

void Label(Graphics& graphics, std::wstring_view text, RectF bounds, float size, Color color,
           bool bold = false, bool center = false)
{
    const Gdiplus::Font font(L"Segoe UI", size,
        bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format(Gdiplus::StringFormatFlagsNoWrap);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    if (center) {
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
    }
    SolidBrush brush(color);
    graphics.DrawString(text.data(), static_cast<int>(text.size()), &font, bounds, &format, &brush);
}

void Line(Graphics& graphics, Color color, float width, PointF start, PointF end)
{
    Gdiplus::Pen pen(color, width);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    graphics.DrawLine(&pen, start, end);
}

void DrawWorker(Graphics& graphics, const OpenApp& app, float top, bool boosted,
                bool animated, float strikeMs)
{
    static const std::array accents{
        Color(189, 161, 255), Color(115, 219, 208), Color(252, 166, 145), Color(252, 209, 127)
    };
    const Color accent = accents[(app.target.pid / 4) % accents.size()];
    const double frequency = boosted ? 4.6 : 1.7;
    const double phase = animated ? static_cast<double>(GetTickCount64()) * 0.0062831853 * frequency +
        static_cast<double>(app.target.pid % 23) : 0.0;
    const float motion = static_cast<float>(std::sin(phase));
    const float jolt = animated && strikeMs >= 220.0f && strikeMs < 360.0f
        ? -7.0f * std::sin((strikeMs - 220.0f) / 140.0f * 3.1415927f) : 0.0f;
    const float headY = top + 11.0f + motion * 1.3f + jolt;

    RoundBox(graphics, {21.0f, top + 7.0f, 88.0f, 66.0f}, 12.0f, Color(35, 29, 52));
    Fill(graphics, Color(54, 42, 72), {26.0f, top + 62.0f, 76.0f, 2.0f});
    Line(graphics, Color(107, 90, 131), 3.5f, {33.0f, top + 42.0f}, {33.0f, top + 59.0f});
    Line(graphics, Color(107, 90, 131), 2.6f, {37.0f, top + 57.0f}, {44.0f, top + 67.0f});
    Line(graphics, accent, 12.0f, {48.0f, top + 40.0f + jolt}, {46.0f, top + 53.0f});
    Line(graphics, accent, 4.0f, {48.0f, top + 42.0f + jolt}, {65.0f, top + 51.0f + motion * 3.1f});
    Line(graphics, accent, 4.0f, {53.0f, top + 44.0f + jolt}, {71.0f, top + 51.0f - motion * 3.1f});

    Circle(graphics, kInk, {33.0f, headY - 1.0f, 32.0f, 32.0f});
    Circle(graphics, accent, {35.0f, headY + 1.0f, 28.0f, 28.0f});
    Circle(graphics, Color(254, 247, 239), {38.0f, headY + 4.0f, 22.0f, 22.0f});
    if (app.icon) {
        graphics.DrawImage(app.icon.get(), RectF(39.0f, headY + 5.0f, 20.0f, 20.0f));
    } else {
        Label(graphics, std::wstring_view(app.title).substr(0, 1), {38.0f, headY + 4.0f, 22.0f, 22.0f},
            13.0f, kInk, true, true);
    }

    RoundBox(graphics, {76.0f, top + 32.0f, 23.0f, 16.0f}, 3.0f, Color(110, 91, 151));
    Fill(graphics, kInk, {79.0f, top + 35.0f, 17.0f, 10.0f});
    Fill(graphics, boosted ? kMint : accent, {81.0f, top + 37.0f, 8.0f + motion * 3.0f, 2.0f});
    Fill(graphics, Color(130, 111, 169), {81.0f, top + 41.0f, 11.0f, 1.0f});
    Line(graphics, Color(110, 91, 151), 2.4f, {87.0f, top + 48.0f}, {87.0f, top + 54.0f});
    Line(graphics, Color(233, 215, 244), 2.0f, {59.0f, top + 54.0f}, {73.0f, top + 54.0f});
    Line(graphics, Color(164, 131, 160), 4.0f, {40.0f, top + 57.0f}, {98.0f, top + 57.0f});
}

const wchar_t* StrikeLabel(BoostResult result)
{
    switch (result) {
    case BoostResult::boosted: return L"CHÁT!  +CPU PRIORITY";
    case BoostResult::alreadyBoosted: return L"ALREADY BOOSTED";
    case BoostResult::denied: return L"PRIORITY DENIED";
    case BoostResult::restorePending: return L"RESTORE PENDING";
    case BoostResult::disabled: return L"BOOST OFF";
    default: return L"PRIORITY UNCHANGED";
    }
}

} // namespace

const OpenApp* Panel::AppAt(int index) const
{
    return index >= 0 && index < Count() ? &apps_[static_cast<size_t>(index)] : nullptr;
}

void Panel::Refresh(HWND window)
{
    const auto* selected = AppAt(selection_);
    const AppTarget previous = selected ? selected->target : AppTarget{};
    RefreshOpenApps(apps_);
    const auto match = std::ranges::find_if(apps_, [&previous](const OpenApp& app) {
        return SameProcess(app.target, previous);
    });
    selection_ = match != apps_.end() ? static_cast<int>(match - apps_.begin()) :
        std::min(std::max(0, selection_), Count() - 1);
    SetScroll(window, firstRow_);
}

void Panel::SetScroll(HWND window, int first)
{
    firstRow_ = std::clamp(first, 0, std::max(0, Count() - visibleRows));
    SCROLLINFO scroll{};
    scroll.cbSize = sizeof(scroll);
    scroll.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    scroll.nMax = std::max(0, Count() - 1);
    scroll.nPage = visibleRows;
    scroll.nPos = firstRow_;
    SetScrollInfo(window, SB_VERT, &scroll, TRUE);
    const int end = std::min(Count(), firstRow_ + visibleRows);
    for (int index = firstRow_; index < end; ++index) {
        LoadAppIcon(apps_[static_cast<size_t>(index)]);
    }
    InvalidateRect(window, nullptr, FALSE);
}

void Panel::Scroll(HWND window, int command)
{
    int first = firstRow_;
    switch (command) {
    case SB_LINEUP: --first; break;
    case SB_LINEDOWN: ++first; break;
    case SB_PAGEUP: first -= visibleRows; break;
    case SB_PAGEDOWN: first += visibleRows; break;
    case SB_TOP: first = 0; break;
    case SB_BOTTOM: first = Count(); break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK: {
        SCROLLINFO scroll{};
        scroll.cbSize = sizeof(scroll);
        scroll.fMask = SIF_TRACKPOS;
        if (GetScrollInfo(window, SB_VERT, &scroll)) {
            first = scroll.nTrackPos;
        }
        break;
    }
    default: return;
    }
    SetScroll(window, first);
}

void Panel::Wheel(HWND window, int delta)
{
    wheelRemainder_ += delta;
    const int rows = wheelRemainder_ / WHEEL_DELTA;
    wheelRemainder_ %= WHEEL_DELTA;
    if (rows) {
        SetScroll(window, firstRow_ - rows);
    }
}

void Panel::Select(HWND window, int index)
{
    if (Count() == 0) {
        return;
    }
    selection_ = std::clamp(index, 0, Count() - 1);
    if (selection_ < firstRow_) {
        SetScroll(window, selection_);
    } else if (selection_ >= firstRow_ + visibleRows) {
        SetScroll(window, selection_ - visibleRows + 1);
    } else {
        InvalidateRect(window, nullptr, FALSE);
    }
}

int Panel::HitTest(HWND window, POINT point) const
{
    RECT client{};
    GetClientRect(window, &client);
    const float scale = DpiScale(window);
    const float x = static_cast<float>(point.x) / scale;
    const float y = static_cast<float>(point.y) / scale;
    const float right = static_cast<float>(client.right) / scale - kGutter;
    if (x < kGutter || x >= right || y < kListTop || y >= kListBottom) {
        return -1;
    }
    const int row = static_cast<int>(y - kListTop);
    const int index = firstRow_ + row / kRowHeight;
    return index < Count() && row % kRowHeight < kRowHeight - 9 ? index : -1;
}

bool Panel::HitsBoostSwitch(HWND window, POINT point) const
{
    RECT client{};
    GetClientRect(window, &client);
    const float scale = DpiScale(window);
    const float fromRight = static_cast<float>(client.right - point.x) / scale;
    const float y = static_cast<float>(point.y) / scale;
    return fromRight >= 14.0f && fromRight <= 111.0f && y >= 34.0f && y <= 57.0f;
}

void Panel::Paint(HWND window, HDC dc, const PanelSettings& settings, const PanelStrike* strike)
{
    RECT client{};
    GetClientRect(window, &client);
    if (client.right <= 0 || client.bottom <= 0) {
        return;
    }
    if (!buffer_ || buffer_->GetWidth() != static_cast<UINT>(client.right) ||
        buffer_->GetHeight() != static_cast<UINT>(client.bottom)) {
        buffer_ = std::make_unique<Gdiplus::Bitmap>(static_cast<int>(client.right),
            static_cast<int>(client.bottom), PixelFormat32bppPARGB);
    }
    const float scale = DpiScale(window);
    const float w = static_cast<float>(client.right) / scale;
    const bool boostReady = settings.whip && settings.boost;
    const bool animated = settings.whip && settings.animation && !settings.reducedMotion;
    const int hover = cursor_ ? HitTest(window, *cursor_) : -1;
    {
        Graphics graphics(buffer_.get());
        graphics.Clear(kBackground);
        graphics.SetPageUnit(Gdiplus::UnitPixel);
        graphics.ScaleTransform(scale, scale);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

        Label(graphics, L"THE DESKTOP WORKROOM", {14.0f, 10.0f, w - 28.0f, 19.0f},
            10.5f, kCoral, true);
        Label(graphics, L"VineWhip", {13.0f, 24.0f, w - 136.0f, 41.0f}, 33.0f, kText, true);
        RoundBox(graphics, {w - 111.0f, 34.0f, 97.0f, 23.0f}, 11.0f,
            boostReady ? Color(40, 68, 61) : Color(57, 47, 73));
        Label(graphics, boostReady ? L"BOOST ON" : L"BOOST OFF", {w - 111.0f, 34.0f, 97.0f, 23.0f},
            10.5f, boostReady ? kMint : kMuted, true, true);
        Label(graphics, settings.whip ? L"Click a worker to crack the whip." : L"Whip Mode is paused.",
            {14.0f, 66.0f, w - 28.0f, 19.0f}, 12.5f, kMuted);

        if (Count() == 0) {
            RoundBox(graphics, {14.0f, static_cast<float>(kListTop), w - 28.0f, 101.0f}, 12.0f,
                Color(44, 36, 61));
            Label(graphics, L"No open apps found", {24.0f, 116.0f, w - 48.0f, 26.0f},
                17.0f, kText, true, true);
            Label(graphics, L"Open an app and it will appear here.", {24.0f, 148.0f, w - 48.0f, 21.0f},
                13.0f, kMuted, false, true);
        }
        const auto clipped = graphics.Save();
        graphics.SetClip(RectF(0.0f, static_cast<float>(kListTop), w,
            static_cast<float>(kListBottom - kListTop)));
        const int end = std::min(Count(), firstRow_ + visibleRows);
        float impactTop = -1.0f;
        for (int index = firstRow_; index < end; ++index) {
            const OpenApp& app = apps_[static_cast<size_t>(index)];
            const float top = static_cast<float>(kListTop + (index - firstRow_) * kRowHeight);
            const bool boosted = app.target.pid == settings.boostedPid && !settings.restorePending;
            const bool restoring = app.target.pid == settings.boostedPid && settings.restorePending;
            const bool hit = strike && SameProcess(strike->target, app.target);
            const bool focused = hover == index || (selection_ == index && GetFocus() == window);
            const bool showResult = hit && strike->result.has_value();
            const Color card = boosted ? Color(38, 65, 58) :
                focused ? Color(67, 51, 88) : Color(44, 36, 61);
            RoundBox(graphics, {14.0f, top, w - 28.0f, 80.0f}, 12.0f, card);
            DrawWorker(graphics, app, top, boosted, animated, hit ? strike->elapsedMs : -1.0f);
            Label(graphics, app.title, {120.0f, top + 14.0f, w - 147.0f, 25.0f}, 15.0f, kText, true);
            const std::wstring status = showResult ? StrikeLabel(*strike->result) :
                restoring ? L"Restore pending" : boosted ? L"Above normal  /  Fast typing" :
                L"Working  /  PID " + std::to_wstring(app.target.pid);
            Label(graphics, status, {120.0f, top + 42.0f, w - 147.0f, 20.0f},
                12.0f, boosted ? kMint : showResult ? kCoral : kMuted, showResult || boosted);
            if (hit) {
                impactTop = top;
            }
        }
        if (animated && strike && impactTop >= 0.0f && strike->elapsedMs < 500.0f) {
            DrawWhipEffect(graphics, {49.0f, impactTop + 27.0f}, strike->elapsedMs,
                settings.intensity, 0.62f, true);
        } else if (animated && hover >= 0 && cursor_) {
            const PointF point(static_cast<float>(cursor_->x) / scale, static_cast<float>(cursor_->y) / scale);
            DrawWhipEffect(graphics, point, -1.0f, settings.intensity, 0.4f, point.X < 55.0f);
        }
        graphics.Restore(clipped);
        Fill(graphics, Color(65, 53, 82), {14.0f, 371.0f, w - 28.0f, 1.0f});
        const std::wstring count = std::to_wstring(Count()) + L" open apps";
        Label(graphics, count, {14.0f, 380.0f, 120.0f, 26.0f}, 12.0f, kMuted, true);
        Label(graphics, settings.hotkeyRegistered ? L"Ctrl+Alt+B  /  Toggle boost" : L"CPU Boost  /  Tray menu",
            {w - 181.0f, 380.0f, 167.0f, 26.0f}, 11.0f, kMuted, false, true);
    }
    Graphics output(dc);
    output.SetPageUnit(Gdiplus::UnitPixel);
    output.DrawImage(buffer_.get(), 0, 0, static_cast<int>(client.right), static_cast<int>(client.bottom));
}
