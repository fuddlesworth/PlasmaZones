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
    /// rather than as a zone filling the box. Floored at zero: the shaft
    /// subtracts a head arm from this, and a well narrower than one head would
    /// otherwise hand a Rectangle a negative width.
    readonly property real _arrowSpan: Math.max(0, Math.round((root.verticalAxis ? root.height : root.width) * 0.45))
    readonly property real _shaftThickness: Math.max(1, Math.round(Kirigami.Units.smallSpacing / 2))
    readonly property real _headArm: Math.max(4, Math.round(Kirigami.Units.gridUnit * 0.4))
    /// The arrow's extent along the STACKING axis, which is always vertical
    /// because the Column stacks the arrow above the caption. On a horizontal
    /// strip that is the arrowhead box; on a vertical one the arrow is drawn
    /// along its own length, so it is the span. Getting this wrong makes the
    /// fit test below measure the wrong thing on one axis only.
    readonly property real _arrowStackExtent: root.verticalAxis ? root._arrowSpan : root._headArm * Math.SQRT2
    /// The stack's own height, used to decide whether the well can seat the
    /// arrow at all. A host may clip this component (LayoutCard does), so on a
    /// short well the choice is between dropping the arrow and silently
    /// truncating the caption, and the caption is the half carrying the
    /// message. The caption term is folded in only when the Label is actually
    /// shown: Column skips an invisible child AND its spacing slot, so
    /// counting it regardless would drop the arrow on a well that could seat
    /// it and leave this component drawing nothing at all.
    readonly property real _stackHeight: root._arrowStackExtent + (captionLabel.visible ? Kirigami.Units.smallSpacing * 2 + captionLabel.height : 0)
    readonly property bool _arrowFits: root.height >= root._stackHeight

    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    Column {
        anchors.centerIn: parent
        spacing: Kirigami.Units.smallSpacing * 2

        Item {
            id: arrow

            anchors.horizontalCenter: parent.horizontalCenter
            width: root.verticalAxis ? root._headArm * Math.SQRT2 : root._arrowSpan
            height: root.verticalAxis ? root._arrowSpan : root._headArm * Math.SQRT2
            opacity: 0.55
            // Dropped rather than clipped on a well too short to seat both it
            // and the caption. A truncated sentence is a worse empty state
            // than no arrow.
            visible: root._arrowFits

            // Shaft. Inset by half a head at each end so the strokes of the
            // two heads close on it rather than crossing it. Floored at zero
            // for a well narrower than one head.
            Rectangle {
                anchors.centerIn: parent
                width: root.verticalAxis ? root._shaftThickness : Math.max(0, arrow.width - root._headArm)
                height: root.verticalAxis ? Math.max(0, arrow.height - root._headArm) : root._shaftThickness
                radius: root._shaftThickness / 2
                color: root.contentColor
            }

            AxisChevron {
                direction: root.verticalAxis ? 2 : 0
                arm: root._headArm
                thickness: root._shaftThickness
                strokeColor: root.contentColor
                anchors.left: root.verticalAxis ? undefined : parent.left
                anchors.top: root.verticalAxis ? parent.top : undefined
                anchors.horizontalCenter: root.verticalAxis ? parent.horizontalCenter : undefined
                anchors.verticalCenter: root.verticalAxis ? undefined : parent.verticalCenter
            }

            AxisChevron {
                direction: root.verticalAxis ? 3 : 1
                arm: root._headArm
                thickness: root._shaftThickness
                strokeColor: root.contentColor
                anchors.right: root.verticalAxis ? undefined : parent.right
                anchors.bottom: root.verticalAxis ? parent.bottom : undefined
                anchors.horizontalCenter: root.verticalAxis ? parent.horizontalCenter : undefined
                anchors.verticalCenter: root.verticalAxis ? undefined : parent.verticalCenter
            }
        }

        Label {
            // NOT `caption`: an id outranks the root object's property scope
            // for unqualified names inside this file, so an id of `caption`
            // would shadow the `caption` string property and a later bare
            // `caption !== ""` would silently test the Item.
            id: captionLabel

            anchors.horizontalCenter: parent.horizontalCenter
            // Bounded against the well and wrapped: the captions run to a
            // short sentence and the well is a thumbnail on some hosts.
            // Floored at zero so a well narrower than the margin does not hand
            // the Label a negative width.
            width: Math.max(0, Math.min(implicitWidth, root.width - Kirigami.Units.gridUnit))
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
            maximumLineCount: 2
            text: root.caption
            visible: root.caption !== ""
            // Every host folds this caption into its own Accessible.name for
            // the well. `Accessible.ignored` on an ancestor only drops that
            // node and reparents its children, so without this the same
            // sentence is announced twice.
            Accessible.ignored: true
            color: root.contentColor
            opacity: 0.7
            font: Kirigami.Theme.smallFont
        }
    }
}
