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
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12
import QtQuick.Controls.Material 2.12

import "../elements"
import SourceModel 1.0
import OpenSoundMeter 1.0

/**
 * Bottom bar settings of a Loudness chart. dataObject is the LoudnessView.
 */
Item {
    id: panel
    property var dataObject
    readonly property int wideWidth: width / 4 - 2
    readonly property int middleWidth: width / 8 - 2

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            spacing: 4

            TitledCombo {
                title: qsTr("measurement")
                tooltip: qsTr("its measurement channel is Left, its reference channel Right")
                Layout.preferredWidth: wideWidth
                model: SourceModel {
                    unrollGroups: true
                    list: sourceList
                }
                textRole: "title"
                valueRole: "source"
                currentIndex: dataObject && dataObject.sourceId !== undefined ? model.indexOf(dataObject.sourceId) : -1
                onActivated: function(index) {
                    dataObject.chooseSource(model.get(index));
                }
            }

            TitledCombo {
                title: qsTr("channels")
                tooltip: qsTr("Stereo: measurement channel = L, reference channel = R. Mono: measurement channel only")
                Layout.preferredWidth: middleWidth
                enabled: dataObject && dataObject.available
                model: [qsTr("Stereo"), qsTr("Mono")]
                currentIndex: dataObject && dataObject.available && dataObject.measurement.loudnessMono ? 1 : 0
                onActivated: function(index) {
                    dataObject.setMono(index === 1);
                }
            }

            TitledCombo {
                title: qsTr("target")
                tooltip: qsTr("integrated loudness turns green within 1 LU of the target, amber within 3 LU")
                Layout.preferredWidth: middleWidth
                model: ["-14 LUFS", "-16 LUFS", "-18 LUFS", "-23 LUFS", "-24 LUFS"]
                currentIndex: dataObject ? model.indexOf(Math.round(dataObject.target) + " LUFS") : -1
                onActivated: function(index) {
                    dataObject.setTarget(parseInt(model[index]));
                }
            }

            Button {
                text: qsTr("reset")
                enabled: dataObject && dataObject.available
                onClicked: dataObject.resetProgramme()
                Material.background: parent.Material.background
                ToolTip.visible: hovered
                ToolTip.text: qsTr("start a new programme: clears integrated, range, true peak and time")
            }

            Item {
                Layout.fillWidth: true
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 6
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            color: Material.secondaryTextColor
            text: qsTr("Loudness (EBU R 128): set the measurement's measurement channel to the stream's left input and its reference channel to the right. The measurement must be switched on.")
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
