// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SettingsFlickable {
    id: root

    readonly property var settingsBridge: settingsController.tilingAlgorithmPage
    readonly property int algorithmPreviewWidth: Kirigami.Units.gridUnit * 18
    readonly property int algorithmPreviewHeight: Kirigami.Units.gridUnit * 10
    // Per-screen override helper (shared app-wide scope, bound below).
    // m-13: Cache the availableAlgorithms PROPERTY (read, not call — it is both a
    // Q_PROPERTY and a same-named Q_INVOKABLE, so the `()` form errors with
    // "is not a function"). Refreshed via the Connections below on its NOTIFY.
    property var _cachedAlgos: settingsController.availableAlgorithms || []
    // The ISettings object (the `appSettings` context property), captured at page
    // scope. LayoutComboBox declares its own `appSettings: settingsController`,
    // which SHADOWS the context property inside the combo's onActivated — writing
    // `appSettings.defaultAutotileAlgorithm` there hit a nonexistent property on
    // the controller and silently dropped every algorithm change. Read/write the
    // algorithm through this reference so it always targets ISettings.
    readonly property var appSettingsObj: appSettings
    readonly property string effectiveAlgorithm: settingValue("Algorithm", appSettingsObj.defaultAutotileAlgorithm)
    // Working algorithm for the whole page (preview, custom-param controls,
    // capability lookups). Driven imperatively rather than bound to
    // algorithmCombo.currentValue: that value transiently resets to the first
    // list entry (BSP) whenever the combo rebuilds its model — e.g. the layout
    // reload that follows a Save — which used to drag the entire page onto BSP.
    // It is set the instant the user picks an algorithm (the combo's
    // onActivated, below) so switching is immediate, and re-synced to the
    // persisted setting on load / external edits / per-screen scope changes via
    // onEffectiveAlgorithmChanged.
    property string selectedAlgorithm: root.effectiveAlgorithm

    onEffectiveAlgorithmChanged: root.selectedAlgorithm = root.effectiveAlgorithm
    // Data-driven algorithm capabilities (lookup from cached availableAlgorithms by ID)
    readonly property var algoCapabilities: {
        const algos = root._cachedAlgos;
        const algoId = root.selectedAlgorithm;
        for (let i = 0; i < algos.length; i++) {
            if (algos[i].id === algoId)
                return algos[i];
        }
        return null;
    }
    readonly property bool algoSupportsSplitRatio: algoCapabilities ? (algoCapabilities.supportsSplitRatio === true) : false
    readonly property bool algoSupportsMasterCount: algoCapabilities ? (algoCapabilities.supportsMasterCount === true) : false
    readonly property bool algoSupportsCustomParams: algoCapabilities ? (algoCapabilities.supportsCustomParams === true) : false
    // Custom param definitions for the selected algorithm.  The binding only
    // depends on algoSupportsCustomParams / selectedAlgorithm.  Because
    // customParamsForAlgorithm() is a Q_INVOKABLE (not a Q_PROPERTY with
    // NOTIFY), QML's binding engine will NOT re-call it when saved values
    // change — so the Repeater model stays stable during slider drags.
    readonly property var customParamDefs: {
        if (!root.algoSupportsCustomParams)
            return [];

        return root.settingsBridge.customParamsForAlgorithm(root.selectedAlgorithm);
    }
    // Live custom-param values (name → value) for the selected algorithm. This
    // is the single source of truth the param controls read and write, and the
    // value the live preview consumes — exactly the role appSettings holds for
    // split ratio / master count. customParamDefs (the Repeater model) is
    // intentionally not refreshed on edit, so the current values live here
    // instead. Re-seeded from the saved/default values whenever the algorithm
    // changes (i.e. customParamDefs re-evaluates).
    property var liveCustomParams: ({})

    function _seedLiveCustomParams() {
        const defs = root.customParamDefs;
        let m = {};
        for (let i = 0; i < defs.length; ++i) {
            const d = defs[i];
            m[d.name] = (d.value !== undefined ? d.value : d.defaultValue);
        }
        root.liveCustomParams = m;
    }

    function setLiveCustomParam(name, value) {
        // Reassign a fresh object so the `var` property emits changed —
        // mutating in place would not re-trigger the preview binding or the
        // delegates' paramValue.
        let m = Object.assign({}, root.liveCustomParams);
        m[name] = value;
        root.liveCustomParams = m;
    }

    onCustomParamDefsChanged: _seedLiveCustomParams()

    // Live per-algorithm built-in tuning (split ratio, master count, max
    // windows) for the selected algorithm. Like liveCustomParams, this is the
    // reactive source the sliders read in global scope so they don't snap back
    // to the non-reactive algorithmSettingsFor() value, and it re-seeds on
    // algorithm change so each algorithm shows its own saved tuning. Per-screen
    // overrides bypass this (they flow through settingValue/writeSetting's
    // per-screen path, which is reactive on its own).
    property var liveAlgoSettings: ({})

    function _seedLiveAlgoSettings() {
        root.liveAlgoSettings = root.settingsBridge.algorithmSettingsFor(root.selectedAlgorithm);
    }

    function setLiveAlgoSetting(key, value) {
        if (psHelper.isPerScreen)
            return;
        let m = Object.assign({}, root.liveAlgoSettings);
        m[key] = value;
        root.liveAlgoSettings = m;
    }

    onSelectedAlgorithmChanged: _seedLiveAlgoSettings()

    Component.onCompleted: {
        _seedLiveCustomParams();
        _seedLiveAlgoSettings();
    }

    // Whether the algorithm uses a center layout (ratio/count labels say "Center" instead of "Master").
    // Check capabilities map first (for future extensibility via scripted algorithm metadata),
    // falling back to hardcoded IDs for built-in algorithms. See PR #256 / M13.
    readonly property bool algoCenterLayout: algoCapabilities ? (algoCapabilities.centerLayout === true) : (root.selectedAlgorithm === "three-column" || root.selectedAlgorithm === "centered-master")

    function settingValue(key, globalValue) {
        return psHelper.settingValue(key, globalValue);
    }

    function writeSetting(key, value, globalSetter) {
        psHelper.writeSetting(key, value, globalSetter);
    }

    contentHeight: content.implicitHeight
    clip: true

    Connections {
        function onAvailableAlgorithmsChanged() {
            root._cachedAlgos = settingsController.availableAlgorithms || [];
        }

        target: settingsController
    }

    PerScreenOverrideHelper {
        id: psHelper

        appSettings: settingsController
        // Shared app-wide scope — a monitor picked on any per-monitor page
        // stays picked here.
        selectedScreenName: settingsController.scopeScreenName
        getterMethod: "getPerScreenAutotileSettings"
        setterMethod: "setPerScreenAutotileSetting"
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // Algorithm Card (per-monitor) — opts into the header scope chip.
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Algorithm")
            searchAnchor: "algorithm"
            collapsible: true
            scopeEnabled: true
            scopeAppSettings: settingsController
            // Algorithm sub-domain only — must not report/reset the Gaps card's
            // per-monitor overrides (shared autotile map, disjoint key subsets).
            scopeHasOverridesMethod: "hasPerScreenAutotileAlgorithmSettings"
            scopeClearerMethod: "clearPerScreenAutotileAlgorithmSettings"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // Live preview - centered at top
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.algorithmPreviewHeight + Kirigami.Units.gridUnit * 1.5

                    // Preview container
                    Item {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        width: root.algorithmPreviewWidth
                        height: root.algorithmPreviewHeight

                        Rectangle {
                            anchors.fill: parent
                            color: Kirigami.Theme.backgroundColor
                            border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
                            border.width: 1
                            radius: Kirigami.Units.smallSpacing

                            AlgorithmPreview {
                                anchors.fill: parent
                                anchors.margins: Kirigami.Units.smallSpacing
                                appSettings: settingsController
                                showLabel: false
                                algorithmId: root.selectedAlgorithm
                                algorithmName: root.algoCapabilities ? (root.algoCapabilities.name || "") : ""
                                // Every layout input the user can edit feeds the preview
                                // from the live slider handle, so the diagram updates as the
                                // user drags any of them (master ratio and master count
                                // included, not just max windows). The "Max N windows"
                                // caption below also tracks the windows slider.
                                windowCount: previewWindowSlider.slider.value
                                splitRatio: root.algoSupportsSplitRatio ? masterRatioSlider.slider.value : (root.algoCapabilities ? root.algoCapabilities.defaultSplitRatio : 0.6)
                                masterCount: root.algoSupportsMasterCount ? masterCountSlider.slider.value : 0
                                customParams: root.liveCustomParams
                                zoneNumberDisplay: root.algoCapabilities ? (root.algoCapabilities.zoneNumberDisplay || "all") : "all"
                            }
                        }

                        // Window count label below preview
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.bottom
                            anchors.topMargin: Kirigami.Units.smallSpacing
                            text: i18np("Max %n window", "Max %n windows", previewWindowSlider.slider.value)
                            font: Kirigami.Theme.fixedWidthFont
                            opacity: 0.7
                        }
                    }
                }

                // Algorithm description (from script metadata)
                Label {
                    Layout.fillWidth: true
                    Layout.maximumWidth: root.algorithmPreviewWidth
                    Layout.alignment: Qt.AlignHCenter
                    text: root.algoCapabilities ? (root.algoCapabilities.description || "") : ""
                    visible: text !== ""
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    font: Kirigami.Theme.smallFont
                }

                // Algorithm selection - uses LayoutComboBox with preview thumbnails
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Kirigami.Units.smallSpacing
                    // Constant cap (not bound to parent.width) — binding a layout
                    // child's max width to its enclosing layout's width feeds the
                    // child size back into the same layout pass (recursive rearrange).
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 25

                    LayoutComboBox {
                        id: algorithmCombo

                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillWidth: true
                        Accessible.name: i18n("Tiling algorithm")
                        appSettings: settingsController
                        showPreview: true
                        layoutFilter: 1 // Autotile algorithms only
                        showNoneOption: false
                        currentLayoutId: "autotile:" + root.effectiveAlgorithm
                        onActivated: {
                            // Extract algorithm ID from autotile: prefixed value
                            let selectedId = algorithmCombo.currentValue;
                            if (selectedId === "")
                                selectedId = root.appSettingsObj.defaultAutotileAlgorithm;
                            else if (selectedId.startsWith("autotile:"))
                                selectedId = selectedId.substring(9);
                            // Drive the page off the user's pick immediately, then
                            // persist. Both go through root.appSettingsObj — NOT the
                            // bare `appSettings`, which the combo shadows with the
                            // controller in this scope (see appSettingsObj above).
                            // Resetting global max-windows / split-ratio / master-count
                            // here would clobber a sibling algorithm's per-algorithm slot.
                            root.selectedAlgorithm = selectedId;
                            root.writeSetting("Algorithm", selectedId, function (v) {
                                root.appSettingsObj.defaultAutotileAlgorithm = v;
                            });
                        }
                    }
                }

                SettingsSeparator {}

                // Max windows
                SettingsRow {
                    title: i18n("Max windows")
                    searchAnchor: "maxWindows"
                    description: i18n("Maximum number of windows to tile on this screen")

                    SettingsSlider {
                        id: previewWindowSlider

                        Accessible.name: i18n("Maximum windows")
                        from: root.settingsBridge.autotileMaxWindowsMin
                        to: root.settingsBridge.autotileMaxWindowsMax
                        stepSize: 1
                        value: root.settingValue("MaxWindows", root.liveAlgoSettings.maxWindows !== undefined ? root.liveAlgoSettings.maxWindows : appSettings.autotileMaxWindows)
                        formatValue: function (v) {
                            return Math.round(v).toString();
                        }
                        onMoved: value => {
                            root.setLiveAlgoSetting("maxWindows", Math.round(value));
                            root.writeSetting("MaxWindows", Math.round(value), function (v) {
                                root.settingsBridge.setAlgorithmMaxWindows(root.selectedAlgorithm, v);
                            });
                        }
                    }
                }

                // Algorithm-specific settings (master-stack, three-column, centered-master)
                SettingsSeparator {
                    visible: root.algoSupportsSplitRatio || root.algoSupportsMasterCount
                }

                SettingsRow {
                    visible: root.algoSupportsSplitRatio
                    title: root.algoCenterLayout ? i18n("Center ratio") : i18n("Master ratio")
                    searchAnchor: "masterRatio"
                    description: root.algoCenterLayout ? i18n("Width proportion allocated to the center column") : i18n("Width proportion allocated to the master area")

                    SettingsSlider {
                        id: masterRatioSlider

                        Accessible.name: root.algoCenterLayout ? i18n("Center ratio") : i18n("Master ratio")
                        from: root.settingsBridge.autotileSplitRatioMin
                        to: root.settingsBridge.autotileSplitRatioMax
                        stepSize: 0.05
                        value: root.settingValue("SplitRatio", root.liveAlgoSettings.splitRatio !== undefined ? root.liveAlgoSettings.splitRatio : appSettings.autotileSplitRatio)
                        formatValue: function (v) {
                            return Math.round(v * 100) + "%";
                        }
                        onMoved: value => {
                            root.setLiveAlgoSetting("splitRatio", value);
                            root.writeSetting("SplitRatio", value, function (v) {
                                root.settingsBridge.setAlgorithmSplitRatio(root.selectedAlgorithm, v);
                            });
                        }
                    }
                }

                SettingsRow {
                    visible: root.algoSupportsSplitRatio
                    title: i18n("Ratio step size")
                    searchAnchor: "ratioStepSize"
                    description: i18n("Amount the ratio changes per keyboard shortcut press")

                    SettingsSlider {
                        Accessible.name: i18n("Ratio step size")
                        from: root.settingsBridge.autotileSplitRatioStepMin
                        to: root.settingsBridge.autotileSplitRatioStepMax
                        stepSize: 0.01
                        value: root.settingValue("SplitRatioStep", appSettings.autotileSplitRatioStep)
                        formatValue: function (v) {
                            return Math.round(v * 100) + "%";
                        }
                        onMoved: value => {
                            root.writeSetting("SplitRatioStep", value, function (v) {
                                appSettings.autotileSplitRatioStep = v;
                            });
                        }
                    }
                }

                SettingsSeparator {
                    visible: root.algoSupportsMasterCount
                }

                SettingsRow {
                    visible: root.algoSupportsMasterCount
                    title: root.algoCenterLayout ? i18n("Center count") : i18n("Master count")
                    searchAnchor: "masterCount"
                    description: root.algoCenterLayout ? i18n("Number of windows in the center column") : i18n("Number of windows in the master area")

                    SettingsSlider {
                        id: masterCountSlider

                        Accessible.name: root.algoCenterLayout ? i18n("Center count") : i18n("Master count")
                        from: root.settingsBridge.autotileMasterCountMin
                        to: root.settingsBridge.autotileMasterCountMax
                        stepSize: 1
                        value: root.settingValue("MasterCount", root.liveAlgoSettings.masterCount !== undefined ? root.liveAlgoSettings.masterCount : appSettings.autotileMasterCount)
                        formatValue: function (v) {
                            return Math.round(v).toString();
                        }
                        onMoved: value => {
                            root.setLiveAlgoSetting("masterCount", Math.round(value));
                            root.writeSetting("MasterCount", Math.round(value), function (v) {
                                root.settingsBridge.setAlgorithmMasterCount(root.selectedAlgorithm, v);
                            });
                        }
                    }
                }

                // =============================================================
                // Custom Algorithm Parameters
                // =============================================================
                // Unlike the built-in sliders above, custom params are stored
                // per-algorithm only — there is no per-screen override for them,
                // so the controls write the global per-algorithm value via
                // setCustomParam() regardless of the active monitor scope.
                Repeater {
                    model: root.customParamDefs

                    delegate: ColumnLayout {
                        id: paramDelegate

                        required property var modelData
                        required property int index
                        readonly property string paramLabel: modelData.description || modelData.name
                        // Show the raw param name as subtitle when description was used as title
                        readonly property string paramDescription: modelData.description ? modelData.name : ""
                        // Current value, read from the page's live map (the single
                        // source of truth). Falls back to the model's saved/default
                        // value before the map is seeded. Because this tracks
                        // liveCustomParams, the slider's on-release re-sync
                        // (Binding when: !pressed) and the switch / combo all settle
                        // on the value the user just set instead of snapping back to
                        // the never-refreshed model value.
                        readonly property var paramValue: {
                            const v = root.liveCustomParams[modelData.name];
                            if (v !== undefined)
                                return v;
                            return modelData.value !== undefined ? modelData.value : modelData.defaultValue;
                        }
                        readonly property real paramMin: modelData.minValue !== undefined ? modelData.minValue : 0
                        readonly property real paramMax: {
                            let mx = modelData.maxValue !== undefined ? modelData.maxValue : 1;
                            // Guard against degenerate range from malformed script metadata
                            return mx > paramMin ? mx : paramMin + 1;
                        }
                        readonly property real paramRange: paramMax - paramMin

                        Layout.fillWidth: true
                        spacing: 0

                        SettingsSeparator {}

                        SettingsRow {
                            Layout.fillWidth: true
                            title: paramLabel
                            description: paramDescription

                            // Number parameter: rendered as a SettingsSlider
                            SettingsSlider {
                                visible: modelData.type === "number"
                                Accessible.name: paramLabel
                                from: paramMin
                                to: paramMax
                                stepSize: {
                                    if (paramRange <= 1)
                                        return 0.01;

                                    if (paramRange <= 10)
                                        return 0.1;

                                    return 1;
                                }
                                value: paramValue
                                formatValue: function (v) {
                                    if (paramRange <= 1)
                                        return Math.round(v * 100) + "%";

                                    if (paramRange <= 10)
                                        return v.toFixed(1);

                                    return Math.round(v).toString();
                                }
                                onMoved: value => {
                                    root.setLiveCustomParam(modelData.name, value);
                                    root.settingsBridge.setCustomParam(root.selectedAlgorithm, modelData.name, value);
                                }
                            }

                            // Bool parameter: rendered as a Switch
                            Switch {
                                visible: modelData.type === "bool"
                                Accessible.name: paramLabel
                                checked: paramValue === true
                                onToggled: {
                                    root.setLiveCustomParam(modelData.name, checked);
                                    root.settingsBridge.setCustomParam(root.selectedAlgorithm, modelData.name, checked);
                                }
                            }

                            // Enum parameter: rendered as a ComboBox
                            ComboBox {
                                id: enumCombo

                                visible: modelData.type === "enum"
                                Accessible.name: paramLabel
                                model: modelData.enumOptions || []
                                currentIndex: {
                                    let opts = modelData.enumOptions || [];
                                    let idx = opts.indexOf(paramValue);
                                    return idx >= 0 ? idx : 0;
                                }
                                onActivated: {
                                    root.setLiveCustomParam(modelData.name, currentText);
                                    root.settingsBridge.setCustomParam(root.selectedAlgorithm, modelData.name, currentText);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
