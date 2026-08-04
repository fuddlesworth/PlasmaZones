// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The drop-target highlight painted while a drag re-insert is armed.
 *
 * Sits beside the Triggers card because it only ever appears during the drag
 * those triggers arm. Two knobs rather than a page: unlike the tab indicator
 * there is no layout to configure, since the rect comes from the engine's own
 * layout math and cannot be positioned independently of where the drop lands.
 *
 * Scrolling needs this drawn where tiling needs nothing. Tiling's feedback is
 * its live restructure, but the scroll engine defers structure to the drop, so
 * without the highlight nothing shows where the window is going.
 */
SettingsCard {
    id: root

    /// The page-level ColorDialog, passed down for the same reason the tab
    /// page shares one: a page rebuild while a row-scoped dialog is open would
    /// tear the popup down under the user.
    property var picker: null

    headerText: i18n("Drop indicator")
    searchAnchor: "scrollingDropIndicator"
    collapsible: true

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        SettingsRow {
            title: i18n("Show drop indicator")
            searchAnchor: "scrollingDropIndicatorEnabled"
            description: i18n("Highlight the space a dragged window will land in while re-inserting it into the scroll strip")

            SettingsSwitch {
                checked: appSettings.scrollingDropIndicatorEnabled
                accessibleName: i18n("Show the drop indicator during a drag re-insert")
                onToggled: function (newValue) {
                    appSettings.scrollingDropIndicatorEnabled = newValue;
                }
            }
        }

        SettingsSeparator {}

        ThemeFallbackColorRow {
            title: i18n("Indicator color")
            searchAnchor: "scrollingDropIndicatorColor"
            description: i18n("Color of the drop indicator. Follows the color scheme unless you pick one.")
            // Dead while the indicator is off, matching how the tab page gates
            // every row below its master switch.
            enabled: appSettings.scrollingDropIndicatorEnabled

            storedColor: appSettings.scrollingDropIndicatorColor
            themeColor: Kirigami.Theme.highlightColor
            picker: root.picker
            onColorChosen: function (hex) {
                appSettings.scrollingDropIndicatorColor = hex;
            }
        }
    }
}
