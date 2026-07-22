// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "../../js/AlgorithmCapabilities.js" as AlgoCaps

SettingsFlickable {
    id: root

    readonly property var settingsBridge: settingsController.tilingAlgorithmPage
    readonly property int algorithmPreviewWidth: Kirigami.Units.gridUnit * 18
    readonly property int algorithmPreviewHeight: Kirigami.Units.gridUnit * 10
    // Per-screen override helper (shared app-wide scope, bound below).
    // Cache the availableAlgorithms PROPERTY (read, not call — it is both a
    // Q_PROPERTY and a same-named Q_INVOKABLE, so the `()` form errors with
    // "is not a function"). Its NOTIFY-backed binding self-refreshes when the
    // list changes (e.g. a .luau reload), so no imperative refresh is needed.
    property var _cachedAlgos: settingsController.availableAlgorithms || []
    // `appSettings` is the ISettings context property (set app-wide via
    // rootContext); read it directly. Handlers here are attached to page-scope
    // objects and to AlgorithmPreviewCard (which declares no `appSettings`), so
    // the bare name resolves to the context property — the children that DO set
    // their own `appSettings` (PerScreenOverrideHelper, the LayoutComboBox nested
    // inside AlgorithmPreviewCard) are never in an ancestor binding's scope chain.
    readonly property string effectiveAlgorithm: settingValue("Algorithm", appSettings.defaultAutotileAlgorithm)
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
    readonly property var algoCapabilities: AlgoCaps.capabilitiesFor(root._cachedAlgos, root.selectedAlgorithm)
    readonly property bool algoSupportsSplitRatio: AlgoCaps.supportsSplitRatio(algoCapabilities)
    readonly property bool algoSupportsMasterCount: AlgoCaps.supportsMasterCount(algoCapabilities)
    readonly property bool algoSupportsCustomParams: AlgoCaps.supportsCustomParams(algoCapabilities)
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

    // Whether the algorithm uses a center layout (ratio/count labels say
    // "Center" instead of "Master"). Read straight from the catalog — the
    // hardcoded "three-column || centered-master" fallback this used to carry
    // was both duplicated on the simple page and unreachable, since
    // algorithmservice.cpp sets centerLayout unconditionally.
    readonly property bool algoCenterLayout: AlgoCaps.centerLayout(algoCapabilities)

    function settingValue(key, globalValue) {
        return psHelper.settingValue(key, globalValue);
    }

    function writeSetting(key, value, globalSetter) {
        psHelper.writeSetting(key, value, globalSetter);
    }

    contentHeight: content.implicitHeight
    clip: true

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

                // Preview, description and picker — the shared block, also
                // hosted by TilingSimplePage. Every layout input the user can
                // edit feeds the preview from the live slider handle, so the
                // diagram updates as the user drags any of them (master ratio
                // and master count included, not just max windows). The
                // "Max N windows" caption also tracks the windows slider.
                AlgorithmPreviewCard {
                    Layout.fillWidth: true
                    previewWidth: root.algorithmPreviewWidth
                    previewHeight: root.algorithmPreviewHeight
                    algorithmId: root.selectedAlgorithm
                    description: AlgoCaps.description(root.algoCapabilities)
                    currentAlgorithmId: root.effectiveAlgorithm
                    captionText: i18np("Max %n window", "Max %n windows", previewWindowSlider.slider.value)
                    windowCount: previewWindowSlider.slider.value
                    // An algorithm with no ratio control of its own draws at the
                    // ratio the catalog declares for it. An algorithm the
                    // catalog does not describe at all yields undefined there,
                    // which must not reach this `real` property, so fall back to
                    // the user's configured global ratio rather than a literal.
                    splitRatio: {
                        if (root.algoSupportsSplitRatio)
                            return masterRatioSlider.slider.value;

                        const catalogRatio = AlgoCaps.defaultSplitRatio(root.algoCapabilities);
                        return catalogRatio !== undefined ? catalogRatio : appSettings.autotileSplitRatio;
                    }
                    supportsMasterCount: root.algoSupportsMasterCount
                    masterCount: masterCountSlider.slider.value
                    customParams: root.liveCustomParams
                    zoneNumberDisplay: AlgoCaps.zoneNumberDisplay(root.algoCapabilities)
                    onAlgorithmActivated: selectedId => {
                        // An empty id means the combo's model rebuilt under the
                        // selection — fall back to the persisted global default.
                        const algoId = selectedId === "" ? appSettings.defaultAutotileAlgorithm : selectedId;
                        // Drive the page off the user's pick immediately, then
                        // persist. Only the Algorithm key is written; resetting
                        // global max-windows / split-ratio / master-count here
                        // would clobber a sibling algorithm's per-algorithm slot.
                        root.selectedAlgorithm = algoId;
                        root.writeSetting("Algorithm", algoId, function (v) {
                            appSettings.defaultAutotileAlgorithm = v;
                        });
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

                        accessibleName: i18n("Maximum windows")
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

                // Ratio and count rows, shown only when the catalog says the
                // algorithm exposes them.
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

                        accessibleName: root.algoCenterLayout ? i18n("Center ratio") : i18n("Master ratio")
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
                        accessibleName: i18n("Ratio step size")
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

                        accessibleName: root.algoCenterLayout ? i18n("Center count") : i18n("Master count")
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
                            title: paramLabel
                            description: paramDescription

                            // Number parameter: rendered as a SettingsSlider
                            SettingsSlider {
                                visible: modelData.type === "number"
                                accessibleName: paramLabel
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
