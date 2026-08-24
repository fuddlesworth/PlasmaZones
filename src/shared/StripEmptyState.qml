// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * @brief What a scrolling surface draws when the strip has nothing on it.
 *
 * This replaces the representative "endless strip" sketch that used to fill
 * the same well: three invented columns, the outer two clipped at the box
 * edges to suggest continuation. The sketch was drawn by the same renderer
 * that draws real zones, in the same fills, so it read as three real windows
 * on a strip that in fact held none. Suppressing its zone NUMBERS was the only
 * concession made to that, and it was not enough.
 *
 * What survives from it is the one honest thing it communicated: which way the
 * columns will run. That is now an axis arrow with a caption, and nothing here
 * claims a window exists.
 *
 * The caller supplies the caption because the empty well has more than one
 * cause and they do not mean the same thing — a strip with no windows on it,
 * a screen where scrolling is staged but not applied, and a daemon that did
 * not answer are three different messages. Folding them into one picture is
 * what the sketch did.
 */
Item {
    id: root

    /// Which way the strip runs on this screen. The arrow is the whole point
    /// of the component, so unlike ZonePreview's tick there is no "none".
    property bool verticalAxis: false
    /// The message under the arrow. Required in practice — see the class note
    /// above on why this component does not pick it.
    property string caption: ""
    /// Arrow and caption colour, theme text at reduced opacity below.
    property color contentColor: Kirigami.Theme.textColor

    /// Arrow length along the strip axis, as a share of the well's extent on
    /// that axis. Short of the full span so the arrow reads as a direction
    /// rather than as a zone filling the box.
    readonly property real _arrowSpan: Math.round((verticalAxis ? root.height : root.width) * 0.45)
    readonly property real _shaftThickness: Math.max(1, Math.round(Kirigami.Units.smallSpacing / 2))
    readonly property real _headArm: Math.max(4, Math.round(Kirigami.Units.gridUnit * 0.4))

    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    /// One arrowhead, two strokes meeting at the tip and splaying by 45
    /// degrees. Built pointing LEFT and rotated, in a SQUARE box: `rotation`
    /// pivots on the item's centre, so a square is the only box whose
    /// on-screen extent survives the 90 and 270 degree legs unchanged.
    component ArrowHead: Item {
        id: head

        /// 0 left, 1 right, 2 up, 3 down.
        required property int direction

        width: root._headArm * Math.SQRT2
        height: width
        rotation: {
            switch (direction) {
            case 1:
                return 180;
            case 2:
                return 90;
            case 3:
                return 270;
            default:
                return 0;
            }
        }
        Accessible.ignored: true

        Repeater {
            // The model IS the splay, so the pair cannot drift out of symmetry.
            model: [-45, 45]

            Rectangle {
                required property real modelData

                width: root._headArm
                height: root._shaftThickness
                radius: root._shaftThickness / 2
                color: root.contentColor
                // The tip, centred in the square box.
                x: (head.width - root._headArm * Math.SQRT1_2) / 2
                y: head.height / 2 - root._shaftThickness / 2
                // Pivot on the tip: both strokes must share one origin or the
                // head opens into a Z.
                transformOrigin: Item.Left
                rotation: modelData
            }
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: Kirigami.Units.smallSpacing * 2

        Item {
            id: arrow

            anchors.horizontalCenter: parent.horizontalCenter
            width: root.verticalAxis ? root._headArm * Math.SQRT2 : root._arrowSpan
            height: root.verticalAxis ? root._arrowSpan : root._headArm * Math.SQRT2
            opacity: 0.55

            // Shaft. Inset by half a head at each end so the strokes of the
            // two heads close on it rather than crossing it.
            Rectangle {
                anchors.centerIn: parent
                width: root.verticalAxis ? root._shaftThickness : arrow.width - root._headArm
                height: root.verticalAxis ? arrow.height - root._headArm : root._shaftThickness
                radius: root._shaftThickness / 2
                color: root.contentColor
            }

            ArrowHead {
                direction: root.verticalAxis ? 2 : 0
                anchors.left: root.verticalAxis ? undefined : parent.left
                anchors.top: root.verticalAxis ? parent.top : undefined
                anchors.horizontalCenter: root.verticalAxis ? parent.horizontalCenter : undefined
                anchors.verticalCenter: root.verticalAxis ? undefined : parent.verticalCenter
            }

            ArrowHead {
                direction: root.verticalAxis ? 3 : 1
                anchors.right: root.verticalAxis ? undefined : parent.right
                anchors.bottom: root.verticalAxis ? parent.bottom : undefined
                anchors.horizontalCenter: root.verticalAxis ? parent.horizontalCenter : undefined
                anchors.verticalCenter: root.verticalAxis ? undefined : parent.verticalCenter
            }
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            // Bounded against the well and wrapped: the captions run to a
            // short sentence and the well is a thumbnail on some hosts.
            width: Math.min(implicitWidth, root.width - Kirigami.Units.gridUnit)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
            maximumLineCount: 2
            text: root.caption
            visible: root.caption !== ""
            color: root.contentColor
            opacity: 0.7
            font: Kirigami.Theme.smallFont
        }
    }
}
