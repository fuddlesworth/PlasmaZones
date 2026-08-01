// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Scrolling → Window: how the strip treats windows (placement,
 * minimum sizes, restore, sticky handling, adjust steps) plus focus and the
 * viewport rows that follow it. The peer of the Tiling and Snapping → Window
 * pages; one of the two advanced scrolling leaves (Columns / Window). Both
 * cards are the shared components the simple page also hosts.
 */
SettingsFlickable {
    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        ScrollingWindowHandlingCard {
            Layout.fillWidth: true
        }

        ScrollingFocusCard {
            Layout.fillWidth: true
        }
    }
}
