// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable card for per-event animation configuration.
 *
 * Required properties:
 *   - eventName: string (e.g. "snapIn")
 *   - eventLabel: string (e.g. "Snap In")
 *   - isParentNode: bool
 *   - showStyleSelector: bool
 *   - styleDomain: string ("window", "overlay", or "both")
 */
Item {
    id: root

    required property string eventName
    required property string eventLabel
    property bool isParentNode: false
    property bool showStyleSelector: true
    /// Animation style domain filter. Controls which styles appear in the dropdown.
    /// "window" = window geometry styles, "overlay" = overlay UI styles, "both" = all.
    property string styleDomain: "both"
    /// When true, the override toggle is hidden and controls are always visible.
    /// Used for the "global" root node which always has an active profile.
    property bool alwaysEnabled: false
    /// Whether the card can be collapsed by clicking the header.
    property bool collapsible: false
    // ── Internal state (all at root level for reliable scoping) ───────
    property bool overrideEnabled: false
    property int currentTimingMode: 0
    property string currentStyle: "morph"
    property string currentCurve: "0.33,1.00,0.68,1.00"
    property real currentSpringDamping: 1
    property real currentSpringStiffness: 800
    property real currentSpringEpsilon: 0.0001
    // Filtered style keys/names based on styleDomain — shows only relevant styles
    readonly property var _allStyleKeys: settingsController.animationStyleKeys()
    readonly property var _allStyleNames: settingsController.animationStyleNames()
    readonly property var styleKeys: {
        var keys = [];
        for (var i = 0; i < _allStyleKeys.length; i++) {
            var domain = settingsController.animationStyleDomain(_allStyleKeys[i]);
            if (root.styleDomain === "both" || domain === root.styleDomain || domain === "both")
                keys.push(_allStyleKeys[i]);

        }
        return keys;
    }
    readonly property var styleNames: {
        var names = [];
        for (var i = 0; i < _allStyleKeys.length; i++) {
            var domain = settingsController.animationStyleDomain(_allStyleKeys[i]);
            if (root.styleDomain === "both" || domain === root.styleDomain || domain === "both")
                names.push(_allStyleNames[i]);

        }
        return names;
    }
    property int currentDuration: 300
    property real currentStyleParam: 0.87
    // Rich description for the curve summary row
    readonly property string curveDescription: {
        if (root.currentTimingMode === CurvePresets.timingModeSpring) {
            var si = CurvePresets.springPresetIndex(root.currentSpringDamping, root.currentSpringStiffness, root.currentSpringEpsilon);
            if (si >= 0)
                return i18n("Spring · %1", CurvePresets.springPresets[si].label);

            return i18n("Spring · Custom");
        }
        var idx = CurvePresets.findIndices(root.currentCurve);
        if (idx.styleIndex >= 0)
            return CurvePresets.easingStyles[idx.styleIndex].label + " · " + CurvePresets.easingDirections[idx.dirIndex].label;

        return i18n("Easing · Custom");
    }
    readonly property string curveSecondary: {
        if (root.currentTimingMode === CurvePresets.timingModeSpring)
            return i18n("Damping: %1 · Stiffness: %2", root.currentSpringDamping.toFixed(2), Math.round(root.currentSpringStiffness));

        return i18n("%1 ms", root.currentDuration);
    }

    function refreshFromTree() {
        var raw = settingsController.rawProfileForEvent(root.eventName);
        var resolved = settingsController.resolvedProfileForEvent(root.eventName);
        var hasRaw = Object.keys(raw).length > 0;
        root.overrideEnabled = hasRaw;
        root.currentTimingMode = raw.timingMode !== undefined ? raw.timingMode : (resolved.timingMode ?? 0);
        root.currentStyle = settingsController.styleKeyForProfile(hasRaw ? raw : resolved);
        root.currentCurve = resolved.easingCurve || "0.33,1.00,0.68,1.00";
        root.currentSpringDamping = resolved.spring ? (resolved.spring.dampingRatio ?? 1) : 1;
        root.currentSpringStiffness = resolved.spring ? (resolved.spring.stiffness ?? 800) : 800;
        root.currentSpringEpsilon = resolved.spring ? (resolved.spring.epsilon ?? 0.0001) : 0.0001;
        root.currentDuration = raw.duration !== undefined ? raw.duration : (resolved.duration ?? appSettings.animationDuration);
        root.currentStyleParam = raw.styleParam !== undefined ? raw.styleParam : (resolved.styleParam ?? 0.87);
    }

    function updateEventProfile(mutator) {
        let raw = settingsController.rawProfileForEvent(root.eventName);
        mutator(raw);
        settingsController.setEventProfile(root.eventName, raw);
    }

    function resolvedProfile() {
        return settingsController.resolvedProfileForEvent(root.eventName);
    }

    implicitHeight: card.implicitHeight
    Layout.fillWidth: true
    Component.onCompleted: refreshFromTree()

    Connections {
        function onAnimationProfileTreeChanged() {
            root.refreshFromTree();
        }

        target: appSettings
    }

    SettingsCard {
        id: card

        anchors.fill: parent
        headerText: root.eventLabel
        showToggle: !root.alwaysEnabled
        toggleChecked: root.alwaysEnabled || root.overrideEnabled
        onToggleClicked: (checked) => {
            if (checked)
                settingsController.setEventProfile(root.eventName, root.resolvedProfile());
            else
                settingsController.clearEventProfile(root.eventName);
        }
        collapsible: root.collapsible

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            // ── Inheritance info (override OFF or parent node with override ON) ─
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Information
                text: root.isParentNode ? i18n("Settings here apply to all child events unless individually overridden.") : i18n("Inheriting from: %1", settingsController.parentChainForEvent(root.eventName))
                visible: !root.alwaysEnabled && (root.isParentNode ? root.overrideEnabled : !root.overrideEnabled)
            }

            Label {
                visible: !root.alwaysEnabled && !root.overrideEnabled
                text: i18n("Current: %1", settingsController.inheritSummaryForEvent(root.eventName))
                font.italic: true
                color: Kirigami.Theme.disabledTextColor
            }

            // ── Override controls (override ON or alwaysEnabled) ────
            ColumnLayout {
                visible: root.alwaysEnabled || root.overrideEnabled
                spacing: Kirigami.Units.smallSpacing

                // ── Curve summary row ─────────────────────────────
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.largeSpacing

                    CurveThumbnail {
                        id: curveThumbnail

                        implicitWidth: Kirigami.Units.gridUnit * 6
                        implicitHeight: Kirigami.Units.gridUnit * 4
                        curve: root.currentCurve
                        timingMode: root.currentTimingMode
                        springDamping: root.currentSpringDamping
                        springStiffness: root.currentSpringStiffness
                        springEpsilon: root.currentSpringEpsilon
                        onClicked: curveDialog.open()
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                        Label {
                            Layout.fillWidth: true
                            text: root.curveDescription
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.curveSecondary
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            color: Kirigami.Theme.disabledTextColor
                            elide: Text.ElideRight
                        }

                    }

                    Button {
                        text: i18n("Customize…")
                        icon.name: "configure"
                        Accessible.name: i18n("Customize curve for %1", root.eventLabel)
                        onClicked: curveDialog.open()
                    }

                }

                SettingsSeparator {
                }

                // ── Timing mode ───────────────────────────────────
                SettingsRow {
                    title: i18n("Timing mode")

                    WideComboBox {
                        id: timingModeCombo

                        Accessible.name: i18n("Timing mode")
                        model: [i18n("Easing"), i18n("Spring")]
                        currentIndex: root.currentTimingMode
                        onActivated: (index) => {
                            root.updateEventProfile(function(raw) {
                                raw.timingMode = index;
                            });
                        }
                    }

                }

                // ── Duration (easing only) ────────────────────────
                SettingsSeparator {
                    visible: root.currentTimingMode === CurvePresets.timingModeEasing
                }

                SettingsRow {
                    visible: root.currentTimingMode === CurvePresets.timingModeEasing
                    title: i18n("Duration")

                    SettingsSlider {
                        from: settingsController.animationDurationMin
                        to: settingsController.animationDurationMax
                        stepSize: 10
                        valueSuffix: " ms"
                        Accessible.name: i18n("Animation duration")
                        labelWidth: Kirigami.Units.gridUnit * 4
                        value: root.currentDuration
                        onMoved: (value) => {
                            root.updateEventProfile(function(raw) {
                                raw.duration = Math.round(value);
                            });
                        }
                    }

                }

                // ── Animation style ───────────────────────────────
                SettingsSeparator {
                    visible: root.showStyleSelector
                }

                SettingsRow {
                    visible: root.showStyleSelector
                    title: i18n("Animation style")

                    WideComboBox {
                        id: styleCombo

                        Accessible.name: i18n("Animation style")
                        model: root.styleNames
                        currentIndex: Math.max(0, root.styleKeys.indexOf(root.currentStyle))
                        onActivated: (index) => {
                            settingsController.setEventAnimationStyle(root.eventName, root.styleKeys[index]);
                        }
                    }

                }

                SettingsRow {
                    visible: root.showStyleSelector && (root.currentStyle === "popin" || root.currentStyle === "scalein")
                    title: i18n("Scale start")

                    SettingsSlider {
                        Accessible.name: i18n("Scale start percentage")
                        from: 10
                        to: 100
                        stepSize: 1
                        valueSuffix: "%"
                        value: Math.round(root.currentStyleParam * 100)
                        onMoved: (value) => {
                            root.updateEventProfile(function(raw) {
                                raw.styleParam = value / 100;
                            });
                        }
                    }

                }

                // ── Shader parameters (when a shader-based style is selected) ──
                SettingsSeparator {
                    visible: shaderParamEditor.visible
                }

                AnimationShaderParamEditor {
                    id: shaderParamEditor

                    Layout.fillWidth: true
                    eventName: root.eventName
                    shaderId: root.currentStyle !== "none" ? root.currentStyle : ""
                }

            }

        }

    }

    CurveEditorDialog {
        id: curveDialog

        parent: root.Window.window ? root.Window.window.contentItem : root
        eventLabel: root.eventLabel
        timingMode: root.currentTimingMode
        easingCurve: root.currentCurve
        springDamping: root.currentSpringDamping
        springStiffness: root.currentSpringStiffness
        springEpsilon: root.currentSpringEpsilon
        onCurveApplied: (curve) => {
            root.updateEventProfile(function(raw) {
                raw.easingCurve = curve;
            });
        }
        onSpringApplied: (damping, stiffness, epsilon) => {
            root.updateEventProfile(function(raw) {
                raw.spring = {
                    "dampingRatio": damping,
                    "stiffness": stiffness,
                    "epsilon": epsilon
                };
            });
        }
    }

}
