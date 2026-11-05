// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Lock.LockLayout, the layout you left as an outline.
//
// Every window of a screen's placement map at 1:1 (A3 §6 a): a static
// 1 px spectrum outline (hue from the cell's t, purple while the window
// is urgent), an 8 % navy fill and the app glyph centred at 32 px and
// 30 %. No titles. Same cell contract as PlacementMiniature (x, y, w, h
// as fractions of the work area, appId, title, t, urgent, occupied), at
// full size and with the tile radius rather than the miniature's 3 px.
//
// `fill` is the one animated field: 0.08 while locked, driven to 1 on
// unlock as the real windows come back (A3 §6 c).

import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme

Item {
    id: root

    // A PlacementMapScreen, or any object with `cells` and `workArea`.
    property var map: null
    // Bound from the map by default; a composer or test can hand cells in
    // directly.
    property var cells: root.map ? root.map.cells : []
    // The work area in this surface's pixels. Falls back to the whole item
    // for a map without one (no daemon).
    property rect workArea: root.map && root.map.workArea && root.map.workArea.width > 0 ? root.map.workArea : Qt.rect(0, 0, root.width, root.height)

    property real fill: 0.08
    property real strokeOpacity: Tokens.stroke_resting
    property real iconOpacity: 0.3
    property int iconSize: 32
    property real radius: Tokens.radius_tile

    // Navy `#0B1730` (A3 consistency table, the tile ground) as the
    // outline fill. A design constant of the lockscreen, not a palette
    // token.
    readonly property color fillColor: "#0B1730"

    // Only windows are outlined: a snapping layout's empty zones are not
    // part of the layout you left.
    readonly property var windows: {
        const list = root.cells ? root.cells : [];
        const out = [];
        for (let i = 0; i < list.length; ++i) {
            const c = list[i];
            if (c && c.occupied !== false)
                out.push(c);
        }
        return out;
    }
    readonly property int outlineCount: outlines.count

    // An outline was clicked: `name` is the window's title, or its app id
    // when the title is empty.
    signal cellClicked(string id, string name)

    Repeater {
        id: outlines

        model: root.windows

        delegate: Item {
            id: outline

            required property var modelData

            readonly property real t: outline.modelData.t !== undefined ? Number(outline.modelData.t) : 0
            readonly property bool urgent: !!outline.modelData.urgent
            readonly property string appId: outline.modelData.appId !== undefined ? String(outline.modelData.appId) : ""
            readonly property string title: outline.modelData.title !== undefined ? String(outline.modelData.title) : ""

            x: root.workArea.x + Number(outline.modelData.x) * root.workArea.width
            y: root.workArea.y + Number(outline.modelData.y) * root.workArea.height
            width: Number(outline.modelData.w) * root.workArea.width
            height: Number(outline.modelData.h) * root.workArea.height

            Rectangle {
                anchors.fill: parent
                radius: root.radius
                color: Qt.rgba(root.fillColor.r, root.fillColor.g, root.fillColor.b, root.fill)
            }

            Rectangle {
                anchors.fill: parent
                radius: root.radius
                color: "transparent"
                border.width: 1
                border.color: outline.urgent ? Spectrum.pending : Spectrum.at(outline.t)
                opacity: root.strokeOpacity
            }

            Kirigami.Icon {
                anchors.centerIn: parent
                width: root.iconSize
                height: root.iconSize
                visible: outline.appId.length > 0 && parent.width >= root.iconSize && parent.height >= root.iconSize
                source: outline.appId
                opacity: root.iconOpacity
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.ArrowCursor
                onClicked: root.cellClicked(String(outline.modelData.id), outline.title.length > 0 ? outline.title : outline.appId)
            }

            Accessible.role: Accessible.Graphic
            Accessible.name: outline.title.length > 0 ? outline.title : outline.appId
        }
    }
}
