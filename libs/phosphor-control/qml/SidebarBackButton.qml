// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import "ThemeHelpers.js" as ThemeHelpers

/**
 * Drill-out header row shown at the top of the sidebar list when the user has
 * drilled into a parent page. Reads as a section header: a leading chevron plus
 * the parent category's name (e.g. "‹ Snapping"); clicking anywhere drills out.
 *
 * The chevron is a text-run glyph inside the same Label as the title (not a
 * Kirigami.Icon): its ink sits flush on the content-left edge exactly like the
 * section-header text below, whereas an icon box centres the glyph and pushes it
 * off the grid. It also mirrors under RTL (‹ → ›) for free. The bottom rule is
 * NOT drawn here — the hosting Sidebar places a Kirigami.Separator below this
 * row so it shares the section dividers' inset. Extracted from Sidebar.qml to
 * keep that file under the 800-line cap (CLAUDE.md).
 */
QQC2.ItemDelegate {
    id: backButton

    /// Parent category name shown after the chevron (e.g. "Snapping").
    property string title: ""
    /// Icon-only rail: collapse to a centered chevron with a tooltip.
    property bool compact: false
    // Match legacy row height for the back button (slightly taller than nav
    // rows so it reads as a header).
    property real backButtonHeight: Kirigami.Units.gridUnit * 2.6

    /// Accessible / tooltip label — announces where "back" goes.
    readonly property string _backLabel: backButton.title.length > 0 ? qsTr("Back to %1").arg(backButton.title) : qsTr("Back")

    //* Fired when the user clicks the back row. Sidebar wires this to drillOut().
    signal backClicked

    Layout.fillWidth: true
    implicitHeight: backButton.backButtonHeight
    // Compact rail centers the chevron (leftPadding 0); otherwise content starts
    // at the same largeSpacing inset as the nav rows / section headers.
    leftPadding: backButton.compact ? 0 : Kirigami.Units.largeSpacing
    rightPadding: backButton.compact ? 0 : Kirigami.Units.largeSpacing
    // Symmetric vertical padding so the content is centered in the row (the
    // ItemDelegate style's default top/bottom padding is asymmetric).
    topPadding: 0
    bottomPadding: 0
    Accessible.name: backButton._backLabel
    Accessible.role: Accessible.Button
    // Tooltip surfaces the destination when compact mode has hidden the label.
    QQC2.ToolTip.visible: backButton.compact && backButton.hovered
    QQC2.ToolTip.text: backButton._backLabel
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
    onClicked: backButton.backClicked()

    background: Rectangle {
        id: backButtonBackground

        // Default Rectangle color is white — without an initial assignment the
        // Behavior would tint from white to "transparent" on the very first
        // paint. Gate the Behavior on completion so the first eval lands without
        // animation. Shared pattern with SidebarRow.qml.
        property bool _behaviorReady: false

        Component.onCompleted: _behaviorReady = true
        color: backButton.hovered ? ThemeHelpers.hoverTint(Kirigami.Theme.textColor) : Qt.rgba(0, 0, 0, 0)
        radius: Kirigami.Units.smallSpacing

        Behavior on color {
            enabled: backButtonBackground._behaviorReady

            PhosphorMotionAnimation {
                profile: "widget.tint.fast"
            }
        }
    }

    // Single Label so the chevron lives in the text run — its ink lands flush on
    // the content-left edge like the section-header text, and it mirrors under
    // RTL. Demi-bold @ 0.8 opacity reads as a header rather than a nav row.
    contentItem: QQC2.Label {
        readonly property string _chevron: LayoutMirroring.enabled ? "›" : "‹"

        text: backButton.compact ? _chevron : _chevron + " " + backButton.title
        font.weight: Font.DemiBold
        opacity: 0.8
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: backButton.compact ? Text.AlignHCenter : Text.AlignLeft
    }
}
