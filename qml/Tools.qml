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
 * Tuning tools: average traces, compare before/after, main + sub alignment,
 * and guided spatial averaging (each capture goes into a folder and a running
 * average of the folder updates).
 */
Item {
    id: tools
    readonly property color base: Material.foreground

    // Union (math source) values; the enum names can't all be used from QML ("dB")
    readonly property int opSum: 0
    readonly property int opSubtract: 1
    readonly property int opAvg: 2
    readonly property int typeVector: 0
    readonly property int typeDB: 2
    readonly property int typePower: 3

    // ------------------------------------------------------------ helpers
    function isTrace(source) {
        return source && source.data && source.data.objectName !== "Group";
    }

    //! a math source (union) in the list being worked in
    function makeUnion(list, sources, operation, type, name) {
        var target = list ? list : applicationWindow.dataSourceList.currentList();
        var created = target.addUnion();
        if (!created || !created.data) {
            return null;
        }
        var u = created.data;
        u.autoName = false;
        u.count = sources.length;
        for (var i = 0; i < sources.length; ++i) {
            u.setSource(i, sources[i]);
        }
        u.type = type;
        u.operation = operation;
        u.name = name;
        u.active = true;
        return created;
    }

    function makeAverage(sources, type) {
        if (sources.length < 2) {
            applicationWindow.message.showError(qsTr("Choose at least two traces to average."));
            return null;
        }
        return makeUnion(null, sources, tools.opAvg, type, qsTr("Avg of %1").arg(sources.length));
    }

    function makeDifference(before, after) {
        if (!before || !after) {
            return null;
        }
        return makeUnion(null, [after, before], tools.opSubtract, tools.typeDB,
                         qsTr("Δ %1 vs %2").arg(after.data.name).arg(before.data.name));
    }

    // ------------------------------------------------------------ spatial averaging
    property bool spatialActive: false
    property var spatialFolder: null
    property var spatialAverage: null
    property int spatialCount: 0

    function startSpatial() {
        var folder = sourceList.addGroup();
        if (!folder || !folder.data) {
            return;
        }
        var now = new Date();
        folder.data.name = qsTr("Spatial avg %1").arg(Qt.formatTime(now, "HH:mm"));
        spatialFolder = folder;
        spatialAverage = null;
        spatialCount = 0;
        spatialActive = true;
        // open it: new captures (Capture, Capture All, chirp auto-store) go into it
        applicationWindow.dataSourceList.list.pop(null);
        applicationWindow.dataSourceList.list.openGroup(folder);
        applicationWindow.dataBarVisible = true;
    }

    function stopSpatial() {
        spatialActive = false;
        if (applicationWindow.dataSourceList.openFolder === spatialFolder) {
            applicationWindow.dataSourceList.list.pop(null);
        }
        spatialFolder = null;
        spatialAverage = null;
    }

    property var spatialPending: []
    function processSpatial() {
        var items = spatialPending;
        spatialPending = [];
        for (var i = 0; i < items.length; ++i) {
            addToSpatial(items[i]);
        }
    }

    function addToSpatial(item) {
        if (!spatialActive || !item || !item.data || item.data.objectName !== "Stored") {
            return;
        }
        spatialCount += 1;
        item.data.name = qsTr("Pos %1").arg(spatialCount);
        var list = spatialFolder.data.sourceList;
        if (!spatialAverage || !spatialAverage.data) {
            if (spatialCount < 2) {
                return;
            }
            // the first two positions start the running average
            var first = null;
            for (var i = 0; i < list.count; ++i) {
                var s = list.get(i);
                if (s && s.data && s.data.objectName === "Stored" && s !== item) {
                    first = s;
                    break;
                }
            }
            if (!first) {
                return;
            }
            spatialAverage = makeUnion(list, [first, item], tools.opAvg, tools.typeDB,
                                       spatialFolder.data.name);
        } else {
            var u = spatialAverage.data;
            var n = u.count;
            u.count = n + 1;
            u.setSource(n, item);
        }
    }

    Connections {
        target: tools.spatialActive && tools.spatialFolder && tools.spatialFolder.data ?
                    tools.spatialFolder.data.sourceList : null
        function onPostItemAppended(item) {
            // let the items settle in the list first (Capture All adds several at once;
            // Qt.callLater runs a function once per turn, so queue them)
            tools.spatialPending.push(item);
            Qt.callLater(tools.processSpatial);
        }
    }

    // ------------------------------------------------------------ dialogs
    component DialogFrame: Popup {
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: 60
        padding: 14
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: applicationAppearance.darkMode ? "#222225" : "#fafafa"
            radius: 6
            border.width: 1
            border.color: Qt.rgba(tools.base.r, tools.base.g, tools.base.b, 0.25)
        }
    }

    SourceModel {
        id: allTraces
        unrollGroups: true
        list: sourceList
    }

    // Average
    DialogFrame {
        id: averageDialog
        width: 380
        height: Math.min(applicationWindow.height - 120, 520)
        property var picked: ({})

        function openDialog() {
            var picked = {};
            for (var i = 0; i < allTraces.count; ++i) {
                var s = allTraces.getShared(i);
                if (tools.isTrace(s) && s.data.objectName === "Stored" && s.data.active) {
                    picked[allTraces.get(i)] = true;
                }
            }
            averageDialog.picked = picked;
            open();
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                text: qsTr("Average traces")
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Tick the traces to average. Live measurements can be averaged too (e.g. several mics).")
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                opacity: 0.7
            }
            ListView {
                id: averageList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: allTraces
                delegate: CheckBox {
                    width: averageList.width
                    height: tools.isTrace(model.source) ? implicitHeight : 0
                    visible: tools.isTrace(model.source)
                    text: model.title
                    checked: averageDialog.picked[allTraces.get(index)] === true
                    onClicked: {
                        var p = averageDialog.picked;
                        p[allTraces.get(index)] = checked;
                        averageDialog.picked = p;
                    }
                }
                ScrollIndicator.vertical: ScrollIndicator {}
            }
            RowLayout {
                Layout.fillWidth: true
                TitledCombo {
                    id: averageType
                    title: qsTr("type")
                    tooltip: qsTr("dB: average of the levels in dB (spatial averaging). Power: energy average.")
                    model: ["dB", "Power"]
                    Layout.preferredWidth: 150
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Cancel")
                    flat: true
                    onClicked: averageDialog.close()
                }
                Button {
                    text: qsTr("Average")
                    flat: true
                    onClicked: {
                        var sources = [];
                        for (var i = 0; i < allTraces.count; ++i) {
                            if (averageDialog.picked[allTraces.get(i)] === true) {
                                sources.push(allTraces.getShared(i));
                            }
                        }
                        if (tools.makeAverage(sources, averageType.currentIndex === 0 ? tools.typeDB : tools.typePower)) {
                            averageDialog.close();
                        }
                    }
                }
            }
        }
    }

    // Compare before / after
    DialogFrame {
        id: compareDialog
        width: 420

        function openDialog() {
            // default: the two most recent stored traces, older one as "before"
            var stored = [];
            for (var i = 0; i < allTraces.count; ++i) {
                var s = allTraces.getShared(i);
                if (s && s.data && s.data.objectName === "Stored") {
                    stored.push(i);
                }
            }
            if (stored.length >= 2) {
                beforeSelect.currentIndex = stored[stored.length - 2];
                afterSelect.currentIndex = stored[stored.length - 1];
            }
            open();
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                text: qsTr("Compare before / after")
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Makes a difference trace (after − before). On a magnitude chart, 0 dB means nothing changed; above 0 means the change added level there.")
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                opacity: 0.7
            }
            TitledCombo {
                id: beforeSelect
                Layout.fillWidth: true
                title: qsTr("before")
                model: allTraces
                textRole: "title"
            }
            TitledCombo {
                id: afterSelect
                Layout.fillWidth: true
                title: qsTr("after")
                model: allTraces
                textRole: "title"
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    text: qsTr("Cancel")
                    flat: true
                    onClicked: compareDialog.close()
                }
                Button {
                    text: qsTr("Compare")
                    flat: true
                    onClicked: {
                        var before = allTraces.getShared(beforeSelect.currentIndex);
                        var after = allTraces.getShared(afterSelect.currentIndex);
                        if (!tools.isTrace(before) || !tools.isTrace(after) || before === after) {
                            applicationWindow.message.showError(qsTr("Choose two different traces."));
                            return;
                        }
                        tools.makeDifference(before, after);
                        compareDialog.close();
                    }
                }
            }
        }
    }

    // Main + sub alignment
    DialogFrame {
        id: alignDialog
        width: 460
        property var a: null
        property var b: null
        property var stats: ({})
        property var sum: null
        property real crossover: 90
        readonly property bool bAdjustable: b && b.data && b.data.objectName === "Stored"

        function pick() {
            a = allTraces.getShared(aSelect.currentIndex);
            b = allTraces.getShared(bSelect.currentIndex);
            sum = null;
            refresh();
        }
        function refresh() {
            if (!tools.isTrace(a) || !tools.isTrace(b)) {
                stats = {};
                return;
            }
            stats = sourceList.alignment(a.data.uuid, b.data.uuid, crossover, 0);
        }
        function findBest() {
            if (!bAdjustable) {
                return;
            }
            var r = sourceList.alignment(a.data.uuid, b.data.uuid, crossover, 10);
            if (!r.ok) {
                stats = r;
                return;
            }
            b.data.delay = Math.round((b.data.delay + r.delayMs) * 100) / 100;
            if (r.invert) {
                b.data.polarity = !b.data.polarity;
            }
            refresh();
        }
        function showSum() {
            if (!tools.isTrace(a) || !tools.isTrace(b)) {
                return;
            }
            if (sum && sum.data) {
                sum.data.active = true;
                return;
            }
            sum = tools.makeUnion(null, [a, b], tools.opSum, tools.typeVector,
                                  qsTr("Sum %1 + %2").arg(a.data.name).arg(b.data.name));
        }
        function openDialog() {
            var stored = [];
            for (var i = 0; i < allTraces.count; ++i) {
                var s = allTraces.getShared(i);
                if (s && s.data && s.data.objectName === "Stored") {
                    stored.push(i);
                }
            }
            if (stored.length >= 2) {
                aSelect.currentIndex = stored[stored.length - 2];
                bSelect.currentIndex = stored[stored.length - 1];
            }
            pick();
            open();
        }

        Timer {
            interval: 500
            repeat: true
            running: alignDialog.opened
            onTriggered: alignDialog.refresh()
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            // drag by the title
            Label {
                Layout.fillWidth: true
                text: qsTr("Main + sub alignment")
                font.bold: true
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeAllCursor
                    property point offset
                    onPressed: function(m) {
                        var p = mapToItem(null, m.x, m.y);
                        offset = Qt.point(p.x - alignDialog.x, p.y - alignDialog.y);
                    }
                    onPositionChanged: function(m) {
                        var p = mapToItem(null, m.x, m.y);
                        alignDialog.x = p.x - offset.x;
                        alignDialog.y = p.y - offset.y;
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Capture the main and the sub separately (same measurement settings), pick them here, set the crossover, then Find best delay. The sub's delay and polarity are changed virtually; put the same values into your DSP.")
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                opacity: 0.7
            }
            TitledCombo {
                id: aSelect
                Layout.fillWidth: true
                title: qsTr("main")
                model: allTraces
                textRole: "title"
                onActivated: alignDialog.pick()
            }
            TitledCombo {
                id: bSelect
                Layout.fillWidth: true
                title: qsTr("sub")
                model: allTraces
                textRole: "title"
                onActivated: alignDialog.pick()
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 4
                columnSpacing: 8
                rowSpacing: 6

                Label { text: qsTr("crossover") }
                ValueBox {
                    Layout.preferredWidth: 80
                    displayText: Math.round(alignDialog.crossover) + " Hz"
                    onAccepted: function(text) {
                        var v = parseFloat(String(text).replace(",", "."));
                        if (v > 10 && v < 20000) { alignDialog.crossover = v; alignDialog.refresh(); }
                    }
                    onStepped: function(steps, fine) {
                        alignDialog.crossover = Math.max(15, Math.round(alignDialog.crossover * Math.pow(2, steps / (fine ? 24 : 12))));
                        alignDialog.refresh();
                    }
                }
                Label { text: qsTr("sub delay") }
                ValueBox {
                    Layout.preferredWidth: 80
                    enabled: alignDialog.bAdjustable
                    displayText: alignDialog.bAdjustable ? Number(alignDialog.b.data.delay).toFixed(2) + " ms" : "—"
                    tooltip: qsTr("type a value, or scroll (Shift: 0.01 ms)")
                    onAccepted: function(text) {
                        var v = parseFloat(String(text).replace("ms", "").replace(",", "."));
                        if (!isNaN(v) && alignDialog.bAdjustable) { alignDialog.b.data.delay = v; alignDialog.refresh(); }
                    }
                    onStepped: function(steps, fine) {
                        if (alignDialog.bAdjustable) {
                            alignDialog.b.data.delay = Math.round((alignDialog.b.data.delay + steps * (fine ? 0.01 : 0.1)) * 100) / 100;
                            alignDialog.refresh();
                        }
                    }
                }

                Label { text: qsTr("sub polarity") }
                MiniButton {
                    Layout.preferredWidth: 80
                    enabled: alignDialog.bAdjustable
                    text: alignDialog.bAdjustable && alignDialog.b.data.polarity ? qsTr("inverted") : qsTr("normal")
                    checked: alignDialog.bAdjustable && alignDialog.b.data.polarity
                    onClicked: { alignDialog.b.data.polarity = !alignDialog.b.data.polarity; alignDialog.refresh(); }
                }
                Label { text: qsTr("sub gain") }
                ValueBox {
                    Layout.preferredWidth: 80
                    enabled: alignDialog.bAdjustable
                    displayText: alignDialog.bAdjustable ? Number(alignDialog.b.data.gain).toFixed(1) + " dB" : "—"
                    onAccepted: function(text) {
                        var v = parseFloat(String(text).replace("dB", "").replace(",", "."));
                        if (!isNaN(v) && alignDialog.bAdjustable) { alignDialog.b.data.gain = v; alignDialog.refresh(); }
                    }
                    onStepped: function(steps, fine) {
                        if (alignDialog.bAdjustable) {
                            alignDialog.b.data.gain = Math.round((alignDialog.b.data.gain + steps * (fine ? 0.1 : 0.5)) * 10) / 10;
                            alignDialog.refresh();
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: statsText.implicitHeight + 16
                radius: 4
                color: Qt.rgba(tools.base.r, tools.base.g, tools.base.b, 0.06)
                Label {
                    id: statsText
                    anchors.fill: parent
                    anchors.margins: 8
                    wrapMode: Text.WordWrap
                    textFormat: Text.RichText
                    text: {
                        var s = alignDialog.stats;
                        if (!s || s.ok === undefined) {
                            return qsTr("Pick the main and the sub.");
                        }
                        if (!s.ok) {
                            return s.error ? s.error : qsTr("Can't compare these traces.");
                        }
                        var eff = Math.round(100 * s.currentEfficiency);
                        var phase = Math.round(s.phaseDiff);
                        var colour = eff >= 90 ? "#4CAF50" : (eff >= 75 ? "#FFC107" : "#F44336");
                        return qsTr("Phase difference at %1 Hz: <b>%2°</b><br/>Summation around the crossover: <b><font color=\"%3\">%4%</font></b> (100% = fully in phase)<br/>Level main − sub: <b>%5 dB</b>")
                            .arg(Math.round(alignDialog.crossover)).arg(phase).arg(colour).arg(eff)
                            .arg(Number(s.levelDiff).toFixed(1));
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                visible: alignDialog.b && !alignDialog.bAdjustable
                text: qsTr("The sub must be a captured trace to adjust it here.")
                color: Material.color(Material.Orange)
                font.pixelSize: 11
            }

            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("Find best delay")
                    enabled: alignDialog.bAdjustable && alignDialog.stats.ok === true
                    onClicked: alignDialog.findBest()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("searches ±10 ms (and polarity) for the best sum around the crossover; prefers the smallest change")
                }
                Button {
                    text: qsTr("Show sum")
                    flat: true
                    enabled: alignDialog.stats.ok === true
                    onClicked: alignDialog.showSum()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("adds a trace of main + sub as they would combine (magnitude and phase)")
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Close")
                    flat: true
                    onClicked: alignDialog.close()
                }
            }
        }
    }

    function openAverage() { averageDialog.openDialog(); }
    function openCompare() { compareDialog.openDialog(); }
    function openAlign() { alignDialog.openDialog(); }
}
