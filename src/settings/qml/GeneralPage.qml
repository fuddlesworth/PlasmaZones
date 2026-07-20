// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SettingsFlickable {
    id: root

    // Capture the context property so child components can access it
    readonly property var settingsBridge: appSettings
    // Frame rate + audio spectrum drive EVERY shader category (overlay,
    // animation, surface decoration), so the Shader Effects and Audio
    // Spectrum cards live here on General rather than on the snapping overlay
    // page. The backing state is still the snappingEffectsPage controller
    // (its bounds + CAVA probe are global, only its name is historical).
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
        // SHORTCUT CHEATSHEET CARD
        // =====================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Shortcut Cheatsheet")
            searchAnchor: "cheatsheet"
            showToggle: true
            toggleChecked: root.settingsBridge.cheatsheetEnabled
            onToggleClicked: checked => {
                root.settingsBridge.cheatsheetEnabled = checked;
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: i18n("Shows an overlay listing every PlasmaZones shortcut with the keys currently bound to it. Open it with its global shortcut, Meta+Alt+/ by default, and dismiss it with Escape or a click outside the card.")
                }
            }
        }

        // =====================================================================
        // RENDERING CARD
        // =====================================================================
        SettingsCard {
            headerText: i18n("Rendering")
            collapsible: true
            searchAnchor: "rendering"
            // Advanced-only: the graphics backend is a power/troubleshooting
            // choice, not an everyday setting. Whole-card gate — a direct
            // ColumnLayout child drops out of layout when hidden.
            visible: settingsController.advancedMode

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Rendering backend")
                    description: i18n("Graphics API used for overlay rendering")
                    searchAnchor: "renderingBackend"

                    ComboBox {
                        id: renderingBackendCombo

                        // currentIndex is a binding and stays one. appSettings
                        // .renderingBackend has a NOTIFY, so the binding already
                        // re-evaluates on every change; a Connections handler
                        // assigning currentIndex would sever it on its first run
                        // and then be the only thing keeping the combo in sync.
                        enabled: !settingsController.daemonRunning
                        Accessible.name: i18n("Rendering backend")
                        // One list of {text, value} pairs rather than two
                        // parallel arrays that have to stay index-aligned.
                        textRole: "text"
                        valueRole: "value"
                        model: settingsController.valueOptions("Rendering", "Backend")
                        currentIndex: Math.max(0, indexOfValue(appSettings.renderingBackend))
                        onActivated: appSettings.renderingBackend = currentValue
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
        // The frame rate applies to all shader categories (overlay, animation,
        // surface decoration), so this card is global (moved here from
        // Snapping → Overlay Appearance). The card deliberately has no master
        // toggle: shader use is decided per layout (a layout whose shader is
        // "none" draws the rectangle overlay), so a global switch would gate
        // nothing the layouts don't already control. The audio spectrum knobs
        // live in their own card below (Shaders.Audio config group).
        SettingsCard {
            headerText: i18n("Shader Effects")
            searchAnchor: "shaderEffects"
            collapsible: true
            // Advanced-only: shader frame-rate tuning is a fine-grained
            // performance knob most users never touch.
            visible: settingsController.advancedMode

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
            }
        }

        // =====================================================================
        // AUDIO SPECTRUM CARD
        // =====================================================================
        // The full CAVA analysis parameter set (Shaders.Audio). Every row
        // below the enable switch is gated on the toggle + cava presence; the
        // values feed both runtimes (daemon overlays and the KWin effect) plus
        // the editor's shader preview.
        SettingsCard {
            headerText: i18n("Audio Spectrum")
            searchAnchor: "audioSpectrum"
            collapsible: true
            // Advanced-only: the full CAVA audio-analysis parameter set is
            // the deepest power-user surface on this page.
            visible: settingsController.advancedMode

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Audio spectrum")
                    searchAnchor: "audioSpectrumEnabled"
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

                // One gate for the whole knob stack: disabling this container
                // propagates to every child, and SettingsRow/SettingsSeparator
                // bind `visible: enabled`, so rows AND separators collapse
                // together when the toggle is off (per-row gates left the
                // separators behind as orphaned divider lines).
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    enabled: audioVizSwitch.checked && root.effectsBridge.cavaAvailable

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Spectrum bars")
                        searchAnchor: "spectrumBars"
                        description: i18n("Number of frequency bands in the audio visualization")

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

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Noise reduction")
                        searchAnchor: "audioNoiseReduction"
                        description: i18n("How smoothly the bars respond. Higher values are slower and calmer while lower values are fast and twitchy.")

                        SettingsSlider {
                            from: root.effectsBridge.audioNoiseReductionMin
                            to: root.effectsBridge.audioNoiseReductionMax
                            value: appSettings.audioNoiseReduction
                            valueSuffix: ""
                            labelWidth: Kirigami.Units.gridUnit * 4
                            onMoved: value => {
                                return appSettings.audioNoiseReduction = Math.round(value);
                            }
                        }
                    }

                    SettingsRow {
                        title: i18n("Extra smoothing")
                        searchAnchor: "audioExtraSmoothing"
                        description: i18n("Additional smoothing applied on top of noise reduction")

                        SettingsSlider {
                            from: root.effectsBridge.audioExtraSmoothingMin
                            to: root.effectsBridge.audioExtraSmoothingMax
                            value: appSettings.audioExtraSmoothing
                            valueSuffix: "%"
                            labelWidth: Kirigami.Units.gridUnit * 4
                            onMoved: value => {
                                return appSettings.audioExtraSmoothing = Math.round(value);
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Automatic gain")
                        searchAnchor: "audioAutosens"
                        description: i18n("Continuously adjusts sensitivity so the bars fill the available range")

                        SettingsSwitch {
                            id: audioAutosensSwitch

                            checked: appSettings.audioAutosens
                            accessibleName: i18n("Automatic gain")
                            onToggled: function (newValue) {
                                appSettings.audioAutosens = newValue;
                            }
                        }
                    }

                    SettingsRow {
                        title: i18n("Sensitivity")
                        searchAnchor: "audioSensitivity"
                        description: audioAutosensSwitch.checked ? i18n("Starting gain that automatic gain adapts from") : i18n("Fixed gain applied to the audio signal")

                        SettingsSlider {
                            from: root.effectsBridge.audioSensitivityMin
                            to: root.effectsBridge.audioSensitivityMax
                            value: appSettings.audioSensitivity
                            valueSuffix: "%"
                            labelWidth: Kirigami.Units.gridUnit * 4
                            onMoved: value => {
                                return appSettings.audioSensitivity = Math.round(value);
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Lowest frequency")
                        searchAnchor: "audioLowerCutoff"
                        description: i18n("Sounds below this frequency are ignored")

                        SettingsSlider {
                            from: root.effectsBridge.audioLowerCutoffHzMin
                            to: root.effectsBridge.audioLowerCutoffHzMax
                            value: appSettings.audioLowerCutoffHz
                            valueSuffix: " Hz"
                            labelWidth: Kirigami.Units.gridUnit * 4
                            onMoved: value => {
                                return appSettings.audioLowerCutoffHz = Math.round(value);
                            }
                        }
                    }

                    SettingsRow {
                        title: i18n("Highest frequency")
                        searchAnchor: "audioHigherCutoff"
                        description: i18n("Sounds above this frequency are ignored")

                        SettingsSlider {
                            from: root.effectsBridge.audioHigherCutoffHzMin
                            to: root.effectsBridge.audioHigherCutoffHzMax
                            value: appSettings.audioHigherCutoffHz
                            valueSuffix: " Hz"
                            labelWidth: Kirigami.Units.gridUnit * 4
                            onMoved: value => {
                                return appSettings.audioHigherCutoffHz = Math.round(value);
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Channels")
                        searchAnchor: "audioChannelMode"
                        description: i18n("Stereo shows left and right bars side by side. Mono collapses to one set of bars.")

                        WideComboBox {
                            Accessible.name: i18n("Channels")
                            textRole: "text"
                            valueRole: "value"
                            model: settingsController.valueOptions("Shaders.Audio", "ChannelMode")
                            // A binding, not a JS assignment: see
                            // renderingBackendCombo above.
                            currentIndex: Math.max(0, indexOfValue(appSettings.audioChannelMode))
                            onActivated: appSettings.audioChannelMode = currentValue
                        }
                    }

                    SettingsRow {
                        title: i18n("Reverse bar order")
                        searchAnchor: "audioReverse"
                        description: i18n("Flip the frequency order of the bars")

                        SettingsSwitch {
                            checked: appSettings.audioReverse
                            accessibleName: i18n("Reverse bar order")
                            onToggled: function (newValue) {
                                appSettings.audioReverse = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Monstercat filter")
                        searchAnchor: "audioMonstercat"
                        description: i18n("Spreads each bar into its neighbors for a smoother outline")

                        SettingsSwitch {
                            checked: appSettings.audioMonstercat
                            accessibleName: i18n("Monstercat filter")
                            onToggled: function (newValue) {
                                appSettings.audioMonstercat = newValue;
                            }
                        }
                    }

                    SettingsRow {
                        title: i18n("Wave filter")
                        searchAnchor: "audioWaves"
                        description: i18n("Rounds the spectrum into soft waves")

                        SettingsSwitch {
                            checked: appSettings.audioWaves
                            accessibleName: i18n("Wave filter")
                            onToggled: function (newValue) {
                                appSettings.audioWaves = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Audio backend")
                        searchAnchor: "audioInputMethod"
                        description: i18n("Leave on Automatic unless capture fails with the detected backend")

                        WideComboBox {
                            Accessible.name: i18n("Audio backend")
                            textRole: "text"
                            valueRole: "value"
                            model: settingsController.valueOptions("Shaders.Audio", "InputMethod")
                            // A binding, not a JS assignment: see
                            // renderingBackendCombo above.
                            currentIndex: Math.max(0, indexOfValue(appSettings.audioInputMethod))
                            onActivated: appSettings.audioInputMethod = currentValue
                        }
                    }

                    SettingsRow {
                        title: i18n("Audio source")
                        searchAnchor: "audioInputSource"
                        description: i18n("Capture device or monitor source. Keep \"auto\" to follow the default output.")

                        TextField {
                            id: audioSourceField

                            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                            Accessible.name: i18n("Audio source")
                            onEditingFinished: appSettings.audioInputSource = text

                            // Defer host-driven updates while the user is
                            // typing. A plain `text:` binding re-fires on every
                            // audioInputSourceChanged, focused or not, so it
                            // overwrote the edit in progress before the focus
                            // guard was ever consulted — the guard only started
                            // working once an unfocused change had severed the
                            // binding it was meant to hold back.
                            Binding on text {
                                value: appSettings.audioInputSource
                                when: !audioSourceField.activeFocus
                                restoreMode: Binding.RestoreNone
                            }
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
        // Success only. A failure toasts through settingsTransferFailed with
        // the reason, which the bool cannot carry.
        onAccepted: {
            if (settingsController.exportAllSettings(settingsController.urlToLocalFile(selectedFile)) && typeof window !== "undefined" && window && window.showToast)
                window.showToast(i18n("Settings exported"));
        }
    }

    FileDialog {
        id: importConfigDialog

        title: i18n("Import Settings")
        nameFilters: [i18n("PlasmaZones Config (*.json *.conf *.ini *.rc)"), i18n("All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            if (settingsController.importAllSettings(settingsController.urlToLocalFile(selectedFile)) && typeof window !== "undefined" && window && window.showToast)
                window.showToast(i18n("Settings imported"));
        }
    }

    // Both transfers report their failures here rather than through the bool:
    // a refused path, a file that vanished, and a file that is not settings at
    // all are the same `false` and want different words.
    Connections {
        function onSettingsTransferFailed(reason) {
            if (typeof window !== "undefined" && window && window.showToast)
                window.showToast(reason);
        }

        target: settingsController
    }
}
