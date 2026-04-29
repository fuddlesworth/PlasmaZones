// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    readonly property var settingsBridge: settingsController.snappingEffectsPage
    readonly property var animBridge: settingsController.animationPage

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // EFFECTS
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: effectsCard.implicitHeight

            SettingsCard {
                id: effectsCard

                anchors.fill: parent
                headerText: i18n("Effects")
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Blur behind zones")
                        description: i18n("Apply a blur effect to the area behind zone overlays")

                        SettingsSwitch {
                            checked: appSettings.enableBlur
                            accessibleName: i18n("Enable blur behind zones")
                            onToggled: function (newValue) {
                                appSettings.enableBlur = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Zone numbers")
                        description: i18n("Display a number label inside each zone")

                        SettingsSwitch {
                            checked: appSettings.showZoneNumbers
                            accessibleName: i18n("Show zone numbers")
                            onToggled: function (newValue) {
                                appSettings.showZoneNumbers = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Flash on layout switch")
                        description: i18n("Briefly flash zones when switching between layouts")

                        SettingsSwitch {
                            checked: appSettings.flashZonesOnSwitch
                            accessibleName: i18n("Flash zones on layout switch")
                            onToggled: function (newValue) {
                                appSettings.flashZonesOnSwitch = newValue;
                            }
                        }
                    }
                }
            }
        }

        // =================================================================
        // SHADER EFFECTS
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: shaderCard.implicitHeight

            SettingsCard {
                id: shaderCard

                anchors.fill: parent
                headerText: i18n("Shader Effects")
                showToggle: true
                toggleChecked: appSettings.enableShaderEffects
                collapsible: true
                onToggleClicked: checked => {
                    return appSettings.enableShaderEffects = checked;
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Frame rate")
                        description: i18n("Target refresh rate for shader animations")

                        SettingsSlider {
                            from: root.settingsBridge.shaderFrameRateMin
                            to: root.settingsBridge.shaderFrameRateMax
                            value: appSettings.shaderFrameRate
                            valueSuffix: " fps"
                            labelWidth: 55
                            onMoved: value => {
                                return appSettings.shaderFrameRate = Math.round(value);
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Audio spectrum")
                        description: root.settingsBridge.cavaAvailable ? i18n("Feed audio spectrum data to shaders that support it") : i18n("CAVA is not installed — install cava to enable audio visualization")

                        SettingsSwitch {
                            id: audioVizSwitch

                            enabled: root.settingsBridge.cavaAvailable
                            checked: appSettings.enableAudioVisualizer
                            accessibleName: i18n("Enable CAVA audio spectrum")
                            onToggled: function (newValue) {
                                appSettings.enableAudioVisualizer = newValue;
                            }
                        }
                    }

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        type: Kirigami.MessageType.Warning
                        text: i18n("CAVA is not installed. Install the <b>cava</b> package to enable audio-reactive shader effects.")
                        visible: !root.settingsBridge.cavaAvailable && shaderCard.toggleChecked
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Spectrum bars")
                        description: i18n("Number of frequency bands in the audio visualization")
                        enabled: audioVizSwitch.checked && root.settingsBridge.cavaAvailable

                        SettingsSlider {
                            from: root.settingsBridge.audioSpectrumBarCountMin
                            to: root.settingsBridge.audioSpectrumBarCountMax
                            stepSize: 2
                            value: appSettings.audioSpectrumBarCount
                            valueSuffix: ""
                            labelWidth: 55
                            onMoved: value => {
                                return appSettings.audioSpectrumBarCount = Math.round(value);
                            }
                        }
                    }
                }
            }
        }

        // =================================================================
        // WINDOW TRANSITIONS (Phase 6 — shader-backed animation effects)
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: transitionsCard.implicitHeight

            SettingsCard {
                id: transitionsCard

                anchors.fill: parent
                headerText: i18n("Window Transitions")
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        Layout.fillWidth: true
                        text: i18n("Select a shader transition effect for each event category. Effects are applied during overlay show/hide and window snap animations.")
                        wrapMode: Text.WordWrap
                        color: Kirigami.Theme.disabledTextColor
                        font: Kirigami.Theme.smallFont
                    }

                    SettingsSeparator {}

                    TransitionEffectRow {
                        eventPath: "zone.snapIn"
                        eventLabel: i18n("Zone Snap In")
                        eventDescription: i18n("When a window snaps into a zone")
                    }

                    SettingsSeparator {}

                    TransitionEffectRow {
                        eventPath: "zone.snapOut"
                        eventLabel: i18n("Zone Snap Out")
                        eventDescription: i18n("When a window leaves a zone")
                    }

                    SettingsSeparator {}

                    TransitionEffectRow {
                        eventPath: "osd.show"
                        eventLabel: i18n("OSD Show")
                        eventDescription: i18n("When the on-screen display appears")
                    }

                    SettingsSeparator {}

                    TransitionEffectRow {
                        eventPath: "osd.hide"
                        eventLabel: i18n("OSD Hide")
                        eventDescription: i18n("When the on-screen display disappears")
                    }

                    SettingsSeparator {}

                    TransitionEffectRow {
                        eventPath: "zone.highlight"
                        eventLabel: i18n("Zone Highlight")
                        eventDescription: i18n("When zones highlight during drag")
                    }

                    SettingsSeparator {}

                    TransitionEffectRow {
                        eventPath: "zone.layoutSwitchIn"
                        eventLabel: i18n("Layout Switch In")
                        eventDescription: i18n("When switching to a new zone layout")
                    }
                }
            }
        }
    }

    component TransitionEffectRow: SettingsRow {
        id: effectRow

        required property string eventPath
        required property string eventLabel
        required property string eventDescription

        title: effectRow.eventLabel
        description: effectRow.eventDescription

        RowLayout {
            spacing: Kirigami.Units.smallSpacing

            ComboBox {
                id: effectCombo

                Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                Accessible.name: i18n("Transition effect for %1", effectRow.eventLabel)

                model: {
                    var items = [
                        {
                            text: i18n("None"),
                            value: ""
                        }
                    ];
                    var effects = root.animBridge.availableTransitionEffects;
                    for (var i = 0; i < effects.length; i++) {
                        items.push({
                            text: effects[i].name,
                            value: effects[i].id
                        });
                    }
                    return items;
                }

                textRole: "text"
                valueRole: "value"

                Component.onCompleted: refreshSelection()

                function refreshSelection() {
                    var current = root.animBridge.effectForPath(effectRow.eventPath);
                    for (var i = 0; i < model.length; i++) {
                        if (model[i].value === current) {
                            currentIndex = i;
                            return;
                        }
                    }
                    currentIndex = 0;
                }

                onActivated: {
                    var selected = model[currentIndex].value;
                    if (selected === "") {
                        root.animBridge.clearEffectForPath(effectRow.eventPath);
                    } else {
                        root.animBridge.setEffectForPath(effectRow.eventPath, selected);
                    }
                }
            }

            Label {
                text: root.animBridge.parentChainForEvent(effectRow.eventPath)
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
                visible: text.length > 0
            }
        }

        Connections {
            target: appSettings
            function onShaderProfileTreeChanged() {
                effectCombo.refreshSelection();
            }
        }
    }
}
