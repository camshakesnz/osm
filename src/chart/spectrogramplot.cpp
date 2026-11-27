/**
 *  OSM
 *  Copyright (C) 2020  Pavel Smokotnin

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
#include "spectrogramplot.h"

using namespace Chart;

SpectrogramPlot::SpectrogramPlot(Settings *settings, QQuickItem *parent): FrequencyBasedPlot(settings, parent),
    m_min(-90), m_mid(-50), m_max(10), m_active(true), m_colorScheme(0)
{
    m_y.configure(AxisType::Linear, 0.f,    4.f,  4);
    setPointsPerOctave(48);
    setFlag(QQuickItem::ItemHasContents);
    m_y.setCentralLabel(m_y.min() - 1.f);
    m_y.setUnit("s");
    connect(this, SIGNAL(pointsPerOctaveChanged(unsigned int)), this, SLOT(update()));
}

void SpectrogramPlot::setSettings(Settings *settings) noexcept
{
    if (settings && (settings->value("type") == "Spectrogram")) {
        FrequencyBasedPlot::setSettings(settings);
    }

    setMin(m_settings->reactValue<SpectrogramPlot, int>("dBMin", this, &SpectrogramPlot::minChanged,
                                                        m_min).toInt());
    setMid(m_settings->reactValue<SpectrogramPlot, int>("dBMid", this, &SpectrogramPlot::midChanged,
                                                        m_mid).toInt());
    setMax(m_settings->reactValue<SpectrogramPlot, int>("dBMax", this, &SpectrogramPlot::maxChanged,
                                                        m_max).toInt());
    setColorScheme(m_settings->reactValue<SpectrogramPlot, int>("colors", this, &SpectrogramPlot::colorSchemeChanged,
                                                            m_colorScheme).toInt());
    setAutoRange(m_settings->reactValue<SpectrogramPlot, bool>("autoRange", this, &SpectrogramPlot::autoRangeChanged,
                                                               m_autoRange).toBool());
    setAutoSpan(m_settings->reactValue<SpectrogramPlot, int>("autoSpan", this, &SpectrogramPlot::autoSpanChanged,
                                                             m_autoSpan).toInt());
}

void SpectrogramPlot::storeSettings() noexcept
{
    if (!m_settings)
        return;

    FrequencyBasedPlot::storeSettings();
    m_settings->setValue("type", "Spectrogram");
    m_settings->setValue("dBMin", m_min);
    m_settings->setValue("dBMid", m_mid);
    m_settings->setValue("dBMax", m_max);
    m_settings->setValue("colors", m_colorScheme);
    m_settings->setValue("autoRange", m_autoRange);
    m_settings->setValue("autoSpan", m_autoSpan);
}

bool SpectrogramPlot::autoRange() const
{
    return m_autoRange;
}

void SpectrogramPlot::setAutoRange(bool autoRange)
{
    if (m_autoRange != autoRange) {
        m_autoRange = autoRange;
        emit autoRangeChanged(m_autoRange);
    }
}

int SpectrogramPlot::autoSpan() const
{
    return m_autoSpan;
}

void SpectrogramPlot::setAutoSpan(int span)
{
    span = span < 10 ? 10 : (span > 120 ? 120 : span);
    if (m_autoSpan != span) {
        m_autoSpan = span;
        emit autoSpanChanged(m_autoSpan);
    }
}

int SpectrogramPlot::colorScheme() const
{
    return m_colorScheme;
}

void SpectrogramPlot::setColorScheme(int scheme)
{
    if (m_colorScheme != scheme) {
        m_colorScheme = scheme;
        emit colorSchemeChanged(m_colorScheme);
        update();
    }
}

QColor SpectrogramPlot::plotAreaColor() const
{
    // heat colours start from black, like Smaart's spectrograph
    return m_colorScheme == SpectrogramPalette::Heat ? QColor(Qt::black) : QColor();
}

int SpectrogramPlot::max() const
{
    return m_max;
}

void SpectrogramPlot::setMax(int max)
{
    if (m_max != max) {
        m_max = max;
        emit maxChanged(m_max);
    }
}

bool SpectrogramPlot::active() const
{
    return m_active;
}

void SpectrogramPlot::setActive(bool active)
{
    if (m_active != active) {
        m_active = active;
        emit activeChanged(m_active);
    }
}

int SpectrogramPlot::mid() const
{
    return m_mid;
}

void SpectrogramPlot::setMid(int mid)
{
    if (m_mid != mid) {
        m_mid = mid;
        emit midChanged(m_mid);
    }
}

int SpectrogramPlot::min() const
{
    return m_min;
}

void SpectrogramPlot::setMin(int min)
{
    if (m_min != min) {
        m_min = min;
        emit minChanged(m_min);
    }
}
