/**
 *  OSM
 *  Copyright (C) 2018  Pavel Smokotnin

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
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import QtQuick.Layouts 1.3
import "qrc:/"
import "qrc:/elements"

/**
 * Measurement block (right side): name, M/R input meters, delay with find and track,
 * gain up/down. The gear (added by SourceLayout) opens the full settings.
 */
Item {
    id: measurement

    property var dataModel;
    property var dataModelData : dataModel.data
    property bool chartable : true;
    property bool highlight : false;
    property string propertiesQml: "qrc:/source/MeasurementProperties.qml"
    readonly property real sampleRate: dataModelData && dataModelData.sampleRate ? dataModelData.sampleRate : 48000
    readonly property color sourceColor: dataModelData ? dataModelData.color : "grey"
    readonly property color base: Material.foreground
    height: 98
    width: (parent ? parent.width : 0)

    function ms(samples) {
        return Number(1000 * samples / sampleRate).toLocaleString(Qt.locale(), 'f', 2);
    }
    function setDelayMs(value) {
        if (!dataModelData || isNaN(value)) {
            return;
        }
        var samples = Math.round(value * sampleRate / 1000);
        dataModelData.delay = Math.max(-96000, Math.min(96000, samples));
    }
    function setGain(value) {
        if (!dataModelData || isNaN(value)) {
            return;
        }
        dataModelData.gain = Math.round(Math.max(-90, Math.min(90, value)) * 10) / 10;
    }
    function parseNumber(text) {
        var t = String(text).replace(/ms|dB|db|\+/g, "").trim();
        var v = Number.fromLocaleString(Qt.locale(), t);
        if (isNaN(v)) {
            v = parseFloat(t.replace(",", "."));
        }
        return v;
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 3
        radius: 4
        color: Qt.rgba(base.r, base.g, base.b, measurement.highlight ? 0.07 : 0.03)
        border.width: measurement.highlight ? 2 : 1
        border.color: dataModelData && dataModelData.active && !dataModelData.chartHidden ?
                          Qt.rgba(sourceColor.r, sourceColor.g, sourceColor.b, 0.85) :
                          Qt.rgba(base.r, base.g, base.b, 0.18)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 10
        anchors.topMargin: 3
        anchors.bottomMargin: 8
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            spacing: 0

            MulticolorCheckBox {
                id: checkbox
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                Layout.alignment: Qt.AlignVCenter
                checked: (dataModelData ? dataModelData.active : false)
                checkedColor: (dataModelData ? dataModelData.color : "none")

                onCheckStateChanged: {
                    if (dataModel && dataModelData)
                        dataModelData.active = checked
                }
                error: (dataModelData ? dataModelData.error : false)
            }

            Label {
                Layout.fillWidth: true
                Layout.rightMargin: 4 // clear of the gear / delete buttons
                font.bold: true
                elide: Text.ElideRight
                text: (dataModelData ? dataModelData.name : "")
            }

            // chirp progress, e.g. "chirp 2/3"
            Label {
                id: chirpLabel
                Layout.rightMargin: 4
                font.pixelSize: 10
                opacity: 0.75
                visible: dataModelData && dataModelData.chirpMode === true && text.length > 0
                text: dataModelData && dataModelData.chirpStatus ? dataModelData.chirpStatus : ""
            }

            // drawn on the charts, or only running (meters, loudness)
            MiniButton {
                id: chartsToggle
                Layout.rightMargin: 82 // clear of the gear / clone / delete buttons
                Layout.preferredWidth: 22
                Layout.preferredHeight: 18
                readonly property bool shown: dataModelData && !dataModelData.chartHidden
                text: shown ? "\u25C9" : "\u25CC"
                font.pixelSize: 11
                opacity: shown ? 0.55 : 1
                checked: !shown
                checkedColor: Material.color(Material.Orange)
                tooltip: shown ? qsTr("shown on the charts: click to keep it off the charts (it still runs for meters and loudness)")
                               : qsTr("not on the charts (still running for meters and loudness): click to show it")
                onClicked: if (dataModelData) dataModelData.chartHidden = !dataModelData.chartHidden
            }
        }

        // input meters: M = measurement channel, R = reference channel
        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            columns: 2
            rowSpacing: 3
            columnSpacing: 5

            Label { text: "M"; font.pixelSize: 9; opacity: 0.7 }
            Meter {
                Layout.fillWidth: true
                implicitWidth: 40 // not parent.width: inside a layout that loops
                implicitHeight: 5
                dBV: (dataModelData ? dataModelData.level : 0)
                peak:(dataModelData ? dataModelData.measurementPeak : 0)
            }
            Label { text: "R"; font.pixelSize: 9; opacity: 0.7 }
            Meter {
                Layout.fillWidth: true
                implicitWidth: 40 // not parent.width: inside a layout that loops
                implicitHeight: 5
                dBV: (dataModelData ? dataModelData.referenceLevel : 0)
                peak:(dataModelData ? dataModelData.referencePeak : 0)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.topMargin: 3
            spacing: 3

            ValueBox {
                id: delayBox
                Layout.preferredWidth: 62
                displayText: dataModelData ? measurement.ms(dataModelData.delay) + "ms" : ""
                tooltip: qsTr("delay: type a value in ms, or scroll (Shift: fine)")
                onAccepted: function(text) { measurement.setDelayMs(measurement.parseNumber(text)); }
                onStepped: function(steps, fine) {
                    if (dataModelData) {
                        measurement.setDelayMs(1000 * dataModelData.delay / sampleRate + steps * (fine ? 0.01 : 0.1));
                    }
                }
            }

            MiniButton {
                Layout.preferredWidth: 34
                text: qsTr("Find")
                font.pixelSize: 11
                tooltip: dataModelData ? qsTr("set the delay to the estimate: %1 ms").arg(measurement.ms(dataModelData.estimated)) : ""
                onClicked: if (dataModelData) dataModelData.delay = dataModelData.estimated
            }

            MiniButton {
                id: trackButton
                Layout.preferredWidth: 46
                readonly property bool supported: dataModelData && dataModelData.trackDelay !== undefined
                visible: supported
                checked: supported && dataModelData.trackDelay
                checkedColor: dataModelData && dataModelData.trackState === 2 ? Material.color(Material.Green) : Material.color(Material.Amber)
                text: qsTr("Track")
                font.pixelSize: 11
                tooltip: !checked ? qsTr("follow the delay automatically") :
                         (dataModelData.trackState === 2 ? qsTr("tracking: locked") :
                                                           qsTr("tracking: waiting for a clear, steady estimate"))
                onClicked: if (supported) dataModelData.trackDelay = !dataModelData.trackDelay
            }

            Item { Layout.fillWidth: true }

            MiniButton {
                Layout.preferredWidth: 20
                text: "−"
                tooltip: qsTr("gain −1 dB (Shift: −0.1)")
                onClicked: function(m) {
                    if (dataModelData) measurement.setGain(dataModelData.gain - ((m.modifiers & Qt.ShiftModifier) ? 0.1 : 1));
                }
            }
            ValueBox {
                Layout.preferredWidth: 44
                displayText: dataModelData ? Number(dataModelData.gain).toLocaleString(Qt.locale(), 'f', 1) + "dB" : ""
                tooltip: qsTr("gain applied to the measurement channel")
                onAccepted: function(text) { measurement.setGain(measurement.parseNumber(text)); }
                onStepped: function(steps, fine) {
                    if (dataModelData) measurement.setGain(dataModelData.gain + steps * (fine ? 0.1 : 0.5));
                }
            }
            MiniButton {
                Layout.preferredWidth: 20
                text: "+"
                tooltip: qsTr("gain +1 dB (Shift: +0.1)")
                onClicked: function(m) {
                    if (dataModelData) measurement.setGain(dataModelData.gain + ((m.modifiers & Qt.ShiftModifier) ? 0.1 : 1));
                }
            }
        }
    }

    Connections {
        target: dataModelData
        function onColorChanged() {
            checkbox.checkedColor = dataModelData.color;
        }
    }

    Component.onCompleted: {
        if (!dataModel || !dataModelData) {
            return;
        }
        if (!dataModelData.isColorValid()) {
            dataModelData.color = sourceList.nextColor();
        }
        dataModelData.errorChanged.connect(function(error) {
            if (error) {
                applicationWindow.message.showError(qsTr("Can't start the %1.<br/>Device is not supported or busy.").arg(dataModelData.name));
            }
        });
    }
}
