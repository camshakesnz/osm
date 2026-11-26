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

//! small flat button for the compact measurement and generator blocks
Rectangle {
    id: control
    property alias text: label.text
    property alias font: label.font
    property alias label: label
    property bool checkable: false
    property bool checked: false
    property color checkedColor: Material.accent
    property color textColor: Material.foreground
    property string tooltip: ""
    readonly property bool hovered: mouse.containsMouse
    readonly property color base: Material.foreground
    signal clicked(var mouse)

    implicitWidth: Math.max(24, label.implicitWidth + 12)
    implicitHeight: 24
    radius: 3
    color: checked ? Qt.rgba(checkedColor.r, checkedColor.g, checkedColor.b, 0.30)
                   : Qt.rgba(base.r, base.g, base.b, mouse.pressed ? 0.22 : (mouse.containsMouse ? 0.14 : 0.06))
    border.width: 1
    border.color: checked ? checkedColor : Qt.rgba(base.r, base.g, base.b, 0.16)
    opacity: enabled ? 1 : 0.4

    Label {
        id: label
        anchors.centerIn: parent
        font.pixelSize: 12
        color: control.textColor
        elide: Text.ElideRight
        width: Math.min(implicitWidth, control.width - 6)
        horizontalAlignment: Text.AlignHCenter
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton
        onClicked: function(m) {
            if (control.checkable) {
                control.checked = !control.checked;
            }
            control.clicked(m);
        }
    }

    ToolTip.visible: control.tooltip.length > 0 && mouse.containsMouse
    ToolTip.text: control.tooltip
    ToolTip.delay: 600
}
