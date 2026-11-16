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
#include "chirp.h"
#include "generatorthread.h"
#include "math/chirpsignal.h"

Chirp::Chirp(GeneratorThread *parent) : OutputDevice(parent),
    m_start(20.f), m_end(20000.f), m_duration(1.f), m_count(1),
    m_position(0), m_finished(false)
{
    m_name = "Chirp";

    connect(parent, &GeneratorThread::enabledChanged, this, &Chirp::enabledChanged);
    connect(parent, &GeneratorThread::startFrequencyChanged, this, &Chirp::setStart);
    connect(parent, &GeneratorThread::endFrequencyChanged, this, &Chirp::setEnd);
    connect(parent, &GeneratorThread::durationChanged, this, &Chirp::setDuration);
    connect(parent, &GeneratorThread::chirpCountChanged, this, &Chirp::setCount);

    m_start = parent->startFrequency();
    m_end = parent->endFrequency();
    m_duration = parent->duration();
    m_count = chirp::clampCount(parent->chirpCount());
}

Sample Chirp::sample()
{
    if (m_sampleRate <= 0) {
        return {0.f};
    }
    const unsigned long position = m_position++;
    const double t = static_cast<double>(position) / m_sampleRate;
    const double period = chirp::periodSeconds(m_duration);

    // a little silence first: the output stream has only just been opened
    const double lead = 0.1;
    const double inSet = t - lead;
    if (inSet < 0) {
        return {0.f};
    }
    const unsigned int index = static_cast<unsigned int>(inSet / period);
    if (index >= m_count) {
        bool expected = false;
        if (m_finished.compare_exchange_strong(expected, true)) {
            emit sequenceFinished();
        }
        return {0.f};
    }
    const double inChirp = inSet - index * period;
    return {m_gain * chirp::sweepSample(inChirp, m_duration, m_start, m_end)};
}

void Chirp::setStart(int start)
{
    m_start = static_cast<float>(start);
}

void Chirp::setEnd(int end)
{
    m_end = static_cast<float>(end);
}

void Chirp::setDuration(float duration)
{
    m_duration = std::max(0.1f, duration);
}

void Chirp::setCount(int count)
{
    m_count = chirp::clampCount(count);
}

void Chirp::enabledChanged(bool)
{
    m_position = 0;
    m_finished = false;
}
