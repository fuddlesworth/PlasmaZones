// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Animations → General — global defaults that apply to every event.
 *
 * This is the "global" branch of the inheritance tree: any sub-page event
 * without its own override falls back through its category up to here.
 * Edits route through `appSettings.animation*` (the existing config-driven
 * `Global` profile in `kSettingsDrivenProfilePaths`), not through the
 * per-event ProfileLoader pipeline — so this page intentionally bypasses
 * `AnimationsPageController.setOverride` and writes through the same
 * `Settings::animationProfile` Q_PROPERTYs the legacy General > Animations
 * card has always used.
 *
 * Timing-mode toggle mirrors PR #291's per-event card: writing a
 * `"spring:omega,zeta"` string into `animationEasingCurve` flips the
 * Global profile to spring physics (the curve string itself is the
 * discriminator — `Settings::animationProfile` parses both forms via
 * `CurveRegistry`). The `_lastEasingCurve` / `_lastSpringOmega/Zeta`
 * state preserves each axis across mode toggles so previewing Spring
 * doesn't permanently lose the user's easing curve.
 */
Flickable {
    id: page

    // Slider sizing constants needed by EasingSettings (mirrors GeneralPage's
    // contract — the embedded component reads these via its `constants` prop).
    readonly property int sliderPreferredWidth: Kirigami.Units.gridUnit * 16
    readonly property int sliderValueLabelWidth: Kirigami.Units.gridUnit * 3
    readonly property var appSettings: settingsController.settings
    // ── Timing-mode state ───────────────────────────────────────────────
    // Computed from the live `animationEasingCurve` string: the "spring:"
    // prefix is the on-disk discriminator. Defining this as a binding
    // means an external write (e.g. from the Presets page's
    // applyAsDefault) flips the toggle without imperative refresh.
    readonly property string _springPrefix: "spring:"
    readonly property bool _isSpring: page._isSpringCurve(page.appSettings.animationEasingCurve)
    readonly property int _currentTimingMode: _isSpring ? CurvePresets.timingModeSpring : CurvePresets.timingModeEasing
    // Last-seen values per axis. When the user toggles the timing-mode
    // combo we write a fresh curve string from these, preserving the
    // other axis's value across the round trip. Initialized below from
    // the saved curve and kept in sync via Connections.
    property string _lastEasingCurve: CurvePresets.defaultEasingCurve
    property real _lastSpringOmega: CurvePresets.defaultSpringOmega
    property real _lastSpringZeta: CurvePresets.defaultSpringZeta

    function _isSpringCurve(curveStr) {
        return typeof curveStr === "string" && curveStr.indexOf(page._springPrefix) === 0;
    }

    function _parseSpring(curveStr) {
        var parts = curveStr.substring(page._springPrefix.length).split(",");
        var w = parseFloat(parts[0]);
        var z = parseFloat(parts[1]);
        return {
            "omega": isFinite(w) ? w : CurvePresets.defaultSpringOmega,
            "zeta": isFinite(z) ? z : CurvePresets.defaultSpringZeta
        };
    }

    // Refresh the cached easing/spring values from whichever axis the
    // current curve string describes. Called once on load and whenever
    // `animationEasingCurve` changes externally.
    function _syncCachedValues() {
        var c = page.appSettings.animationEasingCurve;
        if (typeof c !== "string")
            return ;

        if (page._isSpringCurve(c)) {
            var s = page._parseSpring(c);
            page._lastSpringOmega = s.omega;
            page._lastSpringZeta = s.zeta;
        } else if (c.length > 0) {
            page._lastEasingCurve = c;
        }
    }

    function _writeSpring(omega, zeta) {
        var encoded = page._springPrefix + omega.toFixed(2) + "," + zeta.toFixed(2);
        // No-op-guard before mutating cache so a re-selection of the
        // already-active mode/values doesn't churn bindings even though
        // the underlying setter would no-op.
        if (page.appSettings.animationEasingCurve === encoded)
            return ;

        page._lastSpringOmega = omega;
        page._lastSpringZeta = zeta;
        page.appSettings.animationEasingCurve = encoded;
    }

    function _writeEasing(curveStr) {
        if (page.appSettings.animationEasingCurve === curveStr)
            return ;

        page._lastEasingCurve = curveStr;
        page.appSettings.animationEasingCurve = curveStr;
    }

    contentHeight: content.implicitHeight
    clip: true
    Component.onCompleted: page._syncCachedValues()

    Connections {
        function onAnimationEasingCurveChanged() {
            page._syncCachedValues();
        }

        target: page.appSettings
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            visible: true
            text: i18n("These defaults apply to every animation event unless a sub-page (Window, Zone, OSD, etc.) defines its own override.")
        }

        SettingsCard {
            // ── Mode-agnostic behaviour ───────────────────────────────
            // These rows used to live inside EasingSettings; pulled
            // out so they remain visible (and editable) in spring
            // mode — they configure how the daemon sequences and
            // gates animations regardless of the timing curve.

            id: animationsCard

            Layout.fillWidth: true
            headerText: i18n("Global animation defaults")
            showToggle: true
            toggleChecked: page.appSettings.animationsEnabled
            collapsible: true
            onToggleClicked: function(checked) {
                page.appSettings.animationsEnabled = checked;
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                // ── Easing preview (visible only in easing mode) ──────────
                EasingPreview {
                    id: easingPreview

                    Layout.fillWidth: true
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 28
                    Layout.alignment: Qt.AlignHCenter
                    visible: page._currentTimingMode === CurvePresets.timingModeEasing
                    // Feed the cached easing curve, never the raw setting,
                    // so a "spring:..." write doesn't drive the parser
                    // through invalid bezier territory while the spring
                    // branch is what's actually visible.
                    curve: page._lastEasingCurve
                    animationDuration: page.appSettings.animationDuration
                    previewEnabled: animationsCard.toggleChecked && page._currentTimingMode === CurvePresets.timingModeEasing
                    opacity: animationsCard.toggleChecked ? 1 : 0.4
                    onCurveEdited: function(newCurve) {
                        page._writeEasing(newCurve);
                    }
                }

                // ── Spring preview (visible only in spring mode) ──────────
                SpringPreview {
                    Layout.fillWidth: true
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 28
                    Layout.alignment: Qt.AlignHCenter
                    visible: page._currentTimingMode === CurvePresets.timingModeSpring
                    omega: page._lastSpringOmega
                    zeta: page._lastSpringZeta
                    previewEnabled: animationsCard.toggleChecked && page._currentTimingMode === CurvePresets.timingModeSpring
                    opacity: animationsCard.toggleChecked ? 1 : 0.4
                }

                SettingsSeparator {
                }

                // ── Timing mode ───────────────────────────────────────────
                // Mirrors AnimationEventCard's combo so both global and
                // per-event configuration share the same shape. Switching
                // mode commits a curve string immediately so the live
                // preview and dependent sub-pages re-resolve.
                SettingsRow {
                    title: i18n("Default timing mode")
                    description: i18n("Easing curves run for a fixed duration; springs derive their settle time from physics")

                    WideComboBox {
                        Accessible.name: i18n("Default timing mode")
                        enabled: animationsCard.toggleChecked
                        model: [i18n("Easing"), i18n("Spring")]
                        currentIndex: page._currentTimingMode
                        onActivated: function(index) {
                            if (index === CurvePresets.timingModeSpring)
                                page._writeSpring(page._lastSpringOmega, page._lastSpringZeta);
                            else
                                page._writeEasing(page._lastEasingCurve);
                        }
                    }

                }

                SettingsSeparator {
                }

                // ── Easing controls (visible only in easing mode) ─────────
                EasingSettings {
                    Layout.fillWidth: true
                    visible: page._currentTimingMode === CurvePresets.timingModeEasing
                    // `appSettings` is resolved internally via
                    // settingsController.settings — see EasingSettings.qml's
                    // header comment for the qmlcachegen timing-window
                    // explanation.
                    constants: page
                    animationsEnabled: animationsCard.toggleChecked
                    easingPreview: easingPreview
                }

                // ── Spring controls (visible only in spring mode) ─────────
                SpringSettings {
                    Layout.fillWidth: true
                    visible: page._currentTimingMode === CurvePresets.timingModeSpring
                    animationsEnabled: animationsCard.toggleChecked
                    omega: page._lastSpringOmega
                    zeta: page._lastSpringZeta
                    onSpringChanged: function(omega, zeta) {
                        page._writeSpring(omega, zeta);
                    }
                }

                SettingsSeparator {
                }

                SettingsRow {
                    title: i18n("Multiple windows")
                    description: i18n("How to animate when moving several windows at once")

                    WideComboBox {
                        Accessible.name: i18n("Multiple windows")
                        enabled: animationsCard.toggleChecked
                        model: [i18n("All at once"), i18n("One by one")]
                        currentIndex: page.appSettings.animationSequenceMode
                        onActivated: (index) => {
                            return page.appSettings.animationSequenceMode = index;
                        }
                    }

                }

                SettingsRow {
                    visible: page.appSettings.animationSequenceMode === 1
                    title: i18n("Stagger delay")
                    description: i18n("Pause between each window's animation start")

                    SettingsSlider {
                        enabled: animationsCard.toggleChecked
                        from: settingsController.generalPage.animationStaggerIntervalMin
                        to: settingsController.generalPage.animationStaggerIntervalMax
                        stepSize: 10
                        value: page.appSettings.animationStaggerInterval
                        valueSuffix: " ms"
                        labelWidth: Kirigami.Units.gridUnit * 4
                        onMoved: (value) => {
                            return page.appSettings.animationStaggerInterval = Math.round(value);
                        }
                    }

                }

                SettingsSeparator {
                }

                SettingsRow {
                    title: i18n("Minimum distance")
                    description: page.appSettings.animationMinDistance === 0 ? i18n("Currently: always animate, no threshold") : i18n("Skip animation when geometry changes less than this")

                    SettingsSpinBox {
                        enabled: animationsCard.toggleChecked
                        from: settingsController.generalPage.animationMinDistanceMin
                        to: settingsController.generalPage.animationMinDistanceMax
                        stepSize: 5
                        value: page.appSettings.animationMinDistance
                        onValueModified: (value) => {
                            return page.appSettings.animationMinDistance = value;
                        }
                    }

                }

            }

        }

    }

}
