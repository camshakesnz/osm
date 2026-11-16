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
#include "chirpanalyzer.h"

#include <algorithm>
#include <cmath>

namespace {
//! a chirp is detected when the reference exceeds both of these
constexpr double ABSOLUTE_THRESHOLD = 1e-3;     // -60 dBFS
constexpr double NOISE_MARGIN = 100.;           // +20 dB over the reference noise floor (power)
//! bins with less stimulus energy than this (relative to the max) are not used
constexpr double STIMULUS_FLOOR = 1e-9;         // -90 dB
//! log gate: cycles kept at 20 Hz, growing to ~88 at 20 kHz (matches OSM's LTW mode)
constexpr double LOG_GATE_CYCLES = 28.;
constexpr double LOG_GATE_SLOPE = 0.1667;
constexpr double LOG_GATE_MIN = 0.003;          // seconds
//! a chirp is left out of a band when it is this far from the others (in MADs)
constexpr double OUTLIER_LIMIT = 4.;
//! where the main arrival is searched, relative to the reference
constexpr double PEAK_SEARCH_EARLY = 0.02;      // seconds before
constexpr double PEAK_SEARCH_LATE = 0.75;       // seconds after (~250 m, or DSP latency)
//! first arrival: earliest point within the search window above -15 dB of the peak
constexpr double FIRST_ARRIVAL_THRESHOLD = 0.178;
constexpr double FIRST_ARRIVAL_SEARCH = 0.1;    // seconds before the peak
//! the gate is re-centred when the averaged peak moves more than this
constexpr double RECENTER_LIMIT = 0.001;        // seconds

double median(std::vector<double> v)
{
    if (v.empty()) {
        return 0;
    }
    const size_t m = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + m, v.end());
    double r = v[m];
    if (v.size() % 2 == 0) {
        r = 0.5 * (r + *std::max_element(v.begin(), v.begin() + m));
    }
    return r;
}
}

// ---------------------------------------------------------------- FFT

void ChirpAnalyzer::FFT::resize(unsigned int n)
{
    if (n == m_n) {
        return;
    }
    m_n = n;
    m_rev.assign(n, 0);
    unsigned int bits = 0;
    while ((1u << bits) < n) {
        ++bits;
    }
    for (unsigned int i = 0; i < n; ++i) {
        unsigned int r = 0;
        for (unsigned int b = 0; b < bits; ++b) {
            if (i & (1u << b)) {
                r |= 1u << (bits - 1 - b);
            }
        }
        m_rev[i] = r;
    }
    m_twiddle.resize(n / 2);
    for (unsigned int i = 0; i < n / 2; ++i) {
        m_twiddle[i] = std::polar(1.0, -2. * M_PI * i / n);
    }
}

void ChirpAnalyzer::FFT::run(std::vector<cd> &a, bool inverse) const
{
    const unsigned int n = m_n;
    for (unsigned int i = 0; i < n; ++i) {
        if (i < m_rev[i]) {
            std::swap(a[i], a[m_rev[i]]);
        }
    }
    for (unsigned int len = 2; len <= n; len <<= 1) {
        const unsigned int half = len / 2, step = n / len;
        for (unsigned int i = 0; i < n; i += len) {
            for (unsigned int j = 0; j < half; ++j) {
                cd w = m_twiddle[j * step];
                if (inverse) {
                    w = std::conj(w);
                }
                const cd u = a[i + j];
                const cd v = a[i + j + half] * w;
                a[i + j] = u + v;
                a[i + j + half] = u - v;
            }
        }
    }
    if (inverse) {
        const double k = 1. / n;
        for (auto &v : a) {
            v *= k;
        }
    }
}

void ChirpAnalyzer::FFT::forward(std::vector<cd> &a) const
{
    run(a, false);
}

void ChirpAnalyzer::FFT::inverse(std::vector<cd> &a) const
{
    run(a, true);
}

// ---------------------------------------------------------------- setup

void ChirpAnalyzer::arm(const chirp::Params &params, Gate gate)
{
    m_params = params;
    m_gate = gate;
    m_params.count = chirp::clampCount(static_cast<int>(params.count));
    if (m_params.endFrequency <= m_params.startFrequency) {
        m_params.endFrequency = m_params.startFrequency * 2;
    }
    m_params.endFrequency = std::min(m_params.endFrequency, 0.5 * m_params.sampleRate);
    prepare();
    m_state = State::Armed;
}

void ChirpAnalyzer::disarm()
{
    m_state = State::Idle;
}

void ChirpAnalyzer::prepare()
{
    const double fs = m_params.sampleRate;
    m_captured = 0;
    m_preRoll = static_cast<unsigned int>(std::lround(chirp::PRE_ROLL * fs));
    m_captureLength = m_preRoll +
                      static_cast<unsigned int>(std::ceil((m_params.sweepDuration + m_params.tailDuration) * fs));
    m_fftSize = 1;
    while (m_fftSize < m_captureLength) {
        m_fftSize <<= 1;
    }
    m_fft.resize(m_fftSize);

    m_ringData.assign(m_preRoll, 0.f);
    m_ringRef.assign(m_preRoll, 0.f);
    m_ringPos = 0;
    m_ringFill = 0;
    m_noiseSquared = 0;
    m_noiseAlpha = 1. - std::exp(-1. / (0.2 * fs));

    m_capData.clear();
    m_capRef.clear();
    m_capData.reserve(m_captureLength);
    m_capRef.reserve(m_captureLength);

    const unsigned int bins = m_fftSize / 2 + 1;
    m_syy.assign(bins, 0.);
    m_work.assign(m_fftSize, 0.);
    m_work2.assign(m_fftSize, 0.);
    m_ir.assign(m_fftSize, 0.);
    m_irs.clear();
    m_gated.clear();
    m_gateCenter = 0;

    // keep the gate clear of the 2nd harmonic product, at most 20 ms
    const double advance = chirp::secondHarmonicAdvance(m_params.sweepDuration,
                                                        m_params.startFrequency, m_params.endFrequency);
    m_preGate = std::clamp(0.5 * advance, 0.001, 0.02);

    buildBandLimit();
    buildBands();
    m_spectrum.assign(m_bands.size(), {});
    for (size_t i = 0; i < m_bands.size(); ++i) {
        m_spectrum[i].frequency = static_cast<float>(m_bands[i].frequency);
    }
    m_impulse.assign(std::min(m_fftSize, MAX_IMPULSE_SIZE), 0.f);
    m_peak = 0;
}

void ChirpAnalyzer::buildBands()
{
    m_bands.clear();
    const double fs = m_params.sampleRate;
    const double df = fs / m_fftSize;
    const double fMax = std::min(m_params.endFrequency, 0.5 * fs - df);
    const unsigned int first = std::max(1u, static_cast<unsigned int>(std::ceil(m_params.startFrequency / df)));
    const unsigned int last = static_cast<unsigned int>(std::floor(fMax / df));
    const double ratio = std::pow(2., 1. / POINTS_PER_OCTAVE);

    const double pre = m_preGate * fs;
    const unsigned int maxGate = static_cast<unsigned int>(
                                     std::max(1., std::min(m_params.tailDuration * fs, m_fftSize / 2. - pre - 1.)));
    auto gateFor = [&](double f) -> unsigned int {
        if (m_gate == Gate::Fixed)
        {
            return maxGate;
        }
        const double cycles = LOG_GATE_CYCLES * std::pow(std::max(f, 20.) / 20., LOG_GATE_SLOPE);
        const double seconds = std::max(LOG_GATE_MIN, cycles / f);
        return std::min(maxGate, static_cast<unsigned int>(seconds * fs));
    };

    unsigned int bin = first;
    // raw bins while they are coarser than the band width
    while (bin <= last && df >= bin * df * (ratio - 1.)) {
        m_bands.push_back({bin, bin, bin * df, gateFor(bin * df)});
        ++bin;
    }
    // then log spaced bands
    while (bin <= last) {
        const double edge = bin * df * ratio;
        unsigned int end = bin;
        while (end + 1 <= last && (end + 1) * df < edge) {
            ++end;
        }
        double f = 0;
        for (unsigned int i = bin; i <= end; ++i) {
            f += i * df;
        }
        f /= (end - bin + 1);
        m_bands.push_back({bin, end, f, gateFor(f)});
        bin = end + 1;
    }
    m_firstBin = m_bands.empty() ? 0 : m_bands.front().first;
    m_lastBin  = m_bands.empty() ? 0 : m_bands.back().last;
}

// ---------------------------------------------------------------- capture

bool ChirpAnalyzer::add(float data, float reference)
{
    switch (m_state) {
    case State::Idle:
    case State::Complete:
        return false;

    case State::Armed: {
        const double r2 = static_cast<double>(reference) * reference;
        const double threshold = std::max(ABSOLUTE_THRESHOLD * ABSOLUTE_THRESHOLD, NOISE_MARGIN * m_noiseSquared);
        if (r2 > threshold && m_ringFill > 0) {
            beginCapture();
            m_capData.push_back(data);
            m_capRef.push_back(reference);
            m_state = State::Capturing;
            return false;
        }
        m_noiseSquared += m_noiseAlpha * (r2 - m_noiseSquared);
        if (m_preRoll) {
            m_ringData[m_ringPos] = data;
            m_ringRef[m_ringPos] = reference;
            m_ringPos = (m_ringPos + 1) % m_preRoll;
            m_ringFill = std::min(m_ringFill + 1, m_preRoll);
        }
        return false;
    }

    case State::Capturing:
        m_capData.push_back(data);
        m_capRef.push_back(reference);
        if (m_capData.size() >= m_captureLength) {
            process();
            m_capData.clear();
            m_capRef.clear();
            m_ringFill = 0;
            m_ringPos = 0;
            m_state = (m_captured >= m_params.count) ? State::Complete : State::Armed;
            return true;
        }
        return false;
    }
    return false;
}

void ChirpAnalyzer::beginCapture()
{
    m_capData.clear();
    m_capRef.clear();
    // the pre-roll ring in chronological order, zeros where it was not filled yet
    const unsigned int missing = m_preRoll - m_ringFill;
    for (unsigned int i = 0; i < missing; ++i) {
        m_capData.push_back(0.f);
        m_capRef.push_back(0.f);
    }
    const unsigned int start = (m_ringPos + m_preRoll - m_ringFill) % std::max(1u, m_preRoll);
    for (unsigned int i = 0; i < m_ringFill; ++i) {
        const unsigned int k = (start + i) % m_preRoll;
        m_capData.push_back(m_ringData[k]);
        m_capRef.push_back(m_ringRef[k]);
    }
}

// ---------------------------------------------------------------- processing

void ChirpAnalyzer::process()
{
    const unsigned int N = m_fftSize;
    const unsigned int bins = N / 2 + 1;

    // deconvolve this chirp: h = IFFT(Y / X)
    for (unsigned int i = 0; i < N; ++i) {
        m_work[i]  = i < m_capRef.size()  ? cd(m_capRef[i], 0.) : cd();
        m_work2[i] = i < m_capData.size() ? cd(m_capData[i], 0.) : cd();
    }
    m_fft.forward(m_work);
    m_fft.forward(m_work2);

    double maxSxx = 0;
    for (unsigned int i = 0; i < bins; ++i) {
        maxSxx = std::max(maxSxx, std::norm(m_work[i]));
        m_syy[i] += std::norm(m_work2[i]);
    }
    const double floor = maxSxx * STIMULUS_FLOOR;
    for (unsigned int i = 0; i < bins; ++i) {
        const double sxx = std::norm(m_work[i]);
        // only trust the band the sweep covered; outside it noise / tiny stimulus would
        // dominate the impulse response. The band limit is minimum phase, so its ringing
        // stays after the arrival (inside the gate) and is divided out again later.
        const cd band = m_bandLimit[i];
        cd h = (std::norm(band) > 0 && sxx > floor && maxSxx > 0) ? band * m_work2[i] / m_work[i] : cd();
        if (i == 0 || i == N / 2) {
            h = cd(h.real(), 0.);
        }
        m_work2[i] = h;
        if (i > 0 && i < N / 2) {
            m_work2[N - i] = std::conj(h);
        }
    }
    m_fft.inverse(m_work2);

    std::vector<double> ir(N);
    for (unsigned int i = 0; i < N; ++i) {
        ir[i] = m_work2[i].real();
    }
    m_irs.push_back(std::move(ir));
    ++m_captured;

    // averaged impulse response and its main peak
    const double k = 1. / m_irs.size();
    for (unsigned int i = 0; i < N; ++i) {
        double v = 0;
        for (const auto &h : m_irs) {
            v += h[i];
        }
        m_ir[i] = v * k;
    }
    // find the first arrival (gate start and delay estimate)
    unsigned int peakIndex = findArrival();
    // the pre-roll shifts both channels equally, so index 0 is t = 0 of the reference
    m_peak = peakIndex > N / 2 ? static_cast<long>(peakIndex) - static_cast<long>(N) : static_cast<long>(peakIndex);
    m_peak -= m_bandLimitDelay;

    // gate every chirp around the same centre; re-centre only if the peak really moved
    const bool recenter = m_gated.empty() ||
                          std::abs(static_cast<long>(peakIndex) - m_gateCenter) > RECENTER_LIMIT * m_params.sampleRate;
    if (recenter) {
        m_gateCenter = peakIndex;
        m_gated.assign(m_irs.size(), {});
        for (unsigned int c = 0; c < m_irs.size(); ++c) {
            gateChirp(c);
        }
    } else {
        m_gated.emplace_back();
        gateChirp(static_cast<unsigned int>(m_irs.size() - 1));
    }

    combine();
    buildImpulse();
}

double ChirpAnalyzer::bandWeight(double f) const
{
    const double f1 = m_params.startFrequency, f2 = m_params.endFrequency;
    const double nyquist = 0.5 * m_params.sampleRate;
    const double lo = f1 / std::pow(2., 1. / 3.);
    const double hi = std::min(f2 * std::pow(2., 1. / 6.), nyquist);
    if (f <= lo || f >= hi) {
        return f >= f1 && f <= f2 ? 1. : 0.;
    }
    if (f < f1) {
        return 0.5 * (1. - std::cos(M_PI * std::log(f / lo) / std::log(f1 / lo)));
    }
    if (f > f2) {
        return 0.5 * (1. + std::cos(M_PI * std::log(f / f2) / std::log(hi / f2)));
    }
    return 1.;
}

/**
 * Arrival detection.
 * Room noise is usually strongest at low frequencies, and a single noisy chirp can
 * leave low-frequency wiggles in the broadband impulse that are bigger than the
 * real arrival. So the arrival is looked for in 2-octave bands, using the band with
 * the best peak-to-noise ratio. Within that band the window opens on the *first*
 * arrival above -15 dB of the band's peak, so a strong reflection can't pull the
 * gate or the delay late.
 */
/**
 * Minimum phase version of bandWeight() via the real cepstrum. A zero phase band
 * limit would ring before the arrival, where the pre-gate cuts it off and bends the
 * response near the band edges.
 */
void ChirpAnalyzer::buildBandLimit()
{
    const unsigned int N = m_fftSize;
    const double df = static_cast<double>(m_params.sampleRate) / N;
    constexpr double FLOOR = 1e-6;
    for (unsigned int i = 0; i <= N / 2; ++i) {
        const double w = std::max(FLOOR, bandWeight(i * df));
        m_work[i] = cd(std::log(w), 0.);
        if (i > 0 && i < N / 2) {
            m_work[N - i] = m_work[i];
        }
    }
    m_fft.inverse(m_work);              // real cepstrum
    for (unsigned int n = 1; n < N / 2; ++n) {
        m_work[n] *= 2.;
        m_work[N - n] = 0.;
    }
    m_fft.forward(m_work);
    m_bandLimit.resize(N / 2 + 1);
    for (unsigned int i = 0; i <= N / 2; ++i) {
        const double w = bandWeight(i * df);
        m_bandLimit[i] = w > 0 ? std::exp(m_work[i]) : cd();
    }
    // where the band limit itself peaks, so the reported delay can ignore it
    for (unsigned int i = 0; i <= N / 2; ++i) {
        m_work[i] = m_bandLimit[i];
        if (i > 0 && i < N / 2) {
            m_work[N - i] = std::conj(m_bandLimit[i]);
        }
    }
    m_fft.inverse(m_work);
    unsigned int peak = 0;
    for (unsigned int i = 0; i < N / 2; ++i) {
        if (std::abs(m_work[i].real()) > std::abs(m_work[peak].real())) {
            peak = i;
        }
    }
    m_bandLimitDelay = peak;
}

unsigned int ChirpAnalyzer::findArrival()
{
    const long N = static_cast<long>(m_fftSize);
    const double fs = m_params.sampleRate;
    const double df = fs / N;
    const long early = static_cast<long>(PEAK_SEARCH_EARLY * fs);
    const long late = std::min(static_cast<long>(PEAK_SEARCH_LATE * fs), N / 2 - 1);
    const double advance = chirp::secondHarmonicAdvance(m_params.sweepDuration,
                                                        m_params.startFrequency, m_params.endFrequency);
    const long search = static_cast<long>(std::min(0.8 * advance, FIRST_ARRIVAL_SEARCH) * fs);
    auto wrap = [N](long i) {
        return static_cast<size_t>(((i % N) + N) % N);
    };

    // spectrum of the averaged response
    for (long i = 0; i < N; ++i) {
        m_work[static_cast<size_t>(i)] = cd(m_ir[static_cast<size_t>(i)], 0.);
    }
    m_fft.forward(m_work);

    double bestScore = -1;
    long bestIndex = 0;
    std::vector<double> envelope(static_cast<size_t>(N));
    for (double centre = 62.5; centre <= 16000.; centre *= 2.) {
        if (centre < m_params.startFrequency * 1.4 || centre > m_params.endFrequency / 1.4) {
            continue;
        }
        // analytic, band limited response: positive frequencies only, Hann over +-1 octave
        std::fill(m_work2.begin(), m_work2.end(), cd());
        const long lo = static_cast<long>(centre / 2. / df), hi = std::min(static_cast<long>(centre * 2. / df), N / 2);
        for (long i = std::max(1L, lo); i <= hi; ++i) {
            const double x = std::log2(i * df / centre);      // -1 .. 1
            const double w = 0.5 * (1. + std::cos(M_PI * x));
            m_work2[static_cast<size_t>(i)] = 2. * w * m_work[static_cast<size_t>(i)];
        }
        m_fft.inverse(m_work2);
        for (long i = 0; i < N; ++i) {
            envelope[static_cast<size_t>(i)] = std::abs(m_work2[static_cast<size_t>(i)]);
        }

        long peak = 0;
        double peakValue = -1;
        for (long t = -early; t <= late; ++t) {
            const double v = envelope[wrap(t)];
            if (v > peakValue) {
                peakValue = v;
                peak = t;
            }
        }
        const double noise = median(envelope);
        const double score = noise > 0 ? peakValue / noise : 0.;
        if (score <= bestScore) {
            continue;
        }

        // earliest arrival before the peak that clearly stands out
        const double threshold = std::max(peakValue * FIRST_ARRIVAL_THRESHOLD, 8. * noise);
        long first = peak;
        for (long t = std::max(peak - search, -early); t < peak; ++t) {
            if (envelope[wrap(t)] > threshold) {
                first = t;
                break;
            }
        }
        // settle on the top of that arrival
        const long settle = static_cast<long>(0.002 * fs);
        long top = first;
        for (long t = first; t <= std::min(first + settle, peak); ++t) {
            if (envelope[wrap(t)] > envelope[wrap(top)]) {
                top = t;
            }
        }
        bestScore = score;
        bestIndex = top;
    }

    if (bestScore < 0) {
        // sweep too narrow for any band: broadband peak
        double peakValue = -1;
        for (long t = -early; t <= late; ++t) {
            if (std::abs(m_ir[wrap(t)]) > peakValue) {
                peakValue = std::abs(m_ir[wrap(t)]);
                bestIndex = t;
            }
        }
    } else {
        // refine to the broadband sample with the largest magnitude within +-0.5 ms
        const long r = static_cast<long>(0.0005 * fs);
        long refined = bestIndex;
        for (long t = bestIndex - r; t <= bestIndex + r; ++t) {
            if (std::abs(m_ir[wrap(t)]) > std::abs(m_ir[wrap(refined)])) {
                refined = t;
            }
        }
        bestIndex = refined;
    }
    return static_cast<unsigned int>(wrap(bestIndex));
}

void ChirpAnalyzer::gateChirp(unsigned int c)
{
    const unsigned int N = m_fftSize;
    const std::vector<double> &h = m_irs[c];
    const long pre = static_cast<long>(m_preGate * m_params.sampleRate);
    auto value = [&](long tau) {
        return h[static_cast<size_t>(((m_gateCenter + tau) % static_cast<long>(N) + N) % N)];
    };
    auto rise = [pre](long tau) {   // -pre .. -pre/2 : 0 -> 1
        if (tau >= -pre / 2) {
            return 1.;
        }
        return 0.5 * (1. - std::cos(M_PI * (tau + pre) / (pre / 2.)));
    };

    std::vector<cd> &out = m_gated[c];
    out.assign(m_lastBin - m_firstBin + 1, 0.);

    if (m_gate == Gate::Fixed) {
        // one gate for all frequencies: window the response around the centre and FFT it
        const long post = static_cast<long>(m_bands.front().gate);
        const double taper = std::max(1., 0.1 * post);
        std::fill(m_work.begin(), m_work.end(), cd());
        for (long tau = -pre; tau <= post; ++tau) {
            double w = rise(tau);
            if (tau > post - taper) {
                w *= 0.5 * (1. + std::cos(M_PI * (tau - (post - taper)) / taper));
            }
            m_work[static_cast<size_t>((tau + N) % N)] = value(tau) * w;  // centre moved to t = 0
        }
        m_fft.forward(m_work);
        for (unsigned int i = m_firstBin; i <= m_lastBin; ++i) {
            out[i - m_firstBin] = m_work[i] / m_bandLimit[i];
        }
        return;
    }

    // frequency dependent gate: direct DFT of each band's bins over that band's window
    for (const Band &band : m_bands) {
        const long post = static_cast<long>(band.gate);
        const double half = 0.5 * post;
        for (unsigned int i = band.first; i <= band.last; ++i) {
            const double omega = -2. * M_PI * i / N;
            const cd step = std::polar(1., omega);
            cd rot = std::polar(1., omega * -pre);
            cd sum = 0.;
            for (long tau = -pre; tau <= post; ++tau) {
                double w = rise(tau);
                if (tau > half) {
                    w *= 0.5 * (1. + std::cos(M_PI * (tau - half) / (post - half)));
                }
                sum += value(tau) * w * rot;
                rot *= step;
            }
            out[i - m_firstBin] = sum / m_bandLimit[i];
        }
    }
}

void ChirpAnalyzer::combine()
{
    const double fs = m_params.sampleRate;
    const long center = m_gateCenter > static_cast<long>(m_fftSize / 2) ?
                        m_gateCenter - static_cast<long>(m_fftSize) : m_gateCenter;
    const double tau = static_cast<double>(center) / fs;
    const size_t chirps = m_gated.size();

    std::vector<bool> use(chirps);
    std::vector<cd> bandMean(chirps);
    std::vector<double> re(chirps), im(chirps), dev(chirps);

    for (size_t b = 0; b < m_bands.size(); ++b) {
        const Band &band = m_bands[b];
        const unsigned int width = band.last - band.first + 1;
        Point &p = m_spectrum[b];

        // which chirps agree in this band
        std::fill(use.begin(), use.end(), true);
        if (chirps >= 3) {
            for (size_t c = 0; c < chirps; ++c) {
                cd m = 0.;
                for (unsigned int i = band.first; i <= band.last; ++i) {
                    m += m_gated[c][i - m_firstBin];
                }
                bandMean[c] = m / static_cast<double>(width);
                re[c] = bandMean[c].real();
                im[c] = bandMean[c].imag();
            }
            const cd centre(median(re), median(im));
            for (size_t c = 0; c < chirps; ++c) {
                dev[c] = std::abs(bandMean[c] - centre);
            }
            const double mad = std::max(median(dev), 0.02 * std::abs(centre));
            size_t rejected = 0;
            for (size_t c = 0; c < chirps; ++c) {
                if (dev[c] > OUTLIER_LIMIT * mad && rejected + 1 < (chirps + 1) / 2) {
                    use[c] = false;
                    ++rejected;
                }
            }
        }

        cd sum = 0.;
        double power = 0, total = 0;
        double kept = 0;
        for (size_t c = 0; c < chirps; ++c) {
            kept += use[c] ? 1. : 0.;
        }
        for (unsigned int i = band.first; i <= band.last; ++i) {
            cd avg = 0.;
            double individual = 0;
            for (size_t c = 0; c < chirps; ++c) {
                if (use[c]) {
                    const cd &v = m_gated[c][i - m_firstBin];
                    avg += v;
                    individual += std::norm(v);
                }
            }
            avg /= kept;
            sum += avg;
            power += std::norm(avg);
            total += individual / kept;
        }

        p.magnitude = static_cast<float>(std::sqrt(power / width));
        p.phase = static_cast<float>(std::remainder(std::arg(sum) - 2. * M_PI * band.frequency * tau, 2. * M_PI));
        p.coherence = static_cast<float>(total > 0 ? std::min(1., power / total) : 0.);

        double syy = 0;
        for (unsigned int i = band.first; i <= band.last; ++i) {
            syy += m_syy[i];
        }
        // rms level of the measurement channel in the band, per chirp
        p.module = static_cast<float>(std::sqrt(syy / width / m_captured) * 2. / m_captureLength);
    }
}

void ChirpAnalyzer::buildImpulse()
{
    const unsigned int N = m_fftSize;
    const unsigned int size = static_cast<unsigned int>(m_impulse.size());
    const long half = static_cast<long>(size / 2) - 1;
    for (unsigned int j = 0; j < size; ++j) {
        const long t = static_cast<long>(j) - half;
        const long i = ((t % static_cast<long>(N)) + N) % N;
        m_impulse[j] = static_cast<float>(m_ir[i]);
    }
}
