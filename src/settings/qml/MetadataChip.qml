// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief A small rounded badge for row metadata, in the two flavours the
 *        settings lists use.
 *
 * Neutral (the default) reads as passive metadata: a coverage chip, a count.
 * `highlighted` reads as state, in the highlight colour: the sets list's
 * "Active" pill. Same split, and the same recipe, as the badge cluster on the
 * Rules list's RuleRow.
 *
 *   MetadataChip { text: i18n("Windows") }
 *   MetadataChip { text: i18n("Active"); iconName: "dialog-ok"; highlighted: true }
 */
Rectangle {
    id: root

    required property string text
    /// Optional leading icon. Empty hides it.
    property string iconName: ""
    /// State badge (highlight-tinted) rather than passive metadata.
    property bool highlighted: false
    /// Fully rounded (a pill) rather than the default soft-cornered badge.
    property bool pill: false

    implicitWidth: content.implicitWidth + Kirigami.Units.largeSpacing
    implicitHeight: content.implicitHeight + Kirigami.Units.smallSpacing
    radius: root.pill ? height / 2 : Kirigami.Units.smallSpacing
    color: root.highlighted ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.18) : Kirigami.Theme.alternateBackgroundColor

    RowLayout {
        id: content

        anchors.centerIn: parent
        spacing: Kirigami.Units.smallSpacing / 2

        Kirigami.Icon {
            visible: root.iconName.length > 0
            source: root.iconName
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
        }

        Label {
            text: root.text
            font: Kirigami.Theme.smallFont
            opacity: root.highlighted ? 0.85 : 0.7
        }
    }
}
