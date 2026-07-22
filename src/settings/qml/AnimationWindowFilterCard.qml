// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The animation window filter: which windows are eligible for any
 * animation at all (transient windows, notifications and OSDs, and the
 * minimum width / height thresholds), plus the explainer banner that sits
 * above it.
 *
 * Hosted by BOTH the advanced Animations → General page and the simple-mode
 * Animations page. These are global config toggles on the animation-specific
 * filtering group, distinct from the snapping/tiling and decoration filters
 * that use the same WindowFilterCard shell.
 *
 * The root is a ColumnLayout rather than the WindowFilterCard itself so the
 * banner can be declared here once instead of being copied into both hosts.
 * The banner stays OUTSIDE the card body (matching where the hosts used to
 * put it) so collapsing the card does not hide the explanation of what the
 * card does.
 */
ColumnLayout {
    id: card

    /// The ISettings object holding the animation* filter keys.
    required property QtObject cardSettings
    Layout.fillWidth: true
    spacing: Kirigami.Units.largeSpacing

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        type: Kirigami.MessageType.Information
        // Kirigami.InlineMessage defaults to hidden; opt in explicitly.
        visible: true
        text: i18n("Filtered windows are not animated. Use a Rule to keep a specific application animated even when a filter would exclude it.")
    }

    WindowFilterCard {
        Layout.fillWidth: true

        excludeTransient: card.cardSettings.animationExcludeTransientWindows
        transientDescription: i18n("Skip animations for dialogs, popups, tooltips, and dropdown menus")
        transientAccessibleName: i18n("Exclude transient windows from animations")
        onExcludeTransientToggled: value => {
            card.cardSettings.animationExcludeTransientWindows = value;
        }

        // Animations-only extra row: exclude notifications / OSDs. Supplies
        // its own leading separator so it composes under the transient row.
        insertAfterTransient: Component {
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Exclude notifications and OSDs")
                    // Search anchors are keyed on (page, anchor), and this card's
                    // two hosts are different pages, so both can register the
                    // same id. Matches the sibling filter rows, which are
                    // likewise shared verbatim across the two hosts.
                    searchAnchor: "excludeNotificationsAndOsds"
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
}
