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
#include "loudness.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double PI = 3.14159265358979323846;
constexpr double ABSOLUTE_GATE = -70.;              // LUFS
constexpr double RELATIVE_GATE_INTEGRATED = -10.;   // LU
constexpr double RELATIVE_GATE_RANGE = -20.;        // LU
constexpr int MOMENTARY_BLOCKS = 4;                 // 400 ms in 100 ms steps
constexpr int SHORT_TERM_BLOCKS = 30;               // 3 s
const float NOT_YET = std::numeric_limits<float>::quiet_NaN();

double besselI0(double x)
{
    double sum = 1, term = 1;
    for (int k = 1; k < 50; ++k) {
        term *= (x / (2. * k)) * (x / (2. * k));
        sum += term;
        if (term < 1e-12 * sum) {
            break;
        }
    }
    return sum;
}

double powerFromLufs(double lufs)
{
    return std::pow(10., (lufs + 0.691) / 10.);
}

float dB(float linear)
{
    return linear > 0 ? 20.f * std::log10(linear) : -200.f;
}
}

void LoudnessMeter::Channel::clear() noexcept
{
    shelf.clear();
    highPass.clear();
    history.fill(0.f);
    historyPos = 0;
    squares = 0;
    peak = truePeak = 0;
}

LoudnessMeter::LoudnessMeter()
{
    // 4x interpolation filter for true peak: Kaiser windowed sinc, 48 taps
    constexpr int TAPS = OVERSAMPLING * TAPS_PER_PHASE;
    constexpr double BETA = 5.;
    const double centre = (TAPS - 1) / 2.;
    for (int n = 0; n < TAPS; ++n) {
        const double x = (n - centre) / OVERSAMPLING;
        const double sinc = x == 0 ? 1. : std::sin(PI * x) / (PI * x);
        const double r = (n - centre) / centre;
        const double window = besselI0(BETA * std::sqrt(std::max(0., 1. - r * r))) / besselI0(BETA);
        m_polyphase[n % OVERSAMPLING][n / OVERSAMPLING] = static_cast<float>(sinc * window);
    }
    m_pending.reserve(64);
    m_values.momentary = m_values.shortTerm = m_values.integrated = m_values.range = NOT_YET;
    prepareFilters();
}

void LoudnessMeter::setSampleRate(unsigned int sampleRate)
{
    if (sampleRate == 0 || sampleRate == m_sampleRate) {
        return;
    }
    m_sampleRate = sampleRate;
    prepareFilters();
    reset();
}

void LoudnessMeter::setMono(bool mono)
{
    if (m_mono.exchange(mono) != mono) {
        reset();
    }
}

/**
 * K-weighting (BS.1770-4): a high shelf (+4 dB above ~1.7 kHz) followed by a
 * high-pass (RLB, ~38 Hz), recalculated for any sample rate. At 48 kHz these give
 * exactly the coefficients tabulated in the recommendation.
 */
void LoudnessMeter::prepareFilters()
{
    const double fs = m_sampleRate;
    Biquad shelf;
    {
        const double f0 = 1681.974450955533, gain = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan(PI * f0 / fs);
        const double vh = std::pow(10., gain / 20.);
        const double vb = std::pow(vh, 0.4996667741545416);
        const double a0 = 1. + k / q + k * k;
        shelf.b0 = (vh + vb * k / q + k * k) / a0;
        shelf.b1 = 2. * (k * k - vh) / a0;
        shelf.b2 = (vh - vb * k / q + k * k) / a0;
        shelf.a1 = 2. * (k * k - 1.) / a0;
        shelf.a2 = (1. - k / q + k * k) / a0;
    }
    Biquad highPass;
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan(PI * f0 / fs);
        const double a0 = 1. + k / q + k * k;
        highPass.b0 = 1.;
        highPass.b1 = -2.;
        highPass.b2 = 1.;
        highPass.a1 = 2. * (k * k - 1.) / a0;
        highPass.a2 = (1. - k / q + k * k) / a0;
    }
    m_left.shelf = m_right.shelf = shelf;
    m_left.highPass = m_right.highPass = highPass;
    m_blockSize = std::max(1u, m_sampleRate / 10);
    m_clearAudioState = true;
}

float LoudnessMeter::truePeak(Channel &channel, float sample) const noexcept
{
    channel.history[channel.historyPos] = sample;
    float peak = std::abs(sample);
    for (int phase = 0; phase < OVERSAMPLING; ++phase) {
        float sum = 0;
        unsigned int pos = channel.historyPos;
        for (int tap = 0; tap < TAPS_PER_PHASE; ++tap) {
            sum += m_polyphase[phase][tap] * channel.history[pos];
            pos = pos == 0 ? TAPS_PER_PHASE - 1 : pos - 1;
        }
        peak = std::max(peak, std::abs(sum));
    }
    channel.historyPos = (channel.historyPos + 1) % TAPS_PER_PHASE;
    return peak;
}

void LoudnessMeter::add(float left, float right) noexcept
{
    if (m_clearAudioState.exchange(false)) {
        m_left.clear();
        m_right.clear();
        m_count = 0;
    }
    auto feed = [this](Channel & channel, float sample) {
        const double k = channel.highPass(channel.shelf(sample));
        channel.squares += k * k;
        channel.peak = std::max(channel.peak, std::abs(sample));
        channel.truePeak = std::max(channel.truePeak, truePeak(channel, sample));
    };
    feed(m_left, left);
    feed(m_right, right);

    if (++m_count >= m_blockSize) {
        const Block block {
            m_left.squares / m_count, m_right.squares / m_count,
            m_left.peak, m_right.peak, m_left.truePeak, m_right.truePeak
        };
        m_left.squares = m_right.squares = 0;
        m_left.peak = m_right.peak = m_left.truePeak = m_right.truePeak = 0;
        m_count = 0;
        // try_lock: never wait in the audio thread; a block is only dropped if
        // process() happens to hold the lock at exactly this moment
        if (m_pendingMutex.try_lock()) {
            if (m_pending.size() < 1000) {
                m_pending.push_back(block);
            }
            m_pendingMutex.unlock();
        }
    }
}

void LoudnessMeter::reset() noexcept
{
    m_resetRequest = true;
}

double LoudnessMeter::toLufs(double power) noexcept
{
    return power > 0 ? -0.691 + 10. * std::log10(power) : -std::numeric_limits<double>::infinity();
}

void LoudnessMeter::process()
{
    std::vector<Block> blocks;
    {
        std::lock_guard<std::mutex> guard(m_pendingMutex);
        blocks.swap(m_pending);
    }
    if (m_resetRequest.exchange(false)) {
        blocks.clear();
        m_recent.clear();
        m_momentaryPowers.clear();
        m_shortTermPowers.clear();
        m_blocks = 0;
        m_rangeComputedAt = 0;
        m_maxTruePeakLeft = m_maxTruePeakRight = 0;
        m_clearAudioState = true;
        std::lock_guard<std::mutex> guard(m_valuesMutex);
        m_values = Values();
        m_values.momentary = m_values.shortTerm = m_values.integrated = m_values.range = NOT_YET;
        return;
    }
    if (blocks.empty()) {
        return;
    }

    const bool mono = m_mono.load();
    auto power = [mono](const Block & b) {
        return mono ? b.meanSquareLeft : b.meanSquareLeft + b.meanSquareRight;
    };
    auto windowPower = [&](int count) {
        double sum = 0;
        for (auto it = m_recent.end() - count; it != m_recent.end(); ++it) {
            sum += power(*it);
        }
        return sum / count;
    };

    const double absoluteGate = powerFromLufs(ABSOLUTE_GATE);
    for (const auto &block : blocks) {
        m_recent.push_back(block);
        if (m_recent.size() > SHORT_TERM_BLOCKS) {
            m_recent.pop_front();
        }
        ++m_blocks;
        m_maxTruePeakLeft = std::max(m_maxTruePeakLeft, block.truePeakLeft);
        m_maxTruePeakRight = std::max(m_maxTruePeakRight, block.truePeakRight);

        // gating blocks: 400 ms every 100 ms (75 % overlap); 3 s at 10 Hz for the range
        if (m_recent.size() >= MOMENTARY_BLOCKS) {
            const double p = windowPower(MOMENTARY_BLOCKS);
            if (p > absoluteGate) {
                m_momentaryPowers.push_back(p);
            }
        }
        if (m_recent.size() >= SHORT_TERM_BLOCKS) {
            const double p = windowPower(SHORT_TERM_BLOCKS);
            if (p > absoluteGate) {
                m_shortTermPowers.push_back(p);
            }
        }
    }

    Values values;
    values.momentary = m_recent.size() >= MOMENTARY_BLOCKS ?
                       static_cast<float>(toLufs(windowPower(MOMENTARY_BLOCKS))) : NOT_YET;
    values.shortTerm = m_recent.size() >= SHORT_TERM_BLOCKS ?
                       static_cast<float>(toLufs(windowPower(SHORT_TERM_BLOCKS))) : NOT_YET;

    // integrated: absolute gate, then relative gate 10 LU below the absolute-gated level
    values.integrated = NOT_YET;
    if (!m_momentaryPowers.empty()) {
        double sum = 0;
        for (double p : m_momentaryPowers) {
            sum += p;
        }
        const double relativeGate = sum / m_momentaryPowers.size() * std::pow(10., RELATIVE_GATE_INTEGRATED / 10.);
        double gated = 0;
        unsigned long count = 0;
        for (double p : m_momentaryPowers) {
            if (p > relativeGate) {
                gated += p;
                ++count;
            }
        }
        if (count) {
            values.integrated = static_cast<float>(toLufs(gated / count));
        }
    }

    // loudness range (EBU Tech 3342): 10th to 95th percentile of the gated short-term
    // levels. Sorting is the expensive part, so it is updated once a second.
    {
        std::lock_guard<std::mutex> guard(m_valuesMutex);
        values.range = m_values.range;
    }
    if (m_blocks - m_rangeComputedAt >= 10 || std::isnan(values.range)) {
        m_rangeComputedAt = m_blocks;
        values.range = NOT_YET;
        if (m_shortTermPowers.size() >= 2) {
            double sum = 0;
            for (double p : m_shortTermPowers) {
                sum += p;
            }
            const double relativeGate = sum / m_shortTermPowers.size() * std::pow(10., RELATIVE_GATE_RANGE / 10.);
            std::vector<double> levels;
            levels.reserve(m_shortTermPowers.size());
            for (double p : m_shortTermPowers) {
                if (p > relativeGate) {
                    levels.push_back(toLufs(p));
                }
            }
            if (levels.size() >= 2) {
                std::sort(levels.begin(), levels.end());
                auto percentile = [&levels](double fraction) {
                    const double position = fraction * (levels.size() - 1);
                    const size_t below = static_cast<size_t>(std::floor(position));
                    const size_t above = std::min(levels.size() - 1, below + 1);
                    return levels[below] + (levels[above] - levels[below]) * (position - below);
                };
                values.range = static_cast<float>(percentile(0.95) - percentile(0.10));
            } else {
                values.range = 0;
            }
        }
    }

    const Block &last = m_recent.back();
    values.peakLeft = dB(last.peakLeft);
    values.peakRight = dB(last.peakRight);
    values.maxTruePeakLeft = dB(m_maxTruePeakLeft);
    values.maxTruePeakRight = dB(m_maxTruePeakRight);
    values.duration = m_blocks / 10.f;

    std::lock_guard<std::mutex> guard(m_valuesMutex);
    m_values = values;
}

LoudnessMeter::Values LoudnessMeter::values() const
{
    std::lock_guard<std::mutex> guard(m_valuesMutex);
    return m_values;
}
