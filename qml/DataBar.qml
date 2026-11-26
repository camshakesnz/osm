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
import "source"

/**
 * Left side: stored traces (captures), folders, maths sources and file actions,
 * kept apart from the live measurements on the right.
 * Opening a folder (double-click) makes it the place new captures go.
 */
Item {
    id: dataBar
    property Item list : stack
    readonly property color base: Material.foreground
    //! the folder (group source) that is open, or null at the top level
    readonly property var openFolder: stack.currentItem && stack.currentItem.group ? stack.currentItem.group : null

    onOpenFolderChanged: {
        sourceList.setCaptureList(openFolder && openFolder.data ? openFolder.data.sourceList : null);
    }

    //! the list new items are added to: the open folder, or the top level
    function currentList() {
        return openFolder && openFolder.data ? openFolder.data.sourceList : sourceList;
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8
            Layout.leftMargin: 10
            Layout.rightMargin: 6
            Label {
                Layout.fillWidth: true
                text: qsTr("Data")
                font.bold: true
            }
            MiniButton {
                text: qsTr("Capture")
                tooltip: qsTr("store every active measurement (Ctrl+X)")
                onClicked: applicationWindow.captureAll()
            }
            MiniButton {
                text: "+"
                tooltip: qsTr("add a folder, average, filter, EQ…")
                onClicked: addMenu.popup()
            }
        }

        TargetTrace {
            Layout.fillWidth: true
            visible: targetTraceModel.show
        }

        StackView {
            id: stack
            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.margins: 0
            clip: true

            initialItem: sourceLayoutComponent

            function openGroup(group) {
                stack.push(sourceLayoutComponent, { group: group });
            }
        }

        Component {
            id: sourceLayoutComponent

            ColumnLayout {
                property var group : null
                property alias sources : sourcesLayout.sources
                spacing: 0

                // folder header: click to go back up, drop here to move an item out
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    visible: stack.depth > 1
                    color: Qt.rgba(base.r, base.g, base.b, 0.06)
                    border.color: group && group.data ? group.data.color : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 4
                        spacing: 4

                        MiniButton {
                            text: "‹ " + qsTr("Back")
                            tooltip: qsTr("back to the folder above")
                            onClicked: stack.pop()
                        }
                        Label {
                            Layout.fillWidth: true
                            text: group && group.data ? group.data.name : ""
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        MiniButton {
                            text: "⚙"
                            tooltip: qsTr("folder settings")
                            onClicked: {
                                if (group && group.data) {
                                    applicationWindow.properiesbar.open(group,
                                        group.data.objectName === "Equalizer" ? "qrc:/source/EqualizerProperties.qml"
                                                                              : "qrc:/source/GroupProperties.qml");
                                }
                            }
                        }
                    }

                    DropArea {
                        anchors { fill: parent; margins: 0 }
                        onDropped: {
                            var parentItemInStack = stack.get(stack.index - 1, StackView.DontLoad);
                            var parentSources = parentItemInStack.sources;
                            var target = drag.source.source;

                            if (!sources || !group || !target) {
                                return;
                            }
                            var source = group.data.pop(target.uuid, false);
                            if (source) {
                                parentSources.takeItem(source);
                            }
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.topMargin: 2
                    visible: stack.depth > 1
                    text: qsTr("new captures go into this folder")
                    font.pixelSize: 10
                    opacity: 0.6
                }

                SourceLayout {
                    id: sourcesLayout
                    side: group ? "all" : "data"
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    sources: group ? group.data.sourceList : sourceList
                }
            }
        }

        // file and folder actions
        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            Layout.bottomMargin: 8
            columns: 4
            columnSpacing: 3
            rowSpacing: 3

            MiniButton {
                Layout.fillWidth: true
                text: qsTr("Folder")
                tooltip: qsTr("new folder (double-click a folder to open it)")
                onClicked: dataBar.currentList().addGroup()
            }
            MiniButton {
                Layout.fillWidth: true
                text: qsTr("Import")
                tooltip: qsTr("import a trace or impulse response (txt, csv, wav)")
                onClicked: importDialog.open()
            }
            MiniButton {
                Layout.fillWidth: true
                text: qsTr("Open")
                tooltip: qsTr("open a session file (.osm)")
                onClicked: openDialog.open()
            }
            MiniButton {
                Layout.fillWidth: true
                text: qsTr("Save")
                tooltip: qsTr("save the session (.osm)")
                onClicked: saveDialog.open()
            }
        }
    }

    Menu {
        id: addMenu
        MenuItem {
            text: qsTr("Folder")
            onTriggered: dataBar.currentList().addGroup()
        }
        MenuItem {
            text: qsTr("Math / average")
            onTriggered: sourceList.addUnion()
        }
        MenuItem {
            text: qsTr("Standard line")
            onTriggered: sourceList.addStandardLine()
        }
        MenuItem {
            text: qsTr("Filter")
            onTriggered: sourceList.addFilter()
        }
        MenuItem {
            text: qsTr("Equalizer")
            onTriggered: sourceList.addEqualizer()
        }
        MenuItem {
            text: qsTr("Windowing")
            onTriggered: sourceList.addWindowing()
        }
    }
}
