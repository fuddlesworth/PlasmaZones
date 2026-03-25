// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    readonly property bool bordersActive: hideTitleBarsCheck.checked || showBorderCheck.checked

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // =================================================================
        // Appearance Card (Colors + Decorations + Borders)
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Appearance")
            collapsible: true

            contentItem: Kirigami.FormLayout {
                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Colors")
                }

                CheckBox {
                    id: useSystemColorsCheck

                    Kirigami.FormData.label: i18n("Color scheme:")
                    text: i18n("Use system accent color")
                    checked: appSettings.autotileUseSystemBorderColors
                    onToggled: appSettings.autotileUseSystemBorderColors = checked
                }

                ColorSwatchRow {
                    formLabel: i18n("Active color:")
                    visible: !useSystemColorsCheck.checked
                    color: appSettings.autotileBorderColor
                    onClicked: {
                        activeBorderColorDialog.selectedColor = appSettings.autotileBorderColor;
                        activeBorderColorDialog.open();
                    }
                }

                ColorSwatchRow {
                    formLabel: i18n("Inactive color:")
                    visible: !useSystemColorsCheck.checked
                    color: appSettings.autotileInactiveBorderColor
                    onClicked: {
                        inactiveBorderColorDialog.selectedColor = appSettings.autotileInactiveBorderColor;
                        inactiveBorderColorDialog.open();
                    }
                }

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Decorations")
                }

                CheckBox {
                    id: hideTitleBarsCheck

                    Kirigami.FormData.label: i18n("Title bars:")
                    text: i18n("Hide title bars on tiled windows")
                    checked: appSettings.autotileHideTitleBars
                    onToggled: appSettings.autotileHideTitleBars = checked
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("Remove window title bars while autotiled. Restored when floating or leaving autotile mode.")
                }

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Borders")
                }

                CheckBox {
                    id: showBorderCheck

                    Kirigami.FormData.label: i18n("Border:")
                    text: i18n("Show borders in tiling mode")
                    checked: appSettings.autotileShowBorder
                    onToggled: appSettings.autotileShowBorder = checked
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("Draw colored borders around all windows in tiling mode. Active color for focused, inactive for unfocused. Works with or without hidden title bars.")
                }

                SettingsSpinBox {
                    formLabel: i18n("Width:")
                    visible: root.bordersActive
                    from: settingsController.autotileBorderWidthMin
                    to: settingsController.autotileBorderWidthMax
                    value: appSettings.autotileBorderWidth
                    tooltipText: i18n("Colored border drawn around tiled windows (0 to disable)")
                    onValueModified: (value) => {
                        return appSettings.autotileBorderWidth = value;
                    }
                }

                SettingsSpinBox {
                    formLabel: i18n("Corner radius:")
                    visible: root.bordersActive
                    from: settingsController.autotileBorderRadiusMin
                    to: settingsController.autotileBorderRadiusMax
                    value: appSettings.autotileBorderRadius
                    tooltipText: i18n("Corner radius for the border (0 for square corners)")
                    onValueModified: (value) => {
                        return appSettings.autotileBorderRadius = value;
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
        onAccepted: appSettings.autotileBorderColor = selectedColor
    }

    ColorDialog {
        id: inactiveBorderColorDialog

        title: i18n("Choose Inactive Border Color")
        onAccepted: appSettings.autotileInactiveBorderColor = selectedColor
    }

}
