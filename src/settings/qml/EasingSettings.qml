// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Easing curve controls for the Animations card.
 *
 * Contains the easing style/direction combo boxes, amplitude/period/bounce
 * sliders, duration, sequence mode, stagger interval, and minimum distance.
 *
 * Required properties:
 *   - constants: root object providing sliderPreferredWidth, sliderValueLabelWidth
 *   - animationsEnabled: whether animations are currently enabled
 *   - easingPreview: the EasingPreview component (for curveType, curveAmplitude, etc.)
 *
 * `appSettings` is resolved internally via `settingsController.settings`
 * rather than passed as a required property — that pattern was breaking
 * inside `qmlcachegen`'s AOT-compiled bindings when the parent wrote
 * `appSettings: appSettings`. The right-hand `appSettings` was
 * occasionally evaluated before context-property propagation settled,
 * leaving `easingRoot.appSettings` undefined and every subsequent
 * `animation*` access throwing "Cannot read property 'X' of undefined".
 * Going through `settingsController.settings` skirts that timing
 * window — the controller is also a context property but it's a
 * stable object reference whose `settings` accessor is non-null by
 * SettingsController contract.
 */
ColumnLayout {
    id: easingRoot

    readonly property var appSettings: settingsController.settings
    required property var constants
    required property bool animationsEnabled
    required property var easingPreview

    spacing: Kirigami.Units.smallSpacing

    // Curve lookup table: styles x directions -> bezier/named curve strings.
    // Pulls authoritative entries from the CurvePresets singleton; this
    // legacy "Custom" placeholder at index 0 is preserved because the
    // existing combo's `currentIndex > 0` gating relies on it.
    QtObject {
        id: easingData

        readonly property var styles: [{
            "label": i18n("Custom"),
            "key": "custom"
        }].concat(CurvePresets.easingStyles)
        readonly property var directions: CurvePresets.easingDirections
        readonly property var curves: CurvePresets.easingCurves

        function normalizeCurve(curve) {
            if (!curve || curve === "")
                return "";

            var hasLetter = false;
            for (var i = 0; i < curve.length; i++) {
                var c = curve.charAt(i);
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                    if (c !== 'e' && c !== 'E') {
                        hasLetter = true;
                        break;
                    }
                }
            }
            if (hasLetter) {
                var colonIdx = curve.indexOf(':');
                var name = colonIdx >= 0 ? curve.substring(0, colonIdx).trim() : curve.trim();
                var params = colonIdx >= 0 ? curve.substring(colonIdx + 1).trim() : "";
                var isElastic = name.startsWith("elastic");
                var isBounce = name.startsWith("bounce");
                if (params) {
                    var parts = params.split(",");
                    var amp = parts.length >= 1 ? parseFloat(parts[0]) : 1;
                    if (!isFinite(amp))
                        amp = 1;

                    if (isElastic) {
                        var per = parts.length >= 2 ? parseFloat(parts[1]) : 0.3;
                        if (!isFinite(per))
                            per = 0.3;

                        return name + ":" + amp.toFixed(2) + "," + per.toFixed(2);
                    }
                    if (isBounce) {
                        var bn = parts.length >= 2 ? Math.round(parseFloat(parts[1])) : 3;
                        if (!isFinite(bn))
                            bn = 3;

                        return name + ":" + amp.toFixed(2) + "," + bn;
                    }
                }
                if (isBounce)
                    return name + ":1.00,3";

                if (isElastic)
                    return name + ":1.00,0.30";

                return name;
            }
            var bparts = curve.split(",");
            if (bparts.length !== 4)
                return "";

            var x1 = parseFloat(bparts[0]), y1 = parseFloat(bparts[1]);
            var x2 = parseFloat(bparts[2]), y2 = parseFloat(bparts[3]);
            if (!isFinite(x1) || !isFinite(y1) || !isFinite(x2) || !isFinite(y2))
                return "";

            return x1.toFixed(2) + "," + y1.toFixed(2) + "," + x2.toFixed(2) + "," + y2.toFixed(2);
        }

        function findIndices(curve) {
            var norm = normalizeCurve(curve);
            if (norm === "")
                return {
                "styleIndex": 0,
                "dirIndex": 1
            };

            for (var si = 1; si < styles.length; si++) {
                var key = styles[si].key;
                if (!curves[key])
                    continue;

                for (var di = 0; di < directions.length; di++) {
                    var dirKey = directions[di].key;
                    if (normalizeCurve(curves[key][dirKey]) === norm)
                        return {
                        "styleIndex": si,
                        "dirIndex": di
                    };

                }
            }
            return {
                "styleIndex": 0,
                "dirIndex": 1
            };
        }

        function curveForSelection(styleIndex, dirIndex) {
            if (styleIndex <= 0 || styleIndex >= styles.length)
                return "";

            var key = styles[styleIndex].key;
            var dirKey = directions[dirIndex].key;
            return curves[key] ? curves[key][dirKey] : "";
        }

    }

    // Amplitude/bounce/elastic visibility helper
    QtObject {
        id: curveInfo

        readonly property bool isElastic: easingRoot.easingPreview.curveType === "elastic-in" || easingRoot.easingPreview.curveType === "elastic-out" || easingRoot.easingPreview.curveType === "elastic-in-out"
        readonly property bool isBounce: easingRoot.easingPreview.curveType === "bounce-in" || easingRoot.easingPreview.curveType === "bounce-out" || easingRoot.easingPreview.curveType === "bounce-in-out"
    }

    // ── Style ───────────────────────────────────────────────────────────────
    SettingsRow {
        title: i18n("Style")
        description: i18n("Animation curve style — controls how acceleration feels")

        WideComboBox {
            id: easingStyleCombo

            property bool updating: false

            enabled: easingRoot.animationsEnabled
            model: easingData.styles.map((s) => {
                return s.label;
            })
            currentIndex: easingData.findIndices(easingRoot.appSettings.animationEasingCurve).styleIndex
            onActivated: (index) => {
                if (updating || index <= 0)
                    return ;

                var curve = easingData.curveForSelection(index, easingDirectionCombo.currentIndex);
                if (curve)
                    easingRoot.appSettings.animationEasingCurve = curve;

            }

            Connections {
                function onAnimationEasingCurveChanged() {
                    easingStyleCombo.updating = true;
                    easingStyleCombo.currentIndex = easingData.findIndices(easingRoot.appSettings.animationEasingCurve).styleIndex;
                    easingStyleCombo.updating = false;
                }

                target: easingRoot.appSettings
            }

        }

    }

    SettingsSeparator {
    }

    // ── Direction ───────────────────────────────────────────────────────────
    SettingsRow {
        title: i18n("Direction")
        description: i18n("Ease In accelerates, Ease Out decelerates, In-Out does both")
        opacity: easingStyleCombo.currentIndex > 0 ? 1 : 0.4

        WideComboBox {
            id: easingDirectionCombo

            property bool updating: false

            enabled: easingRoot.animationsEnabled && easingStyleCombo.currentIndex > 0
            model: easingData.directions.map((d) => {
                return d.label;
            })
            currentIndex: easingData.findIndices(easingRoot.appSettings.animationEasingCurve).dirIndex
            onActivated: (index) => {
                if (updating)
                    return ;

                var styleIdx = easingStyleCombo.currentIndex;
                if (styleIdx <= 0)
                    return ;

                var curve = easingData.curveForSelection(styleIdx, index);
                if (curve)
                    easingRoot.appSettings.animationEasingCurve = curve;

            }

            Connections {
                function onAnimationEasingCurveChanged() {
                    easingDirectionCombo.updating = true;
                    easingDirectionCombo.currentIndex = easingData.findIndices(easingRoot.appSettings.animationEasingCurve).dirIndex;
                    easingDirectionCombo.updating = false;
                }

                target: easingRoot.appSettings
            }

        }

    }

    // ── Amplitude (elastic + bounce) ────────────────────────────────────────
    SettingsRow {
        visible: curveInfo.isElastic || curveInfo.isBounce
        title: i18n("Amplitude")
        description: curveInfo.isElastic ? i18n("Strength of elastic overshoot") : i18n("Height of bounce peaks")

        SettingsSlider {
            enabled: easingRoot.animationsEnabled
            from: curveInfo.isElastic ? 1 : 0.5
            to: 3
            stepSize: 0.1
            value: easingRoot.easingPreview.curveAmplitude
            formatValue: function(v) {
                return v.toFixed(1);
            }
            onMoved: (value) => {
                var ct = easingRoot.easingPreview.curveType;
                var amp = value.toFixed(2);
                if (curveInfo.isElastic) {
                    var per = easingRoot.easingPreview.elasticPeriod.toFixed(2);
                    easingRoot.appSettings.animationEasingCurve = ct + ":" + amp + "," + per;
                } else {
                    easingRoot.appSettings.animationEasingCurve = ct + ":" + amp + "," + easingRoot.easingPreview.bouncesCount;
                }
            }
        }

    }

    // ── Bounces (bounce only) ───────────────────────────────────────────────
    SettingsRow {
        visible: curveInfo.isBounce
        title: i18n("Bounces")
        description: i18n("Number of bounce repetitions")

        SettingsSlider {
            enabled: easingRoot.animationsEnabled
            from: 1
            to: 8
            value: easingRoot.easingPreview.bouncesCount
            valueSuffix: ""
            onMoved: (value) => {
                var ct = easingRoot.easingPreview.curveType;
                var amp = easingRoot.easingPreview.curveAmplitude.toFixed(2);
                easingRoot.appSettings.animationEasingCurve = ct + ":" + amp + "," + Math.round(value);
            }
        }

    }

    // ── Period (elastic only) ───────────────────────────────────────────────
    SettingsRow {
        visible: curveInfo.isElastic
        title: i18n("Period")
        description: i18n("Oscillation speed — lower is faster wobble")

        SettingsSlider {
            enabled: easingRoot.animationsEnabled
            from: 0.1
            to: 1
            stepSize: 0.05
            value: easingRoot.easingPreview.elasticPeriod
            formatValue: function(v) {
                return v.toFixed(2);
            }
            onMoved: (value) => {
                var ct = easingRoot.easingPreview.curveType;
                var amp = easingRoot.easingPreview.curveAmplitude.toFixed(2);
                easingRoot.appSettings.animationEasingCurve = ct + ":" + amp + "," + value.toFixed(2);
            }
        }

    }

    SettingsSeparator {
    }

    // ── Duration ────────────────────────────────────────────────────────────
    SettingsRow {
        title: i18n("Duration")
        description: i18n("Total animation time in milliseconds")

        SettingsSlider {
            enabled: easingRoot.animationsEnabled
            from: settingsController.generalPage.animationDurationMin
            to: settingsController.generalPage.animationDurationMax
            stepSize: 10
            value: easingRoot.appSettings.animationDuration
            valueSuffix: " ms"
            labelWidth: Kirigami.Units.gridUnit * 4
            onMoved: (value) => {
                return easingRoot.appSettings.animationDuration = Math.round(value);
            }
        }

    }

    SettingsSeparator {
    }

    // ── Multiple windows ────────────────────────────────────────────────────
    SettingsRow {
        title: i18n("Multiple windows")
        description: i18n("How to animate when moving several windows at once")

        WideComboBox {
            Accessible.name: i18n("Multiple windows")
            enabled: easingRoot.animationsEnabled
            model: [i18n("All at once"), i18n("One by one")]
            currentIndex: easingRoot.appSettings.animationSequenceMode
            onActivated: (index) => {
                return easingRoot.appSettings.animationSequenceMode = index;
            }
        }

    }

    // ── Stagger interval (one by one only) ──────────────────────────────────
    SettingsRow {
        visible: easingRoot.appSettings.animationSequenceMode === 1
        title: i18n("Stagger delay")
        description: i18n("Pause between each window's animation start")

        SettingsSlider {
            enabled: easingRoot.animationsEnabled
            from: settingsController.generalPage.animationStaggerIntervalMin
            to: settingsController.generalPage.animationStaggerIntervalMax
            stepSize: 10
            value: easingRoot.appSettings.animationStaggerInterval
            valueSuffix: " ms"
            labelWidth: Kirigami.Units.gridUnit * 4
            onMoved: (value) => {
                return easingRoot.appSettings.animationStaggerInterval = Math.round(value);
            }
        }

    }

    SettingsSeparator {
    }

    // ── Minimum distance ────────────────────────────────────────────────────
    SettingsRow {
        title: i18n("Minimum distance")
        description: easingRoot.appSettings.animationMinDistance === 0 ? i18n("Currently: always animate, no threshold") : i18n("Skip animation when geometry changes less than this")

        SettingsSpinBox {
            enabled: easingRoot.animationsEnabled
            from: settingsController.generalPage.animationMinDistanceMin
            to: settingsController.generalPage.animationMinDistanceMax
            stepSize: 5
            value: easingRoot.appSettings.animationMinDistance
            onValueModified: (value) => {
                return easingRoot.appSettings.animationMinDistance = value;
            }
        }

    }

}
