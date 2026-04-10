// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    readonly property var settingsBridge: appSettings

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =====================================================================
        // ON-SCREEN DISPLAY CARD
        // =====================================================================
        OsdCard {
            Layout.fillWidth: true
            appSettings: root.settingsBridge
        }

        // =====================================================================
        // RENDERING CARD
        // =====================================================================
        SettingsCard {
            headerText: i18n("Rendering")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Rendering backend")
                    description: i18n("Graphics API used for overlay rendering")

                    ComboBox {
                        id: renderingBackendCombo

                        function syncIndex() {
                            currentIndex = Math.max(0, settingsController.renderingBackendOptions.indexOf(appSettings.renderingBackend));
                        }

                        enabled: !settingsController.daemonRunning
                        Accessible.name: i18n("Rendering backend")
                        model: settingsController.renderingBackendDisplayNames
                        currentIndex: Math.max(0, settingsController.renderingBackendOptions.indexOf(appSettings.renderingBackend))
                        onActivated: (index) => {
                            if (index >= 0 && index < settingsController.renderingBackendOptions.length)
                                appSettings.renderingBackend = settingsController.renderingBackendOptions[index];

                        }

                        Connections {
                            function onRenderingBackendChanged() {
                                renderingBackendCombo.syncIndex();
                            }

                            target: appSettings
                        }

                    }

                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    type: Kirigami.MessageType.Information
                    text: settingsController.daemonRunning ? i18n("Stop the daemon to change the rendering backend.") : i18n("Rendering backend changes take effect after restarting the daemon.")
                    visible: settingsController.daemonRunning || appSettings.renderingBackend !== settingsController.startupRenderingBackend
                }

            }

        }

        // =====================================================================
        // CONFIGURATION CARD
        // =====================================================================
        SettingsCard {
            headerText: i18n("Configuration")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Backup")
                    description: i18n("Export all settings to a file")

                    Button {
                        text: i18n("Export Settings")
                        icon.name: "document-export"
                        onClicked: exportConfigDialog.open()
                    }

                }

                SettingsSeparator {
                }

                SettingsRow {
                    title: i18n("Restore")
                    description: i18n("Import settings from a previously exported file")

                    Button {
                        text: i18n("Import Settings")
                        icon.name: "document-import"
                        onClicked: importConfigDialog.open()
                    }

                }

            }

        }

    }

    FileDialog {
        id: exportConfigDialog

        title: i18n("Export Settings")
        nameFilters: [i18n("PlasmaZones Config (*.json)"), i18n("All files (*)")]
        defaultSuffix: "json"
        fileMode: FileDialog.SaveFile
        onAccepted: settingsController.exportAllSettings(selectedFile.toString().replace(/^file:\/\/+/, "/"))
    }

    FileDialog {
        id: importConfigDialog

        title: i18n("Import Settings")
        nameFilters: [i18n("PlasmaZones Config (*.json *.conf *.ini *.rc)"), i18n("All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: settingsController.importAllSettings(selectedFile.toString().replace(/^file:\/\/+/, "/"))
    }

}
