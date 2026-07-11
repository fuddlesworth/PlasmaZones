// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SettingsFlickable {
    id: root

    // Layout constants (previously from monolith's QtObject)
    readonly property int sliderPreferredWidth: Kirigami.Units.gridUnit * 16
    readonly property int sliderValueLabelWidth: Kirigami.Units.gridUnit * 3
    // Capture the context property so child components can access it
    readonly property var settingsBridge: appSettings
    // Frame rate + audio spectrum drive EVERY shader category (overlay,
    // animation, surface decoration), so the Shader Effects card lives here on
    // General rather than on the snapping overlay page. The backing state is
    // still the snappingEffectsPage controller (its bounds + CAVA probe are
    // global, only its name is historical).
    readonly property var effectsBridge: settingsController.snappingEffectsPage

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        // Animations editor moved to its own top-level sidebar entry
        // (Settings → Animations → General). The General page used to
        // host an inline Animations card; that's been removed in the
        // animation-UI rework so there's a single place to edit motion
        // settings rather than two paths editing the same backing fields.

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
            searchAnchor: "rendering"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Rendering backend")
                    description: i18n("Graphics API used for overlay rendering")
                    searchAnchor: "renderingBackend"

                    ComboBox {
                        id: renderingBackendCombo

                        function syncIndex() {
                            currentIndex = Math.max(0, settingsController.generalPage.renderingBackendOptions.indexOf(appSettings.renderingBackend));
                        }

                        enabled: !settingsController.daemonRunning
                        Accessible.name: i18n("Rendering backend")
                        model: settingsController.generalPage.renderingBackendDisplayNames
                        currentIndex: Math.max(0, settingsController.generalPage.renderingBackendOptions.indexOf(appSettings.renderingBackend))
                        onActivated: index => {
                            if (index >= 0 && index < settingsController.generalPage.renderingBackendOptions.length)
                                appSettings.renderingBackend = settingsController.generalPage.renderingBackendOptions[index];
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
                    // Match SettingsRow's horizontal inset so the banner lines
                    // up with the rows above it instead of running edge-to-edge
                    // to the card border.
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    type: Kirigami.MessageType.Information
                    text: settingsController.daemonRunning ? i18n("Stop the daemon to change the rendering backend.") : i18n("Rendering backend changes take effect after restarting the daemon.")
                    visible: settingsController.daemonRunning || appSettings.renderingBackend !== settingsController.generalPage.startupRenderingBackend
                }
            }
        }

        // =====================================================================
        // SHADER EFFECTS CARD
        // =====================================================================
        // Frame rate + audio spectrum apply to all shader categories (overlay,
        // animation, surface decoration), so this card is global (moved here
        // from Snapping → Overlay Appearance). The card deliberately has no
        // master toggle: shader use is decided per layout (a layout whose
        // shader is "none" draws the rectangle overlay), so a global switch
        // would gate nothing the layouts don't already control.
        SettingsCard {
            id: shaderCard

            headerText: i18n("Shader Effects")
            searchAnchor: "shaderEffects"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Frame rate")
                    searchAnchor: "frameRate"
                    description: i18n("Target refresh rate for shader animations")

                    SettingsSlider {
                        from: root.effectsBridge.shaderFrameRateMin
                        to: root.effectsBridge.shaderFrameRateMax
                        value: appSettings.shaderFrameRate
                        valueSuffix: " fps"
                        labelWidth: Kirigami.Units.gridUnit * 4
                        onMoved: value => {
                            return appSettings.shaderFrameRate = Math.round(value);
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Audio spectrum")
                    searchAnchor: "audioSpectrum"
                    description: root.effectsBridge.cavaAvailable ? i18n("Feed audio spectrum data to shaders that support it") : i18n("CAVA is not installed. Install cava to enable audio visualization.")

                    SettingsSwitch {
                        id: audioVizSwitch

                        enabled: root.effectsBridge.cavaAvailable
                        checked: appSettings.enableAudioVisualizer
                        accessibleName: i18n("Enable CAVA audio spectrum")
                        onToggled: function (newValue) {
                            appSettings.enableAudioVisualizer = newValue;
                        }
                    }
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    type: Kirigami.MessageType.Warning
                    text: i18n("CAVA is not installed. Install the <b>cava</b> package to enable audio-reactive shader effects.")
                    visible: !root.effectsBridge.cavaAvailable
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Spectrum bars")
                    searchAnchor: "spectrumBars"
                    description: i18n("Number of frequency bands in the audio visualization")
                    enabled: audioVizSwitch.checked && root.effectsBridge.cavaAvailable

                    SettingsSlider {
                        from: root.effectsBridge.audioSpectrumBarCountMin
                        to: root.effectsBridge.audioSpectrumBarCountMax
                        stepSize: 2
                        value: appSettings.audioSpectrumBarCount
                        valueSuffix: ""
                        labelWidth: Kirigami.Units.gridUnit * 4
                        onMoved: value => {
                            return appSettings.audioSpectrumBarCount = Math.round(value);
                        }
                    }
                }
            }
        }

        // =====================================================================
        // LAYOUT ASSIGNMENT CARD
        // =====================================================================
        // Mode-neutral: the toggle governs the synthesized default for BOTH the
        // snapping and tiling engines, so it lives on the General page rather
        // than either mode's behavior page.
        SettingsCard {
            headerText: i18n("Layout assignment")
            collapsible: true
            searchAnchor: "layoutAssignment"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Don't assign a layout by default")
                    searchAnchor: "suppressDefaultLayoutAssignment"
                    description: i18n("Snapping and tiling stay off until you assign a layout. A rule can re-enable the default per monitor.")

                    SettingsSwitch {
                        checked: appSettings.suppressDefaultLayoutAssignment
                        accessibleName: i18n("Don't assign a layout by default")
                        onToggled: function (newValue) {
                            appSettings.suppressDefaultLayoutAssignment = newValue;
                        }
                    }
                }
            }
        }

        // =====================================================================
        // WINDOW FILTERING CARD
        // =====================================================================
        // The three global filters previously hosted on the standalone
        // "Exclusions" page. The per-app and per-class lists that used to
        // live there have folded into Rules (Application-subject
        // Exclude rules), so the page itself was deleted; these three global
        // knobs survive here because they apply to ALL windows uniformly
        // rather than matching specific applications.
        WindowFilterCard {
            Layout.fillWidth: true

            excludeTransient: appSettings.excludeTransientWindows
            transientDescription: i18n("Skip dialogs, popups, and toolbars for snapping and tiling")
            transientAccessibleName: i18n("Exclude transient windows")
            onExcludeTransientToggled: value => {
                appSettings.excludeTransientWindows = value;
            }

            minWidth: appSettings.minimumWindowWidth
            minWidthFrom: settingsController.generalPage.minimumWindowWidthMin
            minWidthTo: settingsController.generalPage.minimumWindowWidthMax
            minWidthDescription: i18n("Windows narrower than this are excluded")
            minWidthDisabledDescription: i18n("Disabled (no width threshold)")
            minWidthAccessibleName: i18n("Minimum window width")
            onMinWidthModified: value => {
                appSettings.minimumWindowWidth = value;
            }

            minHeight: appSettings.minimumWindowHeight
            minHeightFrom: settingsController.generalPage.minimumWindowHeightMin
            minHeightTo: settingsController.generalPage.minimumWindowHeightMax
            minHeightDescription: i18n("Windows shorter than this are excluded")
            minHeightDisabledDescription: i18n("Disabled (no height threshold)")
            minHeightAccessibleName: i18n("Minimum window height")
            onMinHeightModified: value => {
                appSettings.minimumWindowHeight = value;
            }
        }

        // =====================================================================
        // CONFIGURATION CARD
        // =====================================================================
        SettingsCard {
            headerText: i18n("Configuration")
            searchAnchor: "configuration"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Backup")
                    searchAnchor: "backup"
                    description: i18n("Export all settings to a file")

                    Button {
                        text: i18n("Export Settings")
                        icon.name: "document-export"
                        Accessible.name: text
                        onClicked: exportConfigDialog.open()
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Restore")
                    searchAnchor: "restore"
                    description: i18n("Import settings from a previously exported file")

                    Button {
                        text: i18n("Import Settings")
                        icon.name: "document-import"
                        Accessible.name: text
                        onClicked: importConfigDialog.open()
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Reset")
                    description: i18n("Restore every setting on every page to its default value")
                    searchAnchor: "resetDefaults"

                    Button {
                        text: i18n("Reset to Defaults")
                        icon.name: "document-revert"
                        Accessible.name: text
                        // Reach the chrome-owned confirmation dialog through
                        // the `window.defaultsConfirmDialog` alias declared
                        // in Main.qml. A bare `defaultsConfirmDialog.open()`
                        // would resolve against this page's scope — Loader
                        // breaks file-id lookup so the id from Main.qml is
                        // unreachable here — and throw `ReferenceError`
                        // at runtime. The `typeof window` guard mirrors the
                        // same defensive shape used elsewhere in the file
                        // for cross-host (KCM / preview) compatibility,
                        // where `window` may be undeclared.
                        onClicked: {
                            if (typeof window !== "undefined" && window && window.defaultsConfirmDialog) {
                                window.defaultsConfirmDialog.open();
                            }
                        }
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
        onAccepted: settingsController.exportAllSettings(settingsController.urlToLocalFile(selectedFile))
    }

    FileDialog {
        id: importConfigDialog

        title: i18n("Import Settings")
        nameFilters: [i18n("PlasmaZones Config (*.json *.conf *.ini *.rc)"), i18n("All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: settingsController.importAllSettings(settingsController.urlToLocalFile(selectedFile))
    }
}
