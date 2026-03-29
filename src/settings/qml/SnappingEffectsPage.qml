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

        // =====================================================================
        // EFFECTS
        // =====================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: effectsCard.implicitHeight

            SettingsCard {
                id: effectsCard

                anchors.fill: parent
                headerText: i18n("Effects")
                collapsible: true

                contentItem: Kirigami.FormLayout {
                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Visual Effects")
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Blur:")
                        text: i18n("Enable blur behind zones")
                        checked: appSettings.enableBlur
                        onToggled: appSettings.enableBlur = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Numbers:")
                        text: i18n("Show zone numbers")
                        checked: appSettings.showZoneNumbers
                        onToggled: appSettings.showZoneNumbers = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Animation:")
                        text: i18n("Flash zones when switching layouts")
                        checked: appSettings.flashZonesOnSwitch
                        onToggled: appSettings.flashZonesOnSwitch = checked
                    }

                }

            }

        }

        // =====================================================================
        // SHADER EFFECTS
        // =====================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: shaderCard.implicitHeight

            SettingsCard {
                id: shaderCard

                anchors.fill: parent
                headerText: i18n("Shader Effects")
                collapsible: true

                contentItem: Kirigami.FormLayout {
                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Shader Effects")
                    }

                    CheckBox {
                        id: shaderEffectsCheck

                        Kirigami.FormData.label: i18n("Shaders:")
                        text: i18n("Enable shader effects")
                        checked: appSettings.enableShaderEffects
                        onToggled: appSettings.enableShaderEffects = checked
                    }

                    ComboBox {
                        id: renderingBackendCombo

                        // Snapshot of the backend value when this page was created.
                        // Note: if the user changes, saves, closes, and re-opens settings
                        // without restarting the daemon, this re-snapshots the saved value
                        // so the "restart required" message won't re-appear. Tracking the
                        // daemon's active backend would require a D-Bus query (future work).
                        property string initialBackend: ""

                        function syncIndex() {
                            currentIndex = Math.max(0, settingsController.renderingBackendOptions.indexOf(appSettings.renderingBackend));
                        }

                        Kirigami.FormData.label: i18n("Rendering backend:")
                        Accessible.name: i18n("Rendering backend")
                        enabled: shaderEffectsCheck.checked
                        opacity: enabled ? 1 : 0.4
                        model: settingsController.renderingBackendOptions
                        displayText: {
                            switch (currentText) {
                            case "auto":
                                return i18n("Automatic");
                            case "vulkan":
                                return i18n("Vulkan");
                            case "opengl":
                                return i18n("OpenGL");
                            default:
                                return currentText;
                            }
                        }
                        currentIndex: Math.max(0, settingsController.renderingBackendOptions.indexOf(appSettings.renderingBackend))
                        onActivated: (index) => {
                            appSettings.renderingBackend = settingsController.renderingBackendOptions[index];
                        }
                        Component.onCompleted: initialBackend = appSettings.renderingBackend

                        Connections {
                            function onRenderingBackendChanged() {
                                renderingBackendCombo.syncIndex();
                            }

                            target: appSettings
                        }

                    }

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        type: Kirigami.MessageType.Information
                        text: i18n("Rendering backend changes take effect after restarting the daemon.")
                        visible: shaderEffectsCheck.checked && appSettings.renderingBackend !== renderingBackendCombo.initialBackend
                    }

                    SettingsSlider {
                        formLabel: i18n("Frame rate:")
                        enabled: shaderEffectsCheck.checked
                        opacity: enabled ? 1 : 0.4
                        from: settingsController.shaderFrameRateMin
                        to: settingsController.shaderFrameRateMax
                        value: appSettings.shaderFrameRate
                        valueSuffix: " fps"
                        labelWidth: 55
                        onMoved: (value) => {
                            return appSettings.shaderFrameRate = Math.round(value);
                        }
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Audio Visualization")
                    }

                    CheckBox {
                        id: audioVizCheck

                        Kirigami.FormData.label: i18n("Audio:")
                        text: i18n("Enable CAVA audio spectrum")
                        enabled: shaderEffectsCheck.checked && settingsController.cavaAvailable
                        checked: appSettings.enableAudioVisualizer
                        onToggled: appSettings.enableAudioVisualizer = checked
                        ToolTip.visible: hovered
                        ToolTip.text: settingsController.cavaAvailable ? i18n("Feeds audio spectrum data to shaders that support it.") : i18n("CAVA is not installed. Install cava to enable audio visualization.")
                    }

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        type: Kirigami.MessageType.Warning
                        text: i18n("CAVA is not installed. Install the <b>cava</b> package to enable audio-reactive shader effects.")
                        visible: !settingsController.cavaAvailable && shaderEffectsCheck.checked
                    }

                    SettingsSlider {
                        formLabel: i18n("Spectrum bars:")
                        enabled: shaderEffectsCheck.checked && audioVizCheck.checked && settingsController.cavaAvailable
                        opacity: enabled ? 1 : 0.4
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
