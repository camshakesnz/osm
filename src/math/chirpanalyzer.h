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
#ifndef MATH_CHIRPANALYZER_H
#define MATH_CHIRPANALYZER_H

#include <complex>
#include <vector>
#include "chirpsignal.h"

/**
 * Captures a set of 1..6 log-sweep chirps and turns them into a
 * transfer function (magnitude, phase, coherence) and impulse response.
 *
 * Method
 *  - each chirp is detected on the reference channel and captured together
 *    with its decay tail (pre-roll + sweep + tail, zero padded to 2^n);
 *  - every chirp is deconvolved against its own reference, h_k = IFFT(Y_k / X_k);
 *  - each h_k is gated around the main peak: the region before the peak that holds
 *    the sweep's harmonic distortion products and the deconvolved noise outside the
 *    gate are removed before the frequency response is calculated (Farina method).
 *    The gate is either one long window or frequency dependent (like OSM's LTW);
 *  - chirps are averaged coherently (complex), so uncorrelated noise drops ~3 dB per
 *    doubling. With 3+ chirps a chirp that disagrees strongly with the others in a band
 *    (a clap, a door, someone talking) is left out of that band;
 *  - coherence is the agreement between chirps, so it needs 2+ chirps to mean anything;
 *  - the result is reduced to raw bins at low frequencies and 1/96 octave bands
 *    above, which keeps plots and the remote link light.
 *
 * No Qt dependencies: the class is used from Measurement and from the tests.
 */
class ChirpAnalyzer
{
public:
    enum class State { Idle, Armed, Capturing, Complete };

    //! how the impulse response is gated before the frequency response is calculated
    enum class Gate {
        Fixed,      //!< one long gate (sweep tail), highest resolution
        Log         //!< frequency dependent gate, same resolution as OSM's LTW mode
    };

    struct Point {
        float frequency = 0;
        float magnitude = 0;    // |H|, linear
        float phase     = 0;    // arg(H), radians
        float coherence = 0;    // 0..1, needs 2+ chirps to be meaningful
        float module    = 0;    // measurement channel level, linear
    };

    static constexpr unsigned int POINTS_PER_OCTAVE = 96;
    static constexpr unsigned int MAX_IMPULSE_SIZE = 1 << 16;

    ChirpAnalyzer() = default;

    //! reset and wait for the first chirp of a new set
    void arm(const chirp::Params &params, Gate gate = Gate::Fixed);
    void disarm();

    /**
     * feed one sample pair
     * @return true when a chirp has just been processed and results were updated
     */
    bool add(float data, float reference);

    State state() const noexcept
    {
        return m_state;
    }
    unsigned int captured() const noexcept
    {
        return m_captured;
    }
    unsigned int count() const noexcept
    {
        return m_params.count;
    }
    const chirp::Params &params() const noexcept
    {
        return m_params;
    }

    //! banded transfer function of the averaged chirps
    const std::vector<Point> &spectrum() const noexcept
    {
        return m_spectrum;
    }

    /**
     * averaged (ungated) impulse response, laid out like OSM's Measurement:
     * index j holds time t = j - (size/2 - 1) samples, t = 0 is the reference
     */
    const std::vector<float> &impulse() const noexcept
    {
        return m_impulse;
    }

    //! first arrival relative to the reference, samples (gate start and delay estimate)
    long peakDelay() const noexcept
    {
        return m_peak;
    }

    //! FFT length used for each chirp
    unsigned int fftSize() const noexcept
    {
        return m_fftSize;
    }

    //! gate applied before the peak, seconds
    double preGate() const noexcept
    {
        return m_preGate;
    }

private:
    using cd = std::complex<double>;

    struct Band {
        unsigned int first, last;   // bin range, inclusive
        double frequency;
        unsigned int gate;          // samples kept after the peak
    };

    class FFT
    {
    public:
        void resize(unsigned int n);
        void forward(std::vector<cd> &a) const;
        void inverse(std::vector<cd> &a) const;
        unsigned int size() const noexcept
        {
            return m_n;
        }
    private:
        void run(std::vector<cd> &a, bool inverse) const;
        unsigned int m_n = 0;
        std::vector<unsigned int> m_rev;
        std::vector<cd> m_twiddle;
    };

    void prepare();
    void beginCapture();
    void process();
    void buildBands();
    void gateChirp(unsigned int k);
    double bandWeight(double frequency) const;
    void buildBandLimit();
    unsigned int findArrival();
    void combine();
    void buildImpulse();

    chirp::Params m_params;
    Gate m_gate = Gate::Fixed;
    State m_state = State::Idle;

    unsigned int m_captured = 0;
    unsigned int m_captureLength = 0;   // samples recorded per chirp
    unsigned int m_preRoll = 0;
    unsigned int m_fftSize = 0;

    // onset detection
    std::vector<float> m_ringData, m_ringRef;
    unsigned int m_ringPos = 0;
    unsigned int m_ringFill = 0;
    double m_noiseSquared = 0;
    double m_noiseAlpha = 0;

    // capture of the current chirp
    std::vector<float> m_capData, m_capRef;

    FFT m_fft;
    std::vector<cd> m_work, m_work2;
    std::vector<double> m_syy;                  // measurement power, summed over chirps
    std::vector<std::vector<double>> m_irs;     // impulse response of every chirp
    std::vector<double> m_ir;                   // averaged impulse response
    std::vector<std::vector<cd>> m_gated;       // gated, delay compensated bins of every chirp
    std::vector<cd> m_bandLimit;                // minimum phase band limit, bins 0..N/2
    long m_bandLimitDelay = 0;                  // samples its peak sits after t = 0
    std::vector<Band> m_bands;
    unsigned int m_firstBin = 0, m_lastBin = 0;
    long m_gateCenter = 0;                      // index in m_irs used as t = 0 for gating

    std::vector<Point> m_spectrum;
    std::vector<float> m_impulse;
    long m_peak = 0;
    double m_preGate = 0;
};

#endif // MATH_CHIRPANALYZER_H
