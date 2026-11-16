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
#ifndef MATH_CHIRPSIGNAL_H
#define MATH_CHIRPSIGNAL_H

#include <algorithm>
#include <cmath>

/**
 * Shared definition of the chirp stimulus.
 *
 * Both the generator (which plays the chirps) and the analyzer (which captures
 * them) use these helpers so that their timing always agrees, even when the
 * input and output devices run at different sample rates.
 *
 * One chirp "period" is:
 *   [ log sweep : sweepDuration ][ tail : room decay ][ guard ]
 *
 * The analyzer captures preRoll + sweep + tail after it detects each chirp
 * on the reference channel; the guard leaves room for detection jitter and
 * system latency before the next chirp starts.
 */
namespace chirp {

constexpr unsigned int MIN_COUNT = 1;
constexpr unsigned int MAX_COUNT = 6;

//! silence after each sweep that is captured as part of the response
constexpr double DEFAULT_TAIL = 1.5;    // seconds
//! extra silence between chirps that is not captured
constexpr double GUARD = 0.35;          // seconds
//! audio kept from before the detected onset
constexpr double PRE_ROLL = 0.1;        // seconds

struct Params {
    unsigned int sampleRate     = 48000;
    double       startFrequency = 20.;
    double       endFrequency   = 20000.;
    double       sweepDuration  = 1.;   // seconds
    double       tailDuration   = DEFAULT_TAIL;
    unsigned int count          = 1;
};

inline unsigned int clampCount(int count)
{
    return static_cast<unsigned int>(std::clamp<int>(count, MIN_COUNT, MAX_COUNT));
}

inline double periodSeconds(double sweepDuration, double tailDuration = DEFAULT_TAIL)
{
    return sweepDuration + tailDuration + GUARD;
}

inline double fadeInSeconds(double sweepDuration)
{
    return std::min(0.05, 0.1 * sweepDuration);
}

inline double fadeOutSeconds(double sweepDuration)
{
    return std::min(0.005, 0.05 * sweepDuration);
}

/**
 * Exponential (log) sine sweep, Farina style, with short half-Hann fades.
 * @param t seconds since the start of the sweep
 * @return sample value in [-1, 1], zero outside [0, T)
 */
inline float sweepSample(double t, double T, double f1, double f2)
{
    if (t < 0 || t >= T || f1 <= 0 || f2 <= f1) {
        return 0.f;
    }
    const double r = std::log(f2 / f1);
    const double phase = 2. * M_PI * f1 * T / r * (std::exp(r * t / T) - 1.);
    double env = 1.;
    const double fi = fadeInSeconds(T), fo = fadeOutSeconds(T);
    if (t < fi) {
        env = 0.5 * (1. - std::cos(M_PI * t / fi));
    } else if (t > T - fo) {
        env = 0.5 * (1. - std::cos(M_PI * (T - t) / fo));
    }
    return static_cast<float>(env * std::sin(phase));
}

//! time between the main impulse and the 2nd harmonic product in the deconvolved response
inline double secondHarmonicAdvance(double T, double f1, double f2)
{
    return T * std::log(2.) / std::log(f2 / f1);
}

} // namespace chirp

#endif // MATH_CHIRPSIGNAL_H
