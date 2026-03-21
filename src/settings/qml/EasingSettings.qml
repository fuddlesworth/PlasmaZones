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
 *   - kcm: the KCM backend object
 *   - constants: root object providing sliderPreferredWidth, sliderValueLabelWidth
 *   - animationsEnabled: whether animations are currently enabled
 *   - easingPreview: the EasingPreview component (for curveType, elasticAmplitude, etc.)
 */
Kirigami.FormLayout {
    id: easingRoot

    required property var kcm
    required property var constants
    required property bool animationsEnabled
    required property var easingPreview

    // Easing style + direction selector (two dropdowns)
    // Curve lookup table: styles x directions -> bezier/named curve strings
    QtObject {
        id: easingData

        // Style definitions: user-friendly name, technical name, curves per direction
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
        // Bezier curves: style -> { in, out, in-out }
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

        // Normalize curve for comparison (2 decimal bezier, canonical named)
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

        // Reverse-lookup: curve string -> { styleIndex, dirIndex }
        function findIndices(curve) {
            var norm = normalizeCurve(curve);
            if (norm === "")
                return {
                "styleIndex": 0,
                "dirIndex": 1
            };

            // Custom, Ease Out
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
            }; // Custom
        }

        // Build curve string from style + direction indices
        function curveForSelection(styleIndex, dirIndex) {
            if (styleIndex <= 0 || styleIndex >= styles.length)
                return "";

            var key = styles[styleIndex].key;
            var dirKey = directions[dirIndex].key;
            return curves[key] ? curves[key][dirKey] : "";
        }

    }

    // Style selector
    WideComboBox {
        id: easingStyleCombo

        property bool updating: false

        Kirigami.FormData.label: i18n("Style:")
        enabled: easingRoot.animationsEnabled
        model: easingData.styles.map((s) => {
            return s.label;
        })
        currentIndex: easingData.findIndices(easingRoot.kcm.animationEasingCurve).styleIndex
        onActivated: (index) => {
            if (updating)
                return ;

            if (index <= 0)
                return ;

            // Custom -- don't change curve
            var curve = easingData.curveForSelection(index, easingDirectionCombo.currentIndex);
            if (curve)
                easingRoot.kcm.animationEasingCurve = curve;

        }
        ToolTip.visible: hovered
        ToolTip.text: i18n("Animation curve style -- controls how acceleration feels")

        Connections {
            function onAnimationEasingCurveChanged() {
                easingStyleCombo.updating = true;
                var idx = easingData.findIndices(easingRoot.kcm.animationEasingCurve);
                easingStyleCombo.currentIndex = idx.styleIndex;
                easingStyleCombo.updating = false;
            }

            target: easingRoot.kcm
        }

    }

    // Direction selector
    WideComboBox {
        id: easingDirectionCombo

        property bool updating: false

        Kirigami.FormData.label: i18n("Direction:")
        enabled: easingRoot.animationsEnabled && easingStyleCombo.currentIndex > 0
        opacity: easingStyleCombo.currentIndex > 0 ? 1 : 0.4
        model: easingData.directions.map((d) => {
            return d.label;
        })
        currentIndex: easingData.findIndices(easingRoot.kcm.animationEasingCurve).dirIndex
        onActivated: (index) => {
            if (updating)
                return ;

            var styleIdx = easingStyleCombo.currentIndex;
            if (styleIdx <= 0)
                return ;

            var curve = easingData.curveForSelection(styleIdx, index);
            if (curve)
                easingRoot.kcm.animationEasingCurve = curve;

        }
        ToolTip.visible: hovered
        ToolTip.text: i18n("Ease In accelerates from rest, Ease Out decelerates to rest, In-Out does both")

        Connections {
            function onAnimationEasingCurveChanged() {
                easingDirectionCombo.updating = true;
                var idx = easingData.findIndices(easingRoot.kcm.animationEasingCurve);
                easingDirectionCombo.currentIndex = idx.dirIndex;
                easingDirectionCombo.updating = false;
            }

            target: easingRoot.kcm
        }

    }

    // Amplitude slider (visible for elastic and bounce presets)
    RowLayout {
        id: amplitudeRow

        readonly property bool isElastic: easingRoot.easingPreview.curveType === "elastic-in" || easingRoot.easingPreview.curveType === "elastic-out" || easingRoot.easingPreview.curveType === "elastic-in-out"
        readonly property bool isBounce: easingRoot.easingPreview.curveType === "bounce-in" || easingRoot.easingPreview.curveType === "bounce-out" || easingRoot.easingPreview.curveType === "bounce-in-out"

        Kirigami.FormData.label: i18n("Amplitude:")
        visible: isElastic || isBounce
        enabled: easingRoot.animationsEnabled
        spacing: Kirigami.Units.smallSpacing

        Slider {
            id: elasticAmplitudeSlider

            Layout.preferredWidth: easingRoot.constants.sliderPreferredWidth
            from: amplitudeRow.isElastic ? 1 : 0.5
            to: 3
            stepSize: 0.1
            value: easingRoot.easingPreview.elasticAmplitude
            onMoved: {
                var ct = easingRoot.easingPreview.curveType;
                var amp = value.toFixed(2);
                if (amplitudeRow.isElastic) {
                    var per = easingRoot.easingPreview.elasticPeriod.toFixed(2);
                    easingRoot.kcm.animationEasingCurve = ct + ":" + amp + "," + per;
                } else {
                    easingRoot.kcm.animationEasingCurve = ct + ":" + amp + "," + easingRoot.easingPreview.bouncesCount;
                }
            }
            Accessible.name: i18n("Amplitude")
            ToolTip.visible: hovered
            ToolTip.text: amplitudeRow.isBounce ? i18n("Controls the bounce height -- higher values exaggerate bounces, lower values flatten them (1.0 = standard)") : i18n("Controls the overshoot intensity of the elastic animation (1.0 = standard)")
        }

        Label {
            text: elasticAmplitudeSlider.value.toFixed(1)
            Layout.preferredWidth: easingRoot.constants.sliderValueLabelWidth
        }

    }

    // Bounce count slider (visible for bounce presets)
    RowLayout {
        Kirigami.FormData.label: i18n("Bounces:")
        visible: amplitudeRow.isBounce
        enabled: easingRoot.animationsEnabled
        spacing: Kirigami.Units.smallSpacing

        Slider {
            id: bouncesSlider

            Layout.preferredWidth: easingRoot.constants.sliderPreferredWidth
            from: 1
            to: 8
            stepSize: 1
            value: easingRoot.easingPreview.bouncesCount
            onMoved: {
                var ct = easingRoot.easingPreview.curveType;
                var amp = easingRoot.easingPreview.elasticAmplitude.toFixed(2);
                easingRoot.kcm.animationEasingCurve = ct + ":" + amp + "," + Math.round(value);
            }
            Accessible.name: i18n("Number of bounces")
            ToolTip.visible: hovered
            ToolTip.text: i18n("Number of bounces before settling -- fewer bounces feel snappier, more feel bouncier")
        }

        Label {
            text: Math.round(bouncesSlider.value).toString()
            Layout.preferredWidth: easingRoot.constants.sliderValueLabelWidth
        }

    }

    // Elastic period slider
    RowLayout {
        Kirigami.FormData.label: i18n("Period:")
        visible: easingRoot.easingPreview.curveType === "elastic-in" || easingRoot.easingPreview.curveType === "elastic-out" || easingRoot.easingPreview.curveType === "elastic-in-out"
        enabled: easingRoot.animationsEnabled
        spacing: Kirigami.Units.smallSpacing

        Slider {
            id: elasticPeriodSlider

            Layout.preferredWidth: easingRoot.constants.sliderPreferredWidth
            from: 0.1
            to: 1
            stepSize: 0.05
            value: easingRoot.easingPreview.elasticPeriod
            onMoved: {
                var ct = easingRoot.easingPreview.curveType;
                var amp = easingRoot.easingPreview.elasticAmplitude.toFixed(2);
                var per = value.toFixed(2);
                easingRoot.kcm.animationEasingCurve = ct + ":" + amp + "," + per;
            }
            Accessible.name: i18n("Elastic period")
            ToolTip.visible: hovered
            ToolTip.text: i18n("Controls the oscillation frequency -- lower values produce tighter, faster bounces")
        }

        Label {
            text: elasticPeriodSlider.value.toFixed(2)
            Layout.preferredWidth: easingRoot.constants.sliderValueLabelWidth
        }

    }

    // Duration slider
    RowLayout {
        Kirigami.FormData.label: i18n("Duration:")
        enabled: easingRoot.animationsEnabled
        spacing: Kirigami.Units.smallSpacing

        Slider {
            id: animationDurationSlider

            Layout.preferredWidth: easingRoot.constants.sliderPreferredWidth
            from: 50
            to: 500
            stepSize: 10
            value: easingRoot.kcm.animationDuration
            onMoved: easingRoot.kcm.animationDuration = Math.round(value)
            Accessible.name: i18n("Animation duration")
            ToolTip.visible: hovered
            ToolTip.text: i18n("How long window animations take to complete (milliseconds)")
        }

        Label {
            text: Math.round(animationDurationSlider.value) + " ms"
            Layout.preferredWidth: easingRoot.constants.sliderValueLabelWidth + 15
        }

    }

    // Sequence mode (all at once vs one by one)
    WideComboBox {
        Kirigami.FormData.label: i18n("Multiple windows:")
        enabled: easingRoot.animationsEnabled
        model: [i18n("Animate all at once"), i18n("Animate one by one (zone order)")]
        currentIndex: easingRoot.kcm.animationSequenceMode
        onActivated: (index) => {
            return easingRoot.kcm.animationSequenceMode = index;
        }
        ToolTip.visible: hovered
        ToolTip.text: i18n("When moving multiple windows (resnap, snap all, autotile, etc.), animate them all together or one after another in zone order.")
    }

    // Stagger interval (only relevant when one by one)
    RowLayout {
        Kirigami.FormData.label: i18n("Delay between windows:")
        visible: easingRoot.kcm.animationSequenceMode === 1
        enabled: easingRoot.animationsEnabled
        spacing: Kirigami.Units.smallSpacing

        Slider {
            Layout.preferredWidth: easingRoot.constants.sliderPreferredWidth
            from: 10
            to: easingRoot.kcm.animationStaggerIntervalMax !== undefined ? easingRoot.kcm.animationStaggerIntervalMax : 200
            stepSize: 10
            value: easingRoot.kcm.animationStaggerInterval
            onMoved: easingRoot.kcm.animationStaggerInterval = Math.round(value)
            Accessible.name: i18n("Delay between each window starting its animation")
            ToolTip.visible: hovered
            ToolTip.text: i18n("When animating one by one: milliseconds between each window starting. Lower values create a fast cascading effect with overlapping animations.")
        }

        Label {
            text: i18n("%1 ms", easingRoot.kcm.animationStaggerInterval)
            Layout.preferredWidth: easingRoot.constants.sliderValueLabelWidth + 15
        }

    }

    // Minimum distance threshold
    RowLayout {
        Kirigami.FormData.label: i18n("Minimum distance:")
        enabled: easingRoot.animationsEnabled
        spacing: Kirigami.Units.smallSpacing

        SpinBox {
            from: 0
            to: 200
            stepSize: 5
            value: easingRoot.kcm.animationMinDistance
            onValueModified: easingRoot.kcm.animationMinDistance = value
            Accessible.name: i18n("Minimum distance")
            ToolTip.visible: hovered
            ToolTip.text: i18n("Skip animation when the geometry change is smaller than this many pixels. Prevents jittery micro-animations.")
        }

        Label {
            text: i18n("px")
        }

        Label {
            text: easingRoot.kcm.animationMinDistance === 0 ? i18n("(always animate)") : ""
            opacity: 0.6
            font.italic: true
        }

    }

}
