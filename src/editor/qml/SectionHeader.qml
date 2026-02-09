// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable section header with accent bar and icon
 *
 * Used in settings dialogs (Visibility, Layout Settings) to introduce
 * grouped sections. Displays a colored accent bar, optional icon, and
 * bold section title.
 */
RowLayout {
    id: root

    required property string title
    property string icon: ""

    Layout.fillWidth: true
    spacing: Kirigami.Units.smallSpacing

    Rectangle {
        width: Math.round(Kirigami.Units.smallSpacing * 0.75)
        height: sectionLabel.height
        color: Kirigami.Theme.highlightColor
        radius: Math.round(Kirigami.Units.smallSpacing / 4)
    }

    Kirigami.Icon {
        source: root.icon
        width: Kirigami.Units.iconSizes.small
        height: Kirigami.Units.iconSizes.small
        visible: root.icon !== ""
    }

    Label {
        id: sectionLabel
        text: root.title
        font.weight: Font.DemiBold
    }
}
