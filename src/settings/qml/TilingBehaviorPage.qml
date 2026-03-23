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

            contentItem: Kirigami.FormLayout {
                ComboBox {
                    Kirigami.FormData.label: i18n("New windows:")
                    Layout.fillWidth: true
                    textRole: "text"
                    valueRole: "value"
                    model: [{
                        "text": i18n("Add after existing windows"),
                        "value": 0
                    }, {
                        "text": i18n("Insert after focused"),
                        "value": 1
                    }, {
                        "text": i18n("Add as main window"),
                        "value": 2
                    }]
                    currentIndex: Math.max(0, indexOfValue(appSettings.autotileInsertPosition))
                    onActivated: appSettings.autotileInsertPosition = currentValue
                }

                CheckBox {
                    Kirigami.FormData.label: i18n("Focus:")
                    text: i18n("Automatically focus newly opened windows")
                    checked: appSettings.autotileFocusNewWindows
                    onToggled: appSettings.autotileFocusNewWindows = checked
                }

                CheckBox {
                    Kirigami.FormData.label: " "
                    text: i18n("Focus follows mouse pointer")
                    checked: appSettings.autotileFocusFollowsMouse
                    onToggled: appSettings.autotileFocusFollowsMouse = checked
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("When enabled, moving mouse over a window focuses it")
                }

                CheckBox {
                    Kirigami.FormData.label: i18n("Constraints:")
                    text: i18n("Respect window minimum size")
                    checked: appSettings.autotileRespectMinimumSize
                    onToggled: appSettings.autotileRespectMinimumSize = checked
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("Windows will not be resized below their minimum size. This may leave gaps in the layout.")
                }

            }

        }

    }

}
