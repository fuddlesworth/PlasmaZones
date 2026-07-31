// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The scrolling Focus card, the peer of TilingFocusCard and
 * SnappingFocusCard. All rows bind the appSettings context property, so the
 * card carries no per-page state.
 */
SettingsCard {
    headerText: i18n("Focus")
    searchAnchor: "scrollingFocus"
    collapsible: true

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        SettingsRow {
            title: i18n("Focus new windows")
            searchAnchor: "scrollingFocusNewWindows"
            description: i18n("Focus a window when it opens")

            SettingsSwitch {
                checked: appSettings.scrollingFocusNewWindows
                accessibleName: i18n("Focus newly opened windows")
                onToggled: function (newValue) {
                    appSettings.scrollingFocusNewWindows = newValue;
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Focus follows mouse")
            searchAnchor: "scrollingFocusFollowsMouse"
            description: i18n("Moving the mouse pointer over a window gives it focus")

            SettingsSwitch {
                checked: appSettings.scrollingFocusFollowsMouse
                accessibleName: i18n("Focus follows mouse pointer")
                onToggled: function (newValue) {
                    appSettings.scrollingFocusFollowsMouse = newValue;
                }
            }
        }
    }
}
