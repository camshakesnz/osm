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
import QtQuick 2.7
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.1
import QtQuick.Layouts 1.1
import QtQuick.Window 2.2
import QtQuick.Controls 1.4
import QtQuick.Dialogs 1.2
import SourceModel 1.0
import OpenSoundMeter 1.0

import "menu" as Menu

ApplicationWindow {
    id:applicationWindow

    property alias properiesbar: bottomtab
    property alias charts: charts
    property alias dataSourceList : righttab
    property alias message : message
    property alias dialog : dialog
    property alias sideMenu: sideMenu
    property string backgroundColor: Material.backgroundColor
    property string accentColor: Material.accent
    property string foregroundColor: Material.foreground
    readonly property bool narrowWindow: width < 1024

    visible: true
    flags: Qt.Window
    title: "Open Sound Meter"
    Component.onCompleted: {
        if (applicationAppearance.visibility === Appearance.FullScreen) {
            return ;
        }

        var mainwindowSettings = applicationSettings.getGroup("mainwindow");

        width   = mainwindowSettings.value("width", minimumWidth);
        height  = mainwindowSettings.value("height", minimumHeight);
        x       = mainwindowSettings.value("x", (Screen.width  - width)  / 2);
        y       = mainwindowSettings.value("y", (Screen.height - height) / 2);

        applicationWindow.onWidthChanged.connect(function() {applicationSettings.setValue("mainwindow/width", width)});
        applicationWindow.onHeightChanged.connect(function() {applicationSettings.setValue("mainwindow/height", height)});
        applicationWindow.onXChanged.connect(function() {applicationSettings.setValue("mainwindow/x", x)});
        applicationWindow.onYChanged.connect(function() {applicationSettings.setValue("mainwindow/y", y)});

        restorePopouts();
    }

    // ---------------------------------------------------------------- pop-out windows
    // Charts opened in their own windows (e.g. numeric meters on a second display).
    // Open ones are listed in layout/windows/ids and come back on the next start.
    property var popoutWindows: ({})

    Component {
        id: popoutComponent
        PopoutWindow {}
    }

    function popoutIds() {
        var ids = applicationSettings.value("layout/windows/ids", "");
        if (!ids) {
            return [];
        }
        return String(ids).split(",").filter(function(id) { return id.length > 0; });
    }

    function savePopoutIds() {
        applicationSettings.setValue("layout/windows/ids", Object.keys(popoutWindows).join(","));
    }

    function createPopout(windowId) {
        var w = popoutComponent.createObject(null, { windowId: windowId });
        if (!w) {
            return null;
        }
        w.closedByUser.connect(function(id) {
            // Forget the window only if the app is still running a moment later:
            // quitting (e.g. Cmd-Q on macOS) can close pop-outs before the main window,
            // and those should come back on the next start.
            var timer = Qt.createQmlObject("import QtQuick 2.0; Timer { interval: 1500; running: true }",
                                           applicationWindow, "popoutCloseTimer");
            timer.triggered.connect(function() {
                timer.destroy();
                if (applicationWindow.askBeforeClose === false) {
                    return;
                }
                delete popoutWindows[id];
                savePopoutIds();
                applicationSettings.removeGroup("layout/windows/" + id);
                w.destroy();
            });
        });
        popoutWindows[windowId] = w;
        return w;
    }

    //! open a chart of `type` in a new window, copying the settings of `fromGroup` if given
    function popOut(type, fromGroup) {
        var windowId = "w" + Date.now().toString(36);
        var group = "layout/windows/" + windowId;
        if (fromGroup) {
            applicationSettings.copyGroup(fromGroup, group);
        }
        applicationSettings.setValue(group + "/type", type || "Numeric");

        // start on another display when there is one
        var screens = Qt.application.screens;
        var target = null;
        for (var i = 0; i < screens.length; ++i) {
            if (screens[i].name !== applicationWindow.screen.name) {
                target = screens[i];
                break;
            }
        }
        var size = { width: 800, height: 500 };
        if (target) {
            applicationSettings.setValue(group + "/window/x", target.virtualX + (target.width - size.width) / 2);
            applicationSettings.setValue(group + "/window/y", target.virtualY + (target.height - size.height) / 2);
        } else {
            applicationSettings.setValue(group + "/window/x", applicationWindow.x + 60);
            applicationSettings.setValue(group + "/window/y", applicationWindow.y + 60);
        }
        applicationSettings.setValue(group + "/window/width", size.width);
        applicationSettings.setValue(group + "/window/height", size.height);
        applicationSettings.setValue(group + "/window/fullScreen", false);

        var w = createPopout(windowId);
        savePopoutIds();
        if (w) {
            w.raise();
            w.requestActivate();
        }
    }

    function restorePopouts() {
        var ids = popoutIds();
        for (var i = 0; i < ids.length; ++i) {
            createPopout(ids[i]);
        }
        savePopoutIds();
    }

    //! close pop-outs when the app quits, keeping them for the next start
    function closePopoutsForQuit() {
        for (var id in popoutWindows) {
            popoutWindows[id].quitting = true;
            popoutWindows[id].close();
        }
    }
    minimumWidth: 768
    minimumHeight: 540
    visibility: applicationAppearance.visibility
    color: applicationAppearance.darkMode ? Material.backgroundColor : "#FFFFFF"
    Material.theme: applicationAppearance.darkMode ? Material.Dark : Material.Light
    Material.accent: Material.Indigo

    Rectangle {
        color: applicationAppearance.darkMode ? Material.backgroundColor : "#ECECEC"
        height: applicationWindow.height
        anchors.fill: parent
        z: -1
    }

    property bool askBeforeClose : true
    onClosing: function (close) {
        if (!askBeforeClose) {
            return;
        }

        dialog.title = qsTr("Close Open Sound Meter?")
        dialog.accepted.connect(closeAccepted);
        dialog.rejected.connect(closeRejected);
        close.accepted = false;
        dialog.open();
    }
    function closeAccepted() {
        askBeforeClose = false;
        autoSaver.stop();
        autoSaver.save();
        closePopoutsForQuit();
        close();
    }
    function closeRejected() {
        dialog.accepted.disconnect(closeAccepted);
        dialog.rejected.disconnect(closeRejected);
    }

    Menu.Side {
        id:sideMenu
        interactive: applicationAppearance.showMenuBar ? false : true
    }
    Menu.Top {
        id: topMenu;
    }

    menuBar: (applicationAppearance.showMenuBar ? topMenu : null)

    GridLayout {
        anchors.fill: parent
        flow: Qt.LeftToRight
        columns: 2
        columnSpacing: 0
        rowSpacing: 0

        //Charts area
        Charts {
            id: charts
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        SideBar {
            id: righttab
            Layout.fillHeight: true
            Layout.rowSpan: applicationWindow.narrowWindow ? 1 : 2
            width: 200
        }

        //Properties area
        PropetiesBar {
            id: bottomtab
            height: 120
            Layout.fillWidth: true
            Layout.columnSpan: applicationWindow.narrowWindow ? 2 : 1
        }

    }

    About {
        id: aboutpopup
        x: 100
        y: 100
        width: applicationWindow.width - 200
        height: applicationWindow.height - 200
        visible: applicationAppearance.showAboutOnStartup
    }

    Updater {
        id: update
    }

    Message {
        id: message

        Connections {
            target: notifier
            function onNewMessage(title, text) {
                message.showError(title + "<br/>" + text);
            }
        }
    }

    ModalDialog {
        id: dialog
    }

    Shortcuts {
        id:shortcutsPopup
        anchors.centerIn: parent
    }

    FileDialog {
        id: saveDialog
        selectExisting: false
        title: qsTr("Please choose a file's name")
        folder: (typeof shortcuts !== 'undefined' ? shortcuts.home : Filesystem.StandardFolder.Home)
        defaultSuffix: "osm"
        nameFilters: ["Open Sound Meter (*.osm)"]
        onAccepted: sourceList.save(saveDialog.fileUrl);
    }

    FileDialog {
        id: openDialog
        selectExisting: true
        title: qsTr("Please choose a file's name")
        folder: (typeof shortcuts !== 'undefined' ? shortcuts.home : Filesystem.StandardFolder.Home)
        defaultSuffix: "osm"
        nameFilters: ["Open Sound Meter (*.osm)"]
        onAccepted: function() {
            applicationWindow.properiesbar.clear();
            if (!sourceList.load(openDialog.fileUrl)) {
                message.showError(qsTr("could not open the file"));
            }
        }
    }

    FileDialog {
        id: importDialog
        selectExisting: true
        title: qsTr("Please choose a file's name")
        folder: (typeof shortcuts !== 'undefined' ? shortcuts.home : Filesystem.StandardFolder.Home)
        defaultSuffix: "txt"
        nameFilters: [
            "txt transfer data file (*.txt *.cal)",
            "csv transfer data file (*.csv)",
            "txt impulse data file (*.txt)",
            "csv impulse data file (*.csv)",
            "wav impulse data file (*.wav)"
        ]
        onAccepted: function() {
            applicationWindow.properiesbar.clear();
            if (!sourceList.import(importDialog.fileUrl, nameFilters.indexOf(selectedNameFilter))) {
                message.showError(qsTr("could not open the file"));
            }
        }
    }
}
