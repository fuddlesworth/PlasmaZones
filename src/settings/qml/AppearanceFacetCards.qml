// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Shared Colors / Decorations / Borders cards for both Tiling → Appearance and
// Snapping → Window Appearance. The two modes were near-verbatim duplicates
// differing only in which appSettings properties they bound (autotile* vs
// snapping*) and a few descriptions. Following GapsSettingsCard, the host page
// feeds current values in and handles the *Toggled / *Modified / *Picked
// signals, so each mode keeps its own reactive bindings to appSettings without
// this component needing to know the property names.
ColumnLayout {
    id: root

    spacing: Kirigami.Units.largeSpacing

    // --- Colors ---
    required property bool useSystemBorderColors
    required property color activeBorderColor
    required property color inactiveBorderColor
    property string activeColorDescription: i18n("Border color for the focused window")
    property string inactiveColorDescription: i18n("Border color for unfocused windows")

    // --- Decorations ---
    required property bool hideTitleBars
    property string hideTitleBarsDescription: i18n("Remove window title bars while tiled, restored when floating")
    property string hideTitleBarsAccessibleName: i18n("Hide title bars on tiled windows")

    // --- Borders ---
    required property bool showBorder
    required property int borderWidth
    required property int borderWidthMin
    required property int borderWidthMax
    required property int borderRadius
    required property int borderRadiusMin
    required property int borderRadiusMax
    property string borderWidthDescription: i18n("Thickness of colored borders around tiled windows")

    signal useSystemBorderColorsToggled(bool checked)
    signal activeBorderColorPicked(color selectedColor)
    signal inactiveBorderColorPicked(color selectedColor)
    signal hideTitleBarsToggled(bool checked)
    signal showBorderToggled(bool checked)
    signal borderWidthModified(int value)
    signal borderRadiusModified(int value)

    // =================================================================
    // Colors Card
    // =================================================================
    SettingsCard {
        Layout.fillWidth: true
        headerText: i18n("Colors")
        searchAnchor: "colors"
        collapsible: true

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            SettingsRow {
                title: i18n("Use system accent color")
                searchAnchor: "useSystemAccentColor"
                description: i18n("Derive border colors from your system color scheme")

                SettingsSwitch {
                    id: useSystemColorsSwitch

                    checked: root.useSystemBorderColors
                    accessibleName: i18n("Use system accent color")
                    onToggled: function (newValue) {
                        root.useSystemBorderColorsToggled(newValue);
                    }
                }
            }

            SettingsSeparator {
                visible: !useSystemColorsSwitch.checked
            }

            SettingsRow {
                visible: !useSystemColorsSwitch.checked
                title: i18n("Active border color")
                searchAnchor: "activeBorderColor"
                description: root.activeColorDescription

                ColorSwatchRow {
                    color: root.activeBorderColor
                    onClicked: {
                        activeBorderColorDialog.selectedColor = root.activeBorderColor;
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
                searchAnchor: "inactiveBorderColor"
                description: root.inactiveColorDescription

                ColorSwatchRow {
                    color: root.inactiveBorderColor
                    onClicked: {
                        inactiveBorderColorDialog.selectedColor = root.inactiveBorderColor;
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
        searchAnchor: "decorations"
        collapsible: true

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            SettingsRow {
                title: i18n("Hide title bars")
                searchAnchor: "hideTitleBars"
                description: root.hideTitleBarsDescription

                SettingsSwitch {
                    checked: root.hideTitleBars
                    accessibleName: root.hideTitleBarsAccessibleName
                    onToggled: function (newValue) {
                        root.hideTitleBarsToggled(newValue);
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
        searchAnchor: "borders"
        showToggle: true
        toggleChecked: root.showBorder
        onToggleClicked: checked => {
            return root.showBorderToggled(checked);
        }
        collapsible: true

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            SettingsRow {
                title: i18n("Border width")
                searchAnchor: "borderWidth"
                description: root.borderWidthDescription

                SettingsSpinBox {
                    from: root.borderWidthMin
                    to: root.borderWidthMax
                    value: root.borderWidth
                    onValueModified: value => {
                        return root.borderWidthModified(value);
                    }
                }
            }

            SettingsSeparator {}

            SettingsRow {
                title: i18n("Corner radius")
                searchAnchor: "cornerRadius"
                description: i18n("Roundness of border corners (0 for square)")

                SettingsSpinBox {
                    from: root.borderRadiusMin
                    to: root.borderRadiusMax
                    value: root.borderRadius
                    onValueModified: value => {
                        return root.borderRadiusModified(value);
                    }
                }
            }
        }
    }

    // =====================================================================
    // Color Dialogs — kept with the swatches that open them.
    // =====================================================================
    ColorDialog {
        id: activeBorderColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Active Border Color")
        onAccepted: root.activeBorderColorPicked(selectedColor)
    }

    ColorDialog {
        id: inactiveBorderColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Inactive Border Color")
        onAccepted: root.inactiveBorderColorPicked(selectedColor)
    }
}
