/**
 *  OSM
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */
import QtQuick 2.15
import QtQuick.Controls 2.15

//! holds the settings panel; scrolls sideways when narrower than the panel needs
Flickable {
    id: host
    property real minimumContentWidth: 1000
    readonly property bool scrolling: contentWidth > width
    //! room left for the content above the scroll bar
    readonly property real usableHeight: height - (scrolling ? 12 : 0)
    clip: true
    contentWidth: Math.max(width, minimumContentWidth)
    contentHeight: height
    flickableDirection: Flickable.HorizontalFlick
    boundsBehavior: Flickable.StopAtBounds
    interactive: contentWidth > width
    ScrollBar.horizontal: ScrollBar {
        policy: host.contentWidth > host.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
    }
}
