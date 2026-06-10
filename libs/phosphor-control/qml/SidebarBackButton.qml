// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Window
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import "ThemeHelpers.js" as ThemeHelpers

/**
 * Drill-out "Back" row shown at the top of the sidebar list when the user
 * has drilled into a parent page.
 *
 * Extracted from Sidebar.qml's inline `ItemDelegate { id: backButton }` to
 * keep Sidebar.qml under the 800-line cap (CLAUDE.md). The visual + a11y
 * behaviour is unchanged — same hover tint, same demi-bold "Back" label,
 * same 1-dp bottom separator.
 */
QQC2.ItemDelegate {
    id: backButton

    // Match legacy row height for the back button (slightly taller than
    // nav rows so it reads as a header).
    property real backButtonHeight: Kirigami.Units.gridUnit * 2.6

    //* Fired when the user clicks the back row. Sidebar wires this to drillOut().
    signal backClicked

    Layout.fillWidth: true
    implicitHeight: backButton.backButtonHeight
    leftPadding: Kirigami.Units.smallSpacing
    rightPadding: Kirigami.Units.smallSpacing
    Accessible.name: qsTr("Back")
    Accessible.role: Accessible.Button
    onClicked: backButton.backClicked()

    background: Rectangle {
        id: backButtonBackground

        // Default Rectangle color is white — without an initial
        // assignment the Behavior would tint from white to
        // "transparent" on the very first paint (binding eval).
        // Gate the Behavior on completion so the first eval
        // lands without animation.
        //
        // Shared pattern with SidebarRow.qml — Qt's `Behavior on X`
        // block must be a sibling of the animated property, so we
        // can't extract this into a reusable Component. If you
        // change the gate/profile here, mirror the edit there.
        property bool _behaviorReady: false

        Component.onCompleted: _behaviorReady = true
        color: backButton.hovered ? ThemeHelpers.hoverTint(Kirigami.Theme.textColor) : Qt.rgba(0, 0, 0, 0)
        radius: Kirigami.Units.smallSpacing

        // Legacy back-button bottom separator — a 1-dp line tucked
        // inside the row's bottom edge so the rail reads as "you're
        // inside a sub-section".
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Kirigami.Units.smallSpacing
            anchors.rightMargin: Kirigami.Units.smallSpacing
            height: Math.round(Screen.devicePixelRatio)
            color: ThemeHelpers.withAlpha(Kirigami.Theme.textColor, 0.1)
        }

        Behavior on color {
            enabled: backButtonBackground._behaviorReady

            PhosphorMotionAnimation {
                profile: "widget.tint.fast"
            }
        }
    }

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            source: "go-previous-symbolic"
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
            opacity: 0.7
        }

        QQC2.Label {
            Layout.fillWidth: true
            text: qsTr("Back")
            // Legacy back-button label uses demi-bold @ 0.8 opacity
            // — reads as a section header rather than another nav row.
            font.weight: Font.DemiBold
            opacity: 0.8
        }
    }
}
