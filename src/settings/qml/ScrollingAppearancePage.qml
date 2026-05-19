// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Scrolling → Appearance sub-page — column border decoration.
 *
 * Mirrors TilingAppearancePage: edits the scroll-mode border colors, the
 * hide-title-bars toggle and the border width / corner radius. Every value is
 * a global Settings Q_PROPERTY (no per-screen overrides — border decoration is
 * mode-wide, like autotile's).
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

                        checked: appSettings.scrollUseSystemBorderColors
                        accessibleName: i18n("Use system accent color")
                        onToggled: function(newValue) {
                            appSettings.scrollUseSystemBorderColors = newValue;
                        }
                    }

                }

                SettingsSeparator {
                    visible: !useSystemColorsSwitch.checked
                }

                SettingsRow {
                    visible: !useSystemColorsSwitch.checked
                    title: i18n("Active border color")
                    description: i18n("Border color for the focused window")

                    ColorSwatchRow {
                        color: appSettings.scrollBorderColor
                        onClicked: {
                            activeBorderColorDialog.selectedColor = appSettings.scrollBorderColor;
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
                    description: i18n("Border color for unfocused windows")

                    ColorSwatchRow {
                        color: appSettings.scrollInactiveBorderColor
                        onClicked: {
                            inactiveBorderColorDialog.selectedColor = appSettings.scrollInactiveBorderColor;
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
                    description: i18n("Remove window title bars while in the scroll layout, restored when a window leaves it")

                    SettingsSwitch {
                        checked: appSettings.scrollHideTitleBars
                        accessibleName: i18n("Hide title bars on scroll-managed windows")
                        onToggled: function(newValue) {
                            appSettings.scrollHideTitleBars = newValue;
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
            toggleChecked: appSettings.scrollShowBorder
            onToggleClicked: (checked) => {
                return appSettings.scrollShowBorder = checked;
            }
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Border width")
                    description: i18n("Thickness of colored borders around scroll columns")

                    SettingsSpinBox {
                        from: 0
                        // Matches the Scrolling.Appearance.Borders schema clamp
                        // (ConfigDefaults::scrollBorderWidthMax) — a larger
                        // value would be silently truncated on save.
                        to: 10
                        value: appSettings.scrollBorderWidth
                        onValueModified: (value) => {
                            return appSettings.scrollBorderWidth = value;
                        }
                    }

                }

                SettingsSeparator {
                }

                SettingsRow {
                    title: i18n("Corner radius")
                    description: i18n("Roundness of border corners (0 for square)")

                    SettingsSpinBox {
                        from: 0
                        // Matches the Scrolling.Appearance.Borders schema clamp
                        // (ConfigDefaults::scrollBorderRadiusMax).
                        to: 20
                        value: appSettings.scrollBorderRadius
                        onValueModified: (value) => {
                            return appSettings.scrollBorderRadius = value;
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
        onAccepted: appSettings.scrollBorderColor = selectedColor
    }

    ColorDialog {
        id: inactiveBorderColorDialog

        title: i18n("Choose Inactive Border Color")
        onAccepted: appSettings.scrollInactiveBorderColor = selectedColor
    }

}
