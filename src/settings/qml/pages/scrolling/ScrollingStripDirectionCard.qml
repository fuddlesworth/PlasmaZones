// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The Strip direction card, hosting the one row of the axis
 * sub-domain of the per-screen scrolling map. It sits above the Focus and
 * view card, which used to carry the row app-wide; the row moved out so it
 * could take a per-monitor scope chip without the chip appearing to govern
 * the app-wide rows around it.
 *
 * The chip scopes to the AXIS sub-domain (hasPerScreenScrollingAxisSettings /
 * clearPerScreenScrollingAxisSettings), the complement of the New columns
 * card's sizing chip on the Columns page — the two share one per-screen
 * entry, and the split keeps either card's reset off the other's override.
 * The SetScrollStripAxis context rule layers over whatever this card
 * resolves, per screen, desktop, or activity.
 */
SettingsCard {
    id: stripDirectionCard

    headerText: i18n("Strip direction")
    searchAnchor: "stripDirection"
    collapsible: true
    scopeEnabled: true
    scopeAppSettings: settingsController
    scopeHasOverridesMethod: "hasPerScreenScrollingAxisSettings"
    scopeClearerMethod: "clearPerScreenScrollingAxisSettings"

    PerScreenOverrideHelper {
        id: psHelper

        appSettings: settingsController
        // Shared app-wide scope — a monitor picked on any per-monitor page
        // stays picked here.
        selectedScreenName: settingsController.scopeScreenName
        getterMethod: "getPerScreenScrollingSettings"
        setterMethod: "setPerScreenScrollingSetting"
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        SettingsRow {
            title: i18n("Strip direction")
            searchAnchor: "stripAxis"
            description: i18nc("the words Match the screen shape, Side to side, and Top to bottom must match the option labels shown in the picker beside this text", "Which way the strip runs. Match the screen shape runs it top to bottom when the usable area is taller than it is wide, and side to side otherwise. Columns still divide across the strip whichever way it runs.")

            WideComboBox {
                Accessible.name: i18n("Strip direction")
                textRole: "text"
                valueRole: "value"
                model: settingsController.valueOptions("Scrolling", "StripAxis")
                storedValue: psHelper.settingValue("StripAxis", appSettings.scrollingStripAxis)
                onActivated: psHelper.writeSetting("StripAxis", currentValue, function (v) {
                    appSettings.scrollingStripAxis = v;
                })
            }
        }
    }
}
