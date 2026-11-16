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
#ifndef CHIRP_H
#define CHIRP_H

#include <atomic>
#include "outputdevice.h"

class GeneratorThread;

/**
 * Plays a set of 1..6 log-sweep chirps, each followed by silence for the
 * room to decay, then signals that the set is finished so the generator
 * can switch itself off. Measurements in "Chirp" mode capture the set.
 */
class Chirp : public OutputDevice
{
    Q_OBJECT

public:
    explicit Chirp(GeneratorThread *parent);

public slots:
    void setStart(int start);
    void setEnd(int end);
    void setDuration(float duration);
    void setCount(int count);
    void enabledChanged(bool);

signals:
    void sequenceFinished();

private:
    Sample sample() override;

    float m_start, m_end, m_duration;
    unsigned int m_count;

    // playback position, reset when the generator is switched on
    std::atomic<unsigned long> m_position;
    std::atomic<bool> m_finished;
};

#endif // CHIRP_H
