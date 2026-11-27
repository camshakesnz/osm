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

/**
 * Compact editable value: shows `displayText`, click to type a new value
 * (Enter applies, Esc cancels), mouse wheel steps it.
 */
Rectangle {
    id: box
    property string displayText: ""
    property string tooltip: ""
    property alias font: input.font
    readonly property color base: Material.foreground
    readonly property bool editing: input.activeFocus
    //! the typed text, when Enter is pressed
    signal accepted(string text)
    //! wheel: +1 / -1 per notch; `fine` when Shift is held
    signal stepped(int steps, bool fine)

    implicitWidth: 60
    implicitHeight: 24
    radius: 3
    color: Qt.rgba(base.r, base.g, base.b, input.activeFocus ? 0.12 : 0.05)
    border.width: 1
    border.color: input.activeFocus ? Material.accent : Qt.rgba(base.r, base.g, base.b, 0.16)

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: 3
        anchors.rightMargin: 3
        verticalAlignment: TextInput.AlignVCenter
        horizontalAlignment: TextInput.AlignHCenter
        color: Material.foreground
        selectionColor: Material.accent
        selectedTextColor: "white"
        font.pixelSize: 12
        selectByMouse: true
        clip: true
        Component.onCompleted: text = box.displayText

        onActiveFocusChanged: {
            if (activeFocus) {
                selectAll();
            } else {
                text = box.displayText;
            }
        }
        onAccepted: {
            box.accepted(text);
            focus = false;
            text = box.displayText;
        }
        Keys.onEscapePressed: {
            text = box.displayText;
            focus = false;
        }
    }

    Connections {
        target: box
        function onDisplayTextChanged() {
            if (!input.activeFocus) {
                input.text = box.displayText;
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        id: hoverArea
        onWheel: function(wheel) {
            var d = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.angleDelta.x;
            if (d !== 0) {
                box.stepped(d > 0 ? 1 : -1, (wheel.modifiers & Qt.ShiftModifier) !== 0);
            }
            wheel.accepted = true;
        }
    }

    ToolTip {
        visible: box.tooltip.length > 0 && hoverArea.containsMouse && !input.activeFocus
        text: box.tooltip
        delay: 600
    }
}
