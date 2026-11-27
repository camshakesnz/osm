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
    if (!(value > floor + 1.f)) {
        return {0.f, 0.f, 0.f, 0.f}; // nothing there (silence): no colour at all
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
        {0.00f, 0.02f, 0.03f, 0.32f},   // min: deep navy
        {0.50f, 0.05f, 0.18f, 0.78f},   //      blue
        {1.00f, 0.05f, 0.60f, 0.10f},   // mid: green
        {1.35f, 0.82f, 0.72f, 0.04f},   //      yellow
        {1.65f, 0.90f, 0.14f, 0.04f},   //      red
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

/**
 * Automatic levels: follow the loudest part of the recent spectrum, so the colours
 * suit whatever is playing (like Smaart's dynamic thresholds). Green sits just below
 * the recent peak, red above it, white 12 dB above it and nothing below `span`.
 */
struct AutoRange {
    float peak = -1000.f;

    //! call once per row with the loudest cell of that row
    void update(float rowPeak, float seconds)
    {
        constexpr float attack  = 15.f; // dB per second: sudden peaks show hot before it catches up
        constexpr float release = 4.f;  // dB per second
        if (!(rowPeak > -130.f)) {
            rowPeak = -130.f; // silence: let the peak fall
        }
        const float dt = seconds > 0.f ? (seconds < 1.f ? seconds : 1.f) : 0.f;
        if (peak < -500.f) {
            peak = rowPeak; // first row
        } else if (rowPeak > peak) {
            float risen = peak + attack * dt;
            // never more than 12 dB behind: a big jump (e.g. the generator coming on)
            // flashes white for a moment instead of for seconds
            if (risen < rowPeak - 12.f) {
                risen = rowPeak - 12.f;
            }
            peak = risen < rowPeak ? risen : rowPeak;
        } else {
            const float fallen = peak - release * dt;
            peak = fallen > rowPeak ? fallen : rowPeak;
        }
    }

    void thresholds(float span, int &min, int &mid, int &max) const
    {
        if (!(peak > -200.f)) {
            return; // nothing yet: keep the manual levels
        }
        if (span < 10.f) {
            span = 10.f;
        }
        // don't chase silence or a quiet noise floor all the way down: it should stay dark
        const float top = peak > -80.f ? peak : -80.f;
        // steady levels sit in the greens and yellows, anything above the recent peak
        // goes red, and white 12 dB above it
        max = static_cast<int>(top + 12.f);
        mid = static_cast<int>(top - 6.f);
        min = static_cast<int>(top - span);
    }
};

} // namespace SpectrogramPalette
} // namespace Chart

#endif // CHART_SPECTROGRAMPALETTE_H
