// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    // Track the global timing mode for driving preview visibility
    property int globalTimingMode: 0

    contentHeight: content.implicitHeight
    clip: true
    Component.onCompleted: {
        var resolved = settingsController.resolvedProfileForEvent("global");
        globalTimingMode = resolved.timingMode ?? 0;
    }

    Connections {
        function onAnimationProfileTreeChanged() {
            var resolved = settingsController.resolvedProfileForEvent("global");
            root.globalTimingMode = resolved.timingMode ?? 0;
        }

        target: appSettings
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // MASTER ENABLE TOGGLE
        // =================================================================
        SettingsCard {
            id: masterCard

            Layout.fillWidth: true
            headerText: i18n("Animations")
            showToggle: true
            toggleChecked: appSettings.animationsEnabled
            onToggleClicked: (checked) => {
                return appSettings.animationsEnabled = checked;
            }
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                // ── Easing preview (full interactive, shown in easing mode) ──
                // Use opacity+clip instead of visible to avoid KDE desktop
                // style ComboBox null animation errors during visibility transitions
                Item {
                    Layout.fillWidth: true
                    clip: true
                    opacity: root.globalTimingMode === CurvePresets.timingModeEasing ? 1 : 0
                    Layout.preferredHeight: root.globalTimingMode === CurvePresets.timingModeEasing ? easingPreviewCentered.implicitHeight : 0
                    implicitHeight: easingPreviewCentered.implicitHeight

                    EasingPreview {
                        id: easingPreviewCentered

                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(Kirigami.Units.gridUnit * 28, parent.width)
                        curve: appSettings.animationEasingCurve
                        animationDuration: appSettings.animationDuration
                        previewEnabled: masterCard.toggleChecked
                        opacity: masterCard.toggleChecked ? 1 : 0.4
                        onCurveEdited: function(newCurve) {
                            appSettings.animationEasingCurve = newCurve;
                            // Sync drag-handle edits to the profile tree
                            var raw = settingsController.rawProfileForEvent("global");
                            if (raw.easingCurve !== newCurve) {
                                raw.easingCurve = newCurve;
                                settingsController.setEventProfile("global", raw);
                            }
                        }
                    }

                }

                // ── Spring preview (full graph, shown in spring mode) ────
                Item {
                    Layout.fillWidth: true
                    clip: true
                    opacity: root.globalTimingMode === CurvePresets.timingModeSpring ? 1 : 0
                    Layout.preferredHeight: root.globalTimingMode === CurvePresets.timingModeSpring ? springPreviewCentered.implicitHeight : 0
                    implicitHeight: springPreviewCentered.implicitHeight

                    SpringPreview {
                        id: springPreviewCentered

                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(Kirigami.Units.gridUnit * 28, parent.width)
                        dampingRatio: globalCard.currentSpringDamping
                        stiffness: globalCard.currentSpringStiffness
                        epsilon: globalCard.currentSpringEpsilon
                        previewEnabled: masterCard.toggleChecked
                        opacity: masterCard.toggleChecked ? 1 : 0.4
                    }

                }

            }

        }

        // =================================================================
        // GLOBAL PROFILE CARD (reuses AnimationEventCard — single source of truth)
        // =================================================================
        AnimationEventCard {
            id: globalCard

            Layout.fillWidth: true
            eventName: "global"
            eventLabel: i18n("Global Defaults")
            isParentNode: true
            alwaysEnabled: true
            collapsible: true
        }

        // =================================================================
        // WINDOW GEOMETRY DOMAIN DEFAULTS
        // =================================================================
        AnimationEventCard {
            Layout.fillWidth: true
            eventName: "windowGeometry"
            eventLabel: i18n("Window Geometry Defaults")
            isParentNode: true
            collapsible: true
            styleDomain: "window"
        }

        // =================================================================
        // OVERLAY DOMAIN DEFAULTS
        // =================================================================
        AnimationEventCard {
            Layout.fillWidth: true
            eventName: "overlay"
            eventLabel: i18n("Overlay Defaults")
            isParentNode: true
            collapsible: true
            styleDomain: "overlay"
        }

        // =================================================================
        // SHARED CONTROLS (flat settings, not per-event)
        // =================================================================
        SettingsCard {
            id: sharedCard

            Layout.fillWidth: true
            headerText: i18n("Multi-Window Behavior")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                SettingsRow {
                    title: i18n("Multiple windows")
                    description: i18n("How to animate when moving several windows at once")

                    WideComboBox {
                        Accessible.name: i18n("Multiple windows")
                        enabled: appSettings.animationsEnabled
                        model: [i18n("All at once"), i18n("One by one")]
                        currentIndex: appSettings.animationSequenceMode
                        onActivated: (index) => {
                            return appSettings.animationSequenceMode = index;
                        }
                    }

                }

                SettingsRow {
                    visible: appSettings.animationSequenceMode === 1
                    title: i18n("Stagger delay")
                    description: i18n("Pause between each window's animation start")

                    SettingsSlider {
                        Accessible.name: i18n("Stagger delay between animations")
                        enabled: appSettings.animationsEnabled
                        from: settingsController.animationStaggerIntervalMin
                        to: settingsController.animationStaggerIntervalMax
                        stepSize: 10
                        value: appSettings.animationStaggerInterval
                        valueSuffix: " ms"
                        labelWidth: Kirigami.Units.gridUnit * 4
                        onMoved: (value) => {
                            return appSettings.animationStaggerInterval = Math.round(value);
                        }
                    }

                }

                SettingsSeparator {
                }

                SettingsRow {
                    title: i18n("Minimum distance")
                    description: appSettings.animationMinDistance === 0 ? i18n("Currently: always animate, no threshold") : i18n("Skip animation when geometry changes less than this")

                    SettingsSpinBox {
                        Accessible.name: i18n("Minimum distance threshold")
                        enabled: appSettings.animationsEnabled
                        from: 0
                        to: settingsController.animationMinDistanceMax
                        stepSize: 5
                        value: appSettings.animationMinDistance
                        onValueModified: (value) => {
                            return appSettings.animationMinDistance = value;
                        }
                    }

                }

            }

        }

    }

}
