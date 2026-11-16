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
import QtQuick 2.12
import QtQuick.Controls.Material 2.12

/**
 * One vertical peak meter, dBFS. Falls back at a PPM-like rate and keeps a
 * peak hold line for two seconds.
 */
Item {
    id: bar

    //! latest sample peak, dBFS
    property real level: -200
    property real minimum: -60
    property real maximum: 0
    //! colour boundaries, dBFS
    property real warning: -18
    property real danger: -6

    property real shown: minimum
    property real hold: minimum
    property real holdTime: 0
    readonly property real fallRate: 12      // dB per second
    readonly property real holdSeconds: 2

    function position(db) {
        return Math.max(0, Math.min(1, (db - minimum) / (maximum - minimum)));
    }

    //! called by the owner on every refresh, dt in seconds
    function update(dt) {
        var target = Math.max(minimum, Math.min(maximum, level));
        shown = Math.max(target, shown - fallRate * dt);
        if (target >= hold) {
            hold = target;
            holdTime = 0;
        } else {
            holdTime += dt;
            if (holdTime > holdSeconds) {
                hold = Math.max(shown, hold - 2 * fallRate * dt);
            }
        }
    }

    function reset() {
        shown = minimum;
        hold = minimum;
        holdTime = 0;
    }

    // track, coloured from the bottom: green, amber above `warning`, red above `danger`
    Rectangle {
        id: track
        anchors.fill: parent
        radius: 2
        gradient: Gradient {
            GradientStop { position: 0.0;                         color: "#E53935" }
            GradientStop { position: 1 - bar.position(bar.danger) - 0.0001; color: "#E53935" }
            GradientStop { position: 1 - bar.position(bar.danger);  color: "#FDD835" }
            GradientStop { position: 1 - bar.position(bar.warning) - 0.0001; color: "#FDD835" }
            GradientStop { position: 1 - bar.position(bar.warning); color: "#43A047" }
            GradientStop { position: 1.0;                         color: "#2E7D32" }
        }
    }

    // unlit part covers the track from the top
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: parent.height * (1 - bar.position(bar.shown))
        color: applicationAppearance.darkMode ? "#2A2A2A" : "#D8D8D8"
        radius: 2
    }

    // peak hold
    Rectangle {
        visible: bar.hold > bar.minimum
        anchors.left: parent.left
        anchors.right: parent.right
        height: 2
        y: parent.height * (1 - bar.position(bar.hold)) - 1
        color: applicationAppearance.darkMode ? "white" : "black"
    }
}
