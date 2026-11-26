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

import SourceModel 1.0
import OpenSoundMeter 1.0
import "elements"

/**
 * Right side: views (Live, TF...), the live measurements with their meters and
 * quick controls, and the signal generator.
 */
Item {
    id: liveBar
    readonly property color base: Material.foreground

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        ViewBar {
            Layout.fillWidth: true
            Layout.topMargin: 6
            Layout.leftMargin: 6
            Layout.rightMargin: 6
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            spacing: 3

            Label {
                text: qsTr("charts")
                font.pixelSize: 11
                opacity: 0.7
            }
            Repeater {
                model: 3
                MiniButton {
                    Layout.preferredWidth: 24
                    text: index + 1
                    checked: applicationWindow.charts.count === index + 1
                    tooltip: qsTr("show %1 chart(s) in this view (Ctrl+%1)").arg(index + 1)
                    onClicked: applicationWindow.charts.count = index + 1
                }
            }
            Item { Layout.fillWidth: true }
            MiniButton {
                text: qsTr("Data")
                checked: applicationWindow.dataBarVisible
                tooltip: qsTr("show or hide the data bar: stored traces and folders (Ctrl+B)")
                onClicked: applicationWindow.dataBarVisible = !applicationWindow.dataBarVisible
            }
            MiniButton {
                text: "☰"
                visible: !applicationAppearance.showMenuBar
                tooltip: qsTr("menu")
                onClicked: applicationWindow.sideMenu.open()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Qt.rgba(base.r, base.g, base.b, 0.12)
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 6
            Label {
                Layout.fillWidth: true
                text: qsTr("Measurements")
                font.bold: true
            }
            MiniButton {
                text: qsTr("+ Add")
                tooltip: qsTr("add a measurement")
                onClicked: sourceList.addMeasurement()
            }
        }

        SourceLayout {
            id: liveList
            side: "live"
            Layout.fillWidth: true
            Layout.fillHeight: true
            sources: sourceList
        }

        Generator {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            Layout.bottomMargin: 4
            spacing: 4

            MouseArea {
                Layout.fillWidth: true
                Layout.preferredHeight: 26
                cursorShape: Qt.PointingHandCursor
                onClicked: aboutpopup.open()

                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    Image {
                        id: logoImage
                        source: "qrc:/images/icons/white80.png"
                        width: 20
                        height: 20
                        anchors.verticalCenter: parent.verticalCenter

                        RotationAnimation on rotation {
                            id: rotateImage
                            from: 0
                            to: 360
                            duration: 1200
                        }
                        Timer {
                            interval: 614657
                            running: true
                            repeat: true
                            onTriggered: rotateImage.start()
                        }
                    }
                    Label {
                        text: "ABOUT"
                        font.pointSize: 10
                        anchors.verticalCenter: parent.verticalCenter
                        Material.foreground: Material.Indigo
                    }
                }
            }

            Button {
                font.family: "Osm"
                text: ""
                flat: true
                implicitHeight: 34
                implicitWidth: 34
                topInset: 0
                bottomInset: 0
                padding: 0
                onClicked: applicationWindow.properiesbar.open(null, "qrc:/RemoteProperties.qml", qsTr("Remote"))
                ToolTip.visible: hovered
                ToolTip.text: qsTr("remote control (iPad / other computers)")
                ToolTip.delay: 600
            }
        }
    }

    Shortcut {
        sequence: "Ctrl+1"
        onActivated: applicationWindow.charts.count = 1
    }
    Shortcut {
        sequence: "Ctrl+2"
        onActivated: applicationWindow.charts.count = 2
    }
    Shortcut {
        sequence: "Ctrl+3"
        onActivated: applicationWindow.charts.count = 3
    }
    Shortcut {
        sequence: "Ctrl+4"
        onActivated: applicationWindow.charts.autoHeight()
    }

    // capture all (store every active measurement)
    Shortcut {
        sequence: "Ctrl+X"
        context: Qt.ApplicationShortcut
        onActivated: applicationWindow.captureAll()
    }

    Shortcut {
        sequence: "Ctrl+R"
        context: Qt.ApplicationShortcut
        onActivated: {
            for (var i = 0; i < sourceList.count; i++) {
                let source = sourceList.get(i).data;
                if (source && source.objectName === "Measurement"){
                    source.resetAverage();
                }
            }
        }
    }
}
