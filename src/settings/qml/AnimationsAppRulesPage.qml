// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

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
 *
 * The rich editor body (curve picker, timing-mode combo, duration
 * slider, shader picker, parameter editor, color dialog) is shared
 * with `AnimationEventCard.qml` via `AnimationProfileEditor.qml` —
 * widgets, working state, and dialogs all live there. This page
 * only carries the rule-list scaffolding (pattern + event combo +
 * window picker + Add button + rules ListView).
 */
SettingsFlickable {
    id: root

    /// Live model — refreshed on every `appRulesChanged` so the list
    /// rebinds after add/remove/move and after Settings::load() Discard.
    property var rulesList: settingsController.animationsPage.appRules()
    /// Window-event paths the rule list can target. Static across the
    /// page lifetime since the underlying ProfilePaths catalogue is
    /// compile-time, so a one-shot capture is correct here.
    readonly property var eventsList: settingsController.animationsPage.animationAppRuleEvents()
    /// Available shader effects from the registry. Init-only on its
    /// own; the Connections block below re-evaluates it on
    /// `shaderEffectsChanged`.
    property var shadersList: settingsController.animationsPage.availableShaderEffects()
    /// Rule kinds — keep in sync with `AnimationAppRule::Kind` JSON
    /// strings.
    readonly property string kindShader: "shader"
    readonly property string kindTiming: "timing"
    /// Translation table for window event paths. Defensive `"window"`
    /// entry covers programmatic Q_INVOKABLE callers persisting a
    /// rule with `eventPath == "window"` (the parent path passes the
    /// controller's whitelist even though the Add Rule combo filters
    /// category nodes out).
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
    /// react when this card opens the dialog.
    property bool _pickerOpenedByUs: false

    function _eventLabel(eventPath) {
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
            // "0.33,1.00,0.68,1.00" surfaces as "Cubic In-Out".
            var curve = rule.curve && rule.curve.length > 0 ? CurvePresets.curveDisplayName(rule.curve) : i18n("default curve");
            return i18n("Timing: %1, %2", curve, duration);
        }
        return i18n("Shader: %1", _shaderName(rule.effectId));
    }

    function _refresh() {
        root.rulesList = settingsController.animationsPage.appRules();
    }

    /// Reset the editor's working state to library defaults so
    /// consecutive adds open with a known starting point.
    function _resetEditor() {
        profileEditor.timingMode = CurvePresets.timingModeEasing;
        profileEditor.duration = CurvePresets.defaultDurationMs;
        profileEditor.easingCurve = CurvePresets.defaultEasingCurve;
        profileEditor.springOmega = CurvePresets.defaultSpringOmega;
        profileEditor.springZeta = CurvePresets.defaultSpringZeta;
        profileEditor.shaderEffectId = "";
        profileEditor.shaderParams = ({
        });
        profileEditor.lockedShaderParams = ({
        });
        profileEditor.overrideCurve = true;
        profileEditor.overrideDuration = true;
    }

    contentHeight: mainCol.implicitHeight

    Connections {
        function onAppRulesChanged() {
            root._refresh();
        }

        function onShaderEffectsChanged() {
            root.shadersList = settingsController.animationsPage.availableShaderEffects();
            // Bump the editor's registry tick so its
            // `shaderParameters(effectId)` schema binding re-evaluates.
            profileEditor.registryRevision += 1;
        }

        target: settingsController.animationsPage
    }

    WindowPickerDialog {
        id: windowPickerDialog

        // Same wiring as `ExclusionsPage.qml` — the controller exposes
        // `cachedRunningWindows()` / `requestRunningWindows()` and the
        // signals the dialog binds against.
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

                // Pattern + window-picker + event row.
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

                // Shared editor — drives both the shader-only kind
                // (timing section hidden) and the timing-only kind
                // (shader section hidden). The kind radio picks which
                // axis the saved rule commits.
                AnimationProfileEditor {
                    id: profileEditor

                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    // Section visibility tracks the kind radio. The
                    // editor's `valueChanged` is intentionally NOT
                    // connected — the rule page batches state into
                    // the Add button click rather than committing
                    // live like the per-event card.
                    showShaderSection: shaderKindRadio.checked
                    showTimingSection: timingKindRadio.checked
                    showOverrideCheckboxes: true
                    // Same lock + randomize toolbar the per-event
                    // card surfaces — the rule editor mirrors every
                    // affordance available on the per-event editor.
                    // Lock state isn't persisted (it's a UI hint for
                    // the randomize roll); it's reset on shader
                    // switch and on successful Add.
                    enableLocking: true
                    enableRandomize: true
                    eventLabel: i18nc("placeholder used in the curve dialog title for app-rule curves", "this rule")
                    availableShaders: root.shadersList || []
                    // Param-write signals fold mutations back into the
                    // editor's own `shaderParams` map. The per-event
                    // card persists immediately; rules just update
                    // local state.
                    onShaderEffectActivated: function(id) {
                        if (id !== profileEditor.shaderEffectId) {
                            // Switching effects clears the working
                            // parameter map AND the lock map —
                            // same-named ids in different shader
                            // schemas are unrelated, so carrying
                            // either across an effect switch would
                            // alias unrelated params.
                            profileEditor.shaderParams = ({
                            });
                            profileEditor.lockedShaderParams = ({
                            });
                        }
                        profileEditor.shaderEffectId = id;
                    }
                    onLockToggleRequested: function(paramId, locked) {
                        profileEditor.lockedShaderParams = profileEditor.lockedAfterToggle(paramId, locked);
                    }
                    onLockAllToggleRequested: function(locked) {
                        profileEditor.lockedShaderParams = profileEditor.lockedAfterAllToggle(locked);
                    }
                    onRandomizeRequested: {
                        // The roll honours the lock map — locked
                        // params are kept at their current values,
                        // unlocked ones are re-rolled within their
                        // schema-declared ranges. The merged map
                        // becomes the new working state.
                        profileEditor.shaderParams = profileEditor.randomizedShaderParams();
                    }
                    onShaderParamWriteRequested: function(effectId, paramId, value) {
                        if (effectId !== profileEditor.shaderEffectId)
                            return ;

                        var next = ({
                        });
                        for (var k in profileEditor.shaderParams) next[k] = profileEditor.shaderParams[k]
                        next[paramId] = value;
                        profileEditor.shaderParams = next;
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
                        // Shader-kind: pattern + event sufficient
                        // (engaged-empty effectId is the explicit-
                        // block sentinel). Timing-kind: at least one
                        // axis must be overridden so the rule actually
                        // does something.
                        enabled: patternField.text.length > 0 && (eventCombo.currentValue || "").length > 0 && (shaderKindRadio.checked || profileEditor.overrideCurve || profileEditor.overrideDuration)
                        ToolTip.text: i18n("Append the rule to the list")
                        ToolTip.visible: hovered
                        onClicked: {
                            var rule = {
                                "classPattern": patternField.text,
                                "eventPath": eventCombo.currentValue || ""
                            };
                            if (shaderKindRadio.checked) {
                                rule.kind = root.kindShader;
                                rule.effectId = profileEditor.shaderEffectId || "";
                                rule.shaderParams = profileEditor.shaderParams;
                            } else {
                                rule.kind = root.kindTiming;
                                // Empty curve / zero duration are the
                                // documented inherit sentinels.
                                rule.curve = profileEditor.overrideCurve ? profileEditor.curveString : "";
                                rule.durationMs = profileEditor.overrideDuration ? profileEditor.duration : 0;
                            }
                            if (settingsController.animationsPage.addAppRule(rule)) {
                                patternField.clear();
                                root._resetEditor();
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

}
