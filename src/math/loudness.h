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
#ifndef MATH_LOUDNESS_H
#define MATH_LOUDNESS_H

#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <vector>

/**
 * Programme loudness to ITU-R BS.1770-4 / EBU R 128 for a stereo (or mono) signal:
 *  - momentary (400 ms), short-term (3 s) and gated integrated loudness, LUFS
 *  - loudness range, LU (EBU Tech 3342)
 *  - sample peak per channel and maximum true peak (4x oversampled), dBFS / dBTP
 *
 * Threading: add() runs in the audio thread and only filters, squares and sums;
 * every 100 ms it hands a small block summary over. process() (any other thread,
 * called regularly) turns those into loudness values. values() can be read at any time.
 *
 * No Qt dependencies, so it can be tested on its own.
 */
class LoudnessMeter
{
public:
    struct Values {
        //! NaN until there is enough signal (e.g. 3 s for short-term)
        float momentary  = 0;
        float shortTerm  = 0;
        float integrated = 0;
        float range      = 0;
        //! highest sample of each channel in the latest 100 ms block, dBFS
        float peakLeft  = -200;
        float peakRight = -200;
        //! highest true peak of each channel since reset, dBTP
        float maxTruePeakLeft  = -200;
        float maxTruePeakRight = -200;
        //! seconds of signal measured since reset
        float duration = 0;
    };

    LoudnessMeter();

    void setSampleRate(unsigned int sampleRate);
    unsigned int sampleRate() const noexcept
    {
        return m_sampleRate;
    }

    //! mono: only the left (first) input counts, as one channel
    void setMono(bool mono);
    bool mono() const noexcept
    {
        return m_mono.load();
    }

    //! audio thread
    void add(float left, float right) noexcept;

    //! any thread other than the audio thread
    void process();
    void reset() noexcept;
    Values values() const;

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        double operator()(double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() noexcept
        {
            z1 = z2 = 0;
        }
    };

    static constexpr int OVERSAMPLING = 4;
    static constexpr int TAPS_PER_PHASE = 12;

    struct Channel {
        Biquad shelf, highPass;
        std::array<float, TAPS_PER_PHASE> history {};
        unsigned int historyPos = 0;
        double squares = 0;
        float peak = 0, truePeak = 0;
        void clear() noexcept;
    };

    //! one 100 ms block, handed from the audio thread to process()
    struct Block {
        double meanSquareLeft, meanSquareRight;
        float peakLeft, peakRight, truePeakLeft, truePeakRight;
    };

    void prepareFilters();
    float truePeak(Channel &channel, float sample) const noexcept;
    static double toLufs(double power) noexcept;

    unsigned int m_sampleRate = 48000;
    unsigned int m_blockSize  = 4800;
    std::array<std::array<float, TAPS_PER_PHASE>, OVERSAMPLING> m_polyphase {};

    // audio thread state
    Channel m_left, m_right;
    unsigned int m_count = 0;
    std::atomic<bool> m_clearAudioState {false};

    // hand over
    std::mutex m_pendingMutex;
    std::vector<Block> m_pending;

    // process() state
    std::atomic<bool> m_mono {false};
    std::atomic<bool> m_resetRequest {false};
    std::deque<Block> m_recent;                 // last 30 blocks (3 s)
    std::vector<double> m_momentaryPowers;      // every 400 ms block, 75 % overlap
    std::vector<double> m_shortTermPowers;      // every 3 s block, at 10 Hz
    unsigned long m_blocks = 0;
    unsigned long m_rangeComputedAt = 0;
    float m_maxTruePeakLeft = 0, m_maxTruePeakRight = 0;

    mutable std::mutex m_valuesMutex;
    Values m_values;
};

#endif // MATH_LOUDNESS_H
