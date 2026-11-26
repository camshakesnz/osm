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
import QtQuick.Window 2.2
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.12
import QtQuick.Controls.Material 2.15
import "elements"

/**
 * Signal generator block (bottom of the right side): type, level, on/off.
 * The gear opens the full generator settings (frequencies, channels, chirps...).
 */
Item {
    id: generatorBlock
    property bool remoteControlled : remoteClient.controlledGenerator && remoteClient.controlledGenerator.data
    property var currentGenerator : (remoteClient.controlledGenerator && remoteClient.controlledGenerator.data ? remoteClient.controlledGenerator.data : generatorModel)
    readonly property color base: Material.foreground

    implicitHeight: column.implicitHeight + 14
    width: parent ? parent.width : 0

    function setGain(value) {
        currentGenerator.gain = Math.round(Math.max(-140, Math.min(0, value)));
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 3
        radius: 4
        color: Qt.rgba(base.r, base.g, base.b, 0.03)
        border.width: 1
        border.color: currentGenerator.enabled ? Material.color(Material.Red) : Qt.rgba(base.r, base.g, base.b, 0.18)
    }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.topMargin: 7
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Label {
                Layout.fillWidth: true
                text: qsTr("Signal generator")
                font.bold: true
                elide: Text.ElideRight
            }

            MiniButton {
                Layout.preferredWidth: 26
                text: "⚙"
                font.pixelSize: 14
                tooltip: qsTr("generator settings")
                onClicked: applicationWindow.properiesbar.open(generatorBlock, "qrc:/GeneratorProperties.qml", qsTr("Signal generator"))
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            DropDown {
                id: typeSelect
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                topInset: 0
                bottomInset: 0
                model: generatorModel.types
                currentIndex: currentGenerator.type
                onActivated: currentGenerator.type = currentIndex
                font.pixelSize: 12
                ToolTip.visible: hovered
                ToolTip.text: qsTr("signal type")
                ToolTip.delay: 600
            }

            MiniButton {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 28
                text: currentGenerator.enabled ? qsTr("On") : qsTr("Off")
                font.bold: true
                checked: currentGenerator.enabled
                checkedColor: Material.color(Material.Red)
                textColor: currentGenerator.enabled ? "white" : Material.foreground
                color: currentGenerator.enabled ? Material.color(Material.Red) :
                                                  Qt.rgba(base.r, base.g, base.b, hovered ? 0.14 : 0.06)
                tooltip: qsTr("generator on/off (Ctrl+G)")
                onClicked: currentGenerator.enabled = !currentGenerator.enabled
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Label {
                text: qsTr("level")
                font.pixelSize: 11
                opacity: 0.7
            }
            Item { Layout.fillWidth: true }
            MiniButton {
                Layout.preferredWidth: 24
                text: "−"
                tooltip: qsTr("level −1 dB (Shift: −6 dB)")
                onClicked: function(m) { generatorBlock.setGain(currentGenerator.gain - ((m.modifiers & Qt.ShiftModifier) ? 6 : 1)); }
            }
            ValueBox {
                Layout.preferredWidth: 54
                displayText: Math.round(currentGenerator.gain) + " dB"
                tooltip: qsTr("generator level, dBFS")
                onAccepted: function(text) {
                    var v = parseFloat(String(text).replace(/dB|db/g, "").replace(",", "."));
                    if (!isNaN(v)) generatorBlock.setGain(v);
                }
                onStepped: function(steps, fine) { generatorBlock.setGain(currentGenerator.gain + steps); }
            }
            MiniButton {
                Layout.preferredWidth: 24
                text: "+"
                tooltip: qsTr("level +1 dB (Shift: +6 dB)")
                onClicked: function(m) { generatorBlock.setGain(currentGenerator.gain + ((m.modifiers & Qt.ShiftModifier) ? 6 : 1)); }
            }
        }

        RowLayout {
            enabled: remoteControlled
            visible: remoteControlled

            Rectangle {
                id: indicator
                width: 7
                height: width
                radius: width /2
                color: updateColor()
                function updateColor() {
                    if (remoteControlled && currentGenerator) {
                        switch(currentGenerator.state) {
                            case 1:
                                return Material.color(Material.Red);
                            case 2:
                                return Material.color(Material.Green);
                        }
                    }
                    //error or unknown state
                    return Material.color(Material.Red);
                }
                Connections {
                    target: remoteControlled ? currentGenerator : null
                    function onStateChanged() {
                        indicator.color = indicator.updateColor();
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                font.pixelSize: 10
                text:  (remoteControlled && currentGenerator ? "@" + currentGenerator.host : "")
            }
        }
    }

    Shortcut {
        sequence: "Ctrl+G"
        onActivated: currentGenerator.enabled = !currentGenerator.enabled;
    }
}
