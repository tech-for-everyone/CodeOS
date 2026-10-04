// Rasterising a captured engine draw command into our Surface.
//
// The engine's paint path ends in Skia/Rosen, which is absent here, so the
// geometry the engine's paint code emits is drawn by us. This header is shared by
// paint_probe.cpp (the headless Stage 2b proof) and window_demo.cpp (the live
// window), so both draw the engine's draw command the same way.
//
// StampLine draws a thick, square-ended stroke through the segment. Thickness is a
// parameter because the engine's pen is a stateless mock (testing_pen.h): the
// width a painter set on a pen never reaches the canvas, so the caller supplies
// the stroke width it configured.

#ifndef ACE_HOST_PAINT_RASTER_HPP
#define ACE_HOST_PAINT_RASTER_HPP

#include <cstdint>
#include <cstdlib>

#include "ace_scene.hpp"
#include "paint_capture.hpp"

namespace acehost {

// Rasterise a captured line as a thick, square-ended stroke.
inline void StampLine(Surface& surface, const LineOp& line, uint32_t color, int thickness)
{
    int x0 = static_cast<int>(line.x0);
    int y0 = static_cast<int>(line.y0);
    const int x1 = static_cast<int>(line.x1);
    const int y1 = static_cast<int>(line.y1);
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    const int lo = -(thickness / 2);
    const int hi = thickness + lo;
    for (;;) {
        for (int oy = lo; oy < hi; ++oy) {
            for (int ox = lo; ox < hi; ++ox) {
                const int px = x0 + ox;
                const int py = y0 + oy;
                if (px >= 0 && px < surface.Width() && py >= 0 && py < surface.Height()) {
                    surface.Data()[static_cast<size_t>(py) * surface.Width() + px] = color;
                }
            }
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

} // namespace acehost

#endif // ACE_HOST_PAINT_RASTER_HPP
