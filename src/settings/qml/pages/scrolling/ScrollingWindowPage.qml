// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Scrolling → Window: how the strip treats windows (placement,
 * minimum sizes, restore, sticky handling, adjust steps) plus focus and the
 * viewport rows that follow it. The peer of the Tiling and Snapping → Window
 * pages; one of the three advanced scrolling leaves (Columns / Tabs /
 * Window). The Window Handling and Focus cards are the shared components
 * the simple page also hosts; the Triggers card is advanced-only, matching
 * the tiling twin.
 */
SettingsFlickable {
    // PAGE-LEVEL for the drop-indicator colour row, the same reason the Tabs
    // page hosts its own: a page rebuild while the dialog is open would
    // destroy a card-scoped dialog and tear the popup down under the user.
    ColorDialog {
        id: dropIndicatorColorDialog

        options: ColorDialog.ShowAlphaChannel
    }

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        ScrollingDragInsertCard {
            Layout.fillWidth: true
        }

        ScrollingDropIndicatorCard {
            Layout.fillWidth: true
            picker: dropIndicatorColorDialog
        }

        ScrollingWindowHandlingCard {
            Layout.fillWidth: true
        }

        ScrollingFocusCard {
            Layout.fillWidth: true
        }
    }
}
