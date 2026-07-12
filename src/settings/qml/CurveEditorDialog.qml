// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Popup dialog for full curve editing (drag-handle bezier or spring sliders).
 *
 * Spring axis uses (omega, zeta) per `PhosphorAnimation::Spring`. The
 * `Save as Preset…` footer is wired in Phase 5; until then it stays hidden
 * to keep the dialog functional without the preset CRUD controller.
 */
Kirigami.Dialog {
    id: root

    property string eventLabel: ""
    property int timingMode: CurvePresets.timingModeEasing
    property string easingCurve: CurvePresets.defaultEasingCurve
    // (omega, zeta) — rad/s and damping ratio, per Spring.h
    property real springOmega: CurvePresets.defaultSpringOmega
    property real springZeta: CurvePresets.defaultSpringZeta
    // Working state — seeded onOpened, cleared onClosed. Apply only
    // commits when `_dirty` flipped (i.e. the working state diverged
    // from the seed values). Without that check, a parent re-binding
    // `easingCurve` while the dialog is open would otherwise see Apply
    // write the now-stale `_workingCurve` back through.
    property string _workingCurve: ""
    property real _workingOmega: 0
    property real _workingZeta: 0
    property bool _dirty: false
    property bool _savingPreset: false

    signal curveApplied(string curve)
    signal springApplied(real omega, real zeta)

    title: i18n("Customize Curve: %1", eventLabel)
    preferredWidth: Math.min(Kirigami.Units.gridUnit * 40, parent ? parent.width * 0.85 : Kirigami.Units.gridUnit * 40)
    preferredHeight: Math.min(Kirigami.Units.gridUnit * 32, parent ? parent.height * 0.8 : Kirigami.Units.gridUnit * 32)
    standardButtons: Kirigami.Dialog.Apply | Kirigami.Dialog.Cancel
    onApplied: {
        if (root._dirty) {
            if (timingMode === CurvePresets.timingModeEasing)
                root.curveApplied(root._workingCurve);
            else
                root.springApplied(_workingOmega, _workingZeta);
        }
        close();
    }
    onRejected: close()
    onClosed: {
        // Clear working state so the next open() re-seeds from the
        // (possibly updated) parent bindings rather than reusing stale
        // values from this session.
        root._workingCurve = "";
        root._workingOmega = 0;
        root._workingZeta = 0;
        root._dirty = false;
    }
    onOpened: {
        root._savingPreset = false;
        // Setting `_workingCurve` flows into easingPreviewInDialog.curve via
        // the declarative binding `curve: root._workingCurve` below. The
        // EasingPreview's onCurveChanged handler then drives parseCurve +
        // replay automatically — no imperative assignment needed (and
        // assigning easingPreviewInDialog.curve directly would SEVER the
        // declarative binding from _workingCurve).
        _workingCurve = easingCurve;
        _workingOmega = springOmega;
        _workingZeta = springZeta;
        root._dirty = false;
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // ── Easing mode ─────────────────────────────────────────────
        EasingPreview {
            id: easingPreviewInDialog

            visible: root.timingMode === CurvePresets.timingModeEasing
            Layout.fillWidth: true
            curve: root._workingCurve
            animationDuration: settingsController.settings.animationDuration
            previewEnabled: root.visible && root.timingMode === CurvePresets.timingModeEasing
            onCurveEdited: function (newCurve) {
                root._workingCurve = newCurve;
                root._dirty = true;
            }
        }

        // ── Easing controls ─────────────────────────────────────────
        SettingsSeparator {
            visible: root.timingMode === CurvePresets.timingModeEasing
        }

        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeEasing
            title: i18n("Preset")

            WideComboBox {
                id: dialogCurvePreset

                Accessible.name: i18n("Easing preset")
                displayText: currentIndex < 0 ? i18n("Custom") : currentText
                model: CurvePresets.easingStyles.map(s => {
                    return s.label;
                })
                currentIndex: CurvePresets.findIndices(root._workingCurve).styleIndex
                onActivated: index => {
                    var dirIdx = dialogDirection.currentIndex >= 0 ? dialogDirection.currentIndex : 1;
                    var curve = CurvePresets.curveForSelection(index, dirIdx);
                    if (curve) {
                        root._workingCurve = curve;
                        root._dirty = true;
                    }
                }
            }
        }

        SettingsSeparator {
            visible: root.timingMode === CurvePresets.timingModeEasing
        }

        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeEasing
            title: i18n("Direction")
            description: i18n("Ease In accelerates, Ease Out decelerates")

            WideComboBox {
                id: dialogDirection

                // Linear's three directions are byte-identical (the curve
                // is the identity bezier 0,0,1,1 in every direction); the
                // combo would otherwise appear interactive while being a
                // no-op. Disable when the style picker is on Linear so the
                // dead control is visually grey rather than misleading.
                enabled: dialogCurvePreset.currentIndex !== 0
                Accessible.name: i18n("Easing direction")
                model: CurvePresets.easingDirections.map(d => {
                    return d.label;
                })
                currentIndex: CurvePresets.findIndices(root._workingCurve).dirIndex
                onActivated: index => {
                    var styleIdx = dialogCurvePreset.currentIndex;
                    if (styleIdx >= 0) {
                        var curve = CurvePresets.curveForSelection(styleIdx, index);
                        if (curve) {
                            root._workingCurve = curve;
                            root._dirty = true;
                        }
                    }
                }
            }
        }

        SettingsSeparator {
            visible: root.timingMode === CurvePresets.timingModeEasing && easingPreviewInDialog.curveType !== "bezier"
        }

        // Amplitude (elastic + bounce)
        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeEasing && (easingPreviewInDialog.curveType.indexOf("elastic") >= 0 || easingPreviewInDialog.curveType.indexOf("bounce") >= 0)
            title: i18n("Amplitude")
            description: easingPreviewInDialog.curveType.indexOf("elastic") >= 0 ? i18n("Strength of elastic overshoot") : i18n("Height of bounce peaks")

            SettingsSlider {
                readonly property bool isElastic: easingPreviewInDialog.curveType.indexOf("elastic") >= 0

                Accessible.name: i18n("Amplitude")
                // Mirrors Easing::clampAmplitude — see EasingSettings.qml.
                from: isElastic ? 1 : 0.5
                to: isElastic ? 2 : 3
                stepSize: 0.1
                value: easingPreviewInDialog.curveAmplitude
                formatValue: function (v) {
                    return v.toFixed(1);
                }
                onMoved: value => {
                    var ct = easingPreviewInDialog.curveType;
                    var amp = value.toFixed(2);
                    if (ct.indexOf("elastic") >= 0) {
                        var per = easingPreviewInDialog.elasticPeriod.toFixed(2);
                        root._workingCurve = ct + ":" + amp + "," + per;
                    } else {
                        root._workingCurve = ct + ":" + amp + "," + easingPreviewInDialog.bouncesCount;
                    }
                    root._dirty = true;
                }
            }
        }

        // Period (elastic only)
        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeEasing && easingPreviewInDialog.curveType.indexOf("elastic") >= 0
            title: i18n("Period")
            description: i18n("Lower values wobble faster")

            SettingsSlider {
                Accessible.name: i18n("Period")
                from: 0.1
                to: 1
                stepSize: 0.05
                value: easingPreviewInDialog.elasticPeriod
                formatValue: function (v) {
                    return v.toFixed(2);
                }
                onMoved: value => {
                    var ct = easingPreviewInDialog.curveType;
                    var amp = easingPreviewInDialog.curveAmplitude.toFixed(2);
                    root._workingCurve = ct + ":" + amp + "," + value.toFixed(2);
                    root._dirty = true;
                }
            }
        }

        // Bounces (bounce only)
        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeEasing && easingPreviewInDialog.curveType.indexOf("bounce") >= 0
            title: i18n("Bounces")
            description: i18n("Number of bounce repetitions")

            SettingsSlider {
                Accessible.name: i18n("Bounces")
                from: 1
                to: 8
                stepSize: 1
                value: easingPreviewInDialog.bouncesCount
                valueSuffix: ""
                onMoved: value => {
                    var ct = easingPreviewInDialog.curveType;
                    var amp = easingPreviewInDialog.curveAmplitude.toFixed(2);
                    root._workingCurve = ct + ":" + amp + "," + Math.round(value);
                    root._dirty = true;
                }
            }
        }

        // ── Spring mode ─────────────────────────────────────────────
        SpringPreview {
            visible: root.timingMode === CurvePresets.timingModeSpring
            Layout.fillWidth: true
            omega: root._workingOmega
            zeta: root._workingZeta
            previewEnabled: root.visible && root.timingMode === CurvePresets.timingModeSpring
        }

        SettingsSeparator {
            visible: root.timingMode === CurvePresets.timingModeSpring
        }

        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeSpring
            title: i18n("Preset")
            description: i18n("Quick-select spring behavior")

            WideComboBox {
                id: springPresetCombo

                Accessible.name: i18n("Spring preset")
                displayText: currentIndex < 0 ? i18n("Custom") : currentText
                model: CurvePresets.springPresets.map(p => {
                    return p.label;
                })
                currentIndex: CurvePresets.springPresetIndex(root._workingOmega, root._workingZeta)
                onActivated: index => {
                    if (index >= 0 && index < CurvePresets.springPresets.length) {
                        var p = CurvePresets.springPresets[index];
                        root._workingOmega = p.omega;
                        root._workingZeta = p.zeta;
                        root._dirty = true;
                    }
                }
            }
        }

        SettingsSeparator {
            visible: root.timingMode === CurvePresets.timingModeSpring
        }

        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeSpring
            title: i18n("Speed (ω)")
            description: i18n("Higher = faster spring response")

            SettingsSlider {
                Accessible.name: i18n("Speed")
                from: settingsController.animationsPage.springOmegaMin
                to: settingsController.animationsPage.springOmegaMax
                stepSize: 0.5
                value: root._workingOmega
                formatValue: function (v) {
                    return v.toFixed(1);
                }
                onMoved: value => {
                    root._workingOmega = value;
                    root._dirty = true;
                }
            }
        }

        SettingsSeparator {
            visible: root.timingMode === CurvePresets.timingModeSpring
        }

        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeSpring
            title: i18n("Damping ratio (ζ)")
            description: i18n("< 1 bouncy, = 1 critical, > 1 overdamped")

            SettingsSlider {
                Accessible.name: i18n("Damping ratio")
                from: settingsController.animationsPage.springZetaMin
                to: settingsController.animationsPage.springZetaMax
                stepSize: 0.05
                value: root._workingZeta
                formatValue: function (v) {
                    return v.toFixed(2);
                }
                onMoved: value => {
                    root._workingZeta = value;
                    root._dirty = true;
                }
            }
        }
    }

    // Save-as-preset footer.
    footerLeadingComponent: Component {
        RowLayout {
            spacing: Kirigami.Units.smallSpacing

            ToolButton {
                text: i18n("Save as Preset…")
                icon.name: "bookmark-new"
                Accessible.name: i18n("Save current curve as preset")
                visible: !root._savingPreset
                onClicked: root._savingPreset = true
            }

            TextField {
                id: saveNameField

                visible: root._savingPreset
                Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                Accessible.name: i18n("Preset name")
                placeholderText: i18n("Preset name…")
                onVisibleChanged: {
                    if (visible) {
                        text = "";
                        forceActiveFocus();
                    }
                }
                onAccepted: {
                    var trimmed = text.trim();
                    if (trimmed.length === 0)
                        return;

                    // Build a Profile JSON shaped like Profile::toJson():
                    // a single `curve` string (easing wire format or
                    // "spring:omega,zeta") plus duration. The controller
                    // stamps `name` automatically.
                    var profile = {
                        "duration": settingsController.settings.animationDuration
                    };
                    if (root.timingMode === CurvePresets.timingModeEasing)
                        profile.curve = root._workingCurve;
                    else
                        profile.curve = "spring:" + root._workingOmega.toFixed(2) + "," + root._workingZeta.toFixed(2);
                    // addUserPreset rejects names that collide with a
                    // built-in event path (would shadow an override
                    // slot on disk). On rejection, leave the entry
                    // field open so the user can rename instead of
                    // silently dismissing — the input text stays in
                    // place because we only flip _savingPreset on
                    // success.
                    if (settingsController.animationsPage.addUserPreset(trimmed, profile))
                        root._savingPreset = false;
                }
                Keys.onEscapePressed: root._savingPreset = false
            }

            ToolButton {
                visible: root._savingPreset
                icon.name: "document-save"
                Accessible.name: i18n("Save preset")
                enabled: saveNameField.text.trim().length > 0
                onClicked: saveNameField.accepted()
                ToolTip.text: i18n("Save")
                ToolTip.visible: hovered
            }

            ToolButton {
                visible: root._savingPreset
                icon.name: "dialog-cancel"
                Accessible.name: i18n("Cancel saving preset")
                onClicked: root._savingPreset = false
                ToolTip.text: i18n("Cancel")
                ToolTip.visible: hovered
            }
        }
    }
}
