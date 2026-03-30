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
 *   - appSettings: the settings backend object
 *   - constants: root object providing sliderPreferredWidth, sliderValueLabelWidth
 *   - animationsEnabled: whether animations are currently enabled
 *   - easingPreview: the EasingPreview component (for curveType, elasticAmplitude, etc.)
 */
ColumnLayout {
    id: easingRoot

    required property var appSettings
    required property var constants
    required property bool animationsEnabled
    required property var easingPreview

    spacing: Kirigami.Units.smallSpacing

    // Curve lookup table: styles x directions -> bezier/named curve strings
    QtObject {
        id: easingData

        readonly property var styles: [{
            "label": i18n("Custom"),
            "key": "custom"
        }, {
            "label": i18n("Linear"),
            "key": "linear"
        }, {
            "label": i18n("Gentle (Sine)"),
            "key": "sine"
        }, {
            "label": i18n("Smooth (Quad)"),
            "key": "quad"
        }, {
            "label": i18n("Standard (Cubic)"),
            "key": "cubic"
        }, {
            "label": i18n("Snappy (Quart)"),
            "key": "quart"
        }, {
            "label": i18n("Sharp (Quint)"),
            "key": "quint"
        }, {
            "label": i18n("Aggressive (Expo)"),
            "key": "expo"
        }, {
            "label": i18n("Circular (Circ)"),
            "key": "circ"
        }, {
            "label": i18n("Overshoot (Back)"),
            "key": "back"
        }, {
            "label": i18n("Elastic"),
            "key": "elastic"
        }, {
            "label": i18n("Bounce"),
            "key": "bounce"
        }]
        readonly property var directions: [{
            "label": i18n("Ease In"),
            "key": "in"
        }, {
            "label": i18n("Ease Out"),
            "key": "out"
        }, {
            "label": i18n("Ease In-Out"),
            "key": "in-out"
        }]
        readonly property var curves: ({
            "linear": {
                "in": "0.00,0.00,1.00,1.00",
                "out": "0.00,0.00,1.00,1.00",
                "in-out": "0.00,0.00,1.00,1.00"
            },
            "sine": {
                "in": "0.12,0.00,0.39,0.00",
                "out": "0.61,1.00,0.88,1.00",
                "in-out": "0.37,0.00,0.63,1.00"
            },
            "quad": {
                "in": "0.11,0.00,0.50,0.00",
                "out": "0.50,1.00,0.89,1.00",
                "in-out": "0.45,0.00,0.55,1.00"
            },
            "cubic": {
                "in": "0.32,0.00,0.67,0.00",
                "out": "0.33,1.00,0.68,1.00",
                "in-out": "0.65,0.00,0.35,1.00"
            },
            "quart": {
                "in": "0.50,0.00,0.75,0.00",
                "out": "0.25,1.00,0.50,1.00",
                "in-out": "0.76,0.00,0.24,1.00"
            },
            "quint": {
                "in": "0.64,0.00,0.78,0.00",
                "out": "0.23,1.00,0.32,1.00",
                "in-out": "0.83,0.00,0.17,1.00"
            },
            "expo": {
                "in": "0.70,0.00,0.84,0.00",
                "out": "0.16,1.00,0.30,1.00",
                "in-out": "0.87,0.00,0.13,1.00"
            },
            "circ": {
                "in": "0.55,0.00,1.00,0.45",
                "out": "0.00,0.55,0.45,1.00",
                "in-out": "0.85,0.00,0.15,1.00"
            },
            "back": {
                "in": "0.36,0.00,0.66,-0.56",
                "out": "0.18,0.89,0.32,1.28",
                "in-out": "0.68,-0.55,0.27,1.55"
            },
            "elastic": {
                "in": "elastic-in:1.0,0.30",
                "out": "elastic-out:1.0,0.30",
                "in-out": "elastic-in-out:1.0,0.30"
            },
            "bounce": {
                "in": "bounce-in:1.0,3",
                "out": "bounce-out:1.0,3",
                "in-out": "bounce-in-out:1.0,3"
            }
        })

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

    Kirigami.Separator {
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing
        Layout.rightMargin: Kirigami.Units.largeSpacing
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
            value: easingRoot.easingPreview.elasticAmplitude
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
                var amp = easingRoot.easingPreview.elasticAmplitude.toFixed(2);
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
                var amp = easingRoot.easingPreview.elasticAmplitude.toFixed(2);
                easingRoot.appSettings.animationEasingCurve = ct + ":" + amp + "," + value.toFixed(2);
            }
        }

    }

    Kirigami.Separator {
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing
        Layout.rightMargin: Kirigami.Units.largeSpacing
    }

    // ── Duration ────────────────────────────────────────────────────────────
    SettingsRow {
        title: i18n("Duration")
        description: i18n("Total animation time in milliseconds")

        SettingsSlider {
            enabled: easingRoot.animationsEnabled
            from: settingsController.animationDurationMin
            to: settingsController.animationDurationMax
            stepSize: 10
            value: easingRoot.appSettings.animationDuration
            valueSuffix: " ms"
            labelWidth: 55
            onMoved: (value) => {
                return easingRoot.appSettings.animationDuration = Math.round(value);
            }
        }

    }

    Kirigami.Separator {
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing
        Layout.rightMargin: Kirigami.Units.largeSpacing
    }

    // ── Multiple windows ────────────────────────────────────────────────────
    SettingsRow {
        title: i18n("Multiple windows")
        description: i18n("How to animate when moving several windows at once")

        WideComboBox {
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
            from: settingsController.animationStaggerIntervalMin
            to: settingsController.animationStaggerIntervalMax
            stepSize: 10
            value: easingRoot.appSettings.animationStaggerInterval
            valueSuffix: " ms"
            labelWidth: 55
            onMoved: (value) => {
                return easingRoot.appSettings.animationStaggerInterval = Math.round(value);
            }
        }

    }

    Kirigami.Separator {
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing
        Layout.rightMargin: Kirigami.Units.largeSpacing
    }

    // ── Minimum distance ────────────────────────────────────────────────────
    SettingsRow {
        title: i18n("Minimum distance")
        description: easingRoot.appSettings.animationMinDistance === 0 ? i18n("Currently: always animate, no threshold") : i18n("Skip animation when geometry changes less than this")

        SettingsSpinBox {
            enabled: easingRoot.animationsEnabled
            from: settingsController.animationMinDistanceMin
            to: settingsController.animationMinDistanceMax
            stepSize: 5
            value: easingRoot.appSettings.animationMinDistance
            onValueModified: (value) => {
                return easingRoot.appSettings.animationMinDistance = value;
            }
        }

    }

}
