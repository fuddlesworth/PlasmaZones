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

    // The border detail controls (width, radius, colours) are hidden while the
    // border is off so the user cannot edit values that would not apply.
    readonly property bool borderVisible: root.ctl.showWindowBorder
    // Same hide-while-off treatment for the opacity+tint layer's rows.
    readonly property bool opacityTintVisible: root.ctl.showWindowOpacityTint
    // The title-bar scope row is only meaningful while title bars are hidden.
    readonly property bool hideTitleBarsOn: root.ctl.hideWindowTitleBars

    // Simple/advanced split on this page: simple mode keeps the everyday
    // decoration controls (border on/off + width/radius/colour, hide title
    // bars, gaps); the power surfaces (the per-window "Apply to" scope pickers
    // for border, title bar and opacity+tint, focus-fade timing, window
    // filtering, the performance card) declare `advancedOnly: true` on their
    // card or row. Note the opacity+tint card itself stays in simple mode; only
    // its scope picker and the separator beside it are advanced.

    // "Apply to" scope options for the border / title-bar / opacity values, in
    // the order the schema declares them. The three keys share one token set,
    // so one lookup serves all three pickers.
    //
    // Sourced from the schema rather than written out here: the tokens are the
    // ones the validator accepts and the words come from the shared label
    // table, so a picker cannot offer a scope the effect would reject.
    readonly property var scopeOptions: settingsController.valueOptions("Windows", "BorderScope")

    // Index of @p scope in scopeOptions, or -1 when it is not a listed token.
    function scopeIndex(scope) {
        for (var i = 0; i < root.scopeOptions.length; ++i) {
            if (root.scopeOptions[i].value === scope) {
                return i;
            }
        }
        return -1;
    }

    // The tint colour is always stored opaque. The opacity-tint shader ignores
    // the colour's own alpha and uses the tint strength slider as the sole
    // control, so storing a translucent colour would silently discard
    // information the user thought they set. Scaling the wash by both was the
    // double-apply the shader was changed to avoid, so do not reintroduce it
    // here by storing alpha. Takes the row's 8-digit #AARRGGBB string.
    function hexToOpaqueHex(hex) {
        return "#FF" + hex.slice(3);
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
                    advancedOnly: true
                    enabled: root.borderVisible
                    title: i18n("Apply to")
                    searchAnchor: "borderScope"
                    description: i18n("Which windows get a border")

                    WideComboBox {
                        Accessible.name: i18n("Apply borders to")
                        textRole: "text"
                        model: root.scopeOptions
                        currentIndex: root.scopeIndex(root.ctl.borderScope)
                        onActivated: index => root.ctl.borderScope = root.scopeOptions[index].value
                    }
                }

                SettingsSeparator {
                    advancedOnly: true
                    enabled: root.borderVisible
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
                // card. Theme-fallback keys with the standard EMPTY sentinel;
                // the DAEMON resolves the sentinel before the value crosses
                // D-Bus, so the effect only ever sees concrete colours. (The
                // rules vocabulary still spells its sentinel "accent", because
                // a rule param's empty slot already means "unset".)
                ThemeFallbackColorRow {
                    visible: root.borderVisible
                    title: i18n("Active border color")
                    // Overridden because the title already says "color".
                    swatchAccessibleName: i18nc("@action:button", "Active border color")
                    searchAnchor: "activeBorderColor"
                    description: i18n("Border color for the focused window. Follows the color scheme unless you pick one.")

                    storedColor: root.ctl.windowBorderColorActive
                    // The colour the focused border actually draws while it
                    // follows the scheme: the live zone highlight, alpha
                    // included — which is the scheme accent unless the user
                    // pinned the zone colour. The "Color scheme" fallback
                    // label stays deliberately loose; the swatch preview is
                    // the truthful part.
                    themeColor: appSettings.highlightColor
                    picker: borderColorDialog
                    onColorChosen: function (hex) {
                        root.ctl.windowBorderColorActive = hex;
                    }
                }

                SettingsSeparator {
                    visible: root.borderVisible
                }

                ThemeFallbackColorRow {
                    visible: root.borderVisible
                    title: i18n("Inactive border color")
                    // See the active row above.
                    swatchAccessibleName: i18nc("@action:button", "Inactive border color")
                    searchAnchor: "inactiveBorderColor"
                    description: i18n("Border color for unfocused windows. Follows the color scheme unless you pick one.")

                    storedColor: root.ctl.windowBorderColorInactive
                    // The unfocused border follows the system INACTIVE colour
                    // (alpha included), not the accent, matching what the
                    // border actually draws.
                    themeColor: appSettings.inactiveColor
                    picker: borderColorDialog
                    onColorChosen: function (hex) {
                        root.ctl.windowBorderColorInactive = hex;
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
                    // Same treatment as the border / title-bar scope rows:
                    // the per-window "Apply to" picker is advanced depth.
                    advancedOnly: true
                    enabled: root.opacityTintVisible
                    title: i18n("Apply to")
                    searchAnchor: "opacityTintScope"
                    description: i18n("Which windows are faded and tinted")

                    WideComboBox {
                        Accessible.name: i18n("Apply opacity and tint to")
                        textRole: "text"
                        model: root.scopeOptions
                        currentIndex: root.scopeIndex(root.ctl.opacityTintScope)
                        onActivated: index => root.ctl.opacityTintScope = root.scopeOptions[index].value
                    }
                }

                SettingsSeparator {
                    // Pairs with the advanced "Apply to" row above: in simple
                    // mode the card leads straight with the Opacity slider.
                    advancedOnly: true
                    enabled: root.opacityTintVisible
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

                ThemeFallbackColorRow {
                    id: tintColorRow

                    visible: root.opacityTintVisible
                    title: i18n("Tint color")
                    // Overridden because the title already says "color".
                    swatchAccessibleName: i18nc("@action:button", "Tint color")
                    searchAnchor: "tintColor"
                    description: i18n("Color the window is washed with when the tint strength is above zero. Follows the color scheme unless you pick one.")

                    storedColor: root.ctl.windowTintColor
                    // Preview the live highlight the tint follows — with its
                    // alpha stripped, because the tint contract on this page
                    // is "stored opaque, strength is the sole alpha" and the
                    // shader ignores the colour's own alpha. Showing the
                    // highlight's zone alpha would make the swatch jump from
                    // half-transparent to solid the moment a colour is picked.
                    themeColor: Qt.rgba(appSettings.highlightColor.r, appSettings.highlightColor.g, appSettings.highlightColor.b, 1)
                    picker: tintColorDialog
                    onColorChosen: function (hex) {
                        // Stored opaque unless it is the row's own sentinel;
                        // the tint strength slider is the sole alpha (see
                        // hexToOpaqueHex).
                        root.ctl.windowTintColor = hex === tintColorRow.sentinel ? hex : root.hexToOpaqueHex(hex);
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
                    advancedOnly: true
                    enabled: root.hideTitleBarsOn
                }

                SettingsRow {
                    advancedOnly: true
                    enabled: root.hideTitleBarsOn
                    title: i18n("Apply to")
                    searchAnchor: "hideTitleBarsScope"
                    description: i18n("Which windows lose their title bar")

                    WideComboBox {
                        Accessible.name: i18n("Hide title bars on")
                        textRole: "text"
                        model: root.scopeOptions
                        currentIndex: root.scopeIndex(root.ctl.titleBarScope)
                        onActivated: index => root.ctl.titleBarScope = root.scopeOptions[index].value
                    }
                }

                SettingsSeparator {
                    advancedOnly: true
                }

                SettingsRow {
                    advancedOnly: true
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
            advancedOnly: true

            excludeTransient: appSettings.decorationExcludeTransientWindows
            transientDescription: i18n("Skip borders for dialogs, popups, and menus")
            transientAccessibleName: i18n("Exclude transient windows from decorations")
            onExcludeTransientToggled: value => {
                appSettings.decorationExcludeTransientWindows = value;
            }

            // Decorations-only extra row: the Plasma panel opt-in. Presented
            // positively ("Decorate Plasma panels") against a stored exclusion
            // key, so the switch reads the way a user thinks about it while the
            // config key stays consistent with the rest of this filtering
            // group. Supplies its own leading separator so it composes under
            // the transient row, matching the animations host.
            insertAfterTransient: Component {
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Decorate Plasma panels")
                        searchAnchor: "decorateShellPanels"
                        description: i18n("Draw the panel decoration set on the Plasma panels. Pick its packs under Decoration → Shell.")

                        SettingsSwitch {
                            checked: !appSettings.decorationExcludeShellPanels
                            accessibleName: i18n("Decorate Plasma panels")
                            onToggled: function (newValue) {
                                appSettings.decorationExcludeShellPanels = !newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Decorate applet popups")
                        searchAnchor: "decorateShellAppletPopups"
                        description: i18n("Draw the applet popup decoration set on the launcher and the system tray popups. Pick its packs under Decoration → Shell.")

                        SettingsSwitch {
                            checked: !appSettings.decorationExcludeShellAppletPopups
                            accessibleName: i18n("Decorate applet popups")
                            onToggled: function (newValue) {
                                appSettings.decorationExcludeShellAppletPopups = !newValue;
                            }
                        }
                    }
                }
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
            advancedOnly: true
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
    // Color Dialogs — page-level and shared, like the scrolling pages: a page
    // rebuild while a row-scoped dialog is open would tear the popup down
    // under the user. The rows connect transiently and write on accept, so no
    // onAccepted lives here.
    // =====================================================================
    ColorDialog {
        id: borderColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Border Color")
    }

    ColorDialog {
        id: tintColorDialog

        // No alpha channel here. Tint strength already controls how strongly
        // the wash lands, and the shader ignores the colour's own alpha.
        title: i18n("Choose Tint Color")
    }

    // Publish the open state so Ctrl+PgUp/PgDown page-stepping cannot swap
    // the page out from under an open page-level dialog — the exact teardown
    // hosting the dialogs at page level exists to prevent. Same pattern
    // (including the standalone-host guard) as RulesPage.
    readonly property bool anyModalOpen: borderColorDialog.visible || tintColorDialog.visible
    onAnyModalOpenChanged: {
        if (typeof window !== "undefined" && window && window._pageOwnedModalOpen !== undefined)
            window._pageOwnedModalOpen = anyModalOpen;
    }
    // Clear a latched true on page swap (RulesPage's own teardown pattern):
    // _pageOwnedModalOpen is a single global flag, and a page destroyed with
    // its dialog up would otherwise leave nav shortcuts dead for the session.
    Component.onDestruction: {
        if (typeof window !== "undefined" && window && window._pageOwnedModalOpen !== undefined)
            window._pageOwnedModalOpen = false;
    }
}
