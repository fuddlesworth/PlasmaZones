// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

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
                            onToggled: appSettings.enableBlur = checked
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Zone numbers")
                        description: i18n("Display a number label inside each zone")

                        SettingsSwitch {
                            checked: appSettings.showZoneNumbers
                            accessibleName: i18n("Show zone numbers")
                            onToggled: appSettings.showZoneNumbers = checked
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Flash on layout switch")
                        description: i18n("Briefly flash zones when switching between layouts")

                        SettingsSwitch {
                            checked: appSettings.flashZonesOnSwitch
                            accessibleName: i18n("Flash zones on layout switch")
                            onToggled: appSettings.flashZonesOnSwitch = checked
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
                onToggleClicked: (checked) => {
                    return appSettings.enableShaderEffects = checked;
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Frame rate")
                        description: i18n("Target refresh rate for shader animations")

                        SettingsSlider {
                            from: settingsController.shaderFrameRateMin
                            to: settingsController.shaderFrameRateMax
                            value: appSettings.shaderFrameRate
                            valueSuffix: " fps"
                            labelWidth: 55
                            onMoved: (value) => {
                                return appSettings.shaderFrameRate = Math.round(value);
                            }
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Audio spectrum")
                        description: settingsController.cavaAvailable ? i18n("Feed audio spectrum data to shaders that support it") : i18n("CAVA is not installed — install cava to enable audio visualization")

                        SettingsSwitch {
                            id: audioVizSwitch

                            enabled: settingsController.cavaAvailable
                            checked: appSettings.enableAudioVisualizer
                            accessibleName: i18n("Enable CAVA audio spectrum")
                            onToggled: appSettings.enableAudioVisualizer = checked
                        }

                    }

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        type: Kirigami.MessageType.Warning
                        text: i18n("CAVA is not installed. Install the <b>cava</b> package to enable audio-reactive shader effects.")
                        visible: !settingsController.cavaAvailable && shaderCard.toggleChecked
                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Spectrum bars")
                        description: i18n("Number of frequency bands in the audio visualization")
                        enabled: audioVizSwitch.checked && settingsController.cavaAvailable

                        SettingsSlider {
                            from: settingsController.audioSpectrumBarCountMin
                            to: settingsController.audioSpectrumBarCountMax
                            stepSize: 2
                            value: appSettings.audioSpectrumBarCount
                            valueSuffix: ""
                            labelWidth: 55
                            onMoved: (value) => {
                                return appSettings.audioSpectrumBarCount = Math.round(value);
                            }
                        }

                    }

                }

            }

        }

    }

}
