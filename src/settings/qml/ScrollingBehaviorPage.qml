// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Scrolling → Behavior sub-page — focus behavior.
 *
 * The scroll-mode counterpart to TilingBehaviorPage's Behavior card: controls
 * whether a window entering the scroll layout takes focus, and whether moving
 * the pointer over a scroll column focuses it. Both are global Settings
 * Q_PROPERTYs handled effect-side by ScrollHandler.
 */
SettingsFlickable {
    id: root

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // Focus Card
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Focus")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Focus new windows")
                    description: i18n("Automatically focus a window as it enters the scroll layout")

                    SettingsSwitch {
                        checked: appSettings.scrollFocusNewWindows
                        accessibleName: i18n("Focus newly opened scroll windows")
                        onToggled: function(newValue) {
                            appSettings.scrollFocusNewWindows = newValue;
                        }
                    }

                }

                SettingsSeparator {
                }

                SettingsRow {
                    title: i18n("Focus follows mouse")
                    description: i18n("Moving the mouse pointer over a scroll column gives it focus")

                    SettingsSwitch {
                        checked: appSettings.scrollFocusFollowsMouse
                        accessibleName: i18n("Focus follows mouse pointer")
                        onToggled: function(newValue) {
                            appSettings.scrollFocusFollowsMouse = newValue;
                        }
                    }

                }

            }

        }

    }

}
