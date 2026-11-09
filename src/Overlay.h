#pragma once

namespace Gdiplus {
class Graphics;
class PointF;
}

enum class Intensity { low, normal, crazy };

void DrawWhipEffect(Gdiplus::Graphics& graphics, Gdiplus::PointF anchor, float elapsedMs,
                    Intensity intensity, float scale, bool mirror);
