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
import QtQuick.Controls 2.12

/**
 * Holds the settings of the selected item (measurement, chart, generator...).
 * main.qml shows it in a floating settings window, or docked along the bottom.
 */
Item {
    property alias stack: propertiesStack
    property var currentObject : null
    property var currentQml : null
    property string title: ""

    //! the panel is on screen (docked, or the settings window is open)
    property bool shown: false
    //! main.qml makes the panel visible
    signal showRequested()

    StackView {
        id: propertiesStack
        anchors.fill: parent
        anchors.margins: 5
        initialItem: topView

        replaceEnter: Transition {
            PropertyAnimation {
                property:   "opacity"
                from:       0
                to:         1
                duration:   100
            }
        }
        replaceExit: Transition {
            PropertyAnimation {
                property:   "opacity"
                from:       1
                to:         0
                duration:   100
            }
        }
    }

    Component {
        id: topView

        Label {
            horizontalAlignment: Label.AlignHCenter
            verticalAlignment: Label.AlignVCenter

            text: qsTr("Click the \u2699 button on a measurement, chart or the generator to change its settings")
        }
    }

    function titleFor(pushObject, propertiesQml, explicitTitle) {
        if (explicitTitle) {
            return explicitTitle;
        }
        if (pushObject && pushObject.data && pushObject.data.name !== undefined) {
            return pushObject.data.name;
        }
        if (propertiesQml) {
            var name = String(propertiesQml).split("/").pop().replace(".qml", "").replace("Properties", "");
            return name.replace(/([a-z])([A-Z])/g, "$1 $2");
        }
        return "";
    }

    //! show the settings of `pushObject`, opening the settings window if needed
    function open(pushObject, propertiesQml, explicitTitle) {
        if (currentObject !== pushObject || currentQml !== propertiesQml) {

            reset();
            currentObject = pushObject;
            currentQml = propertiesQml;
            if (propertiesQml) {
                propertiesStack.replace(
                        propertiesQml,
                        {
                            dataObject: pushObject
                        }
                );
            }
            else
            console.error("qml not set for ", pushObject)
        }
        title = titleFor(pushObject, propertiesQml, explicitTitle);
        showRequested();
    }

    //! switch to `pushObject` only if the settings are already on screen (e.g. clicking a chart)
    function follow(pushObject, propertiesQml, explicitTitle) {
        if (shown && propertiesQml) {
            open(pushObject, propertiesQml, explicitTitle);
        }
    }

    function reset() {
        propertiesStack.clear();
        propertiesStack.push(topView);
        currentObject = null;
        currentQml = null;
        title = "";
    }

    function clear() {
        reset();
    }

    function check() {
        if (!currentObject) {
            reset();
        }
    }
}
