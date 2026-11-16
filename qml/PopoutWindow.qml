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
import QtQuick.Window 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12

/**
 * A chart in its own window, e.g. the numeric meters on a second display.
 * Everything (type, meter layout, size, position, full screen) is kept in the
 * settings group layout/windows/<windowId>, so the window comes back next time.
 */
Window {
    id: popout

    property string windowId
    readonly property string group: "layout/windows/" + windowId
    property var groupSettings: applicationSettings.getGroup(group)
    //! the user closed this window (as opposed to the app quitting)
    signal closedByUser(string windowId)

    title: "Open Sound Meter: " + chart.type
    visible: true
    minimumWidth: 320
    minimumHeight: 200
    color: applicationAppearance.darkMode ? Material.backgroundColor : "#FFFFFF"
    Material.theme: applicationAppearance.darkMode ? Material.Dark : Material.Light
    Material.accent: Material.Indigo

    // the meter tiles read this for their background
    property string backgroundColor: Material.backgroundColor

    function toggleFullScreen() {
        visibility = (visibility === Window.FullScreen) ? Window.Windowed : Window.FullScreen;
    }

    Component.onCompleted: {
        width  = groupSettings.value("window/width", 800);
        height = groupSettings.value("window/height", 500);
        x      = groupSettings.value("window/x", x);
        y      = groupSettings.value("window/y", y);
        if (groupSettings.value("window/fullScreen", false) === true ||
            groupSettings.value("window/fullScreen", false) === "true") {
            visibility = Window.FullScreen;
        }
    }
    onWidthChanged:  if (visibility === Window.Windowed) groupSettings.setValue("window/width", width)
    onHeightChanged: if (visibility === Window.Windowed) groupSettings.setValue("window/height", height)
    onXChanged:      if (visibility === Window.Windowed) groupSettings.setValue("window/x", x)
    onYChanged:      if (visibility === Window.Windowed) groupSettings.setValue("window/y", y)
    onVisibilityChanged: groupSettings.setValue("window/fullScreen", visibility === Window.FullScreen)

    property bool quitting: false
    onClosing: {
        if (!quitting) {
            closedByUser(windowId);
        }
    }

    Rectangle {
        anchors.fill: parent
        color: applicationAppearance.darkMode ? Material.backgroundColor : "#ECECEC"
    }

    Chart {
        id: chart
        anchors.fill: parent
        canPopOut: false
        settings: popout.groupSettings
        type: popout.groupSettings.value("type", "Numeric")
        onTypeChanged: popout.groupSettings.setValue("type", type)
    }

    Button {
        anchors.top: parent.top
        anchors.left: parent.left
        flat: true
        implicitWidth: implicitHeight
        text: popout.visibility === Window.FullScreen ? "↙" : "⤢"
        font.pixelSize: 16
        opacity: hovered ? 1 : 0.35
        onClicked: popout.toggleFullScreen()
        ToolTip.visible: hovered
        ToolTip.text: qsTr("full screen (F11, Esc to leave)")
    }

    Shortcut {
        sequence: "F11"
        onActivated: popout.toggleFullScreen()
    }
    Shortcut {
        sequence: "Esc"
        enabled: popout.visibility === Window.FullScreen
        onActivated: popout.visibility = Window.Windowed
    }
}
