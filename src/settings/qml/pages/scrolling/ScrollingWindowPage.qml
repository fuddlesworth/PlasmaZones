// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Scrolling → Window: how the strip treats windows (placement,
 * minimum sizes, restore, sticky handling, adjust steps) plus focus and the
 * viewport rows that follow it. The peer of the Tiling and Snapping → Window
 * pages; one of the three advanced scrolling leaves (Columns / Tabs /
 * Window). The Window Handling and Focus cards are the shared components
 * the simple page also hosts. The Triggers card appears only on this
 * advanced leaf (the page's tier, not a card flag), matching the tiling
 * twin, and the Drop indicator card sits beside it because it paints only
 * during the drag those triggers arm.
 */
SettingsFlickable {
    // PAGE-LEVEL for the drop-indicator colour row, the same reason the Tabs
    // page hosts its own: a page rebuild while the dialog is open would
    // destroy a card-scoped dialog and tear the popup down under the user.
    ColorDialog {
        id: dropIndicatorColorDialog

        title: i18n("Choose Drop Indicator Color")
        // Alpha offered like every other colour dialog. The border honours a
        // picked alpha directly; the fill's alpha is replaced by the Fill
        // opacity slider beside it, matching the snapping zone overlay's
        // fill, whose dialog also offers the channel while the opacity row
        // stays the one control that decides how solid the fill paints.
        options: ColorDialog.ShowAlphaChannel
    }

    // Publish the open state so page-stepping cannot swap the page out from
    // under the open dialog — same pattern (and standalone-host guard) as
    // RulesPage.
    readonly property bool anyModalOpen: dropIndicatorColorDialog.visible
    onAnyModalOpenChanged: {
        if (typeof window !== "undefined" && window && window._pageOwnedModalOpen !== undefined)
            window._pageOwnedModalOpen = anyModalOpen;
    }
    // Clear a latched true on page swap (RulesPage's own teardown pattern).
    Component.onDestruction: {
        if (typeof window !== "undefined" && window && window._pageOwnedModalOpen !== undefined)
            window._pageOwnedModalOpen = false;
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
