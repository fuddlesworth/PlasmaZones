// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SettingsFlickable {
    id: root

    readonly property var settingsBridge: settingsController.snappingWindowAppearancePage

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // Colors Card
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Colors")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Use system accent color")
                    description: i18n("Derive border colors from your system color scheme")

                    SettingsSwitch {
                        id: useSystemColorsSwitch

                        checked: appSettings.snapWindowUseSystemBorderColors
                        accessibleName: i18n("Use system accent color")
                        onToggled: function (newValue) {
                            appSettings.snapWindowUseSystemBorderColors = newValue;
                        }
                    }
                }

                SettingsSeparator {
                    visible: !useSystemColorsSwitch.checked
                }

                SettingsRow {
                    visible: !useSystemColorsSwitch.checked
                    title: i18n("Active border color")
                    description: i18n("Border color for the focused snapped window")

                    ColorSwatchRow {
                        color: appSettings.snapWindowBorderColor
                        onClicked: {
                            activeBorderColorDialog.selectedColor = appSettings.snapWindowBorderColor;
                            activeBorderColorDialog.open();
                        }
                    }
                }

                SettingsSeparator {
                    visible: !useSystemColorsSwitch.checked
                }

                SettingsRow {
                    visible: !useSystemColorsSwitch.checked
                    title: i18n("Inactive border color")
                    description: i18n("Border color for unfocused snapped windows")

                    ColorSwatchRow {
                        color: appSettings.snapWindowInactiveBorderColor
                        onClicked: {
                            inactiveBorderColorDialog.selectedColor = appSettings.snapWindowInactiveBorderColor;
                            inactiveBorderColorDialog.open();
                        }
                    }
                }
            }
        }

        // =================================================================
        // Decorations Card
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Decorations")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Hide title bars")
                    description: i18n("Remove window title bars while snapped, restored when floating")

                    SettingsSwitch {
                        id: hideTitleBarsSwitch

                        checked: appSettings.snapWindowHideTitleBars
                        accessibleName: i18n("Hide title bars on snapped windows")
                        onToggled: function (newValue) {
                            appSettings.snapWindowHideTitleBars = newValue;
                        }
                    }
                }
            }
        }

        // =================================================================
        // Borders Card
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Borders")
            showToggle: true
            toggleChecked: appSettings.snapWindowShowBorder
            onToggleClicked: checked => {
                return appSettings.snapWindowShowBorder = checked;
            }
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Border width")
                    description: i18n("Thickness of colored borders around snapped windows")

                    SettingsSpinBox {
                        from: root.settingsBridge.snapWindowBorderWidthMin
                        to: root.settingsBridge.snapWindowBorderWidthMax
                        value: appSettings.snapWindowBorderWidth
                        onValueModified: value => {
                            return appSettings.snapWindowBorderWidth = value;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Corner radius")
                    description: i18n("Roundness of border corners (0 for square)")

                    SettingsSpinBox {
                        from: root.settingsBridge.snapWindowBorderRadiusMin
                        to: root.settingsBridge.snapWindowBorderRadiusMax
                        value: appSettings.snapWindowBorderRadius
                        onValueModified: value => {
                            return appSettings.snapWindowBorderRadius = value;
                        }
                    }
                }
            }
        }
    }

    // =====================================================================
    // Color Dialogs
    // =====================================================================
    ColorDialog {
        id: activeBorderColorDialog

        title: i18n("Choose Active Border Color")
        onAccepted: appSettings.snapWindowBorderColor = selectedColor
    }

    ColorDialog {
        id: inactiveBorderColorDialog

        title: i18n("Choose Inactive Border Color")
        onAccepted: appSettings.snapWindowInactiveBorderColor = selectedColor
    }
}
