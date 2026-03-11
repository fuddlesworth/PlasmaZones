// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import ".."
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief General tab - Mode-agnostic settings (OSD, Animations)
 *
 * Contains settings that apply to both snapping and tiling modes,
 * such as on-screen display configuration and window animations.
 */
ScrollView {
    id: root

    required property var kcm
    required property QtObject constants

    clip: true
    contentWidth: availableWidth

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // ═══════════════════════════════════════════════════════════════════════
        // ANIMATIONS CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: animationsCard.implicitHeight

            Kirigami.Card {
                id: animationsCard

                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Animations")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.largeSpacing

                    // Enable toggle
                    CheckBox {
                        id: animationsEnabledCheck

                        Layout.fillWidth: true
                        text: i18n("Smooth window geometry transitions")
                        checked: kcm.animationsEnabled
                        onToggled: kcm.animationsEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Animate windows when snapping to zones or tiling. Applies to both manual snapping and autotiling.")
                    }

                    // Easing curve editor with animated preview
                    EasingPreview {
                        id: easingPreview

                        Layout.fillWidth: true
                        Layout.maximumWidth: 500
                        Layout.alignment: Qt.AlignHCenter
                        curve: kcm.animationEasingCurve
                        animationDuration: kcm.animationDuration
                        previewEnabled: animationsEnabledCheck.checked
                        opacity: animationsEnabledCheck.checked ? 1 : 0.4
                        onCurveEdited: function(newCurve) {
                            kcm.animationEasingCurve = newCurve;
                        }
                    }

                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    Kirigami.FormLayout {
                        Layout.fillWidth: true

                        // Preset selector
                        WideComboBox {
                            // --- Standard ---
                            // --- Sine ---
                            // --- Quad ---
                            // --- Cubic ---
                            // --- Quart ---
                            // --- Quint ---
                            // --- Expo ---
                            // --- Circ ---
                            // --- Back ---
                            // --- Elastic ---
                            // --- Bounce ---

                            id: easingPresetCombo

                            // Section separator sentinel: curve === "__section__"
                            readonly property var presets: [{
                                "name": i18n("Custom"),
                                "curve": ""
                            }, {
                                "name": i18n("--- Standard ---"),
                                "curve": "__section__"
                            }, {
                                "name": i18n("Linear"),
                                "curve": "0.00,0.00,1.00,1.00"
                            }, {
                                "name": i18n("--- Sine ---"),
                                "curve": "__section__"
                            }, {
                                "name": i18n("Ease In (Sine)"),
                                "curve": "0.12,0.00,0.39,0.00"
                            }, {
                                "name": i18n("Ease Out (Sine)"),
                                "curve": "0.61,1.00,0.88,1.00"
                            }, {
                                "name": i18n("Ease In-Out (Sine)"),
                                "curve": "0.37,0.00,0.63,1.00"
                            }, {
                                "name": i18n("--- Quad ---"),
                                "curve": "__section__"
                            }, {
                                "name": i18n("Ease In (Quad)"),
                                "curve": "0.11,0.00,0.50,0.00"
                            }, {
                                "name": i18n("Ease Out (Quad)"),
                                "curve": "0.50,1.00,0.89,1.00"
                            }, {
                                "name": i18n("Ease In-Out (Quad)"),
                                "curve": "0.45,0.00,0.55,1.00"
                            }, {
                                "name": i18n("--- Cubic ---"),
                                "curve": "__section__"
                            }, {
                                "name": i18n("Ease In (Cubic)"),
                                "curve": "0.32,0.00,0.67,0.00"
                            }, {
                                "name": i18n("Ease Out (Cubic)"),
                                "curve": "0.33,1.00,0.68,1.00"
                            }, {
                                "name": i18n("Ease In-Out (Cubic)"),
                                "curve": "0.65,0.00,0.35,1.00"
                            }, {
                                "name": i18n("--- Quart ---"),
                                "curve": "__section__"
                            }, {
                                "name": i18n("Ease In (Quart)"),
                                "curve": "0.50,0.00,0.75,0.00"
                            }, {
                                "name": i18n("Ease Out (Quart)"),
                                "curve": "0.25,1.00,0.50,1.00"
                            }, {
                                "name": i18n("Ease In-Out (Quart)"),
                                "curve": "0.76,0.00,0.24,1.00"
                            }, {
                                "name": i18n("--- Quint ---"),
                                "curve": "__section__"
                            }, {
                                "name": i18n("Ease In (Quint)"),
                                "curve": "0.64,0.00,0.78,0.00"
                            }, {
                                "name": i18n("Ease Out (Quint)"),
                                "curve": "0.23,1.00,0.32,1.00"
                            }, {
                                "name": i18n("Ease In-Out (Quint)"),
                                "curve": "0.83,0.00,0.17,1.00"
                            }, {
                                "name": i18n("--- Expo ---"),
                                "curve": "__section__"
                            }, {
                                "name": i18n("Ease In (Expo)"),
                                "curve": "0.70,0.00,0.84,0.00"
                            }, {
                                "name": i18n("Ease Out (Expo)"),
                                "curve": "0.16,1.00,0.30,1.00"
                            }, {
                                "name": i18n("Ease In-Out (Expo)"),
                                "curve": "0.87,0.00,0.13,1.00"
                            }, {
                                "name": i18n("--- Circ ---"),
                                "curve": "__section__"
                            }, {
                                "name": i18n("Ease In (Circ)"),
                                "curve": "0.55,0.00,1.00,0.45"
                            }, {
                                "name": i18n("Ease Out (Circ)"),
                                "curve": "0.00,0.55,0.45,1.00"
                            }, {
                                "name": i18n("Ease In-Out (Circ)"),
                                "curve": "0.85,0.00,0.15,1.00"
                            }, {
                                "name": i18n("--- Back ---"),
                                "curve": "__section__"
                            }, {
                                "name": i18n("Ease In (Back)"),
                                "curve": "0.36,0.00,0.66,-0.56"
                            }, {
                                "name": i18n("Overshoot"),
                                "curve": "0.18,0.89,0.32,1.28"
                            }, {
                                "name": i18n("Overshoot (In-Out)"),
                                "curve": "0.68,-0.55,0.27,1.55"
                            }, {
                                "name": i18n("--- Elastic ---"),
                                "curve": "__section__"
                            }, {
                                "name": i18n("Elastic In"),
                                "curve": "elastic-in:1.0,0.30"
                            }, {
                                "name": i18n("Elastic Out"),
                                "curve": "elastic-out:1.0,0.30"
                            }, {
                                "name": i18n("Elastic In-Out"),
                                "curve": "elastic-in-out:1.0,0.30"
                            }, {
                                "name": i18n("--- Bounce ---"),
                                "curve": "__section__"
                            }, {
                                "name": i18n("Bounce In"),
                                "curve": "bounce-in:1.0,3"
                            }, {
                                "name": i18n("Bounce Out"),
                                "curve": "bounce-out:1.0,3"
                            }, {
                                "name": i18n("Bounce In-Out"),
                                "curve": "bounce-in-out:1.0,3"
                            }]

                            // Normalize curve string for comparison
                            function normalizeCurve(curve) {
                                if (!curve || curve === "" || curve === "__section__")
                                    return "";

                                // Named curves: normalize by parsing and reconstructing.
                                // Skip 'e'/'E' (scientific notation) to avoid misrouting bezier strings.
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
                                    var isElastic = (name === "elastic-in" || name === "elastic-out" || name === "elastic-in-out");
                                    var isBounce = (name === "bounce-in" || name === "bounce-out" || name === "bounce-in-out");
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
                                    // Bare name without params: apply defaults for round-trip consistency
                                    if (isBounce)
                                        return name + ":1.00,3";

                                    if (isElastic)
                                        return name + ":1.00,0.30";

                                    return name;
                                }
                                // Bezier: normalize to 2 decimal places
                                var bparts = curve.split(",");
                                if (bparts.length !== 4)
                                    return "";

                                var x1 = parseFloat(bparts[0]);
                                var y1 = parseFloat(bparts[1]);
                                var x2 = parseFloat(bparts[2]);
                                var y2 = parseFloat(bparts[3]);
                                if (!isFinite(x1) || !isFinite(y1) || !isFinite(x2) || !isFinite(y2))
                                    return "";

                                return x1.toFixed(2) + "," + y1.toFixed(2) + "," + x2.toFixed(2) + "," + y2.toFixed(2);
                            }

                            // Find matching preset for current curve (normalized comparison)
                            function findPresetIndex(curve) {
                                var norm = normalizeCurve(curve);
                                if (norm === "")
                                    return 0;

                                for (var i = 1; i < presets.length; i++) {
                                    if (presets[i].curve === "__section__")
                                        continue;

                                    if (normalizeCurve(presets[i].curve) === norm)
                                        return i;

                                }
                                return 0; // Custom
                            }

                            Kirigami.FormData.label: i18n("Preset:")
                            enabled: animationsEnabledCheck.checked
                            model: presets.map((p) => {
                                return p.name;
                            })
                            currentIndex: findPresetIndex(kcm.animationEasingCurve)
                            onActivated: (index) => {
                                if (index > 0 && presets[index].curve !== "__section__")
                                    kcm.animationEasingCurve = presets[index].curve;

                            }
                            ToolTip.visible: hovered
                            ToolTip.text: i18n("Select a preset or drag control points to customize")

                            // Update when curve changes externally (e.g. from editor drag)
                            Connections {
                                function onAnimationEasingCurveChanged() {
                                    easingPresetCombo.currentIndex = easingPresetCombo.findPresetIndex(kcm.animationEasingCurve);
                                }

                                target: kcm
                            }

                            delegate: ItemDelegate {
                                width: parent ? parent.width : 0
                                text: modelData
                                enabled: easingPresetCombo.presets[index].curve !== "__section__"
                                font.bold: easingPresetCombo.presets[index].curve === "__section__"
                                font.italic: easingPresetCombo.presets[index].curve === "__section__"
                                leftPadding: easingPresetCombo.presets[index].curve === "__section__" ? Kirigami.Units.smallSpacing : Kirigami.Units.largeSpacing
                                highlighted: easingPresetCombo.highlightedIndex === index
                            }

                        }

                        // Amplitude slider (visible for elastic and bounce presets)
                        RowLayout {
                            id: amplitudeRow

                            readonly property bool isElastic: easingPreview.curveType === "elastic-in" || easingPreview.curveType === "elastic-out" || easingPreview.curveType === "elastic-in-out"
                            readonly property bool isBounce: easingPreview.curveType === "bounce-in" || easingPreview.curveType === "bounce-out" || easingPreview.curveType === "bounce-in-out"

                            Kirigami.FormData.label: i18n("Amplitude:")
                            visible: isElastic || isBounce
                            enabled: animationsEnabledCheck.checked
                            spacing: Kirigami.Units.smallSpacing

                            Slider {
                                id: elasticAmplitudeSlider

                                Layout.preferredWidth: root.constants.sliderPreferredWidth
                                from: amplitudeRow.isElastic ? 1 : 0.5
                                to: 3
                                stepSize: 0.1
                                value: easingPreview.elasticAmplitude
                                onMoved: {
                                    var ct = easingPreview.curveType;
                                    var amp = value.toFixed(2);
                                    if (amplitudeRow.isElastic) {
                                        var per = easingPreview.elasticPeriod.toFixed(2);
                                        kcm.animationEasingCurve = ct + ":" + amp + "," + per;
                                    } else {
                                        kcm.animationEasingCurve = ct + ":" + amp + "," + easingPreview.bouncesCount;
                                    }
                                }
                                Accessible.name: i18n("Amplitude")
                                ToolTip.visible: hovered
                                ToolTip.text: amplitudeRow.isBounce ? i18n("Controls the bounce height — higher values exaggerate bounces, lower values flatten them (1.0 = standard)") : i18n("Controls the overshoot intensity of the elastic animation (1.0 = standard)")
                            }

                            Label {
                                text: elasticAmplitudeSlider.value.toFixed(1)
                                Layout.preferredWidth: root.constants.sliderValueLabelWidth
                            }

                        }

                        // Bounce count slider (visible for bounce presets)
                        RowLayout {
                            Kirigami.FormData.label: i18n("Bounces:")
                            visible: amplitudeRow.isBounce
                            enabled: animationsEnabledCheck.checked
                            spacing: Kirigami.Units.smallSpacing

                            Slider {
                                id: bouncesSlider

                                Layout.preferredWidth: root.constants.sliderPreferredWidth
                                from: 1
                                to: 8
                                stepSize: 1
                                value: easingPreview.bouncesCount
                                onMoved: {
                                    var ct = easingPreview.curveType;
                                    var amp = easingPreview.elasticAmplitude.toFixed(2);
                                    kcm.animationEasingCurve = ct + ":" + amp + "," + Math.round(value);
                                }
                                Accessible.name: i18n("Number of bounces")
                                ToolTip.visible: hovered
                                ToolTip.text: i18n("Number of bounces before settling — fewer bounces feel snappier, more feel bouncier")
                            }

                            Label {
                                text: Math.round(bouncesSlider.value).toString()
                                Layout.preferredWidth: root.constants.sliderValueLabelWidth
                            }

                        }

                        // Elastic period slider
                        RowLayout {
                            Kirigami.FormData.label: i18n("Period:")
                            visible: easingPreview.curveType === "elastic-in" || easingPreview.curveType === "elastic-out" || easingPreview.curveType === "elastic-in-out"
                            enabled: animationsEnabledCheck.checked
                            spacing: Kirigami.Units.smallSpacing

                            Slider {
                                id: elasticPeriodSlider

                                Layout.preferredWidth: root.constants.sliderPreferredWidth
                                from: 0.1
                                to: 1
                                stepSize: 0.05
                                value: easingPreview.elasticPeriod
                                onMoved: {
                                    var ct = easingPreview.curveType;
                                    var amp = easingPreview.elasticAmplitude.toFixed(2);
                                    var per = value.toFixed(2);
                                    kcm.animationEasingCurve = ct + ":" + amp + "," + per;
                                }
                                Accessible.name: i18n("Elastic period")
                                ToolTip.visible: hovered
                                ToolTip.text: i18n("Controls the oscillation frequency — lower values produce tighter, faster bounces")
                            }

                            Label {
                                text: elasticPeriodSlider.value.toFixed(2)
                                Layout.preferredWidth: root.constants.sliderValueLabelWidth
                            }

                        }

                        // Duration slider
                        RowLayout {
                            Kirigami.FormData.label: i18n("Duration:")
                            enabled: animationsEnabledCheck.checked
                            spacing: Kirigami.Units.smallSpacing

                            Slider {
                                id: animationDurationSlider

                                Layout.preferredWidth: root.constants.sliderPreferredWidth
                                from: 50
                                to: 500
                                stepSize: 10
                                value: kcm.animationDuration
                                onMoved: kcm.animationDuration = Math.round(value)
                                Accessible.name: i18n("Animation duration")
                                ToolTip.visible: hovered
                                ToolTip.text: i18n("How long window animations take to complete (milliseconds)")
                            }

                            Label {
                                text: Math.round(animationDurationSlider.value) + " ms"
                                Layout.preferredWidth: root.constants.sliderValueLabelWidth + 15
                            }

                        }

                        // Sequence mode (all at once vs one by one)
                        WideComboBox {
                            Kirigami.FormData.label: i18n("Multiple windows:")
                            enabled: animationsEnabledCheck.checked
                            model: [i18n("Animate all at once"), i18n("Animate one by one (zone order)")]
                            currentIndex: kcm.animationSequenceMode
                            onActivated: (index) => {
                                return kcm.animationSequenceMode = index;
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: i18n("When moving multiple windows (resnap, snap all, autotile, etc.), animate them all together or one after another in zone order.")
                        }

                        // Stagger interval (only relevant when one by one)
                        RowLayout {
                            Kirigami.FormData.label: i18n("Delay between windows:")
                            visible: kcm.animationSequenceMode === 1
                            enabled: animationsEnabledCheck.checked
                            spacing: Kirigami.Units.smallSpacing

                            Slider {
                                Layout.preferredWidth: root.constants.sliderPreferredWidth
                                from: 10
                                to: kcm.animationStaggerIntervalMax
                                stepSize: 10
                                value: kcm.animationStaggerInterval
                                onMoved: kcm.animationStaggerInterval = Math.round(value)
                                Accessible.name: i18n("Delay between each window starting its animation")
                                ToolTip.visible: hovered
                                ToolTip.text: i18n("When animating one by one: milliseconds between each window starting. Lower values create a fast cascading effect with overlapping animations.")
                            }

                            Label {
                                text: i18n("%1 ms", kcm.animationStaggerInterval)
                                Layout.preferredWidth: root.constants.sliderValueLabelWidth + 15
                            }

                        }

                        // Minimum distance threshold
                        RowLayout {
                            Kirigami.FormData.label: i18n("Minimum distance:")
                            enabled: animationsEnabledCheck.checked
                            spacing: Kirigami.Units.smallSpacing

                            SpinBox {
                                from: 0
                                to: 200
                                stepSize: 5
                                value: kcm.animationMinDistance
                                onValueModified: kcm.animationMinDistance = value
                                ToolTip.visible: hovered
                                ToolTip.text: i18n("Skip animation when the geometry change is smaller than this many pixels. Prevents jittery micro-animations.")
                            }

                            Label {
                                text: i18n("px")
                            }

                            Label {
                                text: kcm.animationMinDistance === 0 ? i18n("(always animate)") : ""
                                opacity: 0.6
                                font.italic: true
                            }

                        }

                    }

                }

            }

        }

        // ═══════════════════════════════════════════════════════════════════════
        // ON-SCREEN DISPLAY CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: osdCard.implicitHeight

            Kirigami.Card {
                id: osdCard

                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("On-Screen Display")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        id: showOsdCheckbox

                        Kirigami.FormData.label: i18n("Layout switch:")
                        text: i18n("Show OSD when switching layouts")
                        checked: kcm.showOsdOnLayoutSwitch
                        onToggled: kcm.showOsdOnLayoutSwitch = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Keyboard navigation:")
                        text: i18n("Show OSD when using keyboard navigation")
                        checked: kcm.showNavigationOsd
                        onToggled: kcm.showNavigationOsd = checked
                    }

                    WideComboBox {
                        id: osdStyleCombo

                        readonly property int osdStyleNone: 0
                        readonly property int osdStyleText: 1
                        readonly property int osdStylePreview: 2

                        Kirigami.FormData.label: i18n("OSD style:")
                        enabled: showOsdCheckbox.checked || kcm.showNavigationOsd
                        currentIndex: Math.max(0, Math.min(kcm.osdStyle, 2))
                        model: [i18n("None"), i18n("Text only"), i18n("Visual preview")]
                        onActivated: (index) => {
                            kcm.osdStyle = index;
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: currentIndex === 0 ? i18n("No OSD shown. Enable layout switch or keyboard navigation above to show OSD.") : (currentIndex === 1 ? i18n("Show layout name as text only") : i18n("Show visual layout preview"))
                    }

                    WideComboBox {
                        Kirigami.FormData.label: i18n("Overlay style:")
                        currentIndex: Math.max(0, Math.min(kcm.overlayDisplayMode, 1))
                        model: [i18n("Full zone highlight"), i18n("Compact preview")]
                        onActivated: (index) => {
                            kcm.overlayDisplayMode = index;
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: currentIndex === 0 ? i18n("Highlight each zone as a full-size translucent rectangle while dragging") : i18n("Show a small layout thumbnail inside each zone while dragging")
                    }

                }

            }

        }

    }

}
