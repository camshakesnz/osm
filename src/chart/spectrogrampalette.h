/**
 *  OSM
 *  Copyright (C) 2026

 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef CHART_SPECTROGRAMPALETTE_H
#define CHART_SPECTROGRAMPALETTE_H

#include <array>
#include <cstddef>

namespace Chart {
namespace SpectrogramPalette {

enum Type {
    Heat    = 0,    //!< nothing below min, then blue, green, yellow, red, white when hot
    Classic = 1     //!< the original OSM colours: blue, green, red
};

struct Rgba {
    float r, g, b, a;
};

struct Stop {
    float position;
    float r, g, b;
};

inline Rgba mix(const Stop &first, const Stop &second, float position)
{
    float k = (second.position > first.position) ?
              (position - first.position) / (second.position - first.position) : 0.f;
    k = k < 0.f ? 0.f : (k > 1.f ? 1.f : k);
    return {
        first.r + k * (second.r - first.r),
        first.g + k * (second.g - first.g),
        first.b + k * (second.b - first.b),
        1.f
    };
}

/**
 * Colour of a spectrogram cell at `value` dB.
 * min: lowest level that is drawn, mid: where it turns green, max: top of the scale.
 */
inline Rgba color(float value, float min, float mid, float max, int type)
{
    constexpr float floor = -140.f;
    if (!(value > floor)) {
        value = floor;
    }
    if (value > max) {
        value = max;
    }
    if (mid <= min) {
        mid = min + 1.f;
    }
    if (max <= mid) {
        max = mid + 1.f;
    }

    if (type == Classic) {
        static constexpr Stop blue  {0.f, 0x21 / 255.f, 0x96 / 255.f, 0xF3 / 255.f};
        static constexpr Stop green {1.f, 0x8B / 255.f, 0xC3 / 255.f, 0x4A / 255.f};
        static constexpr Stop red   {2.f, 0xF4 / 255.f, 0x43 / 255.f, 0x36 / 255.f};
        if (value < min) {
            return {blue.r, blue.g, blue.b, (value - floor) / (min - floor)};
        }
        if (value < mid) {
            return mix(blue, green, (value - min) / (mid - min));
        }
        return mix(green, red, 1.f + (value - mid) / (max - mid));
    }

    // Heat: nothing below min (a short fade in over 10 dB), deep blue at min,
    // green at mid, then yellow, red and white at max.
    static constexpr std::array<Stop, 6> stops {{
        {0.00f, 0.05f, 0.08f, 0.55f},   // min: deep blue
        {0.50f, 0.10f, 0.35f, 1.00f},   //      blue
        {1.00f, 0.10f, 0.80f, 0.15f},   // mid: green
        {1.35f, 0.95f, 0.88f, 0.10f},   //      yellow
        {1.65f, 1.00f, 0.16f, 0.08f},   //      red
        {2.00f, 1.00f, 1.00f, 1.00f},   // max: white
    }};
    constexpr float fade = 10.f;
    if (value < min) {
        float alpha = (value - (min - fade)) / fade;
        alpha = alpha < 0.f ? 0.f : alpha;
        return {stops[0].r, stops[0].g, stops[0].b, alpha};
    }
    float position = (value < mid) ? (value - min) / (mid - min)
                                   : 1.f + (value - mid) / (max - mid);
    for (std::size_t i = 1; i < stops.size(); ++i) {
        if (position <= stops[i].position) {
            return mix(stops[i - 1], stops[i], position);
        }
    }
    return {1.f, 1.f, 1.f, 1.f};
}

} // namespace SpectrogramPalette
} // namespace Chart

#endif // CHART_SPECTROGRAMPALETTE_H
