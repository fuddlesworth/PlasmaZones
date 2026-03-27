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
    // Color customization (passed through to ZonePreview)
    property color windowColor: Kirigami.Theme.highlightColor
    property color windowBorder: Kirigami.Theme.textColor
    // Throttled zone calculation (~60fps cap) to avoid redundant recalcs
    // when multiple properties change in the same frame
    property var zones: []
    property string zoneNumberDisplay: "all"
    // Algorithm display name (avoids hardcoded switch statement)
    property string algorithmName: ""
    // Algorithm name label (hidden when used inside the Tiling tab's algorithm section
    // where the name is already shown alongside the combo box)
    property bool showLabel: true

    function recalcZones() {
        if (root.algorithmId !== "")
            root.zones = root.appSettings.generateAlgorithmPreview(root.algorithmId, root.windowCount, root.splitRatio, root.masterCount);

    }

    onAlgorithmIdChanged: recalcTimer.restart()
    onWindowCountChanged: recalcTimer.restart()
    onSplitRatioChanged: recalcTimer.restart()
    onMasterCountChanged: recalcTimer.restart()
    Component.onCompleted: recalcTimer.start()

    Timer {
        id: recalcTimer

        interval: 16 // ~60fps cap
        onTriggered: root.recalcZones()
    }

    // Render using shared ZonePreview (same component used in LayoutComboBox dropdowns)
    QFZCommon.ZonePreview {
        anchors.fill: parent
        zones: root.zones
        isHovered: true
        showZoneNumbers: true
        zoneNumberDisplay: root.zoneNumberDisplay
        highlightColor: Qt.rgba(root.windowColor.r, root.windowColor.g, root.windowColor.b, 0.7)
        borderColor: Qt.rgba(root.windowBorder.r, root.windowBorder.g, root.windowBorder.b, 0.9)
        zonePadding: 1
        edgeGap: 0
        minZoneSize: 4
        animationDuration: 0
    }

    // Master indicator dots overlaid on master windows
    Repeater {
        model: root.zones

        Rectangle {
            required property var modelData
            required property int index

            visible: (root.algorithmId === "master-stack" || root.algorithmId === "wide" || root.algorithmId === "centered-master") && index < root.masterCount
            x: ((modelData.relativeGeometry && modelData.relativeGeometry.x) || 0) * root.width + Kirigami.Units.smallSpacing
            y: ((modelData.relativeGeometry && modelData.relativeGeometry.y) || 0) * root.height + Kirigami.Units.smallSpacing
            width: Kirigami.Units.smallSpacing * 2
            height: Kirigami.Units.smallSpacing * 2
            radius: Kirigami.Units.smallSpacing
            color: Kirigami.Theme.positiveTextColor
        }

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
