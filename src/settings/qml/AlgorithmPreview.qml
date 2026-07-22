// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon
import "AlgorithmCapabilities.js" as AlgoCaps

/**
 * @brief Visual preview of autotiling algorithm layouts
 *
 * Renders a preview of how windows would be arranged with
 * the given algorithm settings. Delegates zone calculation to the
 * real C++ algorithm classes via appSettings.generateAlgorithmPreview(),
 * and renders using the shared ZonePreview component.
 */
Item {
    id: root

    // Settings reference for calling generateAlgorithmPreview()
    required property var appSettings
    // Algorithm configuration
    property string algorithmId: ""
    property int windowCount: 4
    property real splitRatio: 0.6
    property int masterCount: 1
    // Per-algorithm custom param values (name → value). Just another layout
    // input, on equal footing with windowCount / splitRatio / masterCount: a
    // change re-runs the C++ preview through the same recalc path.
    property var customParams: ({})
    // Computed zones, rendered by ZonePreview. Recomputed by recalcTimer, which
    // throttles to ~60fps so several input changes in one frame coalesce into a
    // single C++ call.
    property var zones: []
    property string zoneNumberDisplay: "all"
    // Read the availableAlgorithms PROPERTY (not a call): it is exposed as both a
    // Q_PROPERTY and a same-named Q_INVOKABLE, so `availableAlgorithms()` resolves
    // to the property value and errors with "is not a function". The property has
    // a NOTIFY, so the Connections below refresh this cache when the list changes.
    property var _cachedAlgos: root.appSettings ? (root.appSettings.availableAlgorithms || []) : []
    // Resolved once at the root level (not per-delegate) so the Repeater's
    // delegates share one catalog scan instead of repeating it N times.
    readonly property var _currentAlgoCapabilities: AlgoCaps.capabilitiesFor(root._cachedAlgos, root.algorithmId)
    readonly property bool _currentAlgoSupportsMasterCount: AlgoCaps.supportsMasterCount(root._currentAlgoCapabilities)
    readonly property bool _currentAlgoProducesOverlappingZones: AlgoCaps.producesOverlappingZones(root._currentAlgoCapabilities)
    function recalcZones() {
        // The host may not have resolved its controller yet, and
        // Component.onCompleted starts the timer unconditionally.
        if (!root.appSettings) {
            root.zones = [];
            return;
        }
        if (root.algorithmId !== "") {
            root.zones = root.appSettings.generateAlgorithmPreview(root.algorithmId, root.windowCount, root.splitRatio, root.masterCount, root.customParams);
            // Retry once if a stale watchdog interrupt caused empty results —
            // the first call clears the interrupt flag, so the second succeeds.
            if (root.zones.length === 0 && root.windowCount > 0)
                root.zones = root.appSettings.generateAlgorithmPreview(root.algorithmId, root.windowCount, root.splitRatio, root.masterCount, root.customParams);
        } else {
            root.zones = [];
        }
    }

    onAlgorithmIdChanged: recalcTimer.restart()
    onWindowCountChanged: recalcTimer.restart()
    onSplitRatioChanged: recalcTimer.restart()
    onMasterCountChanged: recalcTimer.restart()
    onCustomParamsChanged: recalcTimer.restart()
    Component.onCompleted: recalcTimer.start()

    Connections {
        function onAvailableAlgorithmsChanged() {
            // _cachedAlgos self-refreshes through its NOTIFY-backed binding, so
            // there is nothing to reassign here (a bare `_cachedAlgos = …` would
            // sever that binding). Only the preview needs a nudge: the zones come
            // from a Q_INVOKABLE with no NOTIFY of its own, so nothing else
            // re-runs it when a .luau is edited and the registry reloads under an
            // unchanged algorithmId. Without this the drawn layout stays at the
            // previous revision.
            recalcTimer.restart();
        }

        target: root.appSettings
    }

    Timer {
        id: recalcTimer

        interval: 16 // ~60fps cap
        onTriggered: root.recalcZones()
    }

    // Render using shared ZonePreview (same component used in LayoutComboBox dropdowns)
    QFZCommon.ZonePreview {
        id: zonePreview

        anchors.fill: parent
        zones: root.zones
        isHovered: true
        showZoneNumbers: true
        zoneNumberDisplay: root.zoneNumberDisplay
        producesOverlappingZones: root._currentAlgoProducesOverlappingZones
        // Straight from the shared zone-color pipeline (ZoneColorDefaults
        // resolves the user's effective zone colors, the same values the daemon
        // pushes into its live overlays). Fidelity is base-color-only: the
        // border alpha is re-applied at a fixed 0.9 and fill opacity comes from
        // ZonePreview's activeOpacity, so a custom alpha in the effective
        // colors is not reproduced here.
        highlightColor: QFZCommon.ZoneColorDefaults.previewActiveZoneColor
        borderColor: Qt.rgba(QFZCommon.ZoneColorDefaults.previewZoneBorderColor.r, QFZCommon.ZoneColorDefaults.previewZoneBorderColor.g, QFZCommon.ZoneColorDefaults.previewZoneBorderColor.b, 0.9)
        zonePadding: 1
        edgeGap: 0
        minZoneSize: 4
        showMasterDot: root._currentAlgoSupportsMasterCount && root.algorithmId !== ""
        masterCount: root.masterCount
        animationDuration: 0
    }
}
