// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The animation window filter: which windows are eligible for any
 * animation at all (transient windows, notifications and OSDs, and the
 * minimum width / height thresholds).
 *
 * Hosted by BOTH the advanced Animations → General page and the simple-mode
 * Animations page. These are global config toggles on the animation-specific
 * filtering group, distinct from the snapping/tiling and decoration filters
 * that use the same WindowFilterCard shell.
 */
WindowFilterCard {
    id: card

    /// The ISettings object holding the animation* filter keys.
    required property QtObject cardSettings
    /// Search anchor id for the notifications row. Passed whole rather
    /// than composed from a prefix: concatenating onto an identifier
    /// silently changes its casing, which breaks the catalogue's exact-id
    /// match. The two hosts register distinct ids because the global search
    /// catalogue keys on (page, anchor).
    property string notificationsAnchor: "excludeNotificationsAndOsds"

    Layout.fillWidth: true

    excludeTransient: card.cardSettings.animationExcludeTransientWindows
    transientDescription: i18n("Skip animations for dialogs, popups, tooltips, and dropdown menus")
    transientAccessibleName: i18n("Exclude transient windows from animations")
    onExcludeTransientToggled: value => {
        card.cardSettings.animationExcludeTransientWindows = value;
    }

    // Animations-only extra row: exclude notifications / OSDs. Supplies its
    // own leading separator so it composes under the transient row.
    insertAfterTransient: Component {
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            SettingsSeparator {}

            SettingsRow {
                title: i18n("Exclude notifications and OSDs")
                searchAnchor: card.notificationsAnchor
                description: i18n("Skip animations for notification popups and on-screen displays such as volume and brightness")

                SettingsSwitch {
                    checked: card.cardSettings.animationExcludeNotificationsAndOsd
                    accessibleName: i18n("Exclude notifications and on-screen displays from animations")
                    onToggled: function (newValue) {
                        card.cardSettings.animationExcludeNotificationsAndOsd = newValue;
                    }
                }
            }
        }
    }

    minWidth: card.cardSettings.animationMinimumWindowWidth
    minWidthFrom: settingsController.generalPage.animationMinimumWindowWidthMin
    minWidthTo: settingsController.generalPage.animationMinimumWindowWidthMax
    minWidthDescription: i18n("Windows narrower than this will not animate")
    minWidthDisabledDescription: i18n("Disabled. No width threshold.")
    minWidthAccessibleName: i18n("Minimum window width for animations")
    onMinWidthModified: value => {
        card.cardSettings.animationMinimumWindowWidth = value;
    }

    minHeight: card.cardSettings.animationMinimumWindowHeight
    minHeightFrom: settingsController.generalPage.animationMinimumWindowHeightMin
    minHeightTo: settingsController.generalPage.animationMinimumWindowHeightMax
    minHeightDescription: i18n("Windows shorter than this will not animate")
    minHeightDisabledDescription: i18n("Disabled. No height threshold.")
    minHeightAccessibleName: i18n("Minimum window height for animations")
    onMinHeightModified: value => {
        card.cardSettings.animationMinimumWindowHeight = value;
    }
}
