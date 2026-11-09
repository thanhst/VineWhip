#include "Overlay.h"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <cmath>
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

float Clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float EaseOut(float progress)
{
    const float remaining = 1.0f - Clamp01(progress);
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
    float progress = Clamp01(ms / 100.0f);
    progress *= progress;
    float tipArc = 0.0f;
    if (ms >= 220.0f) {
        start = &kStrike;
        end = &kIdle;
        const float t = Clamp01((ms - 220.0f) / 280.0f);
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
    return Color(static_cast<BYTE>(color.GetAlpha() * Clamp01(opacity)),
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
    const float opacity = 1.0f - Clamp01(age / 265.0f);
    if (age < 100.0f) {
        const float radius = 10.0f + 27.0f * EaseOut(age / 100.0f);
        std::array<PointF, 18> star;
        for (size_t i = 0; i < star.size(); ++i) {
            const float angle = static_cast<float>(i) * std::numbers::pi_v<float> / 9.0f;
            const float r = radius * (i % 2 == 0 ? 1.0f : 0.55f);
            star[i] = {r * std::cos(angle), r * std::sin(angle)};
        }
        const float flash = 1.0f - Clamp01(age / 100.0f);
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

} // namespace

void DrawWhipEffect(Gdiplus::Graphics& graphics, Gdiplus::PointF anchor, float elapsedMs,
                    Intensity intensity, float scale, bool mirror)
{
    const auto saved = graphics.Save();
    graphics.TranslateTransform(anchor.X, anchor.Y);
    graphics.ScaleTransform(mirror ? -scale : scale, scale);
    if (elapsedMs >= 112.0f && elapsedMs < 220.0f && intensity != Intensity::low) {
        DrawWhip(graphics, WhipPose(elapsedMs - 22.0f), 0.28f, true);
        if (intensity == Intensity::crazy) {
            DrawWhip(graphics, WhipPose(elapsedMs - 39.0f), 0.18f, true);
        }
    }
    DrawWhip(graphics, WhipPose(elapsedMs), 1.0f, false);
    if (elapsedMs >= 220.0f) {
        const int sparks = intensity == Intensity::crazy ? 20 : intensity == Intensity::normal ? 11 : 5;
        DrawBurst(graphics, elapsedMs, sparks);
    }
    graphics.Restore(saved);
}
