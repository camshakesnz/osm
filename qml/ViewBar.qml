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
 * View buttons: each view keeps its own charts (e.g. Live: RTA, spectrograph,
 * SPL; TF: impulse, phase, magnitude). Right-click a view to rename, duplicate
 * or delete it. Space flips between the last two views, Alt+1…6 picks one.
 */
Item {
    id: viewBar
    implicitHeight: flow.implicitHeight

    Flow {
        id: flow
        width: parent.width
        spacing: 3

        Repeater {
            model: applicationWindow.viewList

            MiniButton {
                id: viewButton
                width: Math.max(44, implicitWidth)
                height: 28
                text: applicationWindow.viewName(modelData)
                font.pixelSize: 12
                font.bold: checked
                checked: applicationWindow.currentViewId === modelData
                tooltip: qsTr("view %1 (Alt+%2), right-click for options").arg(text).arg(index + 1)
                onClicked: applicationWindow.setView(modelData)

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: {
                        viewMenu.viewId = modelData;
                        viewMenu.popup();
                    }
                    onPressAndHold: {
                        viewMenu.viewId = modelData;
                        viewMenu.popup();
                    }
                }
            }
        }

        MiniButton {
            width: 28
            height: 28
            text: "+"
            tooltip: qsTr("new view (a copy of this one)")
            onClicked: {
                var id = applicationWindow.addView(applicationWindow.currentViewId);
                renameDialog.openFor(id);
            }
        }
    }

    Menu {
        id: viewMenu
        property string viewId: ""
        MenuItem {
            text: qsTr("Rename…")
            onTriggered: renameDialog.openFor(viewMenu.viewId)
        }
        MenuItem {
            text: qsTr("Duplicate")
            onTriggered: {
                var id = applicationWindow.addView(viewMenu.viewId);
                renameDialog.openFor(id);
            }
        }
        MenuItem {
            text: qsTr("Delete")
            enabled: applicationWindow.viewList.length > 1
            onTriggered: applicationWindow.deleteView(viewMenu.viewId)
        }
    }

    Popup {
        id: renameDialog
        property string viewId: ""
        parent: Overlay.overlay
        modal: true
        x: (parent.width - width) / 2
        y: (parent.height - height) / 3
        width: 300
        padding: 14

        function openFor(id) {
            viewId = id;
            nameField.text = applicationWindow.viewName(id);
            visible = true;
            nameField.forceActiveFocus();
            nameField.selectAll();
        }
        function accept() {
            if (nameField.text.trim().length > 0) {
                applicationWindow.renameView(viewId, nameField.text.trim());
            }
            close();
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                text: qsTr("View name")
                font.bold: true
            }
            TextField {
                id: nameField
                Layout.fillWidth: true
                selectByMouse: true
                onAccepted: renameDialog.accept()
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    text: qsTr("Cancel")
                    flat: true
                    onClicked: renameDialog.close()
                }
                Button {
                    text: qsTr("OK")
                    flat: true
                    onClicked: renameDialog.accept()
                }
            }
        }
    }

    Shortcut {
        sequence: "Space"
        onActivated: applicationWindow.toggleView()
    }
    Repeater {
        model: 6
        Item {
            Shortcut {
                sequence: "Alt+" + (index + 1)
                onActivated: {
                    if (index < applicationWindow.viewList.length) {
                        applicationWindow.setView(applicationWindow.viewList[index]);
                    }
                }
            }
        }
    }
}
