// g++ -std=c++17 -O2 -I../../src tests: synthetic main + sub around a crossover
#include "math/alignment.h"
#include <cassert>
#include <cmath>
#include <cstdio>

int main()
{
    const double PI = 3.14159265358979323846;
    std::vector<double> f;
    std::vector<std::complex<double>> a, b;
    // 2nd-order-ish low/high pass, sub arrives 3.2 ms late and inverted
    for (int i = 1; i < 8192; ++i) {
        double freq = i * 48000.0 / 16384;
        f.push_back(freq);
        std::complex<double> s(0, freq / 90.0);
        std::complex<double> hp = s * s / (s * s + std::sqrt(2.0) * s + 1.0);
        std::complex<double> lp = 1.0 / (s * s + std::sqrt(2.0) * s + 1.0);
        a.push_back(hp);
        b.push_back(-lp * std::polar(1.0, -2 * PI * freq * 3.2e-3));
    }
    auto r = alignment::align(f, a, b, 90, 10);
    std::printf("ok %d delay %.2f invert %d eff %.3f (was %.3f) phase %.1f level %.2f\n",
                r.ok, r.delayMs, r.invert, r.efficiency, r.currentEfficiency, r.phaseDiffDeg, r.levelDiffDb);
    assert(r.ok);
    assert(r.efficiency > r.currentEfficiency);
    assert(r.efficiency > 0.9);
    // applying the result should leave nothing to gain
    std::vector<std::complex<double>> b2;
    for (size_t i = 0; i < f.size(); ++i) {
        b2.push_back(b[i] * std::polar(r.invert ? -1.0 : 1.0, -2 * PI * f[i] * r.delayMs / 1000));
    }
    auto r2 = alignment::align(f, a, b2, 90, 10);
    std::printf("after: delay %.2f invert %d eff %.3f\n", r2.delayMs, r2.invert, r2.currentEfficiency);
    assert(std::abs(r2.delayMs) < 0.05 && !r2.invert);
    // the same response, 2.5 ms late: should come back by exactly 2.5 ms
    std::vector<std::complex<double>> late;
    for (size_t i = 0; i < f.size(); ++i) {
        late.push_back(a[i] * std::polar(1.0, -2 * PI * f[i] * 2.5e-3));
    }
    auto r3 = alignment::align(f, a, late, 90, 10);
    std::printf("late copy: delay %.2f invert %d eff %.3f\n", r3.delayMs, r3.invert, r3.efficiency);
    assert(std::abs(r3.delayMs + 2.5) < 0.015 && !r3.invert);
    std::puts("alignment tests passed");
    return 0;
}
