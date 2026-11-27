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
import "elements"
import QtQuick.Controls 2.15 as C2

ApplicationWindow {
    id:applicationWindow

    property alias properiesbar: bottomtab
    property alias charts: charts
    property alias dataSourceList : dataBar
    property alias liveBar : righttab
    property alias message : message
    property alias dialog : dialog
    property alias sideMenu: sideMenu
    property string backgroundColor: Material.backgroundColor
    property string accentColor: Material.accent
    property string foregroundColor: Material.foreground
    readonly property bool narrowWindow: width < 1024

    function boolSetting(key, defaultValue) {
        var v = applicationSettings.value(key);
        if (v === undefined) {
            return defaultValue;
        }
        return v === true || v === "true";
    }

    // ---------------------------------------------------------------- layout
    //! stored traces and folders on the left
    property bool dataBarVisible: boolSetting("ui/dataBar", true)
    onDataBarVisibleChanged: applicationSettings.setValue("ui/dataBar", dataBarVisible)
    //! settings along the bottom (the old way) instead of a floating window
    property bool settingsDocked: boolSetting("ui/settingsDocked", false)
    onSettingsDockedChanged: {
        applicationSettings.setValue("ui/settingsDocked", settingsDocked);
        if (settingsDocked) {
            settingsWindow.close();
        } else if (bottomtab.currentObject !== null || bottomtab.currentQml) {
            settingsWindow.open();
        }
    }

    //! big buttons along the bottom (Capture, Average, Align...)
    property bool commandBarVisible: boolSetting("ui/commandBar", true)
    onCommandBarVisibleChanged: applicationSettings.setValue("ui/commandBar", commandBarVisible)
    //! the trace last clicked in either list (for dB +/-, Capture)
    property var selectedSource: null

    function forEachSource(list, fn) {
        for (var i = 0; i < list.count; ++i) {
            var s = list.get(i);
            if (!s || !s.data) {
                continue;
            }
            fn(s);
            if (s.data.objectName === "Group" && s.data.sourceList) {
                forEachSource(s.data.sourceList, fn);
            }
        }
    }
    //! "Measure @ 14:34", then "Measure @ 14:34 (2)"... so captures can be told apart
    function uniqueName(name) {
        var taken = {};
        forEachSource(sourceList, function(s) { taken[s.data.name] = true; });
        if (!taken[name]) {
            return name;
        }
        for (var n = 2; ; ++n) {
            if (!taken[name + " (" + n + ")"]) {
                return name + " (" + n + ")";
            }
        }
    }
    function storeMeasurement(source) {
        var stored = source.store();
        if (stored && stored.data) {
            stored.data.autoName(source.name);
            stored.data.name = uniqueName(stored.data.name);
            stored.data.active = true;
            sourceList.appendCapture(stored);
        }
    }
    //! Capture: the selected measurement, or the first active one
    function captureSelected() {
        var s = selectedSource && selectedSource.data ? selectedSource.data : null;
        if (s && s.objectName === "Measurement") {
            storeMeasurement(s);
            return;
        }
        for (var i = 0; i < sourceList.count; i++) {
            var m = sourceList.get(i).data;
            if (m && m.active && !m.chartHidden && m.objectName === "Measurement") {
                storeMeasurement(m);
                return;
            }
        }
    }
    function resetAverages() {
        for (var i = 0; i < sourceList.count; i++) {
            var m = sourceList.get(i).data;
            if (m && m.objectName === "Measurement") {
                m.resetAverage();
            }
        }
    }
    //! dB + / dB -: gain offset of the selected trace
    function nudgeGain(delta) {
        var s = selectedSource && selectedSource.data ? selectedSource.data : null;
        if (!s || s.gain === undefined) {
            message.showError(qsTr("Click a trace or measurement first."));
            return;
        }
        s.gain = Math.round((s.gain + delta) * 10) / 10;
    }
    function clearAllDb() {
        forEachSource(sourceList, function(s) {
            if (s.data.objectName === "Stored") {
                s.data.gain = 0;
            }
        });
    }

    //! store every active measurement (Ctrl+X, Capture)
    function captureAll() {
        for (var i = 0; i < sourceList.count; i++) {
            let source = sourceList.get(i).data;
            // measurements kept off the charts (e.g. a stream feed) aren't captured
            if (source && source.active && !source.chartHidden && source.objectName === "Measurement") {
                storeMeasurement(source);
            }
        }
    }

    // ---------------------------------------------------------------- views
    // Each view keeps its own charts under views/<id>/charts (see Charts.qml).
    property var viewList: []
    property string currentViewId: initViews()
    property string previousViewId: ""
    onCurrentViewIdChanged: applicationSettings.setValue("views/current", currentViewId)

    function viewIdsFromSettings() {
        var ids = applicationSettings.value("views/list");
        if (ids === undefined || ids === null || String(ids).length === 0) {
            return [];
        }
        return String(ids).split("|").filter(function(id) { return id.length > 0; });
    }
    function saveViewList(ids) {
        applicationSettings.setValue("views/list", ids.join("|"));
        viewList = ids;
    }
    function viewName(id) {
        var name = applicationSettings.value("views/" + id + "/name");
        return name === undefined ? id : String(name);
    }
    function presetView(id, name, types, weights) {
        var g = "views/" + id;
        applicationSettings.setValue(g + "/name", name);
        applicationSettings.setValue(g + "/charts/count", types.length);
        for (var i = 0; i < types.length; ++i) {
            applicationSettings.setValue(g + "/charts/" + (i + 1) + "/type", types[i]);
        }
        applicationSettings.setValue(g + "/charts/weights", weights);
    }
    //! the built-in views
    function hasDefault(id) {
        return id === "live" || id === "tf";
    }
    function defaultView(id) {
        applicationSettings.removeGroup("views/" + id + "/charts");
        if (id === "live") {
            // three SPL meters along the top, spectrum, spectrograph
            presetView("live", qsTr("Live"), ["Numeric", "Spectrum", "Spectrogram"], "0.2,0.42,0.38");
            applicationSettings.setValue("views/live/charts/1/rows", 1);
            applicationSettings.setValue("views/live/charts/1/columns", 3);
            applicationSettings.setValue("views/live/charts/2/fill", true);
        } else if (id === "tf") {
            presetView("tf", qsTr("TF"), ["Impulse", "Magnitude", "Phase"], "0.26,0.4,0.34");
        }
    }
    //! put a built-in view back to how it starts (keeps its name)
    function resetView(id) {
        if (!hasDefault(id)) {
            return;
        }
        var name = viewName(id);
        defaultView(id);
        applicationSettings.setValue("views/" + id + "/name", name);
        if (id === currentViewId) {
            bottomtab.reset();
            charts.loadView();
        }
    }

    //! first start with views: a Live and a TF view, plus the charts the user already had
    function initViews() {
        var ids = viewIdsFromSettings();
        if (ids.length === 0) {
            defaultView("live");
            defaultView("tf");
            ids = ["live", "tf"];
            if (applicationSettings.value("layout/charts/1/type") !== undefined) {
                applicationSettings.copyGroup("layout/charts", "views/mine/charts");
                applicationSettings.setValue("views/mine/name", qsTr("My layout"));
                ids.push("mine");
            }
            applicationSettings.setValue("views/list", ids.join("|"));
        }
        viewList = ids;
        var current = applicationSettings.value("views/current");
        current = current === undefined ? "" : String(current);
        return ids.indexOf(current) >= 0 ? current : ids[0];
    }
    function setView(id) {
        if (id === currentViewId || viewList.indexOf(id) < 0) {
            return;
        }
        // chart settings belong to the charts being replaced
        if (bottomtab.currentQml && /\/(Plot|SPL|Loudness)\//.test(String(bottomtab.currentQml))) {
            bottomtab.reset();
        }
        previousViewId = currentViewId;
        currentViewId = id;
    }
    //! Space: back to the previous view (or the next one)
    function toggleView() {
        if (previousViewId && viewList.indexOf(previousViewId) >= 0) {
            setView(previousViewId);
        } else if (viewList.length > 1) {
            setView(viewList[(viewList.indexOf(currentViewId) + 1) % viewList.length]);
        }
    }
    function addView(copyFrom) {
        var id = "v" + Date.now().toString(36);
        if (copyFrom) {
            applicationSettings.copyGroup("views/" + copyFrom + "/charts", "views/" + id + "/charts");
        }
        applicationSettings.setValue("views/" + id + "/name", qsTr("View %1").arg(viewList.length + 1));
        var ids = viewList.slice();
        ids.push(id);
        saveViewList(ids);
        setView(id);
        return id;
    }
    function renameView(id, name) {
        applicationSettings.setValue("views/" + id + "/name", name);
        viewList = viewList.slice(); // refresh the buttons
    }
    function deleteView(id) {
        if (viewList.length < 2) {
            return;
        }
        var ids = viewList.filter(function(v) { return v !== id; });
        if (currentViewId === id) {
            setView(previousViewId && ids.indexOf(previousViewId) >= 0 ? previousViewId : ids[0]);
        }
        if (previousViewId === id) {
            previousViewId = "";
        }
        saveViewList(ids);
        applicationSettings.removeGroup("views/" + id);
    }

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
    // darker than Material's dark grey, closer to Smaart
    Material.background: applicationAppearance.darkMode ? "#161618" : "#FAFAFA"
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

    readonly property color dividerColor: Qt.rgba(Material.foreground.r, Material.foreground.g, Material.foreground.b, 0.12)

    ColumnLayout {
    anchors.fill: parent
    spacing: 0

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 0

        // stored traces, folders, files
        DataBar {
            id: dataBar
            Layout.fillHeight: true
            Layout.preferredWidth: 220
            visible: applicationWindow.dataBarVisible && applicationWindow.width >= 980
        }
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: dividerColor
            visible: dataBar.visible
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            //Charts area
            Charts {
                id: charts
                Layout.fillWidth: true
                Layout.fillHeight: true
                viewId: applicationWindow.currentViewId
            }

            // settings docked along the bottom (optional)
            Item {
                id: dockHost
                Layout.fillWidth: true
                Layout.preferredHeight: applicationWindow.settingsDocked ? 150 : 0
                visible: applicationWindow.settingsDocked

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: dividerColor
                }
                C2.Label {
                    id: dockTitle
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 10
                    anchors.topMargin: 4
                    text: bottomtab.title
                    font.bold: true
                    font.pixelSize: 12
                    opacity: 0.8
                }
                MiniButton {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 3
                    text: qsTr("Float")
                    tooltip: qsTr("show settings in a floating window instead")
                    onClicked: applicationWindow.settingsDocked = false
                }
                Item {
                    id: dockBody
                    anchors.fill: parent
                    anchors.topMargin: 24
                }
            }
        }

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: dividerColor
        }

        // views, measurements, generator
        LiveBar {
            id: righttab
            Layout.fillHeight: true
            Layout.preferredWidth: 262
        }
    }

    CommandBar {
        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight
        visible: applicationWindow.commandBarVisible
    }
    }

    Tools {
        id: tools
    }

    //Properties area: in the floating settings window, or docked along the bottom
    PropetiesBar {
        id: bottomtab
        parent: applicationWindow.settingsDocked ? dockBody : floatBody
        anchors.fill: parent
        shown: applicationWindow.settingsDocked || settingsWindow.opened
        onShowRequested: {
            if (!applicationWindow.settingsDocked && !settingsWindow.opened) {
                settingsWindow.open();
            }
        }
    }

    C2.Popup {
        id: settingsWindow
        parent: C2.Overlay.overlay
        modal: false
        focus: true
        closePolicy: C2.Popup.CloseOnEscape
        padding: 0
        width: Math.min(applicationWindow.width - 16, 1180)
        height: 176

        function clampPosition() {
            var ox = Number(applicationSettings.value("ui/settingsWindow/x", -1));
            var oy = Number(applicationSettings.value("ui/settingsWindow/y", -1));
            if (ox < 0 || oy < 0) {
                // first time: along the bottom of the charts
                ox = Math.max(8, (charts.mapToItem(null, 0, 0).x + (charts.width - width) / 2));
                oy = applicationWindow.contentItem.height - height - 10;
            }
            x = Math.max(0, Math.min(ox, applicationWindow.width - width));
            y = Math.max(0, Math.min(oy, applicationWindow.contentItem.height - height));
        }
        onAboutToShow: clampPosition()

        background: Rectangle {
            color: applicationAppearance.darkMode ? "#222225" : "#fafafa"
            radius: 6
            border.width: 1
            border.color: Qt.rgba(Material.foreground.r, Material.foreground.g, Material.foreground.b, 0.25)
        }

        contentItem: ColumnLayout {
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 30

                // drag the window by its title bar
                MouseArea {
                    id: dragBar
                    anchors.fill: parent
                    cursorShape: Qt.SizeAllCursor
                    property point offset
                    onPressed: function(m) {
                        var p = mapToItem(null, m.x, m.y);
                        offset = Qt.point(p.x - settingsWindow.x, p.y - settingsWindow.y);
                    }
                    onPositionChanged: function(m) {
                        var p = mapToItem(null, m.x, m.y);
                        settingsWindow.x = Math.max(0, Math.min(p.x - offset.x, applicationWindow.width - settingsWindow.width));
                        settingsWindow.y = Math.max(0, Math.min(p.y - offset.y, applicationWindow.contentItem.height - settingsWindow.height));
                    }
                    onReleased: {
                        applicationSettings.setValue("ui/settingsWindow/x", Math.round(settingsWindow.x));
                        applicationSettings.setValue("ui/settingsWindow/y", Math.round(settingsWindow.y));
                    }
                }

                C2.Label {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: titleButtons.left
                    text: "⚙  " + (bottomtab.title ? bottomtab.title : qsTr("Settings"))
                    font.bold: true
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }

                Row {
                    id: titleButtons
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    MiniButton {
                        text: qsTr("Dock")
                        tooltip: qsTr("keep settings along the bottom of the window")
                        onClicked: applicationWindow.settingsDocked = true
                    }
                    MiniButton {
                        text: "✕"
                        tooltip: qsTr("close (Esc)")
                        onClicked: settingsWindow.close()
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: dividerColor
                }
            }

            Item {
                id: floatBody
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }

    Shortcut {
        sequence: "Ctrl+B"
        onActivated: applicationWindow.dataBarVisible = !applicationWindow.dataBarVisible
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
            dataBar.list.pop(null);
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
