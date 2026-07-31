// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Scrolling → View: how the strip's viewport follows focus, plus the
 * Meta+wheel column-focus gesture. One of the three advanced scrolling
 * leaves (View / Columns / Window), split from the original single page.
 *
 * Rows bind the app-wide `appSettings` context property directly — the
 * scrolling settings are plain Settings Q_PROPERTYs with no page
 * sub-controller. App-wide only, matching the tiling/snapping behavior
 * pages: per-context centering is a rules job (the SetCenterFocusedColumn
 * context action), not a per-monitor chip.
 */
SettingsFlickable {
    id: root

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Focus and view")
            searchAnchor: "focusAndView"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Center the focused column")
                    searchAnchor: "centerFocusedColumn"
                    description: i18n("With Never, the strip stays still until the focused column would leave the screen. With Always, the focused column parks in the middle. With On overflow, it centers only once the strip is wider than the screen.")

                    WideComboBox {
                        Accessible.name: i18n("Center the focused column")
                        textRole: "text"
                        valueRole: "value"
                        model: settingsController.valueOptions("Scrolling", "CenterFocusedColumn")
                        storedValue: appSettings.scrollingCenterFocusedColumn
                        onActivated: appSettings.scrollingCenterFocusedColumn = currentValue
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Center a lone column")
                    searchAnchor: "alwaysCenterSingleColumn"
                    description: i18n("When the strip holds a single column, center it even when Center the focused column is set to Never.")

                    SettingsSwitch {
                        checked: appSettings.scrollingAlwaysCenterSingleColumn
                        accessibleName: i18n("Center a lone column")
                        onToggled: function (newValue) {
                            appSettings.scrollingAlwaysCenterSingleColumn = newValue;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Scroll columns with the mouse wheel")
                    searchAnchor: "wheelFocusEnabled"
                    description: i18n("Hold Meta or Meta+Alt and scroll to move column focus along the strip. Off, the compositor keeps those wheel shortcuts.")

                    SettingsSwitch {
                        checked: appSettings.scrollingWheelFocusEnabled
                        accessibleName: i18n("Scroll columns with the mouse wheel")
                        onToggled: function (newValue) {
                            appSettings.scrollingWheelFocusEnabled = newValue;
                        }
                    }
                }

                SettingsRow {
                    title: i18n("Invert wheel direction")
                    searchAnchor: "wheelFocusInverted"
                    description: i18n("Scrolling down focuses the previous column instead of the next one")
                    enabled: appSettings.scrollingWheelFocusEnabled

                    SettingsSwitch {
                        checked: appSettings.scrollingWheelFocusInverted
                        accessibleName: i18n("Invert wheel direction")
                        onToggled: function (newValue) {
                            appSettings.scrollingWheelFocusInverted = newValue;
                        }
                    }
                }
            }
        }
    }
}
