// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

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

                    // Cubic bezier curve editor with animated preview
                    EasingPreview {
                        id: easingPreview
                        Layout.fillWidth: true
                        Layout.maximumWidth: 500
                        Layout.alignment: Qt.AlignHCenter
                        curve: kcm.animationEasingCurve
                        animationDuration: kcm.animationDuration
                        previewEnabled: animationsEnabledCheck.checked
                        opacity: animationsEnabledCheck.checked ? 1.0 : 0.4
                        onCurveEdited: function(newCurve) {
                            kcm.animationEasingCurve = newCurve
                        }
                    }

                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    Kirigami.FormLayout {
                        Layout.fillWidth: true

                        // Preset selector
                        ComboBox {
                            id: easingPresetCombo
                            Kirigami.FormData.label: i18n("Preset:")
                            enabled: animationsEnabledCheck.checked

                            readonly property var presets: [
                                { name: i18n("Custom"), curve: "" },
                                { name: i18n("Linear"), curve: "0.00,0.00,1.00,1.00" },
                                { name: i18n("Ease Out (Quad)"), curve: "0.25,0.46,0.45,0.94" },
                                { name: i18n("Ease Out (Cubic)"), curve: "0.33,1.00,0.68,1.00" },
                                { name: i18n("Ease In-Out (Cubic)"), curve: "0.65,0.00,0.35,1.00" },
                                { name: i18n("Overshoot"), curve: "0.18,0.89,0.32,1.28" },
                                { name: i18n("Ease Out (Quart)"), curve: "0.25,1.00,0.50,1.00" },
                                { name: i18n("Ease Out (Quint)"), curve: "0.23,1.00,0.32,1.00" },
                                { name: i18n("Ease Out (Expo)"), curve: "0.16,1.00,0.30,1.00" },
                                { name: i18n("Ease In-Out (Quad)"), curve: "0.46,0.03,0.52,0.96" },
                                { name: i18n("Overshoot (In-Out)"), curve: "0.68,-0.55,0.27,1.55" }
                            ]

                            model: presets.map(p => p.name)

                            // Normalize curve string to "x1,y1,x2,y2" with 2 decimals for comparison
                            function normalizeCurve(curve) {
                                if (!curve || curve === "") return ""
                                var parts = curve.split(",")
                                if (parts.length !== 4) return ""
                                var x1 = parseFloat(parts[0])
                                var y1 = parseFloat(parts[1])
                                var x2 = parseFloat(parts[2])
                                var y2 = parseFloat(parts[3])
                                if (!isFinite(x1) || !isFinite(y1) || !isFinite(x2) || !isFinite(y2))
                                    return ""
                                return x1.toFixed(2) + "," + y1.toFixed(2) + "," + x2.toFixed(2) + "," + y2.toFixed(2)
                            }

                            // Find matching preset for current curve (normalized comparison)
                            function findPresetIndex(curve) {
                                var norm = normalizeCurve(curve)
                                if (norm === "") return 0
                                for (var i = 1; i < presets.length; i++) {
                                    if (normalizeCurve(presets[i].curve) === norm)
                                        return i
                                }
                                return 0 // Custom
                            }

                            currentIndex: findPresetIndex(kcm.animationEasingCurve)

                            onActivated: (index) => {
                                if (index > 0) {
                                    kcm.animationEasingCurve = presets[index].curve
                                }
                            }

                            // Update when curve changes externally (e.g. from editor drag)
                            Connections {
                                target: kcm
                                function onAnimationEasingCurveChanged() {
                                    easingPresetCombo.currentIndex = easingPresetCombo.findPresetIndex(kcm.animationEasingCurve)
                                }
                            }

                            ToolTip.visible: hovered
                            ToolTip.text: i18n("Select a preset or drag control points to customize")
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

                        // Minimum distance threshold
                        RowLayout {
                            Kirigami.FormData.label: i18n("Min. distance:")
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

                    ComboBox {
                        id: osdStyleCombo
                        Kirigami.FormData.label: i18n("OSD style:")
                        enabled: showOsdCheckbox.checked || kcm.showNavigationOsd

                        readonly property int osdStyleNone: 0
                        readonly property int osdStyleText: 1
                        readonly property int osdStylePreview: 2

                        currentIndex: Math.max(0, Math.min(kcm.osdStyle, 2))
                        model: [
                            i18n("None"),
                            i18n("Text only"),
                            i18n("Visual preview")
                        ]
                        onActivated: (index) => {
                            kcm.osdStyle = index
                        }

                        ToolTip.visible: hovered
                        ToolTip.text: currentIndex === 0
                            ? i18n("No OSD shown. Enable layout switch or keyboard navigation above to show OSD.")
                            : (currentIndex === 1 ? i18n("Show layout name as text only") : i18n("Show visual layout preview"))
                    }
                }
            }
        }
    }
}
