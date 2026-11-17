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
import QtQuick.Layouts 1.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import OpenSoundMeter 1.0
import SourceModel 1.0
import "../elements"
import "../"

/**
 * Loudness chart (EBU R 128): L/R peak meters (dBFS) and programme loudness (LUFS).
 * Reads a Measurement: its measurement channel is Left and its reference channel Right
 * (or measurement channel only, in mono).
 * Settings (chart group): loudness/source, loudness/target.
 */
Item {
    id: view

    property var settings
    //! the user's choice (saved); until there is one, the first source in the list is used
    property var chosenId: settings ? settings.value("loudness/source", "") : ""
    property var sourceId: undefined
    property real target: settings ? Number(settings.value("loudness/target", -16)) : -16

    property var values: ({})
    property var measurement: null
    readonly property bool available: measurement !== null && typeof measurement.loudness === "function"

    //! spacing that scales with the chart
    readonly property real margin: Math.max(10, Math.min(28, Math.min(width, height) * 0.03))

    // used by LoudnessProperties.qml in the bottom bar
    function chooseSource(id) {
        if (settings) settings.setValue("loudness/source", id);
        chosenId = id;
        resolve();
    }
    function setTarget(t) {
        if (settings) settings.setValue("loudness/target", t);
        target = t;
    }
    function setMono(mono) {
        if (available) measurement.loudnessMono = mono;
    }
    function resetProgramme() {
        if (available) measurement.resetLoudness();
        leftBar.reset();
        rightBar.reset();
    }

    readonly property color textColor: applicationAppearance.darkMode ? "#EEEEEE" : "#202020"
    readonly property color dimColor: applicationAppearance.darkMode ? "#9E9E9E" : "#616161"

    function resolve() {
        var shared = chosenId ? sourceList.getByUUid(chosenId) : null;
        var id = chosenId;
        if (!shared || !shared.data) {
            id = sourceList.firstSource();
            shared = sourceList.getByUUid(id);
        }
        if (sourceId !== id) {
            sourceId = id;
        }
        var data = shared && shared.data ? shared.data : null;
        if (data !== measurement) {
            measurement = data;
            leftBar.reset();
            rightBar.reset();
        }
    }

    function finite(v) {
        return v !== undefined && v !== null && isFinite(v);
    }
    function lufs(v) {
        return finite(v) && v > -70 ? v.toFixed(1) : "—";
    }
    function dbfs(v) {
        return finite(v) && v > -150 ? v.toFixed(1) : "—";
    }
    function duration(seconds) {
        if (!finite(seconds)) {
            return "0:00";
        }
        var s = Math.floor(seconds), h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60);
        s = s % 60;
        return (h > 0 ? h + ":" + (m < 10 ? "0" : "") : "") + m + ":" + (s < 10 ? "0" : "") + s;
    }
    //! green within 1 LU of the target, amber within 3 LU
    function targetColor(v) {
        if (!finite(v) || v <= -70) {
            return textColor;
        }
        var off = Math.abs(v - target);
        return off <= 1 ? "#43A047" : (off <= 3 ? "#FB8C00" : "#E53935");
    }

    Rectangle {
        anchors.fill: parent
        color: applicationAppearance.darkMode ? "#1E1E1E" : "#F4F4F4"
    }

    Timer {
        id: refresh
        interval: 50
        repeat: true
        running: view.visible
        property real last: 0
        onTriggered: {
            view.resolve();
            var now = Date.now();
            var dt = last ? (now - last) / 1000 : 0.05;
            last = now;
            if (!view.available) {
                view.values = ({});
                return;
            }
            var v = view.measurement.loudness();
            view.values = v;
            leftBar.level = v.peakLeft;
            rightBar.level = v.mono ? v.peakLeft : v.peakRight;
            leftBar.update(dt);
            rightBar.update(dt);
        }
    }

    // ------------------------------------------------------------ header
    // settings live in the bottom bar (LoudnessProperties.qml): click the chart
    PropertiesOpener {
        id: opener
        pushObject: view
        propertiesQml: "qrc:/Loudness/LoudnessProperties.qml"
        onClicked: open()
    }

    Column {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: view.margin * 0.6
        anchors.leftMargin: view.margin
        anchors.rightMargin: 160    // the chart type selector sits top right
        spacing: 2

        Label {
            width: parent.width
            elide: Text.ElideRight
            text: view.available ? view.measurement.name : qsTr("Loudness")
            color: view.textColor
            font.pixelSize: Math.max(15, Math.min(24, view.height * 0.035))
            font.bold: true
        }
        Label {
            width: parent.width
            elide: Text.ElideRight
            color: view.dimColor
            font.pixelSize: Math.max(11, Math.min(15, view.height * 0.022))
            text: view.available ?
                      (view.measurement.loudnessMono ? qsTr("Mono") : qsTr("Stereo")) +
                      "  ·  " + qsTr("target %1 LUFS").arg(Math.round(view.target)) :
                      qsTr("click to choose a measurement")
        }
    }

    // ------------------------------------------------------------ meters and numbers
    Item {
        id: body
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: view.margin
        anchors.bottomMargin: Math.max(34, view.margin)   // keep clear of the pop-out button
        visible: view.available

        // L/R bars with a dBFS scale
        Item {
            id: meters
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            width: Math.min(260, Math.max(150, view.width * 0.22))

            readonly property real labelHeight: 40
            readonly property real barHeight: height - labelHeight
            readonly property bool mono: view.values.mono === true
            readonly property real barWidth: mono ? width - dbScale.width - 6 : (width - dbScale.width - 6 - 8) / 2
            readonly property var allTicks: [0, -3, -6, -9, -12, -18, -24, -30, -40, -50, -60]
            //! only labels at least ~16 px apart, so short bars don't get a pile-up at the top
            readonly property var ticks: {
                var kept = [], lastY = -1000, range = leftBar.maximum - leftBar.minimum;
                for (var i = 0; i < allTicks.length; ++i) {
                    var y = barHeight * (leftBar.maximum - allTicks[i]) / range;
                    if (y - lastY >= 16 || i === allTicks.length - 1 && barHeight - lastY >= 10) {
                        kept.push(allTicks[i]);
                        lastY = y;
                    }
                }
                return kept;
            }

            Item {
                id: dbScale
                width: 30
                height: meters.barHeight
                Repeater {
                    model: meters.ticks
                    Label {
                        text: modelData
                        font.pixelSize: 11
                        color: view.dimColor
                        x: dbScale.width - width
                        y: dbScale.height * (1 - (modelData - leftBar.minimum) / (leftBar.maximum - leftBar.minimum)) - height / 2
                    }
                }
            }

            LevelBar {
                id: leftBar
                x: dbScale.width + 6
                width: meters.barWidth
                height: meters.barHeight
            }
            LevelBar {
                id: rightBar
                visible: !meters.mono
                x: leftBar.x + leftBar.width + 8
                width: meters.barWidth
                height: meters.barHeight
            }

            Repeater {
                model: [
                    {name: "L", bar: leftBar,  tp: "maxTruePeakLeft"},
                    {name: "R", bar: rightBar, tp: "maxTruePeakRight"}
                ]
                Column {
                    visible: modelData.bar.visible
                    x: modelData.bar.x
                    y: meters.barHeight + 2
                    width: modelData.bar.width
                    property var tp: view.values.mono ? view.values.maxTruePeakLeft : view.values[modelData.tp]
                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        font.bold: true
                        color: view.textColor
                        text: meters.mono ? "M" : modelData.name
                    }
                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: 11
                        color: view.finite(parent.tp) && parent.tp > -1 ? "#E53935" : view.dimColor
                        text: "TP " + view.dbfs(parent.tp)
                    }
                }
            }
        }

        // big numbers
        Column {
            id: numbers
            anchors.left: meters.right
            anchors.leftMargin: Math.max(24, view.width * 0.04)
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: big * 0.15

            readonly property real big: Math.max(32, Math.min(body.height * 0.17, (body.width - meters.width) * 0.12))

            Repeater {
                model: [
                    {title: qsTr("SHORT-TERM"), key: "shortTerm", target: false},
                    {title: qsTr("INTEGRATED"), key: "integrated", target: true}
                ]
                Column {
                    spacing: 0
                    Label {
                        text: modelData.title
                        color: view.dimColor
                        font.pixelSize: Math.max(11, numbers.big * 0.17)
                        font.letterSpacing: 1.5
                    }
                    Row {
                        spacing: numbers.big * 0.15
                        Label {
                            id: bigValue
                            text: view.lufs(view.values[modelData.key])
                            font.pixelSize: numbers.big
                            font.bold: true
                            color: !view.finite(view.values[modelData.key]) || view.values[modelData.key] <= -70 ? view.dimColor :
                                   (modelData.target ? view.targetColor(view.values[modelData.key]) : view.textColor)
                            opacity: view.finite(view.values[modelData.key]) && view.values[modelData.key] > -70 ? 1 : 0.4
                        }
                        Label {
                            y: bigValue.baselineOffset - baselineOffset
                            text: "LUFS"
                            color: view.dimColor
                            font.pixelSize: Math.max(12, numbers.big * 0.28)
                        }
                    }
                }
            }

            // small figures
            Grid {
                columns: 4
                columnSpacing: Math.max(18, numbers.big * 0.4)
                rowSpacing: 0
                topPadding: numbers.big * 0.1
                property real valueSize: Math.max(16, numbers.big * 0.32)

                Repeater {
                    model: [qsTr("MOMENTARY"), qsTr("RANGE"), qsTr("TRUE PEAK MAX"), qsTr("TIME")]
                    Label {
                        text: modelData
                        color: view.dimColor
                        font.pixelSize: Math.max(10, numbers.big * 0.13)
                        font.letterSpacing: 1
                    }
                }
                Label {
                    text: view.lufs(view.values.momentary) + " LUFS"
                    color: view.textColor
                    font.pixelSize: parent.valueSize
                }
                Label {
                    text: (view.finite(view.values.range) ? view.values.range.toFixed(1) : "—") + " LU"
                    color: view.textColor
                    font.pixelSize: parent.valueSize
                }
                Label {
                    property var tp: view.values.mono ? view.values.maxTruePeakLeft :
                                     Math.max(view.values.maxTruePeakLeft, view.values.maxTruePeakRight)
                    text: view.dbfs(tp) + " dBTP"
                    color: view.finite(tp) && tp > -1 ? "#E53935" : view.textColor
                    font.pixelSize: parent.valueSize
                }
                Label {
                    text: view.duration(view.values.duration)
                    color: view.textColor
                    font.pixelSize: parent.valueSize
                }
            }
        }
    }

    Label {
        anchors.centerIn: parent
        visible: !view.available
        horizontalAlignment: Text.AlignHCenter
        color: view.dimColor
        text: qsTr("Click here to choose a measurement.\nIts measurement channel is read as Left and its reference channel as Right.")
    }
    Label {
        anchors.centerIn: parent
        visible: view.available && view.values.active === false
        color: view.dimColor
        text: qsTr("This measurement is switched off")
    }
}
