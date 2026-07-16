// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Square-ish picker tile used in AddRuleSheet's two grids.
 *
 * Rectangle + MouseArea (matches `MonitorOverviewTile`'s house style — using
 * ItemDelegate + custom contentItem in a GridLayout left tiles rendering an
 * empty background because the Control's default sizing path couldn't see
 * past the overridden contentItem). Renders icon + bold title + small
 * description with hover/press highlight; `activated` fires on click.
 */
Rectangle {
    id: tile

    required property string iconSource
    required property string label
    required property string description
    /// Tile HEIGHT is fixed by the parent grid (so tiles don't grow tall
    /// when one description is shorter). Tile WIDTH has a baseline
    /// `implicitWidth` here so the GridLayout's columns have non-zero
    /// implicit width — that drives the sheet's overall sizing. The
    /// delegate site sets `Layout.fillWidth: true` so the tile still flexes
    /// when the sheet has more horizontal room than the baseline assumes.
    required property int tileHeight

    signal activated

    Accessible.name: tile.label
    Accessible.description: tile.description
    Accessible.role: Accessible.Button
    Accessible.focusable: true
    activeFocusOnTab: true
    implicitWidth: Kirigami.Units.gridUnit * 10
    implicitHeight: tile.tileHeight
    radius: Kirigami.Units.smallSpacing
    color: tileMouse.pressed ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.25) : tileMouse.containsMouse ? Qt.tint(Kirigami.Theme.alternateBackgroundColor, Qt.alpha(Kirigami.Theme.hoverColor, 0.12)) : Kirigami.Theme.alternateBackgroundColor
    border.width: 1
    border.color: tile.activeFocus ? Kirigami.Theme.focusColor : tileMouse.containsMouse ? Kirigami.Theme.hoverColor : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
    Keys.onReturnPressed: tile.activated()
    // Numpad Enter alias, matching the sibling card components.
    Keys.onEnterPressed: tile.activated()
    Keys.onSpacePressed: tile.activated()

    MouseArea {
        id: tileMouse

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        onClicked: {
            // Move active focus to the clicked tile so a previously
            // keyboard-focused sibling doesn't keep the focus ring and the key
            // handlers.
            tile.forceActiveFocus();
            tile.activated();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: Kirigami.Units.iconSizes.huge
            Layout.preferredWidth: Kirigami.Units.iconSizes.huge
            source: tile.iconSource
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            text: tile.label
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            elide: Text.ElideRight
            font: Kirigami.Theme.smallFont
            horizontalAlignment: Text.AlignHCenter
            maximumLineCount: 3
            opacity: 0.7
            text: tile.description
            wrapMode: Text.WordWrap
        }
    }

    // Match MonitorOverviewTile's transition feel — fade the background /
    // border on hover via the shared PhosphorMotionAnimation profile so
    // every tile in the app moves with the same easing curve.
    Behavior on color {
        PhosphorMotionAnimation {
            profile: "widget.hover"
            durationOverride: Kirigami.Units.shortDuration
        }
    }

    Behavior on border.color {
        PhosphorMotionAnimation {
            profile: "widget.hover"
            durationOverride: Kirigami.Units.shortDuration
        }
    }
}
