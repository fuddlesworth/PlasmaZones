// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The snapping Focus card, shared between the advanced
 * Snapping → Window page and the simple-mode Snapping page.
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
            description: i18n("Focus a window when it is automatically placed into a zone on open")

            SettingsSwitch {
                checked: appSettings.snappingFocusNewWindows
                accessibleName: i18n("Focus newly placed windows")
                onToggled: function (newValue) {
                    appSettings.snappingFocusNewWindows = newValue;
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Focus follows mouse")
            searchAnchor: "focusFollowsMouse"
            description: i18n("Moving the mouse pointer over a snapped window gives it focus")

            SettingsSwitch {
                checked: appSettings.snappingFocusFollowsMouse
                accessibleName: i18n("Snapped window focus follows mouse pointer")
                onToggled: function (newValue) {
                    appSettings.snappingFocusFollowsMouse = newValue;
                }
            }
        }
    }
}
