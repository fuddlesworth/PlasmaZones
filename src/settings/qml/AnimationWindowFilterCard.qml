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
    /// Prefix for this host's search anchors, so the two pages register
    /// distinct ids for the same row (anchors are page-scoped, but the
    /// global search catalogue keys on the pair).
    property string anchorPrefix: ""

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
                searchAnchor: card.anchorPrefix + "excludeNotificationsAndOsds"
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
