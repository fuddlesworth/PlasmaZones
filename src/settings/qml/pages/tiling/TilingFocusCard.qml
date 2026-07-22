// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The tiling Focus card, shared between the advanced
 * Tiling → Window page and the simple-mode Tiling page.
 */
SettingsCard {
    headerText: i18n("Focus")
    searchAnchor: "focus"
    collapsible: true

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        SettingsRow {
            title: i18n("Focus new windows")
            searchAnchor: "focusNewWindows"
            description: i18n("Focus a window when it opens")

            SettingsSwitch {
                checked: appSettings.autotileFocusNewWindows
                accessibleName: i18n("Focus newly opened windows")
                onToggled: function (newValue) {
                    appSettings.autotileFocusNewWindows = newValue;
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Focus follows mouse")
            searchAnchor: "focusFollowsMouse"
            description: i18n("Moving the mouse pointer over a window gives it focus")

            SettingsSwitch {
                checked: appSettings.autotileFocusFollowsMouse
                accessibleName: i18n("Focus follows mouse pointer")
                onToggled: function (newValue) {
                    appSettings.autotileFocusFollowsMouse = newValue;
                }
            }
        }
    }
}
