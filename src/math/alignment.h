/**
 *  OSM
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */
#ifndef MATH_ALIGNMENT_H
#define MATH_ALIGNMENT_H

#include <complex>
#include <vector>

namespace alignment {

struct Result {
    bool   ok = false;
    double delayMs = 0;             //!< extra delay to add to B (negative: B earlier)
    bool   invert = false;          //!< flip B's polarity as well
    double efficiency = 0;          //!< 0..1: |A+B| / (|A|+|B|) around the crossover, after the change
    double currentEfficiency = 0;   //!< the same, as things are now
    double phaseDiffDeg = 0;        //!< phase of A minus phase of B at the crossover, now (-180..180)
    double levelDiffDb = 0;         //!< level of A minus level of B around the crossover
};

/**
 * Best delay (and polarity) for B so that A + B sums best around `crossover`
 * (one third of an octave either side, each octave weighted the same).
 * `a` and `b` are complex responses on the same `frequency` grid.
 * Searches ±maxMs in 0.01 ms steps; maxMs = 0 only reports the current state.
 * Among near-equal results (within 0.5 %) the smallest delay change wins, so it
 * does not jump a whole cycle for nothing.
 */
Result align(const std::vector<double> &frequency,
             const std::vector<std::complex<double>> &a,
             const std::vector<std::complex<double>> &b,
             double crossover, double maxMs);

} // namespace alignment

#endif // MATH_ALIGNMENT_H
