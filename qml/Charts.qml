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
import QtQuick.Controls 1.4 //SplitView needs exactly 1.4
import QtQuick.Layouts 1.3

/**
 * The charts area. Each view (e.g. "Live", "TF") keeps its own charts, types and
 * chart settings under views/<id>/charts; switching views rebuilds the charts
 * from that group, so the views never change each other.
 */
Item {
    id: chartsLayout
    //! current view id; empty uses the old single layout (layout/charts)
    property string viewId: ""
    //! settings group of the current view's charts (set by loadView)
    property string prefix: "layout/charts"
    property int count: 1
    property bool loading: false
    //! the view has no stored chart heights yet (first time it is opened)
    property bool needsAutoHeight: false
    //! the chart that takes up spare height when the window grows: the view's biggest one
    property int fillIndex: 1

    function groupFor(index) {
        return prefix + "/" + index;
    }

    //! rebuild the charts for the current view
    function loadView() {
        if (loader.item) {
            // take the old charts off screen first and build the new ones a moment
            // later, so the render thread has finished with the old ones
            loader.item.visible = false;
            reloadTimer.restart();
            return;
        }
        buildView();
    }

    function buildView() {
        loading = true;
        prefix = viewId ? "views/" + viewId + "/charts" : "layout/charts";
        needsAutoHeight = (applicationSettings.value(prefix + "/sized") === undefined);
        count = Math.max(1, Math.min(3, Number(applicationSettings.value(prefix + "/count", 1))));
        var stored = applicationSettings.value(prefix + "/weights");
        var weights = String(stored === undefined ? "" : stored).split(",").map(Number);
        fillIndex = 1;
        for (var i = 1; i < Math.min(weights.length, 3); ++i) {
            if (weights[i] > weights[fillIndex - 1]) {
                fillIndex = i + 1;
            }
        }
        loader.active = false;
        loader.active = true;
        loading = false;
    }

    Timer {
        id: reloadTimer
        interval: 80
        repeat: false
        onTriggered: chartsLayout.buildView()
    }

    function autoHeight() {
        if (loader.item) {
            loader.item.autoHeight();
        }
    }

    onViewIdChanged: loadView()
    onCountChanged: {
        if (loading) {
            return;
        }
        applicationSettings.setValue(prefix + "/count", count);
        autoHeight();
    }
    Component.onCompleted: {
        if (!loader.active) {
            loadView();
        }
    }

    Loader {
        id: loader
        anchors.fill: parent
        active: false
        sourceComponent: splitComponent
    }

    Component {
        id: splitComponent

        SplitView {
            id: sv
            orientation: Qt.Vertical
            readonly property int minimunHeight: 100

            function autoHeight() {
                // optional starting proportions for a view, e.g. "0.42,0.38,0.2"
                var stored = applicationSettings.value(chartsLayout.prefix + "/weights");
                var weights = String(stored === undefined ? "" : stored).split(",")
                    .map(function(w) { return Number(w); })
                    .filter(function(w) { return w > 0; });
                var charts = [first, second, third];
                var total = 0;
                for (var i = 0; i < chartsLayout.count; ++i) {
                    total += (weights.length === 3 ? weights[i] : 1);
                }
                for (var j = 0; j < charts.length; ++j) {
                    var w = (weights.length === 3 ? weights[j] : 1);
                    charts[j].height = Math.max(minimunHeight, sv.height * w / total);
                }
            }

            // a view opened for the first time gets its starting proportions once it has a size
            property bool pendingAutoHeight: chartsLayout.needsAutoHeight
            function applyPendingHeight() {
                if (pendingAutoHeight && sv.height > 3 * minimunHeight) {
                    pendingAutoHeight = false;
                    autoHeight();
                    applicationSettings.setValue(chartsLayout.prefix + "/sized", true);
                }
            }
            onHeightChanged: applyPendingHeight()
            Component.onCompleted: Qt.callLater(applyPendingHeight)

            Chart {
                id: first
                Layout.fillWidth: true
                Layout.minimumHeight: sv.minimunHeight
                Layout.preferredWidth: parent.width
                Layout.fillHeight: Math.min(chartsLayout.fillIndex, chartsLayout.count) === 1
                height: applicationSettings.value(chartsLayout.groupFor(1) + "/height", 300)
                onHeightChanged: applicationSettings.setValue(chartsLayout.groupFor(1) + "/height", height)
                type: applicationSettings.value(chartsLayout.groupFor(1) + "/type", "Spectrum")
                onTypeChanged: applicationSettings.setValue(chartsLayout.groupFor(1) + "/type", type)

                settingsGroup: chartsLayout.groupFor(1)
                Component.onCompleted: {
                    settings = applicationSettings.getGroup(chartsLayout.groupFor(1));
                }
            }

            Chart {
                id: second
                visible: chartsLayout.count > 1
                Layout.fillWidth: true
                Layout.minimumHeight: sv.minimunHeight
                Layout.preferredWidth: parent.width
                Layout.fillHeight: Math.min(chartsLayout.fillIndex, chartsLayout.count) === 2
                height: applicationSettings.value(chartsLayout.groupFor(2) + "/height", 300)
                onHeightChanged: applicationSettings.setValue(chartsLayout.groupFor(2) + "/height", height)
                type: applicationSettings.value(chartsLayout.groupFor(2) + "/type", "Spectrum")
                onTypeChanged: applicationSettings.setValue(chartsLayout.groupFor(2) + "/type", type)

                settingsGroup: chartsLayout.groupFor(2)
                Component.onCompleted: {
                    settings = applicationSettings.getGroup(chartsLayout.groupFor(2));
                }
            }

            Chart {
                id: third
                visible: chartsLayout.count > 2
                Layout.fillWidth: true
                Layout.minimumHeight: sv.minimunHeight
                Layout.preferredWidth: parent.width
                Layout.fillHeight: Math.min(chartsLayout.fillIndex, chartsLayout.count) === 3
                height: applicationSettings.value(chartsLayout.groupFor(3) + "/height", 300)
                onHeightChanged: applicationSettings.setValue(chartsLayout.groupFor(3) + "/height", height)
                type: applicationSettings.value(chartsLayout.groupFor(3) + "/type", "Spectrum")
                onTypeChanged: applicationSettings.setValue(chartsLayout.groupFor(3) + "/type", type)

                settingsGroup: chartsLayout.groupFor(3)
                Component.onCompleted: {
                    settings = applicationSettings.getGroup(chartsLayout.groupFor(3));
                }
            }

            SystemPalette { id: pal }
            handleDelegate: Rectangle {
                width: 1
                height: 1
                color: Qt.darker(pal.window, 1.5)

                MouseArea {
                    anchors.fill: parent
                    propagateComposedEvents: true
                    onDoubleClicked: {
                        sv.autoHeight();
                    }
                }
            }
        }
    }
}
