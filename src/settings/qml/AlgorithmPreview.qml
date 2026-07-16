// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

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
    // Color customization (passed through to ZonePreview). The defaults track
    // the shared zone-color pipeline (ZoneColorDefaults resolves the user's
    // effective zone colors, the same values the daemon pushes into its live
    // overlays). Fidelity is base-color-only: the ZonePreview bindings below
    // re-apply fixed 0.7 / 0.9 alphas, so a custom alpha in the effective
    // colors is not reproduced here.
    property color windowColor: QFZCommon.ZoneColorDefaults.previewActiveZoneColor
    property color windowBorder: QFZCommon.ZoneColorDefaults.previewZoneBorderColor
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
    // Computed once at the root level (not per-delegate) to avoid N redundant
    // C++ calls inside the Repeater delegate.
    readonly property bool _currentAlgoSupportsMasterCount: {
        var algos = root._cachedAlgos;
        for (var i = 0; i < algos.length; i++) {
            if (algos[i].id === root.algorithmId && algos[i].supportsMasterCount)
                return true;
        }
        return false;
    }
    readonly property bool _currentAlgoProducesOverlappingZones: {
        var algos = root._cachedAlgos;
        for (var i = 0; i < algos.length; i++) {
            if (algos[i].id === root.algorithmId && algos[i].producesOverlappingZones)
                return true;
        }
        return false;
    }
    // Algorithm display name (avoids hardcoded switch statement)
    property string algorithmName: ""
    // Algorithm name label (hidden when used inside the Tiling tab's algorithm section
    // where the name is already shown alongside the combo box)
    property bool showLabel: true

    function recalcZones() {
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
            root._cachedAlgos = root.appSettings.availableAlgorithms || [];
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
        highlightColor: Qt.rgba(root.windowColor.r, root.windowColor.g, root.windowColor.b, 0.7)
        borderColor: Qt.rgba(root.windowBorder.r, root.windowBorder.g, root.windowBorder.b, 0.9)
        zonePadding: 1
        edgeGap: 0
        minZoneSize: 4
        showMasterDot: root._currentAlgoSupportsMasterCount && root.algorithmId !== ""
        masterCount: root.masterCount
        animationDuration: 0
    }

    Label {
        visible: root.showLabel
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: Math.round(Kirigami.Units.smallSpacing / 2)
        text: root.algorithmName || root.algorithmId
        font: Kirigami.Theme.smallFont
        opacity: 0.5
    }
}
