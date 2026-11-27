/**
 *  OSM
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import QtQuick.Layouts 1.3
import "elements"

/**
 * Smaart-style command bar along the bottom of the window: big buttons for the
 * things done over and over while tuning.
 */
Item {
    id: bar
    implicitHeight: 40
    readonly property color base: Material.foreground

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Qt.rgba(base.r, base.g, base.b, 0.12)
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 5

        component CommandButton: MiniButton {
            Layout.fillWidth: true
            Layout.preferredWidth: 100 // all the same width
            Layout.fillHeight: true
            font.pixelSize: 12
        }

        CommandButton {
            text: qsTr("Data Bar")
            checked: applicationWindow.dataBarVisible
            tooltip: qsTr("show or hide stored traces and folders (Ctrl+B)")
            onClicked: applicationWindow.dataBarVisible = !applicationWindow.dataBarVisible
        }
        CommandButton {
            text: qsTr("Capture")
            tooltip: qsTr("store the selected measurement (or the first active one)")
            onClicked: applicationWindow.captureSelected()
        }
        CommandButton {
            text: qsTr("Capture All")
            tooltip: qsTr("store every active measurement (Ctrl+X)")
            onClicked: applicationWindow.captureAll()
        }
        CommandButton {
            text: qsTr("Reset Avg")
            tooltip: qsTr("restart averaging on every measurement (Ctrl+R)")
            onClicked: applicationWindow.resetAverages()
        }
        CommandButton {
            text: tools.spatialActive ? qsTr("Spatial: %1 pos").arg(tools.spatialCount) : qsTr("Spatial Avg")
            checked: tools.spatialActive
            checkedColor: Material.color(Material.Green)
            tooltip: tools.spatialActive ?
                         qsTr("click to finish. Each capture is a mic position; the average updates as you go") :
                         qsTr("start a spatial average: captures (or chirp sets) go into a new folder and are averaged as you go")
            onClicked: tools.spatialActive ? tools.stopSpatial() : tools.startSpatial()
        }
        CommandButton {
            text: qsTr("Average…")
            tooltip: qsTr("average any traces you pick")
            onClicked: tools.openAverage()
        }
        CommandButton {
            text: qsTr("Compare…")
            tooltip: qsTr("difference between two traces (after − before)")
            onClicked: tools.openCompare()
        }
        CommandButton {
            text: qsTr("Align…")
            tooltip: qsTr("main + sub alignment at the crossover")
            onClicked: tools.openAlign()
        }
        CommandButton {
            text: qsTr("dB −")
            tooltip: qsTr("selected trace −1 dB (Shift: −0.1)")
            onClicked: function(m) { applicationWindow.nudgeGain((m.modifiers & Qt.ShiftModifier) ? -0.1 : -1); }
        }
        CommandButton {
            text: qsTr("dB +")
            tooltip: qsTr("selected trace +1 dB (Shift: +0.1)")
            onClicked: function(m) { applicationWindow.nudgeGain((m.modifiers & Qt.ShiftModifier) ? 0.1 : 1); }
        }
        CommandButton {
            text: qsTr("Clear dB")
            tooltip: qsTr("set the gain offset of every stored trace back to 0 dB")
            onClicked: applicationWindow.clearAllDb()
        }
    }
}
