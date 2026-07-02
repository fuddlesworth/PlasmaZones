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

    // The min-size window filters are rule-backed: each is the threshold in the match
    // of a managed general min-size baseline Exclude rule (Width / Height LessThan N).
    // The baseline is always daemon-seeded, so the controls only ever UPDATE its match
    // threshold (0 = never matches = disabled); read/write route through the
    // RuleController. The transient toggle stays config-backed.
    readonly property var ruleController: settingsController.rulesPage
    property int generalReloadTick: 0

    // Threshold N of a min-size baseline (Width/Height LessThan N), or the fallback
    // when the rule/match is absent. Reading generalReloadTick keeps it reactive.
    function minSizeThreshold(ruleId, fallback) {
        root.generalReloadTick;
        const rule = root.ruleController.ruleJson(ruleId);
        if (rule && rule.id && rule.match && rule.match.value !== undefined)
            return rule.match.value;
        return fallback;
    }

    // Write a min-size threshold onto the baseline rule's match (0 = disabled).
    // No find-or-create: the daemon seeds the baseline, so a missing rule is
    // only the transient fresh-profile state before the first seed — the write
    // is dropped and the spinbox snaps back to the displayed fallback.
    function writeMinSize(ruleId, field, value) {
        var rule = root.ruleController.ruleJson(ruleId);
        if (!rule || !rule.id)
            return;
        rule.match = {
            "field": field,
            "op": "lessThan",
            "value": Math.max(0, value)
        };
        root.ruleController.updateRuleFromJson(rule);
        root.generalReloadTick++;
    }

    Connections {
        target: root.ruleController
        function onRulesLoaded() {
            root.generalReloadTick++;
        }
        function onBaselinesChanged() {
            root.generalReloadTick++;
        }
    }

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
        SettingsCard {
            headerText: i18n("Window filtering")
            collapsible: true
            searchAnchor: "windowFiltering"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Exclude transient windows")
                    description: i18n("Skip dialogs, popups, and toolbars for snapping and tiling")
                    searchAnchor: "excludeTransient"

                    SettingsSwitch {
                        checked: appSettings.excludeTransientWindows
                        accessibleName: i18n("Exclude transient windows")
                        onToggled: function (newValue) {
                            appSettings.excludeTransientWindows = newValue;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Minimum window width")
                    description: root.minSizeThreshold(settingsController.generalMinWidthRuleId(), settingsController.generalPage.minimumWindowWidthDefault) === 0 ? i18n("Disabled (no width threshold)") : i18n("Windows narrower than this are excluded")

                    SettingsSpinBox {
                        // Schema-driven bounds — see GeneralPageController's
                        // minimumWindowWidthMin/Max Q_PROPERTYs. Literal
                        // bounds would silently truncate any saved value
                        // outside the literal range when the SpinBox clamped
                        // the bound `value` on render.
                        from: settingsController.generalPage.minimumWindowWidthMin
                        to: settingsController.generalPage.minimumWindowWidthMax
                        stepSize: 10
                        value: root.minSizeThreshold(settingsController.generalMinWidthRuleId(), settingsController.generalPage.minimumWindowWidthDefault)
                        unitText: ""
                        Accessible.name: i18n("Minimum window width")
                        onValueModified: value => {
                            root.writeMinSize(settingsController.generalMinWidthRuleId(), "width", value);
                        }
                        textFromValue: function (value) {
                            return value === 0 ? i18n("Off") : i18nc("pixel-unit suffix in spin box", "%1 px", value);
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Minimum window height")
                    description: root.minSizeThreshold(settingsController.generalMinHeightRuleId(), settingsController.generalPage.minimumWindowHeightDefault) === 0 ? i18n("Disabled (no height threshold)") : i18n("Windows shorter than this are excluded")

                    SettingsSpinBox {
                        from: settingsController.generalPage.minimumWindowHeightMin
                        to: settingsController.generalPage.minimumWindowHeightMax
                        stepSize: 10
                        value: root.minSizeThreshold(settingsController.generalMinHeightRuleId(), settingsController.generalPage.minimumWindowHeightDefault)
                        unitText: ""
                        Accessible.name: i18n("Minimum window height")
                        onValueModified: value => {
                            root.writeMinSize(settingsController.generalMinHeightRuleId(), "height", value);
                        }
                        textFromValue: function (value) {
                            return value === 0 ? i18n("Off") : i18nc("pixel-unit suffix in spin box", "%1 px", value);
                        }
                    }
                }
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
