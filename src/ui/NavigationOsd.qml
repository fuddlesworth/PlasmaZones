// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * Navigation OSD Window - Shows brief feedback when using keyboard navigation
 * to move or focus windows between zones
 * Auto-dismisses after ~1 second
 */
Window {
    id: root

    Accessible.name: i18n("Navigation feedback")
    Accessible.description: i18n("Brief feedback when using keyboard navigation to move or focus windows between zones")

    // Navigation feedback data
    property bool success: true
    property string action: "" // "move", "focus", "push", "restore", "float", "swap", "rotate", "snap", "cycle"
    property string reason: "" // Failure reason if !success

    // Zone data for preview
    property var zones: []
    property string highlightedZoneId: "" // Zone to highlight in preview

    // Screen info for aspect ratio (bounded to prevent layout issues)
    property real screenAspectRatio: 16 / 9
    readonly property real safeAspectRatio: Math.max(0.5, Math.min(4.0, screenAspectRatio))

    // Timing
    property int displayDuration: 1000 // ms before auto-hide (shorter than layout OSD)
    property int fadeInDuration: 150
    property int fadeOutDuration: 200

    // Theme colors
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property color highlightColor: Kirigami.Theme.highlightColor
    property color errorColor: Kirigami.Theme.negativeTextColor

    // Computed properties
    readonly property string directionText: {
        // Action-based messages for keyboard navigation feedback
        if (action === "move") {
            return success ? i18n("Moved") : i18n("No zone")
        } else if (action === "focus") {
            return success ? i18n("Focus") : i18n("No zone")
        } else if (action === "push") {
            return success ? i18n("Pushed") : i18n("No empty zone")
        } else if (action === "restore") {
            return success ? i18n("Restored") : i18n("Failed")
        } else if (action === "float") {
            return success ? i18n("Floating") : i18n("Failed")
        } else if (action === "snap") {
            return success ? i18n("Snapped") : i18n("Failed")
        } else if (action === "swap") {
            return success ? i18n("Swapped") : i18n("Failed")
        } else if (action === "rotate") {
            return success ? i18n("Rotated") : i18n("Failed")
        } else if (action === "cycle") {
            return success ? i18n("Focus") : i18n("Failed")
        } else {
            // Note: "autotile" and "algorithm" actions now use LayoutOsd with visual preview
            return success ? i18n("Done") : i18n("Failed")
        }
    }

    readonly property string directionArrow: {
        // Try to extract direction from reason (e.g., "no_adjacent_zone_left" -> "←")
        if (reason.indexOf("left") >= 0) {
            return "←"
        } else if (reason.indexOf("right") >= 0) {
            return "→"
        } else if (reason.indexOf("up") >= 0) {
            return "↑"
        } else if (reason.indexOf("down") >= 0) {
            return "↓"
        }
        // Default: no arrow
        return ""
    }

    // Signals
    signal dismissed()

    // Window configuration - LayerShellQt handles overlay behavior on Wayland
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"

    // Size based on preview (matches LayoutOsd)
    width: container.width + 40
    height: container.height + 40

    // Start hidden, will be shown with animation
    opacity: 0
    visible: false

    // Show the OSD with animation
    function show() {
        // Stop any running animations to prevent conflicts
        showAnimation.stop();
        hideAnimation.stop();
        dismissTimer.stop();

        // Reset state for fresh animation
        root.opacity = 0;
        container.scale = 0.8;

        root.visible = true;
        showAnimation.start();
        dismissTimer.restart();
    }

    // Hide the OSD with animation
    function hide() {
        // Stop show animation if running
        showAnimation.stop();
        dismissTimer.stop();

        // Only hide if visible
        if (root.visible) {
            hideAnimation.start();
        }
    }

    // Auto-dismiss timer
    Timer {
        id: dismissTimer

        interval: root.displayDuration
        onTriggered: root.hide()
    }

    // Show animation
    ParallelAnimation {
        id: showAnimation

        NumberAnimation {
            target: root
            property: "opacity"
            from: 0
            to: 1
            duration: root.fadeInDuration
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: container
            property: "scale"
            from: 0.8
            to: 1
            duration: root.fadeInDuration
            easing.type: Easing.OutBack
            easing.overshoot: 1.2
        }

    }

    // Hide animation
    SequentialAnimation {
        id: hideAnimation

        ParallelAnimation {
            NumberAnimation {
                target: root
                property: "opacity"
                to: 0
                duration: root.fadeOutDuration
                easing.type: Easing.InCubic
            }

            NumberAnimation {
                target: container
                property: "scale"
                to: 0.9
                duration: root.fadeOutDuration
                easing.type: Easing.InCubic
            }

        }

        ScriptAction {
            script: {
                root.visible = false;
                root.dismissed();
            }
        }

    }

    // Shadow effect
    MultiEffect {
        source: container
        anchors.fill: container
        shadowEnabled: true
        shadowColor: Qt.rgba(0, 0, 0, 0.5)
        shadowBlur: 1.0
        shadowVerticalOffset: 4
        shadowHorizontalOffset: 0
    }

    // Main container - matches LayoutOsd format exactly
    Rectangle {
        id: container

        anchors.centerIn: parent
        width: previewContainer.visible ? previewContainer.width + Kirigami.Units.gridUnit * 3 : Math.max(messageLabel.width + Kirigami.Units.gridUnit * 2, 200)
        height: previewContainer.height + messageLabel.height + Kirigami.Units.gridUnit * 3
        color: Qt.rgba(backgroundColor.r, backgroundColor.g, backgroundColor.b, 0.95)
        radius: Kirigami.Units.gridUnit * 1.5
        border.color: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.15)
        border.width: 1

        // Zone preview (matches LayoutOsd size and style)
        Item {
            id: previewContainer

            anchors.top: parent.top
            anchors.topMargin: Kirigami.Units.gridUnit * 1.5
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.success && zones.length > 0 ? 200 : 0
            height: root.success && zones.length > 0 ? Math.round(200 / root.safeAspectRatio) : 0
            visible: root.success && zones.length > 0

            // Background for preview area
            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.08)
                radius: Kirigami.Units.smallSpacing
            }

            // Zone preview using shared component (matches LayoutOsd settings)
            QFZCommon.ZonePreview {
                anchors.fill: parent
                anchors.margins: 4
                zones: root.zones
                isHovered: false
                isActive: true
                zonePadding: 2
                edgeGap: 2
                minZoneSize: 12
                showZoneNumbers: true
                inactiveOpacity: 0.3
                activeOpacity: 0.6
                animationDuration: 150
            }

        }

        // Message label (matches LayoutOsd nameLabel format)
        Label {
            id: messageLabel

            Accessible.name: root.directionArrow !== "" ? (root.directionArrow + " " + root.directionText) : root.directionText

            anchors.top: previewContainer.visible ? previewContainer.bottom : parent.top
            anchors.topMargin: previewContainer.visible ? Kirigami.Units.gridUnit : Kirigami.Units.gridUnit * 1.5
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: Kirigami.Units.gridUnit * 1.5
            text: root.directionArrow !== "" ? (root.directionArrow + " " + root.directionText) : root.directionText
            font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.2
            font.weight: Font.Medium
            color: root.success ? textColor : errorColor
            horizontalAlignment: Text.AlignHCenter
        }

    }

    // Click to dismiss
    MouseArea {
        anchors.fill: parent
        onClicked: root.hide()
    }

}
