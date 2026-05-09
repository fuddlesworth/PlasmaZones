// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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
    property var rulesList: settingsController.animationsPage.appRules()
    /// Window-event paths the rule list can target. Static across the
    /// page lifetime since the underlying ProfilePaths catalogue is
    /// compile-time.
    readonly property var eventsList: settingsController.animationsPage.animationAppRuleEvents()
    /// Available shader effects from the registry, including a "None"
    /// entry the picker prepends via `includeNoneEntry: true`.
    readonly property var shadersList: settingsController.animationsPage.availableShaderEffects()
    /// Rule kinds — keep in sync with `AnimationAppRule::Kind` JSON
    /// strings. Used by the kind radio.
    readonly property string kindShader: "shader"
    readonly property string kindTiming: "timing"

    function _kindLabel(kind) {
        if (kind === root.kindTiming)
            return i18n("Timing");

        return i18n("Shader");
    }

    function _eventLabel(eventPath) {
        for (var i = 0; i < eventsList.length; ++i) {
            if (eventsList[i].path === eventPath)
                return eventsList[i].label;

        }
        return eventPath;
    }

    function _shaderName(effectId) {
        if (!effectId)
            return i18n("(no shader)");

        for (var i = 0; i < shadersList.length; ++i) {
            if (shadersList[i].id === effectId)
                return shadersList[i].name;

        }
        return effectId;
    }

    function _ruleSummary(rule) {
        if (rule.kind === root.kindTiming) {
            var duration = rule.durationMs > 0 ? i18n("%1 ms", rule.durationMs) : i18n("default duration");
            var curve = rule.curve && rule.curve.length > 0 ? rule.curve : i18n("default curve");
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

        target: settingsController.animationsPage
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

                // Shader payload row — visible when Shader kind selected.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing
                    visible: shaderKindRadio.checked

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
                            shaderPicker.currentShaderId = id;
                        }
                    }

                }

                // Timing payload row — visible when Timing kind selected.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing
                    visible: timingKindRadio.checked

                    Label {
                        text: i18n("Curve:")
                    }

                    TextField {
                        id: curveField

                        Layout.fillWidth: true
                        placeholderText: i18n("e.g., 0.42,0.0,0.58,1.0   or   spring:30,0.7   (blank = inherit)")
                        Accessible.name: i18n("Curve specifier (blank to inherit per-event default)")
                    }

                    Label {
                        text: i18n("Duration:")
                    }

                    SpinBox {
                        id: durationSpin

                        from: 0
                        to: 60000
                        stepSize: 50
                        value: 0
                        editable: true
                        Accessible.name: i18n("Duration in milliseconds (zero inherits per-event default)")
                        textFromValue: function(value, locale) {
                            if (value === 0)
                                return i18n("inherit");

                            return i18n("%1 ms", value);
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
                        // pattern. For timing-kind, require either a
                        // non-empty curve or a non-zero duration so the
                        // rule actually overrides something.
                        enabled: patternField.text.length > 0 && (shaderKindRadio.checked || curveField.text.length > 0 || durationSpin.value > 0)
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
                            } else {
                                rule.kind = root.kindTiming;
                                rule.curve = curveField.text;
                                rule.durationMs = durationSpin.value;
                            }
                            if (settingsController.animationsPage.addAppRule(rule)) {
                                patternField.clear();
                                shaderPicker.currentShaderId = "";
                                curveField.clear();
                                durationSpin.value = 0;
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
                                Layout.maximumWidth: Math.min(implicitWidth, parent.width * 0.25)
                            }

                            Label {
                                text: i18nc("event-path label inline with rule pattern", "on %1", root._eventLabel(ruleDelegate.modelData.eventPath))
                                opacity: 0.8
                                Layout.alignment: Qt.AlignVCenter
                                elide: Text.ElideRight
                                Layout.maximumWidth: Math.min(implicitWidth, parent.width * 0.25)
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
