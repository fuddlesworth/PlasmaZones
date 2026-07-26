// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Tiling → Scrolling: the niri-style scrolling engine's own knobs.
 *
 * Every row binds the app-wide `appSettings` context property directly — the
 * scrolling settings are plain Settings Q_PROPERTYs with no page sub-controller,
 * so there is no per-page state to carry here.
 */
SettingsFlickable {
    id: root

    // Width kind selector values, mirroring ConfigDefaults'
    // scrollingDefaultColumnWidthKind vocabulary and value bounds, read
    // once from ConfigDefaults via the controller — the C++ side is the
    // single home for these numbers (kind ints, slider range, spin range).
    readonly property var _scrollWidthConsts: settingsController.scrollingWidthConstants()
    readonly property int widthKindProportion: _scrollWidthConsts.kindProportion
    readonly property int widthKindFixed: _scrollWidthConsts.kindFixed
    readonly property int widthKindClientDecides: _scrollWidthConsts.kindClientDecides
    readonly property int fieldWidth: Kirigami.Units.gridUnit * 16

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // Focus and View Card
        // =================================================================
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
                        model: settingsController.valueOptions("Tiling.Scrolling", "CenterFocusedColumn")
                        currentIndex: Math.max(0, indexOfValue(appSettings.scrollingCenterFocusedColumn))
                        onActivated: appSettings.scrollingCenterFocusedColumn = currentValue
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Center a lone column")
                    searchAnchor: "alwaysCenterSingleColumn"
                    description: i18n("When the strip holds a single column, center it regardless of how Center the focused column is set")

                    SettingsSwitch {
                        checked: appSettings.scrollingAlwaysCenterSingleColumn
                        accessibleName: i18n("Center a lone column")
                        onToggled: function (newValue) {
                            appSettings.scrollingAlwaysCenterSingleColumn = newValue;
                        }
                    }
                }
            }
        }

        // =================================================================
        // New Columns Card
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("New columns")
            searchAnchor: "newColumns"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Default width")
                    searchAnchor: "defaultColumnWidthKind"
                    description: i18n("How wide a column is when it first opens")

                    WideComboBox {
                        Accessible.name: i18n("Default column width")
                        textRole: "text"
                        valueRole: "value"
                        model: settingsController.valueOptions("Tiling.Scrolling", "DefaultColumnWidthKind")
                        currentIndex: Math.max(0, indexOfValue(appSettings.scrollingDefaultColumnWidthKind))
                        onActivated: appSettings.scrollingDefaultColumnWidthKind = currentValue
                    }
                }

                SettingsSeparator {
                    visible: appSettings.scrollingDefaultColumnWidthKind !== root.widthKindClientDecides
                }

                // Proportion and pixel widths share one stored value, so only
                // the control matching the selected kind is shown. A disabled
                // SettingsRow collapses out of the layout, which is also what
                // hides both rows for "window decides".
                SettingsRow {
                    title: i18n("Proportion of the screen")
                    searchAnchor: "defaultColumnWidthProportion"
                    description: i18n("How much of the usable screen width a new column takes")
                    enabled: appSettings.scrollingDefaultColumnWidthKind === root.widthKindProportion

                    SettingsSlider {
                        accessibleName: i18n("Proportion of the screen")
                        from: root._scrollWidthConsts.proportionMin
                        to: root._scrollWidthConsts.proportionMax
                        stepSize: root._scrollWidthConsts.proportionStep
                        value: appSettings.scrollingDefaultColumnWidthValue
                        formatValue: function (v) {
                            return Math.round(v * 100) + "%";
                        }
                        onMoved: function (newValue) {
                            appSettings.scrollingDefaultColumnWidthValue = newValue;
                        }
                    }
                }

                SettingsRow {
                    title: i18n("Fixed width")
                    searchAnchor: "defaultColumnWidthFixed"
                    description: i18n("How many pixels wide a new column is")
                    enabled: appSettings.scrollingDefaultColumnWidthKind === root.widthKindFixed

                    SettingsSpinBox {
                        id: fixedWidthSpin

                        accessibleName: i18n("Fixed column width")
                        from: root._scrollWidthConsts.fixedMin
                        to: root._scrollWidthConsts.fixedMax
                        stepSize: root._scrollWidthConsts.fixedStep
                        onValueModified: value => appSettings.scrollingDefaultColumnWidthValue = value
                        // Fed through a guarded Binding rather than a plain
                        // `value:` one: SettingsSpinBox echoes each edit back
                        // into its own `value`, which would destroy a plain
                        // binding after the first edit and strand every later
                        // config-side change.
                        Binding on value {
                            value: Math.round(appSettings.scrollingDefaultColumnWidthValue)
                            when: !fixedWidthSpin.editing
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Open new columns as")
                    searchAnchor: "defaultColumnDisplay"
                    description: i18n("Normal stacks the windows of a column above each other. Tabbed shows one window at a time behind a tab strip.")

                    WideComboBox {
                        Accessible.name: i18n("Open new columns as")
                        textRole: "text"
                        valueRole: "value"
                        model: settingsController.valueOptions("Tiling.Scrolling", "DefaultColumnDisplay")
                        currentIndex: Math.max(0, indexOfValue(appSettings.scrollingDefaultColumnDisplay))
                        onActivated: appSettings.scrollingDefaultColumnDisplay = currentValue
                    }
                }
            }
        }

        // =================================================================
        // Presets Card
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Width and height presets")
            searchAnchor: "scrollingPresets"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Column widths")
                    searchAnchor: "presetColumnWidths"
                    description: i18n("Comma-separated fractions of the work area width, cycled by the preset shortcuts")

                    TextField {
                        id: widthPresetField

                        width: root.fieldWidth
                        Accessible.name: i18n("Column width presets")
                        placeholderText: i18nc("@info:placeholder comma separated column width fractions", "0.333,0.5,0.667")
                        // Echo the stored value back after the write: Enter
                        // fires editingFinished with focus still held, and the
                        // guarded Binding below is blocked while focused — so a
                        // destructive canonicalisation (dropped garbage entries,
                        // de-duped values) would stay invisible until focus
                        // moves. Reading back shows what was actually kept.
                        onEditingFinished: {
                            appSettings.scrollingPresetColumnWidths = text;
                            text = appSettings.scrollingPresetColumnWidths;
                        }
                        // Guarded Binding, not a plain `text:` one — typing
                        // severs a plain binding, and per-page Discard/Reset,
                        // profile switches, and schema canonicalisation would
                        // then never reach the field again.
                        Binding on text {
                            value: appSettings.scrollingPresetColumnWidths
                            when: !widthPresetField.activeFocus
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Window heights")
                    searchAnchor: "presetWindowHeights"
                    description: i18n("Comma-separated fractions of the work area height, cycled by the preset shortcuts")

                    TextField {
                        id: heightPresetField

                        width: root.fieldWidth
                        Accessible.name: i18n("Window height presets")
                        placeholderText: i18nc("@info:placeholder comma separated window height fractions", "0.333,0.5,0.667")
                        // Same echo rationale as the width field.
                        onEditingFinished: {
                            appSettings.scrollingPresetWindowHeights = text;
                            text = appSettings.scrollingPresetWindowHeights;
                        }
                        // Same guarded-binding rationale as the width field.
                        Binding on text {
                            value: appSettings.scrollingPresetWindowHeights
                            when: !heightPresetField.activeFocus
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }
            }
        }
    }
}
