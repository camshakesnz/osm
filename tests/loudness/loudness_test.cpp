// Loudness meter checks against EBU Tech 3341 (loudness) and Tech 3342 (range).
//   g++ -O2 -std=c++17 -Isrc tests/loudness/loudness_test.cpp src/math/loudness.cpp -o loudness_test && ./loudness_test
#include "math/loudness.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

static const double PI = 3.14159265358979323846;
static int failures = 0;

static void check(bool ok, const char *what, double value, double expected, double tol)
{
    std::printf("  [%s] %-58s %8.2f (expected %.1f +-%.1f)\n", ok ? "PASS" : "FAIL", what, value, expected, tol);
    if (!ok) ++failures;
}
static void expect(const char *what, double value, double expected, double tol)
{
    check(std::abs(value - expected) <= tol, what, value, expected, tol);
}

struct Segment { double seconds, dbfs, frequency; bool rightToo; };

// play segments of sine into the meter, calling process() as OSM does (every ~80 ms)
static LoudnessMeter::Values run(LoudnessMeter &m, unsigned fs, const std::vector<Segment> &segments,
                                 std::function<void(const LoudnessMeter::Values &)> each = nullptr)
{
    unsigned long n = 0;
    const unsigned processEvery = fs * 8 / 100;
    for (const auto &s : segments) {
        const double a = s.dbfs <= -200 ? 0 : std::pow(10., s.dbfs / 20.);
        const unsigned long count = static_cast<unsigned long>(s.seconds * fs);
        for (unsigned long i = 0; i < count; ++i, ++n) {
            const float v = static_cast<float>(a * std::sin(2 * PI * s.frequency * n / fs));
            m.add(v, s.rightToo ? v : 0.f);
            if (n % processEvery == 0) {
                m.process();
                if (each) each(m.values());
            }
        }
    }
    m.process();
    return m.values();
}

int main()
{
    for (unsigned fs : {48000u, 44100u, 96000u}) {
        std::printf("Sample rate %u Hz\n", fs);
        {
            LoudnessMeter m; m.setSampleRate(fs);
            auto v = run(m, fs, {{20, -23, 1000, true}});
            expect("3341 #1: stereo 1 kHz -23 dBFS, momentary", v.momentary, -23, 0.1);
            expect("3341 #1: short-term", v.shortTerm, -23, 0.1);
            expect("3341 #1: integrated", v.integrated, -23, 0.1);
        }
        {
            LoudnessMeter m; m.setSampleRate(fs);
            auto v = run(m, fs, {{20, -33, 1000, true}});
            expect("3341 #2: stereo 1 kHz -33 dBFS, integrated", v.integrated, -33, 0.1);
        }
        {
            LoudnessMeter m; m.setSampleRate(fs);
            auto v = run(m, fs, {{10, -36, 1000, true}, {60, -23, 1000, true}, {10, -36, 1000, true}});
            expect("3341 #3: -36/-23/-36 dBFS, relative gate, integrated", v.integrated, -23, 0.1);
        }
        {
            LoudnessMeter m; m.setSampleRate(fs);
            auto v = run(m, fs, {{10, -72, 1000, true}, {10, -36, 1000, true}, {60, -23, 1000, true},
                                 {10, -36, 1000, true}, {10, -72, 1000, true}});
            expect("3341 #4: with -72 dBFS (absolute gate), integrated", v.integrated, -23, 0.1);
        }
        if (fs == 48000) {
            LoudnessMeter m; m.setSampleRate(fs);
            auto v = run(m, fs, {{20, -20, 1000, true}, {20, -30, 1000, true}});
            expect("3342 #1: -20 then -30 dBFS, loudness range LU", v.range, 10, 1);
            LoudnessMeter m2; m2.setSampleRate(fs);
            v = run(m2, fs, {{20, -20, 1000, true}, {20, -15, 1000, true}});
            expect("3342 #2: -20 then -15 dBFS, loudness range LU", v.range, 5, 1);
            LoudnessMeter m3; m3.setSampleRate(fs);
            v = run(m3, fs, {{20, -40, 1000, true}, {20, -20, 1000, true}});
            expect("3342 #3: -40 then -20 dBFS, loudness range LU", v.range, 20, 1);
        }
        {
            // short-term reacts within 3 s, momentary within 0.4 s
            LoudnessMeter m; m.setSampleRate(fs);
            run(m, fs, {{5, -40, 1000, true}});
            auto v = run(m, fs, {{0.5, -20, 1000, true}});
            expect("momentary 0.5 s after a step to -20 dBFS", v.momentary, -20, 0.2);
        }
    }

    std::printf("Mono and peaks (48 kHz)\n");
    {
        LoudnessMeter m; m.setMono(true);
        auto v = run(m, 48000, {{10, -23, 1000, false}});
        expect("mono 1 kHz -23 dBFS, integrated (one channel)", v.integrated, -26, 0.1);
        LoudnessMeter s;
        v = run(s, 48000, {{10, -23, 1000, false}});
        expect("same signal on left only, stereo mode", v.integrated, -26, 0.1);
    }
    {
        LoudnessMeter m;
        auto v = run(m, 48000, {{2, -6, 997, true}});
        expect("sample peak, 997 Hz at -6 dBFS", v.peakLeft, -6, 0.05);
        expect("true peak, 997 Hz at -6 dBFS", v.maxTruePeakLeft, -6, 0.1);
    }
    {
        // fs/4 sine at 45 deg phase: every sample sits 3 dB under the true peak
        LoudnessMeter m;
        const double a = std::pow(10., -6. / 20.);
        for (unsigned long n = 0; n < 48000; ++n) {
            const float v = static_cast<float>(a * std::sin(2 * PI * 12000. * n / 48000. + PI / 4));
            m.add(v, v);
            if (n % 3840 == 0) m.process();
        }
        m.process();
        auto v = m.values();
        expect("12 kHz at 45 deg: sample peak under-reads", v.peakLeft, -9.01, 0.05);
        expect("12 kHz at 45 deg: true peak", v.maxTruePeakLeft, -6, 0.5);
    }
    {
        LoudnessMeter m;
        run(m, 48000, {{5, -23, 1000, true}});
        m.reset();
        m.process();
        auto v = m.values();
        check(std::isnan(v.integrated) && v.maxTruePeakLeft < -150, "reset clears integrated and true peak", 0, 0, 0);
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
