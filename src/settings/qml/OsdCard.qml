// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief On-Screen Display settings card for the General settings page.
 *
 * Contains OSD toggle, style, and overlay display mode settings.
 *
 * Required properties:
 *   - appSettings: the settings backend object
 */
Item {
    id: osdRoot

    required property var appSettings

    Layout.fillWidth: true
    implicitHeight: osdCard.implicitHeight

    SettingsCard {
        id: osdCard

        anchors.fill: parent
        headerText: i18n("On-Screen Display")
        collapsible: true

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            SettingsRow {
                title: i18n("Layout switch OSD")
                description: i18n("Show notification when switching between zone layouts")

                SettingsSwitch {
                    checked: osdRoot.appSettings.showOsdOnLayoutSwitch
                    accessibleName: i18n("Show OSD on layout switch")
                    onToggled: function (newValue) {
                        osdRoot.appSettings.showOsdOnLayoutSwitch = newValue;
                    }
                }
            }

            SettingsSeparator {}

            SettingsRow {
                title: i18n("Desktop switch OSD")
                description: i18n("Show notification on virtual desktop change, activity change, and daemon startup")

                SettingsSwitch {
                    checked: osdRoot.appSettings.showOsdOnDesktopSwitch
                    accessibleName: i18n("Show OSD on desktop switch")
                    onToggled: function (newValue) {
                        osdRoot.appSettings.showOsdOnDesktopSwitch = newValue;
                    }
                }
            }

            SettingsSeparator {}

            SettingsRow {
                title: i18n("Keyboard navigation OSD")
                description: i18n("Show notification when moving windows with keyboard shortcuts")

                SettingsSwitch {
                    checked: osdRoot.appSettings.showNavigationOsd
                    accessibleName: i18n("Show keyboard navigation OSD")
                    onToggled: function (newValue) {
                        osdRoot.appSettings.showNavigationOsd = newValue;
                    }
                }
            }

            // OSD style (osdStyle) moved to Appearance → Daemon Surfaces → OSDs:
            // it styles the OSD surface. The toggles above ("which OSD shows")
            // and Overlay style below stay here as behavior.

            SettingsSeparator {}

            SettingsRow {
                title: i18n("Overlay style")
                description: i18n("How zones appear while dragging a window")

                WideComboBox {
                    Accessible.name: i18n("Overlay style")
                    currentIndex: Math.max(0, Math.min(osdRoot.appSettings.overlayDisplayMode, 1))
                    model: [i18n("Full zone highlight"), i18n("Compact preview")]
                    onActivated: index => {
                        osdRoot.appSettings.overlayDisplayMode = index;
                    }
                }
            }
        }
    }
}
