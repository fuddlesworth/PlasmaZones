// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// "Appearance → Windows" page. The window border, title-bar, and gap values are
// plain config settings (Windows.* and Gaps.*), edited through the
// ISettings-backed WindowAppearanceController. Each control reads/writes the
// matching controller property; per-window overrides remain ordinary
// higher-priority rules edited on the Rules page.
SettingsFlickable {
    id: root

    // The config-backed controller: window border / title bar + gap values and
    // the CONSTANT slider bounds.
    readonly property var ctl: settingsController.windowAppearancePage

    // "Follow the system accent" sentinel. The effect resolves it to the live
    // system accent colour at paint time, so a border colour stored as this token
    // tracks Plasma accent changes without an edit.
    readonly property string accentToken: "accent"
    // Concrete fallback colour written when the user turns the accent toggle off
    // (KDE accent blue, opaque).
    readonly property string defaultBorderHex: "#FF3DAEE9"

    // The border detail controls (width, radius, colours) are hidden while the
    // border is off so the user cannot edit values that would not apply.
    readonly property bool borderVisible: root.ctl.showWindowBorder
    // The title-bar scope row is only meaningful while title bars are hidden.
    readonly property bool hideTitleBarsOn: root.ctl.hideWindowTitleBars

    // "Apply to" scope options for the border / title-bar values, in display
    // order. `scope` is the token stored in config (sourced from the controller so
    // it stays in lockstep with the schema validator and the effect); `label` is
    // user-facing text.
    readonly property var scopeOptions: [
        {
            "scope": root.ctl.scopeTokenTiled,
            "label": i18n("Tiled and snapped windows")
        },
        {
            "scope": root.ctl.scopeTokenNormal,
            "label": i18n("All normal windows")
        },
        {
            "scope": root.ctl.scopeTokenAll,
            "label": i18n("All windows")
        }
    ]

    // Index of @p scope in scopeOptions, or -1 when it is not a listed token.
    function scopeIndex(scope) {
        for (var i = 0; i < root.scopeOptions.length; ++i) {
            if (root.scopeOptions[i].scope === scope) {
                return i;
            }
        }
        return -1;
    }

    // Always emit the full 8-digit #AARRGGBB form so the stored value matches
    // what the effect resolves (it parses #AARRGGBB / #RRGGBB / #RGB).
    function colorToHex(c) {
        function pad(v) {
            return Math.round(v * 255).toString(16).padStart(2, '0');
        }
        return ("#" + pad(c.a) + pad(c.r) + pad(c.g) + pad(c.b)).toUpperCase();
    }

    // Scope-aware gap values for the Gaps card. gapValue() reads C++ state (the
    // per-monitor config override, falling back to the global value) that QML
    // can't bind to reactively, so these are refreshed imperatively by
    // refreshGaps() from the Connections below whenever the scope, the per-
    // monitor overrides, or the global gap values change.
    property int gapInnerValue: 0
    property int gapOuterValue: 0
    property bool gapUsePerSideValue: false
    property int gapTopValue: 0
    property int gapBottomValue: 0
    property int gapLeftValue: 0
    property int gapRightValue: 0

    function refreshGaps() {
        const scope = settingsController.scopeScreenName;
        root.gapInnerValue = root.ctl.gapValue(scope, "InnerGap");
        root.gapOuterValue = root.ctl.gapValue(scope, "OuterGap");
        root.gapUsePerSideValue = root.ctl.gapValue(scope, "UsePerSideOuterGap");
        root.gapTopValue = root.ctl.gapValue(scope, "OuterGapTop");
        root.gapBottomValue = root.ctl.gapValue(scope, "OuterGapBottom");
        root.gapLeftValue = root.ctl.gapValue(scope, "OuterGapLeft");
        root.gapRightValue = root.ctl.gapValue(scope, "OuterGapRight");
    }

    Component.onCompleted: root.refreshGaps()

    // Re-read the scoped gap values when the monitor scope or its overrides
    // change (mirrors the per-screen refresh on the Tiling Algorithm page).
    Connections {
        target: settingsController
        function onScopeScreenNameChanged() {
            root.refreshGaps();
        }
        function onPerScreenOverridesChanged() {
            root.refreshGaps();
        }
    }

    // Re-read when a GLOBAL gap value changes (e.g. an "All Monitors" edit or a
    // per-page reset) so the card tracks the config even while scoped globally.
    Connections {
        target: root.ctl
        function onInnerGapChanged() {
            root.refreshGaps();
        }
        function onOuterGapChanged() {
            root.refreshGaps();
        }
        function onUsePerSideOuterGapChanged() {
            root.refreshGaps();
        }
        function onOuterGapTopChanged() {
            root.refreshGaps();
        }
        function onOuterGapBottomChanged() {
            root.refreshGaps();
        }
        function onOuterGapLeftChanged() {
            root.refreshGaps();
        }
        function onOuterGapRightChanged() {
            root.refreshGaps();
        }
    }

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // Borders Card — the master "show border" toggle plus width/radius.
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Borders")
            searchAnchor: "borders"
            showToggle: true
            toggleChecked: root.ctl.showWindowBorder
            onToggleClicked: checked => root.ctl.showWindowBorder = checked
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    visible: root.borderVisible
                    title: i18n("Apply to")
                    searchAnchor: "borderScope"
                    description: i18n("Which windows get a border")

                    WideComboBox {
                        id: borderScopeCombo

                        Accessible.name: i18n("Apply borders to")
                        textRole: "label"
                        model: root.scopeOptions
                        currentIndex: root.scopeIndex(root.ctl.borderScope)
                        onActivated: index => root.ctl.borderScope = root.scopeOptions[index].scope
                    }
                }

                SettingsSeparator {
                    visible: root.borderVisible
                }

                SettingsRow {
                    visible: root.borderVisible
                    title: i18n("Border width")
                    searchAnchor: "borderWidth"
                    description: i18n("Thickness of the colored border around windows")

                    SettingsSpinBox {
                        id: borderWidthSpin

                        from: root.ctl.borderWidthMin
                        to: root.ctl.borderWidthMax
                        onValueModified: value => root.ctl.windowBorderWidth = value
                        // Feed value through a guarded Binding so a config change
                        // keeps refreshing the control: a plain `value:` binding is
                        // destroyed by SettingsSpinBox's own edit echo after the
                        // first edit. RestoreNone + the focus gate keeps a live edit
                        // from being clobbered.
                        Binding on value {
                            value: root.ctl.windowBorderWidth
                            when: !borderWidthSpin.editing
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }

                SettingsSeparator {
                    visible: root.borderVisible
                }

                SettingsRow {
                    visible: root.borderVisible
                    title: i18n("Corner radius")
                    searchAnchor: "cornerRadius"
                    description: i18n("Roundness of the border corners (0 for square)")

                    SettingsSpinBox {
                        id: borderRadiusSpin

                        from: root.ctl.borderRadiusMin
                        to: root.ctl.borderRadiusMax
                        onValueModified: value => root.ctl.windowBorderRadius = value
                        // See borderWidthSpin: guarded Binding so a config change
                        // keeps refreshing after the first edit destroys a plain
                        // binding.
                        Binding on value {
                            value: root.ctl.windowBorderRadius
                            when: !borderRadiusSpin.editing
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }
            }
        }

        // =================================================================
        // Colors Card — active + inactive border colours, with a system
        // accent toggle that writes the "accent" sentinel.
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            visible: root.borderVisible
            headerText: i18n("Colors")
            searchAnchor: "colors"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Use system accent color")
                    searchAnchor: "useSystemAccentColor"
                    description: i18n("Follow the system color scheme for the border color")

                    SettingsSwitch {
                        id: useAccentSwitch

                        // True when the focused colour carries the accent sentinel.
                        checked: root.ctl.windowBorderColorActive === root.accentToken
                        accessibleName: i18n("Use system accent color")
                        onToggled: function (newValue) {
                            const colorValue = newValue ? root.accentToken : root.defaultBorderHex;
                            root.ctl.windowBorderColorActive = colorValue;
                            root.ctl.windowBorderColorInactive = colorValue;
                        }
                    }
                }

                SettingsSeparator {
                    visible: !useAccentSwitch.checked
                }

                SettingsRow {
                    visible: !useAccentSwitch.checked
                    title: i18n("Active border color")
                    searchAnchor: "activeBorderColor"
                    description: i18n("Border color for the focused window")

                    ColorSwatchRow {
                        color: {
                            // Map the accent sentinel to the live system highlight
                            // colour (alpha included) — the colour the focused border
                            // actually draws. A stored "accent" value would otherwise
                            // coerce to black.
                            const raw = root.ctl.windowBorderColorActive;
                            return raw === root.accentToken ? (appSettings ? appSettings.highlightColor : Kirigami.Theme.highlightColor) : raw;
                        }
                        onClicked: {
                            const raw = root.ctl.windowBorderColorActive;
                            activeBorderColorDialog.selectedColor = raw === root.accentToken ? (appSettings ? appSettings.highlightColor : root.defaultBorderHex) : raw;
                            activeBorderColorDialog.open();
                        }
                    }
                }

                SettingsSeparator {
                    visible: !useAccentSwitch.checked
                }

                SettingsRow {
                    visible: !useAccentSwitch.checked
                    title: i18n("Inactive border color")
                    searchAnchor: "inactiveBorderColor"
                    description: i18n("Border color for unfocused windows")

                    ColorSwatchRow {
                        color: {
                            // The unfocused border follows the system INACTIVE colour
                            // (alpha included), not the accent, matching what the
                            // border actually draws.
                            const raw = root.ctl.windowBorderColorInactive;
                            return raw === root.accentToken ? (appSettings ? appSettings.inactiveColor : Kirigami.Theme.highlightColor) : raw;
                        }
                        onClicked: {
                            const raw = root.ctl.windowBorderColorInactive;
                            inactiveBorderColorDialog.selectedColor = raw === root.accentToken ? (appSettings ? appSettings.inactiveColor : root.defaultBorderHex) : raw;
                            inactiveBorderColorDialog.open();
                        }
                    }
                }
            }
        }

        // =================================================================
        // Decorations Card — hide title bars.
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
                    description: i18n("Remove window title bars, restored when a window floats")

                    SettingsSwitch {
                        checked: root.ctl.hideWindowTitleBars
                        accessibleName: i18n("Hide title bars")
                        onToggled: newValue => root.ctl.hideWindowTitleBars = newValue
                    }
                }

                SettingsSeparator {
                    visible: root.hideTitleBarsOn
                }

                SettingsRow {
                    visible: root.hideTitleBarsOn
                    title: i18n("Apply to")
                    searchAnchor: "hideTitleBarsScope"
                    description: i18n("Which windows lose their title bar")

                    WideComboBox {
                        id: titleBarScopeCombo

                        Accessible.name: i18n("Hide title bars on")
                        textRole: "label"
                        model: root.scopeOptions
                        currentIndex: root.scopeIndex(root.ctl.titleBarScope)
                        onActivated: index => root.ctl.titleBarScope = root.scopeOptions[index].scope
                    }
                }
            }
        }

        // =================================================================
        // Gaps Card — the unified inner/outer gap model, config-backed. Smart
        // gaps is tiling-only and lives on the Tiling → Window page, so it is
        // hidden here.
        // =================================================================
        GapsSettingsCard {
            Layout.fillWidth: true
            searchAnchor: "gaps"
            gapMin: root.ctl.outerGapMin
            gapMax: root.ctl.outerGapMax
            primaryGapMin: root.ctl.innerGapMin
            primaryGapMax: root.ctl.innerGapMax
            primaryGapLabel: i18n("Inner gap")
            primaryGapDescription: i18n("Space between windows")
            outerGapLabel: i18n("Outer gap")
            outerGapDescription: i18n("Space from the screen edges to windows")
            showSmartGaps: false
            // Per-monitor scope chip, config-backed. "All Monitors" ("" scope)
            // edits the global gap config; a specific monitor edits its per-screen
            // override. The chip's override dot / reset poll hasPerScreenGapOverride /
            // clearPerScreenGapOverride on the controller.
            scopeEnabled: true
            scopeAppSettings: settingsController
            scopeHasOverridesMethod: "hasPerScreenGapOverride"
            scopeClearerMethod: "clearPerScreenGapOverride"
            primaryGapValue: root.gapInnerValue
            outerGapValue: root.gapOuterValue
            usePerSideOuterGap: root.gapUsePerSideValue
            outerGapTopValue: root.gapTopValue
            outerGapBottomValue: root.gapBottomValue
            outerGapLeftValue: root.gapLeftValue
            outerGapRightValue: root.gapRightValue
            onPrimaryGapModified: value => root.ctl.writeGap(settingsController.scopeScreenName, "InnerGap", value)
            onOuterGapModified: value => root.ctl.writeGap(settingsController.scopeScreenName, "OuterGap", value)
            onUsePerSideOuterGapToggled: checked => root.ctl.writeGap(settingsController.scopeScreenName, "UsePerSideOuterGap", checked)
            onOuterGapTopModified: value => root.ctl.writeGap(settingsController.scopeScreenName, "OuterGapTop", value)
            onOuterGapBottomModified: value => root.ctl.writeGap(settingsController.scopeScreenName, "OuterGapBottom", value)
            onOuterGapLeftModified: value => root.ctl.writeGap(settingsController.scopeScreenName, "OuterGapLeft", value)
            onOuterGapRightModified: value => root.ctl.writeGap(settingsController.scopeScreenName, "OuterGapRight", value)
        }
    }

    // =====================================================================
    // Color Dialogs
    // =====================================================================
    ColorDialog {
        id: activeBorderColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Active Border Color")
        onAccepted: root.ctl.windowBorderColorActive = root.colorToHex(selectedColor)
    }

    ColorDialog {
        id: inactiveBorderColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Inactive Border Color")
        onAccepted: root.ctl.windowBorderColorInactive = root.colorToHex(selectedColor)
    }
}
