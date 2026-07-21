// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "AlgorithmCapabilities.js" as AlgoCaps

/**
 * @brief The simple-mode tiling surface (SimpleOnly counterpart of the
 * Algorithm page).
 *
 * Leads with the everyday tiling decisions in one card: WHICH algorithm
 * tiles your windows (picker + live preview + description), how wide the
 * master area is, and how many windows tile. The shared
 * TilingWindowHandlingCard and TilingFocusCard follow, so placement,
 * drag/overflow behaviour, smart gaps and focus policy are all here too —
 * the same components the advanced Window page hosts. Left to advanced
 * mode: ratio step size, master count, custom script parameters and the
 * per-monitor overrides.
 *
 * Global scope only: unlike TilingAlgorithmPage there is no per-monitor
 * scope chip here, so every write goes to the global setting (or the
 * selected algorithm's global per-algorithm slot, matching the advanced
 * page's global path — never the flat global SplitRatio, which would
 * clobber sibling algorithms' slots).
 *
 * One consequence worth knowing: a per-monitor override authored in advanced
 * mode still WINS over anything edited here. A user who set a per-monitor Max
 * windows or Master ratio, then switched to simple, will see this page's
 * slider move while that monitor's effective value does not change, because
 * the override shadows the global slot this page writes. Simple mode shows no
 * scope chip, so nothing on screen says so.
 */
SettingsFlickable {
    id: root

    readonly property var settingsBridge: settingsController.tilingAlgorithmPage
    readonly property int algorithmPreviewWidth: Kirigami.Units.gridUnit * 18
    readonly property int algorithmPreviewHeight: Kirigami.Units.gridUnit * 10
    // Cache the availableAlgorithms PROPERTY (read, not call — it is both a
    // Q_PROPERTY and a same-named Q_INVOKABLE, so the `()` form errors).
    // Refreshed via the Connections below on its NOTIFY.
    property var _cachedAlgos: settingsController.availableAlgorithms || []
    // The ISettings context property captured at page scope: LayoutComboBox
    // declares its own `appSettings: settingsController`, which SHADOWS the
    // context property inside the combo's handlers (same trap as on
    // TilingAlgorithmPage — see appSettingsObj there).
    readonly property var appSettingsObj: appSettings
    // Working algorithm for preview/capability lookups. Imperative for the
    // same reason as on TilingAlgorithmPage: the combo's currentValue
    // transiently resets while its model rebuilds after a Save.
    property string selectedAlgorithm: root.appSettingsObj.defaultAutotileAlgorithm

    readonly property var algoCapabilities: AlgoCaps.capabilitiesFor(root._cachedAlgos, root.selectedAlgorithm)
    readonly property bool algoSupportsSplitRatio: AlgoCaps.supportsSplitRatio(algoCapabilities)
    readonly property bool algoCenterLayout: AlgoCaps.centerLayout(algoCapabilities)
    // Saved per-algorithm tuning (split ratio, master count, max windows) for
    // the selected algorithm — feeds the preview and re-seeds the ratio
    // slider on algorithm change. Custom params are read once per algorithm
    // for the preview; the simple page never edits them.
    property var algoSettings: ({})
    property var previewCustomParams: ({})

    function _seedFromAlgorithm() {
        root.algoSettings = root.settingsBridge.algorithmSettingsFor(root.selectedAlgorithm);
        const defs = root.settingsBridge.customParamsForAlgorithm(root.selectedAlgorithm) || [];
        let m = {};
        for (let i = 0; i < defs.length; ++i) {
            const d = defs[i];
            m[d.name] = (d.value !== undefined ? d.value : d.defaultValue);
        }
        root.previewCustomParams = m;
    }

    // Set while this page's own sliders write a per-algorithm slot, so the
    // resulting store NOTIFY does not re-seed mid-drag and fight the live
    // handle (same pattern as the animation defaults editor's committing
    // guard).
    //
    // Raised and lowered around the setter call in ONE statement block, which
    // is only correct because the signal it guards against,
    // autotilePerAlgorithmSettingsChanged, is emitted SYNCHRONOUSLY by
    // Settings::setAutotilePerAlgorithmSettings (src/config/settings.cpp, the Q_EMIT sits
    // directly after the store write, same thread, direct connection). If that
    // setter ever grows a save timer or a queued emit, the flag would already
    // be down when the signal lands and _seedFromAlgorithm would fight the
    // drag — lower it from Qt.callLater at that point, not before.
    property bool _committingSlot: false

    onSelectedAlgorithmChanged: _seedFromAlgorithm()
    Component.onCompleted: _seedFromAlgorithm()

    // External per-algorithm slot changes (a Discard, a profile switch, an
    // edit on the advanced Algorithm page in the same session) move the
    // saved tuning under us — follow them.
    Connections {
        function onAutotilePerAlgorithmSettingsChanged() {
            if (!root._committingSlot)
                root._seedFromAlgorithm();
        }

        target: root.appSettingsObj
    }

    // External edits (a Discard, a profile switch, a rule apply) move the
    // persisted default under us — follow it.
    Connections {
        function onDefaultAutotileAlgorithmChanged() {
            root.selectedAlgorithm = root.appSettingsObj.defaultAutotileAlgorithm;
        }

        target: root.appSettingsObj
    }

    Connections {
        function onAvailableAlgorithmsChanged() {
            root._cachedAlgos = settingsController.availableAlgorithms || [];
        }

        target: settingsController
    }

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Tiling")
            searchAnchor: "tilingSimple"
            // Matches every shared card below it, and the advanced
            // counterpart's lead card — a non-collapsible card at the top of a
            // page of collapsible ones reads as a broken affordance.
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // Preview, description and picker — the shared block, also
                // hosted by TilingAlgorithmPage. Fed from the saved tuning
                // instead of live slider handles, and with no window-count
                // caption, since this page leads with the picker itself.
                //
                // masterCount reads root.appSettingsObj, NOT the bare
                // `appSettings`: AlgorithmPreview declares its own
                // `appSettings`, which shadows the context property inside the
                // component — the same trap documented for LayoutComboBox. The
                // controller has no autotileMasterCount, so the bare form reads
                // undefined, coerces to 0, and is clamped to 1 downstream, and
                // the preview would silently ignore the configured master
                // count.
                AlgorithmPreviewCard {
                    Layout.fillWidth: true
                    previewWidth: root.algorithmPreviewWidth
                    previewHeight: root.algorithmPreviewHeight
                    searchAnchor: "simpleAlgorithm"
                    algorithmId: root.selectedAlgorithm
                    description: root.algoCapabilities ? (root.algoCapabilities.description || "") : ""
                    currentAlgorithmId: root.appSettingsObj.defaultAutotileAlgorithm
                    windowCount: maxWindowsSlider.slider.value
                    splitRatio: root.algoSupportsSplitRatio ? masterRatioSlider.slider.value : AlgoCaps.defaultSplitRatio(root.algoCapabilities)
                    supportsMasterCount: AlgoCaps.supportsMasterCount(root.algoCapabilities)
                    masterCount: root.algoSettings.masterCount !== undefined ? root.algoSettings.masterCount : root.appSettingsObj.autotileMasterCount
                    customParams: root.previewCustomParams
                    zoneNumberDisplay: AlgoCaps.zoneNumberDisplay(root.algoCapabilities)
                    onAlgorithmActivated: selectedId => {
                        // An empty id means the combo's model rebuilt under the
                        // selection — fall back to the persisted default.
                        const algoId = selectedId === "" ? root.appSettingsObj.defaultAutotileAlgorithm : selectedId;
                        root.selectedAlgorithm = algoId;
                        root.appSettingsObj.defaultAutotileAlgorithm = algoId;
                    }
                }

                SettingsSeparator {
                    enabled: root.algoSupportsSplitRatio
                }

                SettingsRow {
                    enabled: root.algoSupportsSplitRatio
                    title: root.algoCenterLayout ? i18n("Center ratio") : i18n("Master ratio")
                    searchAnchor: "simpleMasterRatio"
                    description: root.algoCenterLayout ? i18n("Width proportion allocated to the center column") : i18n("Width proportion allocated to the master area")

                    SettingsSlider {
                        id: masterRatioSlider

                        accessibleName: root.algoCenterLayout ? i18n("Center ratio") : i18n("Master ratio")
                        from: root.settingsBridge.autotileSplitRatioMin
                        to: root.settingsBridge.autotileSplitRatioMax
                        stepSize: 0.05
                        value: root.algoSettings.splitRatio !== undefined ? root.algoSettings.splitRatio : root.appSettingsObj.autotileSplitRatio
                        formatValue: function (v) {
                            return Math.round(v * 100) + "%";
                        }
                        onMoved: value => {
                            // Per-algorithm slot for the selected algorithm —
                            // the same global path the Algorithm page uses.
                            let m = Object.assign({}, root.algoSettings);
                            m.splitRatio = value;
                            root.algoSettings = m;
                            // try/finally: QML has no RAII, and a throw here
                            // would latch the flag permanently, after which
                            // this page silently stops following a Discard,
                            // a profile switch or an advanced-page edit.
                            root._committingSlot = true;
                            try {
                                root.settingsBridge.setAlgorithmSplitRatio(root.selectedAlgorithm, value);
                            } finally {
                                root._committingSlot = false;
                            }
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Max windows")
                    searchAnchor: "simpleMaxWindows"
                    description: i18n("Maximum number of windows to tile")

                    SettingsSlider {
                        id: maxWindowsSlider

                        accessibleName: i18n("Maximum windows")
                        from: root.settingsBridge.autotileMaxWindowsMin
                        to: root.settingsBridge.autotileMaxWindowsMax
                        stepSize: 1
                        value: root.algoSettings.maxWindows !== undefined ? root.algoSettings.maxWindows : root.appSettingsObj.autotileMaxWindows
                        formatValue: function (v) {
                            return Math.round(v).toString();
                        }
                        onMoved: value => {
                            let m = Object.assign({}, root.algoSettings);
                            m.maxWindows = Math.round(value);
                            root.algoSettings = m;
                            // try/finally: QML has no RAII, and a throw here
                            // would latch the flag permanently, after which
                            // this page silently stops following a Discard,
                            // a profile switch or an advanced-page edit.
                            root._committingSlot = true;
                            try {
                                root.settingsBridge.setAlgorithmMaxWindows(root.selectedAlgorithm, Math.round(value));
                            } finally {
                                root._committingSlot = false;
                            }
                        }
                    }
                }
            }
        }

        // The shared cards below are the same components the advanced
        // Tiling → Window page hosts — full parity (Smart gaps included),
        // no forked rows.
        TilingWindowHandlingCard {
            Layout.fillWidth: true
        }

        TilingFocusCard {
            Layout.fillWidth: true
        }
    }
}
