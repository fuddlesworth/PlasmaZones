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
    // tracks Plasma accent changes without an edit. Sourced from the controller so
    // it stays in lockstep with the config layer, the schema validator, and the
    // effect.
    readonly property string accentToken: root.ctl.accentColorToken
    // Concrete fallback colour written when the user turns the accent toggle off
    // (KDE accent blue, opaque). Also controller-sourced for the same reason.
    readonly property string defaultBorderHex: root.ctl.defaultBorderColorHex

    // The border detail controls (width, radius, colours) are hidden while the
    // border is off so the user cannot edit values that would not apply.
    readonly property bool borderVisible: root.ctl.showWindowBorder
    // Same hide-while-off treatment for the opacity+tint layer's rows.
    readonly property bool opacityTintVisible: root.ctl.showWindowOpacityTint
    // The title-bar scope row is only meaningful while title bars are hidden.
    readonly property bool hideTitleBarsOn: root.ctl.hideWindowTitleBars

    // Simple/advanced gate. In simple mode this page keeps the everyday
    // decoration controls (border on/off + width/radius/colour, hide title
    // bars, gaps) and hides the power surfaces: the per-window "Apply to"
    // scope pickers, focus-fade timing, opacity/tint, window filtering, and
    // the performance card. Advanced mode shows everything.
    readonly property bool advancedMode: settingsController.advancedMode

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

                // Easy/custom mode split: any decoration shader pack on a
                // window replaces this plain border outright (see the effect's
                // updateWindowDecoration), so say so where the border is edited.
                Label {
                    Layout.fillWidth: true
                    visible: root.borderVisible
                    text: i18n("Windows that use custom decoration shaders show those instead of this border.")
                    font.italic: true
                    color: Kirigami.Theme.disabledTextColor
                    wrapMode: Text.WordWrap
                }

                SettingsRow {
                    visible: root.borderVisible && root.advancedMode
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
                    visible: root.borderVisible && root.advancedMode
                }

                SettingsRow {
                    visible: root.borderVisible
                    title: i18n("Border width")
                    searchAnchor: "borderWidth"
                    description: i18n("Thickness of the colored border around windows")

                    SettingsSpinBox {
                        id: borderWidthSpin

                        accessibleName: i18n("Border width")
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

                        accessibleName: i18n("Corner radius")
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

                SettingsSeparator {
                    visible: root.borderVisible
                }

                // ── Border colours — a border concern, so they live in this
                // card: system accent toggle (writes the "accent" sentinel)
                // with explicit active/inactive pickers when it is off.
                SettingsRow {
                    visible: root.borderVisible
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
                    visible: root.borderVisible && !useAccentSwitch.checked
                }

                SettingsRow {
                    visible: root.borderVisible && !useAccentSwitch.checked
                    title: i18n("Active border color")
                    searchAnchor: "activeBorderColor"
                    description: i18n("Border color for the focused window")

                    ColorSwatchRow {
                        accessibleName: i18n("Active border color")
                        color: {
                            // Map the accent sentinel to the live system highlight
                            // colour (alpha included) — the colour the focused border
                            // actually draws. A stored "accent" value would otherwise
                            // coerce to black.
                            const raw = root.ctl.windowBorderColorActive;
                            return raw === root.accentToken ? appSettings.highlightColor : raw;
                        }
                        onClicked: {
                            const raw = root.ctl.windowBorderColorActive;
                            activeBorderColorDialog.selectedColor = raw === root.accentToken ? appSettings.highlightColor : raw;
                            activeBorderColorDialog.open();
                        }
                    }
                }

                SettingsSeparator {
                    visible: root.borderVisible && !useAccentSwitch.checked
                }

                SettingsRow {
                    visible: root.borderVisible && !useAccentSwitch.checked
                    title: i18n("Inactive border color")
                    searchAnchor: "inactiveBorderColor"
                    description: i18n("Border color for unfocused windows")

                    ColorSwatchRow {
                        accessibleName: i18n("Inactive border color")
                        color: {
                            // The unfocused border follows the system INACTIVE colour
                            // (alpha included), not the accent, matching what the
                            // border actually draws.
                            const raw = root.ctl.windowBorderColorInactive;
                            return raw === root.accentToken ? appSettings.inactiveColor : raw;
                        }
                        onClicked: {
                            const raw = root.ctl.windowBorderColorInactive;
                            inactiveBorderColorDialog.selectedColor = raw === root.accentToken ? appSettings.inactiveColor : raw;
                            inactiveBorderColorDialog.open();
                        }
                    }
                }
            }
        }

        // =================================================================
        // Opacity Card — the plain opacity+tint layer: fades matched windows
        // and can wash them with a colour, rendered by the reserved
        // opacity-tint decoration shader. Custom decoration shaders replace
        // it wholesale, mirroring the Borders card above.
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Opacity and tint")
            searchAnchor: "opacityTint"
            // Advanced-only: a niche visual layer most users leave off.
            visible: root.advancedMode
            showToggle: true
            toggleChecked: root.ctl.showWindowOpacityTint
            onToggleClicked: checked => root.ctl.showWindowOpacityTint = checked
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    visible: root.opacityTintVisible
                    text: i18n("Windows that use custom decoration shaders show those instead of this opacity and tint.")
                    font.italic: true
                    color: Kirigami.Theme.disabledTextColor
                    wrapMode: Text.WordWrap
                }

                SettingsRow {
                    visible: root.opacityTintVisible
                    title: i18n("Apply to")
                    searchAnchor: "opacityTintScope"
                    description: i18n("Which windows are faded and tinted")

                    WideComboBox {
                        id: opacityTintScopeCombo

                        Accessible.name: i18n("Apply opacity and tint to")
                        textRole: "label"
                        model: root.scopeOptions
                        currentIndex: root.scopeIndex(root.ctl.opacityTintScope)
                        onActivated: index => root.ctl.opacityTintScope = root.scopeOptions[index].scope
                    }
                }

                SettingsSeparator {
                    visible: root.opacityTintVisible
                }

                SettingsRow {
                    visible: root.opacityTintVisible
                    title: i18n("Opacity")
                    searchAnchor: "windowOpacity"
                    description: i18n("How visible matched windows stay, where 100% is fully opaque")

                    SettingsSlider {
                        accessibleName: i18n("Opacity")
                        from: Math.round(root.ctl.windowOpacityMin * 100)
                        to: Math.round(root.ctl.windowOpacityMax * 100)
                        value: Math.round(root.ctl.windowOpacity * 100)
                        onMoved: value => root.ctl.windowOpacity = value / 100
                    }
                }

                SettingsSeparator {
                    visible: root.opacityTintVisible
                }

                SettingsRow {
                    visible: root.opacityTintVisible
                    title: i18n("Tint strength")
                    searchAnchor: "tintStrength"
                    description: i18n("How strongly the tint color blends over the window, where 0% keeps it untinted")

                    SettingsSlider {
                        accessibleName: i18n("Tint strength")
                        from: Math.round(root.ctl.windowTintStrengthMin * 100)
                        to: Math.round(root.ctl.windowTintStrengthMax * 100)
                        value: Math.round(root.ctl.windowTintStrength * 100)
                        onMoved: value => root.ctl.windowTintStrength = value / 100
                    }
                }

                SettingsSeparator {
                    visible: root.opacityTintVisible
                }

                SettingsRow {
                    visible: root.opacityTintVisible
                    title: i18n("Use system accent color")
                    searchAnchor: "useSystemAccentTint"
                    description: i18n("Follow the system color scheme for the tint color")

                    SettingsSwitch {
                        id: useAccentTintSwitch

                        checked: root.ctl.windowTintColor === root.accentToken
                        accessibleName: i18n("Use system accent color for the tint")
                        onToggled: function (newValue) {
                            // The tint colour's config default IS the border
                            // default (ConfigDefaults::windowTintColor returns
                            // windowBorderColorActive), so toggling accent off
                            // restores the same fallback the border swatches use.
                            root.ctl.windowTintColor = newValue ? root.accentToken : root.defaultBorderHex;
                        }
                    }
                }

                SettingsSeparator {
                    visible: root.opacityTintVisible && !useAccentTintSwitch.checked
                }

                SettingsRow {
                    visible: root.opacityTintVisible && !useAccentTintSwitch.checked
                    title: i18n("Tint color")
                    searchAnchor: "tintColor"
                    description: i18n("Color the window is washed with when the tint strength is above zero")

                    ColorSwatchRow {
                        accessibleName: i18n("Tint color")
                        color: {
                            // Same accent-sentinel mapping as the border swatches:
                            // preview the live highlight instead of coercing the
                            // token to black.
                            const raw = root.ctl.windowTintColor;
                            return raw === root.accentToken ? appSettings.highlightColor : raw;
                        }
                        onClicked: {
                            const raw = root.ctl.windowTintColor;
                            tintColorDialog.selectedColor = raw === root.accentToken ? appSettings.highlightColor : raw;
                            tintColorDialog.open();
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
                    visible: root.hideTitleBarsOn && root.advancedMode
                }

                SettingsRow {
                    visible: root.hideTitleBarsOn && root.advancedMode
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

                SettingsSeparator {
                    visible: root.advancedMode
                }

                SettingsRow {
                    visible: root.advancedMode
                    title: i18n("Focus fade duration")
                    searchAnchor: "focusFadeDuration"
                    description: i18n("How long decorations take to fade between focused and unfocused. Zero switches instantly.")

                    SettingsSlider {
                        accessibleName: i18n("Focus fade duration")
                        from: root.ctl.focusFadeDurationMin
                        to: root.ctl.focusFadeDurationMax
                        stepSize: 10
                        value: root.ctl.focusFadeDuration
                        valueSuffix: " ms"
                        labelWidth: Kirigami.Units.gridUnit * 4
                        onMoved: value => {
                            root.ctl.focusFadeDuration = Math.round(value);
                        }
                    }
                }
            }
        }

        // =================================================================
        // Window Filtering Card — which windows get a border at all. Shared
        // component (also on Snapping → General and Animations), bound here to
        // the independent Decorations.WindowFiltering group. Unlike the snapping
        // filter, the transient toggle is real: turning it off draws borders
        // onto dialogs / popups. Defaults preserve prior behavior (transients
        // skipped, no size threshold).
        // =================================================================
        WindowFilterCard {
            Layout.fillWidth: true
            // Advanced-only: which windows get decorated is a power filter.
            visible: root.advancedMode

            excludeTransient: appSettings.decorationExcludeTransientWindows
            transientDescription: i18n("Skip borders for dialogs, popups, and menus")
            transientAccessibleName: i18n("Exclude transient windows from decorations")
            onExcludeTransientToggled: value => {
                appSettings.decorationExcludeTransientWindows = value;
            }

            // Spin-box bounds come from generalPage (the shared schema-bounds
            // controller that also serves the animation filter card), not
            // root.ctl — the same cross-controller sourcing AnimationsGeneralPage
            // uses for its filter bounds.
            minWidth: appSettings.decorationMinimumWindowWidth
            minWidthFrom: settingsController.generalPage.decorationMinimumWindowWidthMin
            minWidthTo: settingsController.generalPage.decorationMinimumWindowWidthMax
            minWidthDescription: i18n("Windows narrower than this get no border")
            minWidthDisabledDescription: i18n("Disabled (no width threshold)")
            minWidthAccessibleName: i18n("Minimum window width for decorations")
            onMinWidthModified: value => {
                appSettings.decorationMinimumWindowWidth = value;
            }

            minHeight: appSettings.decorationMinimumWindowHeight
            minHeightFrom: settingsController.generalPage.decorationMinimumWindowHeightMin
            minHeightTo: settingsController.generalPage.decorationMinimumWindowHeightMax
            minHeightDescription: i18n("Windows shorter than this get no border")
            minHeightDisabledDescription: i18n("Disabled (no height threshold)")
            minHeightAccessibleName: i18n("Minimum window height for decorations")
            onMinHeightModified: value => {
                appSettings.decorationMinimumWindowHeight = value;
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

        // =====================================================================
        // PERFORMANCE CARD
        // =====================================================================
        // An animated decoration pack redraws every window wearing it on every
        // frame, and that alone keeps the graphics card in its highest power
        // state for as long as the packs are on screen. What costs is not how
        // much each frame draws, it is that there is a frame to draw at all —
        // so these bound WHEN decorations animate rather than how much they do.
        // Lives here, next to the decoration settings themselves, because this is
        // where someone looks when their fans spin up after engaging a pack.
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Performance")
            searchAnchor: "decorationPerformance"
            // Advanced-only: decoration animation power tuning.
            visible: root.advancedMode
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Animate only the active window")
                    searchAnchor: "decorationAnimateFocusedOnly"
                    description: i18n("Other windows keep their decoration but stop moving. Saves graphics card use roughly in proportion to how many windows you have open.")

                    SettingsSwitch {
                        checked: appSettings.decorationAnimateFocusedOnly
                        accessibleName: i18n("Animate only the active window")
                        // SettingsSwitch does NOT flip its own `checked` — it emits
                        // toggled(newValue) and leaves `checked` bound to the source.
                        // Writing `= checked` here would write back the value we
                        // already have, so the switch would never change anything.
                        onToggled: newValue => appSettings.decorationAnimateFocusedOnly = newValue
                    }
                }

                SettingsRow {
                    title: i18n("Pause while you are away")
                    searchAnchor: "decorationPauseWhenIdle"
                    description: i18n("Stop animating decorations once you have been idle, and start again on the first key press or mouse movement.")

                    SettingsSwitch {
                        checked: appSettings.decorationPauseWhenIdle
                        accessibleName: i18n("Pause while you are away")
                        onToggled: newValue => appSettings.decorationPauseWhenIdle = newValue
                    }
                }

                SettingsRow {
                    title: i18n("Idle after")
                    searchAnchor: "decorationIdleTimeout"
                    description: i18n("How long to wait with no input before decorations stop animating.")
                    enabled: appSettings.decorationPauseWhenIdle

                    SettingsSlider {
                        accessibleName: i18n("Idle after")
                        from: root.ctl.decorationIdleTimeoutSecMin
                        to: root.ctl.decorationIdleTimeoutSecMax
                        // The range runs to an hour, so a 1s step would put the
                        // useful half-minute band inside a few pixels of track.
                        stepSize: 5
                        value: appSettings.decorationIdleTimeoutSec
                        valueSuffix: " s"
                        labelWidth: Kirigami.Units.gridUnit * 5
                        onMoved: value => {
                            appSettings.decorationIdleTimeoutSec = Math.round(value);
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

    ColorDialog {
        id: tintColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Tint Color")
        onAccepted: root.ctl.windowTintColor = root.colorToHex(selectedColor)
    }
}
