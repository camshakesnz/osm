// Simulation tests for ChirpAnalyzer.
// Builds a fake system (delay + EQ + reflection + room tail + mild distortion),
// plays a chirp set through it with different kinds of interference, and
// compares the analyzer's output against the known linear response.
#include "math/chirpanalyzer.h"
#include "math/chirpsignal.h"

#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using cd = std::complex<double>;
static const unsigned FS = 48000;

struct Biquad {
    double b0, b1, b2, a1, a2;
    double z1 = 0, z2 = 0;
    double operator()(double x)
    {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    cd response(double f) const
    {
        cd z = std::polar(1.0, -2 * M_PI * f / FS);
        return (b0 + b1 * z + b2 * z * z) / (1. + a1 * z + a2 * z * z);
    }
};
static Biquad peaking(double f0, double q, double gdb)
{
    double A = std::pow(10, gdb / 40), w = 2 * M_PI * f0 / FS, al = std::sin(w) / (2 * q);
    double a0 = 1 + al / A;
    return {(1 + al * A) / a0, -2 * std::cos(w) / a0, (1 - al * A) / a0, -2 * std::cos(w) / a0, (1 - al / A) / a0};
}
static Biquad highpass(double f0, double q)
{
    double w = 2 * M_PI * f0 / FS, al = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + al;
    return {(1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0, -2 * c / a0, (1 - al) / a0};
}
static Biquad lowpass(double f0, double q)
{
    double w = 2 * M_PI * f0 / FS, al = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + al;
    return {(1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0, -2 * c / a0, (1 - al) / a0};
}

struct System {
    std::vector<Biquad> eq;
    int delay;              // samples
    double reflGain;        // early reflection
    int reflDelay;
    bool roomTail;
    std::vector<double> tail;   // late reverb FIR (sparse-ish noise decay)

    cd response(double f) const
    {
        cd h = 1.;
        for (auto &b : eq) h *= b.response(f);
        cd d = std::polar(1.0, -2 * M_PI * f * delay / FS);
        cd r = 1. + reflGain * std::polar(1.0, -2 * M_PI * f * reflDelay / FS);
        return h * d * r;
    }
};

static std::vector<double> runLinear(System sys, const std::vector<float> &x)
{
    std::vector<double> y(x.size(), 0);
    std::vector<double> eqOut(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        double v = x[i];
        for (auto &b : sys.eq) v = b(v);
        eqOut[i] = v;
    }
    for (size_t i = 0; i < x.size(); ++i) {
        double v = 0;
        if (i >= (size_t)sys.delay) v += eqOut[i - sys.delay];
        if (i >= (size_t)(sys.delay + sys.reflDelay)) v += sys.reflGain * eqOut[i - sys.delay - sys.reflDelay];
        y[i] = v;
    }
    if (sys.roomTail) {
        // sparse convolution with the tail
        std::vector<double> out = y;
        for (size_t k = 0; k < sys.tail.size(); ++k) {
            double g = sys.tail[k];
            if (g == 0) continue;
            for (size_t i = k; i < x.size(); ++i) out[i] += g * eqOut[i - k];
        }
        y.swap(out);
    }
    return y;
}

// generator: N chirps with the shared period
static std::vector<float> chirps(const chirp::Params &p, double leadIn)
{
    double period = chirp::periodSeconds(p.sweepDuration, p.tailDuration);
    size_t total = (size_t)((leadIn + period * p.count + 0.5) * FS);
    std::vector<float> s(total, 0.f);
    const float gain = std::pow(10.f, -6.f / 20.f);
    for (unsigned c = 0; c < p.count; ++c) {
        size_t start = (size_t)((leadIn + c * period) * FS);
        for (size_t i = 0; i < (size_t)(p.sweepDuration * FS); ++i)
            s[start + i] = gain * chirp::sweepSample((double)i / FS, p.sweepDuration, p.startFrequency, p.endFrequency);
    }
    return s;
}

// pink-ish noise (Paul Kellet filter)
struct Pink {
    std::mt19937 rng{1234};
    std::normal_distribution<double> n{0, 1};
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    double operator()()
    {
        double w = n(rng);
        b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520; b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
        double o = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362; b6 = w * 0.115926;
        return o * 0.11;
    }
};

static double rms(const std::vector<double> &v)
{
    double s = 0; for (double x : v) s += x * x; return std::sqrt(s / v.size());
}

struct Result {
    std::vector<ChirpAnalyzer::Point> spectrum;
    std::vector<float> impulse;
    long peak;
    unsigned captured;
};

enum Noise { None, Pinkish, Transients, Babble };

static Result measure(const System &sys, chirp::Params p, Noise noise, double snrDb, bool distort, unsigned seed = 7,
                      ChirpAnalyzer::Gate gate = ChirpAnalyzer::Gate::Fixed)
{
    auto ref = chirps(p, 0.4);
    auto y = runLinear(sys, ref);
    if (distort) for (auto &v : y) v = std::tanh(0.5 * v) / 0.5;   // soft clipping driver, ~0.7 dB compression at peak

    // interference measured against the chirp level at the mic
    double sig = 0; size_t cnt = 0;
    for (size_t i = 0; i < y.size(); ++i) if (ref[i] != 0) { sig += y[i] * y[i]; ++cnt; }
    sig = std::sqrt(sig / std::max<size_t>(1, cnt));
    double target = sig * std::pow(10, -snrDb / 20);

    std::mt19937 rng(seed);
    std::vector<double> n(y.size(), 0);
    if (noise == Pinkish) {
        Pink pk; for (auto &v : n) v = pk();
    } else if (noise == Transients) {
        // claps / dropped gear: short loud bursts at random times
        std::uniform_real_distribution<double> u(0, 1);
        std::normal_distribution<double> g(0, 1);
        for (size_t i = 0; i < n.size(); ++i) {
            // one bang during the first sweep, then one every ~4 s at random
            if (i == (size_t)(0.9 * FS) || u(rng) < 0.25 / FS) {
                double amp = 1 + 3 * u(rng);
                for (size_t k = 0; k < FS / 20 && i + k < n.size(); ++k)
                    n[i + k] += amp * g(rng) * std::exp(-double(k) / (FS * 0.01));
            }
        }
    } else if (noise == Babble) {
        // band-limited, amplitude-modulated noise, a stand-in for talking/HVAC
        Pink pk; Biquad bp = peaking(500, 0.7, 12);
        std::uniform_real_distribution<double> u(0, 1);
        double env = 0.5;
        for (size_t i = 0; i < n.size(); ++i) {
            if (i % (FS / 8) == 0) env = 0.2 + u(rng);
            n[i] = bp(pk()) * env;
        }
    }
    if (noise != None) {
        double k = rms(n) > 0 ? target / rms(n) : 0;
        if (noise == Transients) k = sig * 4 / 4.0;   // bangs peak roughly 12 dB above the chirp
        for (size_t i = 0; i < y.size(); ++i) y[i] += k * n[i];
    }

    ChirpAnalyzer a;
    p.sampleRate = FS;
    a.arm(p, gate);
    for (size_t i = 0; i < y.size(); ++i) a.add((float)y[i], ref[i]);
    return {a.spectrum(), a.impulse(), a.peakDelay(), a.captured()};
}

struct Err { double magLF, magMid, magHF, phaseMid; };

static Err compare(const Result &r, const System &truth)
{
    double e[3] = {0, 0, 0}; int n[3] = {0, 0, 0};
    double ph = 0; int nph = 0;
    for (auto &p : r.spectrum) {
        double f = p.frequency;
        if (f < 25 || f > 18000) continue;
        cd h = truth.response(f);
        double d = 20 * std::log10(std::max(1e-9f, p.magnitude)) - 20 * std::log10(std::abs(h));
        int k = f < 100 ? 0 : (f < 5000 ? 1 : 2);
        e[k] += d * d; n[k]++;
        if (f >= 100 && f < 5000) {
            double dp = std::remainder(p.phase - std::arg(h), 2 * M_PI);
            ph += dp * dp; nph++;
        }
    }
    return {std::sqrt(e[0] / n[0]), std::sqrt(e[1] / n[1]), std::sqrt(e[2] / n[2]), std::sqrt(ph / nph) * 180 / M_PI};
}

static void dump(const char *name, const Result &r, const System &truth)
{
    std::string fn = std::string("out_") + name + ".csv";
    FILE *f = std::fopen(fn.c_str(), "w");
    std::fprintf(f, "frequency,magnitude_db,phase_deg,coherence,true_db,true_phase_deg\n");
    for (auto &p : r.spectrum) {
        cd h = truth.response(p.frequency);
        std::fprintf(f, "%g,%g,%g,%g,%g,%g\n", p.frequency, 20 * std::log10(std::max(1e-9f, p.magnitude)),
                     p.phase * 180 / M_PI, p.coherence, 20 * std::log10(std::abs(h)), std::arg(h) * 180 / M_PI);
    }
    std::fclose(f);
    fn = std::string("ir_") + name + ".csv";
    f = std::fopen(fn.c_str(), "w");
    std::fprintf(f, "time_ms,value\n");
    long half = (long)r.impulse.size() / 2 - 1;
    for (size_t j = 0; j < r.impulse.size(); j += 2)
        std::fprintf(f, "%g,%g\n", ((long)j - half) * 1000.0 / FS, r.impulse[j]);
    std::fclose(f);
}

int main()
{
    int failures = 0;
    auto check = [&](bool ok, const char *what) {
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok) ++failures;
    };

    chirp::Params p;
    p.startFrequency = 20; p.endFrequency = 20000; p.sweepDuration = 1.0; p.count = 1;

    // ------------------------------------------------------------ 1. clean accuracy
    System dry{{highpass(45, 0.707), peaking(1000, 2, 6), peaking(4000, 1, -4), lowpass(16000, 0.707)},
               (int)(0.0253 * FS), 0.0, 0, false, {}};
    std::printf("1. Clean system (25.3 ms delay, HPF 45 Hz, +6 dB @1k, -4 dB @4k, LPF 16k)\n");
    auto r = measure(dry, p, None, 0, false);
    auto e = compare(r, dry);
    std::printf("  peak delay %.2f ms (true 25.30 ms)\n", r.peak * 1000.0 / FS);
    std::printf("  magnitude error rms: LF %.3f dB, mid %.3f dB, HF %.3f dB; phase mid %.2f deg\n",
                e.magLF, e.magMid, e.magHF, e.phaseMid);
    check(std::abs(r.peak * 1000.0 / FS - 25.3) < 0.05, "delay found within 0.05 ms");
    check(e.magMid < 0.1 && e.magHF < 0.1 && e.magLF < 0.3, "magnitude within 0.1 dB (0.3 dB below 100 Hz)");
    check(e.phaseMid < 2, "phase within 2 degrees, 100 Hz - 5 kHz");
    dump("clean", r, dry);

    // ------------------------------------------------------------ 2. harmonic rejection
    std::printf("2. Same system driven into soft clipping\n");
    auto rd = measure(dry, p, None, 0, true);
    auto ed = compare(rd, dry);
    std::printf("  magnitude error rms: LF %.3f dB, mid %.3f dB, HF %.3f dB (includes real compression)\n",
                ed.magLF, ed.magMid, ed.magHF);
    check(ed.magMid < 1.0, "distortion products gated out of the response (mid < 1 dB)");
    {
        // energy in the harmonic zone of the ungated impulse shows the distortion is really there
        double main = 0, harm = 0; long half = (long)rd.impulse.size() / 2 - 1;
        long adv = (long)(chirp::secondHarmonicAdvance(1.0, 20, 20000) * FS);
        for (size_t j = 0; j < rd.impulse.size(); ++j) {
            long t = (long)j - half - rd.peak;
            double v = rd.impulse[j] * rd.impulse[j];
            if (std::abs(t) < 200) main += v;
            if (std::abs(t + adv) < 400) harm += v;
        }
        std::printf("  2nd harmonic product in impulse: %.1f dB below the main peak (kept out of the response)\n",
                    10 * std::log10(main / std::max(1e-30, harm)));
    }

    // ------------------------------------------------------------ 3. room + interference, 1..6 chirps
    System room = dry;
    room.reflGain = -0.5; room.reflDelay = (int)(0.008 * FS);
    std::printf("3. Noise rejection vs number of chirps (pink noise at the mic)\n");
    std::printf("   SNR   | chirps | LF err | mid err | HF err | mean coherence\n");
    double prevMid = 1e9;
    for (double snr : {10.0, 0.0, -10.0}) {
        for (unsigned c : {1u, 3u, 6u}) {
            p.count = c;
            auto rr = measure(room, p, Pinkish, snr, false, 11);
            auto ee = compare(rr, room);
            double coh = 0; int nc = 0;
            for (auto &pt : rr.spectrum) if (pt.frequency > 100 && pt.frequency < 5000) { coh += pt.coherence; ++nc; }
            std::printf("  %4.0f dB |   %u    | %5.2f  | %5.2f   | %5.2f  | %.2f\n", snr, c, ee.magLF, ee.magMid, ee.magHF, coh / nc);
            if (snr == -10.0) {
                char name[64]; std::snprintf(name, sizeof name, "pink_m10_%u", c);
                dump(name, rr, room);
                if (c > 1) check(ee.magMid < prevMid, "more chirps -> lower error at -10 dB SNR");
                prevMid = ee.magMid;
            }
            if (c == 6 && snr == 10.0) check(ee.magMid < 0.6, "fixed gate, 10 dB SNR, 6 chirps: mid error < 0.6 dB at 1/96 oct");
            check(rr.captured == c, "every chirp detected");
        }
    }

    std::printf("4. Bangs (one in the first sweep, then ~every 4 s, peaking ~12 dB over the chirp), log gate\n");
    for (unsigned c : {1u, 3u, 6u}) {
        p.count = c;
        auto rr = measure(room, p, Transients, -10, false, 3, ChirpAnalyzer::Gate::Log);
        auto clean = measure(room, p, None, 0, false, 3, ChirpAnalyzer::Gate::Log);
        double e2 = 0; int n2 = 0;
        for (size_t b = 0; b < rr.spectrum.size(); ++b) { double f = rr.spectrum[b].frequency; if (f < 100 || f > 10000) continue;
            double d = 20*std::log10(std::max(1e-9f, rr.spectrum[b].magnitude)) - 20*std::log10(std::max(1e-9f, clean.spectrum[b].magnitude)); e2 += d*d; ++n2; }
        std::printf("  %u chirps: error 100 Hz - 10 kHz %.2f dB\n", c, std::sqrt(e2 / n2));
    }
    std::printf("5. Babble / HVAC-like noise at 0 dB SNR\n");
    for (unsigned c : {1u, 6u}) {
        p.count = c;
        auto rr = measure(room, p, Babble, 0, false, 5);
        auto ee = compare(rr, room);
        std::printf("  %u chirps: LF %.2f, mid %.2f, HF %.2f dB\n", c, ee.magLF, ee.magMid, ee.magHF);
        if (c == 6) dump("babble_6", rr, room);
    }

    // ------------------------------------------------------------ 5b. log gate
    std::printf("5b. Log (LTW-style) gate, pink noise, compared with its own clean result\n");
    {
        p.count = 1;
        auto cleanLog = measure(room, p, None, 0, false, 7, ChirpAnalyzer::Gate::Log);
        dump("log_clean", cleanLog, room);
        for (double snr : {0.0, -10.0}) {
            for (unsigned c : {1u, 6u}) {
                p.count = c;
                auto rr = measure(room, p, Pinkish, snr, false, 11, ChirpAnalyzer::Gate::Log);
                double e[3] = {0, 0, 0}; int n[3] = {0, 0, 0};
                for (size_t b = 0; b < rr.spectrum.size(); ++b) {
                    double f = rr.spectrum[b].frequency;
                    if (f < 25 || f > 18000) continue;
                    double d = 20 * std::log10(std::max(1e-9f, rr.spectrum[b].magnitude)) -
                               20 * std::log10(std::max(1e-9f, cleanLog.spectrum[b].magnitude));
                    int k = f < 100 ? 0 : (f < 5000 ? 1 : 2);
                    e[k] += d * d; n[k]++;
                }
                std::printf("  %4.0f dB SNR, %u chirps: LF %.2f, mid %.2f, HF %.2f dB\n", snr, c,
                            std::sqrt(e[0] / n[0]), std::sqrt(e[1] / n[1]), std::sqrt(e[2] / n[2]));
                if (snr == -10.0) { char name[64]; std::snprintf(name, sizeof name, "log_m10_%u", c); dump(name, rr, room); }
                if (snr == 0.0 && c == 6) check(std::sqrt(e[1] / n[1]) < 1.0, "log gate, 0 dB SNR, 6 chirps: mid error < 1 dB at 1/96 oct");
            }
        }
        // timing
        p.count = 6;
        auto t0 = std::chrono::steady_clock::now();
        measure(room, p, None, 0, false, 7, ChirpAnalyzer::Gate::Log);
        auto t1 = std::chrono::steady_clock::now();
        std::printf("  6-chirp log-gate run (incl. simulation): %.2f s\n", std::chrono::duration<double>(t1 - t0).count());
    }

    // ------------------------------------------------------------ 5c. reflection louder than direct
    std::printf("5c. Reflection 6 dB louder than the direct sound (blocked line of sight)\n");
    {
        System blocked = dry;
        blocked.eq.push_back(peaking(1000, 0.1, -6));       // ~-6 dB broadband on the direct path
        blocked.reflGain = 2.0; blocked.reflDelay = (int)(0.006 * FS);
        p.count = 3;
        auto rb = measure(blocked, p, Pinkish, 10, false, 21, ChirpAnalyzer::Gate::Log);
        std::printf("  delay found %.2f ms (direct 25.30 ms, reflection 31.30 ms)\n", rb.peak * 1000.0 / FS);
        check(std::abs(rb.peak * 1000.0 / FS - 25.3) < 0.2, "window and delay lock to the direct arrival, not the reflection");
    }

    // ------------------------------------------------------------ 6. other sweep settings
    std::printf("6. Short 0.5 s sweep, 40 Hz - 18 kHz, 3 chirps, clean\n");
    chirp::Params q = p; q.sweepDuration = 0.5; q.startFrequency = 40; q.endFrequency = 18000; q.count = 3;
    auto rq = measure(dry, q, None, 0, false);
    auto eq = compare(rq, dry);
    std::printf("  mid %.3f dB, HF %.3f dB, captured %u\n", eq.magMid, eq.magHF, rq.captured);
    check(eq.magMid < 0.1 && rq.captured == 3, "short sweep accurate, all chirps detected");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
