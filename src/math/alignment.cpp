/**
 *  OSM
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */
#include "alignment.h"
#include <cmath>

namespace alignment {

namespace {
constexpr double PI = 3.14159265358979323846;
}

Result align(const std::vector<double> &frequency,
             const std::vector<std::complex<double>> &a,
             const std::vector<std::complex<double>> &b,
             double crossover, double maxMs)
{
    Result r;
    if (frequency.size() != a.size() || frequency.size() != b.size() || !(crossover > 0)) {
        return r;
    }

    const double low  = crossover / std::pow(2.0, 1.0 / 3.0);
    const double high = crossover * std::pow(2.0, 1.0 / 3.0);

    std::vector<size_t> bins;
    std::vector<double> weights;
    size_t nearest = 0;
    double nearestDistance = 1e300;
    double levelSum = 0, weightSum = 0;
    for (size_t i = 0; i < frequency.size(); ++i) {
        const double f = frequency[i];
        if (f <= 0) {
            continue;
        }
        const double distance = std::abs(std::log(f / crossover));
        if (distance < nearestDistance && std::abs(a[i]) > 0 && std::abs(b[i]) > 0) {
            nearestDistance = distance;
            nearest = i;
        }
        if (f < low || f > high || !(std::abs(a[i]) > 0) || !(std::abs(b[i]) > 0)) {
            continue;
        }
        bins.push_back(i);
        weights.push_back(1.0 / f);   // linear bins: equal weight per octave
        levelSum  += (20 * std::log10(std::abs(a[i]) / std::abs(b[i]))) / f;
        weightSum += 1.0 / f;
    }
    if (bins.empty()) {
        return r;
    }

    auto score = [&](double tauMs, double sign) {
        double sum = 0, total = 0;
        for (size_t k = 0; k < bins.size(); ++k) {
            const size_t i = bins[k];
            const double angle = -2 * PI * frequency[i] * tauMs / 1000.0;
            const std::complex<double> shifted = b[i] * std::polar(sign, angle);
            sum   += weights[k] * std::abs(a[i] + shifted);
            total += weights[k] * (std::abs(a[i]) + std::abs(b[i]));
        }
        return total > 0 ? sum / total : 0;
    };

    r.ok = true;
    r.currentEfficiency = score(0, 1);
    r.efficiency = r.currentEfficiency;
    r.levelDiffDb = levelSum / weightSum;
    r.phaseDiffDeg = std::arg(a[nearest] / b[nearest]) * 180.0 / PI;

    if (maxMs > 0) {
        constexpr double step = 0.01;
        const int steps = static_cast<int>(std::round(maxMs / step));
        double best = -1;
        std::vector<double> scores;
        scores.reserve(static_cast<size_t>(4 * steps + 2));
        for (int sign = 0; sign < 2; ++sign) {
            for (int s = -steps; s <= steps; ++s) {
                const double value = score(s * step, sign ? -1 : 1);
                scores.push_back(value);
                if (value > best) {
                    best = value;
                }
            }
        }
        // the best peak, unless another peak (a different cycle) is nearly as good and a
        // smaller change; only peaks count, so the answer lands on the top of one
        const int width = 2 * steps + 1;
        double bestDistance = 1e300;
        for (int sign = 0; sign < 2; ++sign) {
            for (int s = -steps; s <= steps; ++s) {
                const size_t index = static_cast<size_t>(sign * width + (s + steps));
                const double value = scores[index];
                const bool left  = s == -steps || value >= scores[index - 1];
                const bool right = s ==  steps || value >= scores[index + 1];
                if (!left || !right || value < best * 0.995) {
                    continue;
                }
                const double distance = std::abs(s * step) + (sign ? 0.05 : 0);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    r.delayMs = s * step;
                    r.invert = sign == 1;
                    r.efficiency = value;
                }
            }
        }
    }
    return r;
}

} // namespace alignment
