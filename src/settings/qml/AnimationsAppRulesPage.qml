// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

/**
 * @brief Animations App Rules — per-window-class shader / timing override.
 *
 * Backed by `AnimationsPageController::appRules()` and friends. Each
 * rule entry persists through `Settings::animationAppRules` and is
 * resolved at animation-start time by the kwin-effect's
 * `resolveAnimationShaderProfile` / `resolveAnimationMotionProfile`
 * cascade — the rule layer wins over the per-event default for
 * matching windows; non-matching windows fall through to the
 * Animations → Windows defaults unchanged.
 *
 * Rules carry one of two payload kinds:
 *
 *   - Shader: replaces the per-event shader effect for matching
 *     windows. An empty effect id is the documented "block default
 *     for this app on this event" sentinel — no shader plays even
 *     if the per-event default is configured.
 *
 *   - Timing: overrides the curve / duration of the snap-animation
 *     motion for matching windows. Empty curve and zero duration
 *     are inherit sentinels (each axis falls through to the global
 *     animator profile independently).
 *
 * Rules are an ordered list — the first rule whose `classPattern`
 * substring-matches the window class wins on each axis. Order is
 * editable via the row's up/down buttons.
 */
SettingsFlickable {
    id: root

    /// Live model — refreshed on every `appRulesChanged` so the list
    /// rebinds after add/remove/move and after Settings::load() Discard.
    /// Init-only on its own; the Connections block below re-evaluates
    /// it on every controller signal.
    property var rulesList: settingsController.animationsPage.appRules()
    /// Window-event paths the rule list can target. Static across the
    /// page lifetime since the underlying ProfilePaths catalogue is
    /// compile-time, so a one-shot capture is correct here.
    readonly property var eventsList: settingsController.animationsPage.animationAppRuleEvents()
    /// Available shader effects from the registry, including a "None"
    /// entry the picker prepends via `includeNoneEntry: true`. Captured
    /// at component creation; refreshed via the Connections block on
    /// `shaderEffectsChanged` so dropping a new shader pack while this
    /// page is open reflects in the picker without reopening.
    property var shadersList: settingsController.animationsPage.availableShaderEffects()
    /// Rule kinds — keep in sync with `AnimationAppRule::Kind` JSON
    /// strings. Used by the kind radio.
    readonly property string kindShader: "shader"
    readonly property string kindTiming: "timing"
    /// Translation table for the C++ controller's `eventLabel()`,
    /// which produces raw English ("Open", "Close", ...) by
    /// title-casing camelCase segments. The controller stays language-
    /// neutral (its callers cross daemon / settings boundaries); the
    /// translation hook lives here so the user sees localised labels.
    /// Keys mirror `PhosphorAnimation::ProfilePaths::Window*` —
    /// keep in sync with that header so a new event path picks up a
    /// localised label instead of falling through to the raw English
    /// fallback (visible but not translated).
    ///
    /// The bare `"window"` parent path is NOT surfaced in the Add
    /// Rule combo (`animationAppRuleEvents()` filters category nodes),
    /// but it IS in `ProfilePaths::allBuiltInPaths()` and therefore
    /// passes the controller's `isValidEventPath` whitelist — a
    /// programmatic Q_INVOKABLE caller could persist a rule whose
    /// `eventPath == "window"` and the rules-list rendering path
    /// would land here. Keep the entry so that defensive case still
    /// renders a localised label.
    readonly property var _eventTranslations: ({
        "window": i18nc("window-event category, used as inheritance anchor", "Window"),
        "window.open": i18nc("window-event verb", "Open"),
        "window.close": i18nc("window-event verb", "Close"),
        "window.minimize": i18nc("window-event verb", "Minimize"),
        "window.maximize": i18nc("window-event verb", "Maximize"),
        "window.move": i18nc("window-event verb", "Move"),
        "window.resize": i18nc("window-event verb", "Resize"),
        "window.focus": i18nc("window-event verb", "Focus")
    })
    /// Gates the window-picker callback so other cards on this page
    /// (or sibling pages sharing the same SettingsController) don't
    /// react when this card opens the dialog. Mirrors the same pattern
    /// `AppRulesCard.qml` uses for snap rules.
    property bool _pickerOpenedByUs: false
    // ── Shader-rule working state ───────────────────────────────────
    /// The shader-parameter overrides the user has dialled in for the
    /// currently-picked effect. Bound through `ShaderParameterEditor`
    /// below; the schema (parameter list + types) comes from
    /// `settingsController.animationsPage.shaderParameters(effectId)`,
    /// the current values live here. Persisted into the rule's
    /// `shaderParams` map on Add. Reset on shader switch (different
    /// effects use different parameter ids).
    property var currentShaderParams: ({
    })
    /// Bumped on `shaderEffectsChanged` so any binding that reads
    /// `shaderParameters(effectId)` re-evaluates when a pack is
    /// dropped / removed mid-session. Same pattern as
    /// `AnimationEventCard::_shaderRegistryRev`.
    property int _shaderRegistryRev: 0
    // ── Timing-rule working state ───────────────────────────────────
    /// Mirrors the per-event editor's state shape so the same widgets
    /// (CurveThumbnail, CurveEditorDialog) can drive the rule form
    /// without users having to type curve specifiers by hand.
    property int currentTimingMode: CurvePresets.timingModeEasing
    property int currentDuration: CurvePresets.defaultDurationMs
    property string currentEasingCurve: CurvePresets.defaultEasingCurve
    property real currentSpringOmega: CurvePresets.defaultSpringOmega
    property real currentSpringZeta: CurvePresets.defaultSpringZeta
    /// Inheritance opt-out flags — when off, the rule writes the
    /// engaged-empty / zero sentinel for that axis so the resolver
    /// falls through to the per-event default. When on, the working
    /// state above is committed.
    property bool overrideCurve: true
    property bool overrideDuration: true
    /// Composes the curve-string wire format the rule schema expects,
    /// matching `AnimationEventCard::currentCurveString`.
    readonly property string currentCurveString: {
        if (currentTimingMode === CurvePresets.timingModeSpring)
            return "spring:" + currentSpringOmega.toFixed(2) + "," + currentSpringZeta.toFixed(2);

        return currentEasingCurve;
    }

    /// "Easing — Cubic In-Out" / "Spring — Snappy" (or "Custom").
    function _curveSummary() {
        if (currentTimingMode === CurvePresets.timingModeSpring) {
            var si = CurvePresets.springPresetIndex(currentSpringOmega, currentSpringZeta);
            if (si >= 0)
                return i18n("Spring · %1", CurvePresets.springPresets[si].label);

            return i18n("Spring · Custom");
        }
        var idx = CurvePresets.findIndices(currentEasingCurve);
        if (idx.styleIndex >= 0)
            return CurvePresets.easingStyles[idx.styleIndex].label + " · " + CurvePresets.easingDirections[idx.dirIndex].label;

        return i18n("Easing · Custom");
    }

    function _eventLabel(eventPath) {
        // Prefer the localised label; fall back to the controller's
        // raw English (or the path itself) if the path is not in our
        // translation table — that way a future ProfilePaths addition
        // shows up readable instead of breaking the row entirely.
        // Use `root.` prefixes throughout so QML's binding-dependency
        // tracker registers reads against the page's properties (bare
        // identifiers can resolve through enclosing scopes and miss
        // re-evaluation when the property changes).
        var translated = root._eventTranslations[eventPath];
        if (translated)
            return translated;

        for (var i = 0; i < root.eventsList.length; ++i) {
            if (root.eventsList[i].path === eventPath)
                return root.eventsList[i].label;

        }
        return eventPath;
    }

    function _shaderName(effectId) {
        if (!effectId)
            return i18n("(no shader)");

        for (var i = 0; i < root.shadersList.length; ++i) {
            if (root.shadersList[i].id === effectId)
                return root.shadersList[i].name;

        }
        return effectId;
    }

    function _ruleSummary(rule) {
        if (rule.kind === root.kindTiming) {
            var duration = rule.durationMs > 0 ? i18n("%1 ms", rule.durationMs) : i18n("default duration");
            // Render the curve via `CurvePresets.curveDisplayName` so
            // "0.33,1.00,0.68,1.00" surfaces as "Cubic In-Out" instead
            // of the raw spec.
            var curve = rule.curve && rule.curve.length > 0 ? CurvePresets.curveDisplayName(rule.curve) : i18n("default curve");
            return i18n("Timing: %1, %2", curve, duration);
        }
        return i18n("Shader: %1", _shaderName(rule.effectId));
    }

    function _refresh() {
        root.rulesList = settingsController.animationsPage.appRules();
    }

    contentHeight: mainCol.implicitHeight

    Connections {
        function onAppRulesChanged() {
            root._refresh();
        }

        function onShaderEffectsChanged() {
            // Refresh the shader catalogue so a newly-installed pack
            // appears in the per-rule picker without the user having
            // to leave and re-open the page. Also bump
            // `_shaderRegistryRev` so the parameter editor's schema
            // binding re-evaluates against the new effect set.
            root.shadersList = settingsController.animationsPage.availableShaderEffects();
            root._shaderRegistryRev += 1;
        }

        target: settingsController.animationsPage
    }

    WindowPickerDialog {
        id: windowPickerDialog

        // Pass `settingsController` directly — it exposes the
        // `cachedRunningWindows()` / `requestRunningWindows()` Q_INVOKABLEs
        // and the `runningWindowsAvailable` / `runningWindowsTimedOut`
        // signals the dialog binds against. Same wiring as
        // `ExclusionsPage.qml`.
        appSettings: settingsController
    }

    Connections {
        function onPicked(value) {
            patternField.text = value;
            root._pickerOpenedByUs = false;
        }

        function onClosed() {
            root._pickerOpenedByUs = false;
        }

        target: windowPickerDialog
        enabled: root._pickerOpenedByUs
    }

    ColumnLayout {
        id: mainCol

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Override the global per-event animation for specific windows. Useful when one app should dissolve while everything else uses the default open animation, or when a slow app should animate faster.")
            visible: true
        }

        SettingsCard {
            id: addCard

            Layout.fillWidth: true
            headerText: i18n("Add Rule")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // Pattern + event row.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    TextField {
                        id: patternField

                        Layout.fillWidth: true
                        placeholderText: i18n("Window class pattern (e.g., firefox, org.kde.dolphin)")
                        Accessible.name: i18n("Window class pattern")
                    }

                    ToolButton {
                        icon.name: "crosshairs"
                        ToolTip.text: i18n("Pick from running windows")
                        ToolTip.visible: hovered
                        Accessible.name: i18n("Pick window class from running windows")
                        onClicked: {
                            // Set the gate before opening so the
                            // page's `onPicked` Connections takes
                            // effect; cleared inside the picked /
                            // closed handlers.
                            root._pickerOpenedByUs = true;
                            windowPickerDialog.openForClasses();
                        }
                    }

                    Label {
                        text: i18n("Event:")
                    }

                    ComboBox {
                        id: eventCombo

                        Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                        textRole: "label"
                        valueRole: "path"
                        model: root.eventsList
                        currentIndex: 0
                        Accessible.name: i18n("Animation event for the new rule")
                    }

                }

                // Kind radio — shader vs timing.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.largeSpacing

                    Label {
                        text: i18n("Override:")
                    }

                    RadioButton {
                        id: shaderKindRadio

                        text: i18n("Shader effect")
                        checked: true
                        ToolTip.text: i18n("Replace the per-event shader effect for matching windows. Empty effect blocks the default.")
                        ToolTip.visible: hovered
                    }

                    RadioButton {
                        id: timingKindRadio

                        text: i18n("Motion timing")
                        ToolTip.text: i18n("Override the curve and duration for the matching windows' motion.")
                        ToolTip.visible: hovered
                    }

                }

                // Shader payload — visible when Shader kind selected.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing
                    visible: shaderKindRadio.checked

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Label {
                            text: i18n("Shader:")
                        }

                        PZCommon.ShaderPickerButton {
                            id: shaderPicker

                            Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                            shaders: root.shadersList || []
                            currentShaderId: ""
                            noneShaderId: ""
                            includeNoneEntry: true
                            noneText: i18nc("@item:inlistbox", "(no shader)")
                            placeholderText: i18nc("@action:button", "Pick shader…")
                            onShaderSelected: function(id) {
                                // Switching effects clears the
                                // working parameter map — same-named
                                // ids in different shader schemas are
                                // unrelated, mirroring
                                // AnimationEventCard's behaviour.
                                if (id !== shaderPicker.currentShaderId)
                                    root.currentShaderParams = ({
                                });

                                shaderPicker.currentShaderId = id;
                            }
                        }

                    }

                    // Inline shader-parameter editor — same widget the
                    // per-event card uses, with locking / randomize /
                    // image affordances disabled (rule context doesn't
                    // need them). Surfaces only when the picked effect
                    // has parameters.
                    PZCommon.ShaderParameterEditor {
                        id: shaderParamEditor

                        readonly property var _paramSchema: {
                            root._shaderRegistryRev; // dependency for reactivity
                            return shaderPicker.currentShaderId.length > 0 ? settingsController.animationsPage.shaderParameters(shaderPicker.currentShaderId) : [];
                        }

                        Layout.fillWidth: true
                        visible: shaderPicker.currentShaderId.length > 0 && _paramSchema.length > 0
                        parameters: _paramSchema
                        currentValues: root.currentShaderParams
                        enableLocking: false
                        enableRandomize: false
                        enableGroups: true
                        enableImage: false
                        compact: true
                        onValueChanged: function(paramId, value) {
                            // QML / JS map mutation: copy-then-write so
                            // the engine sees the property change and
                            // re-evaluates the editor's currentValues
                            // binding.
                            var next = ({
                            });
                            for (var k in root.currentShaderParams) next[k] = root.currentShaderParams[k]
                            next[paramId] = value;
                            root.currentShaderParams = next;
                        }
                        onRequestColorPicker: function(paramId, paramName, current) {
                            ruleColorDialog.paramId = paramId;
                            ruleColorDialog.paramName = paramName;
                            ruleColorDialog.selectedColor = current;
                            ruleColorDialog.open();
                        }
                    }

                }

                // Timing payload — visible when Timing kind selected.
                // Mirrors `AnimationEventCard`'s curve / duration UX so
                // users can pick presets via the same CurveEditorDialog
                // instead of typing curve specifiers by hand.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing
                    visible: timingKindRadio.checked

                    // Curve override — checkbox gates the editor and
                    // controls whether the saved rule sets the curve
                    // axis or leaves it for the per-event default.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.largeSpacing

                        CheckBox {
                            id: curveOverrideCheck

                            text: i18n("Override curve")
                            checked: root.overrideCurve
                            onToggled: root.overrideCurve = checked
                            ToolTip.text: i18n("When off, matching windows use the per-event default curve.")
                            ToolTip.visible: hovered
                        }

                        // Curve summary row — same widget set as the
                        // per-event editor: thumbnail preview + textual
                        // description + Customize… button. Disabled
                        // when "Override curve" is off.
                        CurveThumbnail {
                            id: ruleCurveThumbnail

                            implicitWidth: Kirigami.Units.gridUnit * 6
                            implicitHeight: Kirigami.Units.gridUnit * 4
                            curve: root.currentEasingCurve
                            timingMode: root.currentTimingMode
                            omega: root.currentSpringOmega
                            zeta: root.currentSpringZeta
                            opacity: root.overrideCurve ? 1 : 0.5
                            onClicked: {
                                if (root.overrideCurve)
                                    ruleCurveDialog.open();

                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root._curveSummary()
                            elide: Text.ElideRight
                            opacity: root.overrideCurve ? 1 : 0.5
                        }

                        Button {
                            text: i18n("Customize…")
                            icon.name: "configure"
                            enabled: root.overrideCurve
                            Accessible.name: i18n("Customize curve for the new rule")
                            onClicked: ruleCurveDialog.open()
                        }

                    }

                    // Timing-mode toggle — only meaningful when curve
                    // override is on, since it picks which working-
                    // state axis (easing vs spring) feeds
                    // currentCurveString.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.largeSpacing
                        enabled: root.overrideCurve

                        Label {
                            text: i18n("Timing mode:")
                        }

                        ComboBox {
                            id: timingModeCombo

                            Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                            model: [i18n("Easing"), i18n("Spring")]
                            currentIndex: root.currentTimingMode
                            onActivated: function(index) {
                                root.currentTimingMode = index;
                            }
                            Accessible.name: i18n("Timing mode for the rule's curve")
                        }

                    }

                    // Duration override — independent of curve override
                    // so a rule can change the duration without
                    // touching the curve and vice versa. Spring mode
                    // derives its own settle time, so the slider is
                    // hidden in that case (matches AnimationEventCard).
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.largeSpacing
                        visible: !root.overrideCurve || root.currentTimingMode === CurvePresets.timingModeEasing

                        CheckBox {
                            id: durationOverrideCheck

                            text: i18n("Override duration")
                            checked: root.overrideDuration
                            onToggled: root.overrideDuration = checked
                            ToolTip.text: i18n("When off, matching windows use the per-event default duration.")
                            ToolTip.visible: hovered
                        }

                        Slider {
                            id: durationSlider

                            Layout.fillWidth: true
                            from: 50
                            to: 2000
                            stepSize: 10
                            value: root.currentDuration
                            enabled: root.overrideDuration
                            onMoved: root.currentDuration = value
                            Accessible.name: i18n("Animation duration in milliseconds")
                        }

                        Label {
                            text: i18n("%1 ms", Math.round(root.currentDuration))
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 4
                            opacity: root.overrideDuration ? 1 : 0.5
                        }

                    }

                }

                // Add button row.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing

                    Item {
                        Layout.fillWidth: true
                    }

                    Button {
                        id: addButton

                        text: i18n("Add rule")
                        icon.name: "list-add"
                        // For shader-kind, allow empty effectId (the
                        // explicit-block sentinel) — only require a
                        // pattern. For timing-kind, require at least
                        // one of curve / duration to actually be
                        // overridden so the rule does something. The
                        // event-combo gate guards against a future
                        // empty `eventsList` (e.g. taxonomy refactor)
                        // building rules with empty `eventPath` that
                        // the controller would silently reject.
                        enabled: patternField.text.length > 0 && (eventCombo.currentValue || "").length > 0 && (shaderKindRadio.checked || root.overrideCurve || root.overrideDuration)
                        ToolTip.text: i18n("Append the rule to the list")
                        ToolTip.visible: hovered
                        onClicked: {
                            var rule = {
                                "classPattern": patternField.text,
                                "eventPath": eventCombo.currentValue || ""
                            };
                            if (shaderKindRadio.checked) {
                                rule.kind = root.kindShader;
                                rule.effectId = shaderPicker.currentShaderId || "";
                                // Snapshot the working parameter map
                                // — empty when the effect has no
                                // params or the user kept defaults.
                                rule.shaderParams = root.currentShaderParams;
                            } else {
                                rule.kind = root.kindTiming;
                                // Empty curve / zero duration are the
                                // documented inherit sentinels — when
                                // the user turned the override off,
                                // we send the sentinel so the resolver
                                // falls through to the per-event
                                // default on that axis.
                                rule.curve = root.overrideCurve ? root.currentCurveString : "";
                                rule.durationMs = root.overrideDuration ? root.currentDuration : 0;
                            }
                            if (settingsController.animationsPage.addAppRule(rule)) {
                                patternField.clear();
                                shaderPicker.currentShaderId = "";
                                root.currentShaderParams = ({
                                });
                                // Reset timing state to library defaults
                                // so consecutive adds open with a known
                                // starting point.
                                root.currentTimingMode = CurvePresets.timingModeEasing;
                                root.currentDuration = CurvePresets.defaultDurationMs;
                                root.currentEasingCurve = CurvePresets.defaultEasingCurve;
                                root.currentSpringOmega = CurvePresets.defaultSpringOmega;
                                root.currentSpringZeta = CurvePresets.defaultSpringZeta;
                                root.overrideCurve = true;
                                root.overrideDuration = true;
                                // Reset the event combo to the first
                                // entry so consecutive adds open with
                                // a known starting point instead of
                                // inheriting the previous selection.
                                eventCombo.currentIndex = 0;
                            }
                        }
                    }

                }

            }

        }

        SettingsCard {
            id: rulesCard

            Layout.fillWidth: true
            headerText: i18n("Rules")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                ListView {
                    id: rulesListView

                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(contentHeight, Kirigami.Units.gridUnit * 5)
                    Layout.minimumHeight: Kirigami.Units.gridUnit * 5
                    Layout.margins: Kirigami.Units.smallSpacing
                    clip: true
                    model: root.rulesList
                    interactive: false
                    spacing: 0

                    Kirigami.PlaceholderMessage {
                        anchors.centerIn: parent
                        width: parent.width - Kirigami.Units.gridUnit * 4
                        visible: rulesListView.count === 0
                        text: i18n("No rules configured")
                        explanation: i18n("Add a rule above to override the default animation for a specific window class.")
                    }

                    delegate: ItemDelegate {
                        id: ruleDelegate

                        required property var modelData
                        required property int index

                        width: ListView.view.width
                        // Pure presentation — no hover/press feedback (no
                        // click handler) so the delegate doesn't pretend
                        // to be interactive when only its child buttons
                        // are.
                        hoverEnabled: false

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                source: "application-x-executable"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Label {
                                text: ruleDelegate.modelData.classPattern
                                font.bold: true
                                Layout.alignment: Qt.AlignVCenter
                                elide: Text.ElideRight
                                // Anchor cap to the delegate width
                                // (explicit `id`) rather than `parent`
                                // so a future restructure of the inner
                                // RowLayout doesn't silently flip the
                                // anchor target.
                                Layout.maximumWidth: Math.min(implicitWidth, ruleDelegate.width * 0.25)
                            }

                            Label {
                                text: i18nc("event-path label inline with rule pattern", "on %1", root._eventLabel(ruleDelegate.modelData.eventPath))
                                opacity: 0.8
                                Layout.alignment: Qt.AlignVCenter
                                elide: Text.ElideRight
                                Layout.maximumWidth: Math.min(implicitWidth, ruleDelegate.width * 0.25)
                            }

                            Label {
                                text: root._ruleSummary(ruleDelegate.modelData)
                                opacity: 0.7
                                Layout.alignment: Qt.AlignVCenter
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            ToolButton {
                                icon.name: "go-up"
                                enabled: ruleDelegate.index > 0
                                ToolTip.text: i18n("Move up")
                                ToolTip.visible: hovered
                                Accessible.name: i18n("Move rule for %1 up", ruleDelegate.modelData.classPattern)
                                onClicked: settingsController.animationsPage.moveAppRule(ruleDelegate.index, ruleDelegate.index - 1)
                            }

                            ToolButton {
                                icon.name: "go-down"
                                enabled: ruleDelegate.index < rulesListView.count - 1
                                ToolTip.text: i18n("Move down")
                                ToolTip.visible: hovered
                                Accessible.name: i18n("Move rule for %1 down", ruleDelegate.modelData.classPattern)
                                onClicked: settingsController.animationsPage.moveAppRule(ruleDelegate.index, ruleDelegate.index + 1)
                            }

                            ToolButton {
                                icon.name: "edit-delete"
                                ToolTip.text: i18n("Remove rule")
                                ToolTip.visible: hovered
                                Accessible.name: i18n("Remove rule for %1", ruleDelegate.modelData.classPattern)
                                onClicked: settingsController.animationsPage.removeAppRule(ruleDelegate.index)
                            }

                        }

                    }

                }

                Label {
                    text: i18n("Rules are checked top-to-bottom. The first matching rule wins per axis (shader and timing are resolved independently). Non-matched windows fall through to the per-event defaults under Animations → Windows.")
                    font.italic: true
                    opacity: 0.7
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                }

            }

        }

    }

    // Pop the curve editor as a window-level dialog so it doesn't get
    // clipped by the host SettingsFlickable. Same parent fallback the
    // per-event card uses (`AnimationEventCard.qml`): real Window when
    // available, the page itself otherwise during early init.
    CurveEditorDialog {
        id: ruleCurveDialog

        parent: root.Window.window ? root.Window.window.contentItem : root
        eventLabel: i18nc("placeholder used in the curve dialog title for app-rule curves", "this rule")
        timingMode: root.currentTimingMode
        easingCurve: root.currentEasingCurve
        springOmega: root.currentSpringOmega
        springZeta: root.currentSpringZeta
        onCurveApplied: function(curve) {
            root.currentEasingCurve = curve;
            root.currentTimingMode = CurvePresets.timingModeEasing;
        }
        onSpringApplied: function(omega, zeta) {
            root.currentSpringOmega = omega;
            root.currentSpringZeta = zeta;
            root.currentTimingMode = CurvePresets.timingModeSpring;
        }
    }

    // Native color picker for `ShaderParameterEditor`'s color-typed
    // parameter rows. Same wiring as `AnimationEventCard.qml` —
    // ColorDialog runs in its own platform window so no `parent`
    // assignment is needed (and none is accepted).
    ColorDialog {
        id: ruleColorDialog

        property string paramId: ""
        property string paramName: ""

        title: paramName.length > 0 ? i18nc("@title:window", "Choose %1", paramName) : i18nc("@title:window", "Pick color")
        onAccepted: {
            if (ruleColorDialog.paramId === "")
                return ;

            var next = ({
            });
            for (var k in root.currentShaderParams) next[k] = root.currentShaderParams[k]
            next[ruleColorDialog.paramId] = selectedColor.toString();
            root.currentShaderParams = next;
        }
    }

}
