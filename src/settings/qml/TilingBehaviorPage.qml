// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // Behavior Card
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Behavior")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("New window placement")
                    description: i18n("Where newly opened windows appear in the tiling order")

                    ComboBox {
                        Layout.fillWidth: false
                        Accessible.name: i18n("New window placement")
                        textRole: "text"
                        valueRole: "value"
                        model: [{
                            "text": i18n("After existing"),
                            "value": 0
                        }, {
                            "text": i18n("After focused"),
                            "value": 1
                        }, {
                            "text": i18n("As main window"),
                            "value": 2
                        }]
                        currentIndex: Math.max(0, indexOfValue(appSettings.autotileInsertPosition))
                        onActivated: appSettings.autotileInsertPosition = currentValue
                    }

                }

                SettingsSeparator {
                }

                SettingsRow {
                    title: i18n("Focus new windows")
                    description: i18n("Automatically focus windows when they open")

                    SettingsSwitch {
                        checked: appSettings.autotileFocusNewWindows
                        accessibleName: i18n("Focus newly opened windows")
                        onToggled: appSettings.autotileFocusNewWindows = checked
                    }

                }

                SettingsSeparator {
                }

                SettingsRow {
                    title: i18n("Focus follows mouse")
                    description: i18n("Moving the mouse pointer over a window gives it focus")

                    SettingsSwitch {
                        checked: appSettings.autotileFocusFollowsMouse
                        accessibleName: i18n("Focus follows mouse pointer")
                        onToggled: appSettings.autotileFocusFollowsMouse = checked
                    }

                }

                SettingsSeparator {
                }

                SettingsRow {
                    title: i18n("Respect minimum size")
                    description: i18n("Prevent windows from being resized below their minimum; may leave gaps")

                    SettingsSwitch {
                        checked: appSettings.autotileRespectMinimumSize
                        accessibleName: i18n("Respect window minimum size")
                        onToggled: appSettings.autotileRespectMinimumSize = checked
                    }

                }

            }

        }

    }

}
