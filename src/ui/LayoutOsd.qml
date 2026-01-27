// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * Layout OSD Window - Shows visual layout preview when switching layouts
 * Auto-dismisses after a configurable duration
 * Provides visual feedback superior to text-only Plasma OSD
 */
Window {
    id: root

    // Layout data
    property string layoutId: ""
    property string layoutName: ""
    property var zones: []

    // Screen info for aspect ratio (bounded to prevent layout issues)
    property real screenAspectRatio: 16 / 9
    readonly property real safeAspectRatio: Math.max(0.5, Math.min(4.0, screenAspectRatio))

    // Timing
    property int displayDuration: 1500 // ms before auto-hide
    property int fadeInDuration: 150
    property int fadeOutDuration: 200

    // Theme colors
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property color highlightColor: Kirigami.Theme.highlightColor

    // Signals
    signal dismissed()

    // Window configuration
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint | Qt.BypassWindowManagerHint
    color: "transparent"

    // Size based on preview
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

    // Main container
    Rectangle {
        id: container

        anchors.centerIn: parent
        width: previewContainer.width + Kirigami.Units.gridUnit * 3
        height: previewContainer.height + nameLabel.height + Kirigami.Units.gridUnit * 3
        color: Qt.rgba(backgroundColor.r, backgroundColor.g, backgroundColor.b, 0.95)
        radius: Kirigami.Units.gridUnit * 1.5
        border.color: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.15)
        border.width: 1

        // Layout preview
        Item {
            id: previewContainer

            anchors.top: parent.top
            anchors.topMargin: Kirigami.Units.gridUnit * 1.5
            anchors.horizontalCenter: parent.horizontalCenter
            width: 200
            height: Math.round(200 / root.safeAspectRatio)

            // Background for preview area
            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.08)
                radius: Kirigami.Units.smallSpacing
            }

            // Zone preview using shared component
            QFZCommon.ZonePreview {
                anchors.fill: parent
                anchors.margins: 4
                zones: root.zones
                isHovered: false
                isActive: true
                zonePadding: 2
                minZoneSize: 12
                showZoneNumbers: true
                inactiveOpacity: 0.3
                activeOpacity: 0.6
                animationDuration: 150
            }

        }

        // Layout name label
        Label {
            id: nameLabel

            anchors.top: previewContainer.bottom
            anchors.topMargin: Kirigami.Units.gridUnit
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: Kirigami.Units.gridUnit * 1.5
            text: root.layoutName
            font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.2
            font.weight: Font.Medium
            color: textColor
            horizontalAlignment: Text.AlignHCenter
        }

        // Subtle highlight accent at top
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 4
            anchors.leftMargin: parent.width * 0.25
            anchors.rightMargin: parent.width * 0.25
            height: 3
            radius: 1.5
            color: highlightColor
            opacity: 0.7
        }

    }

    // Click to dismiss
    MouseArea {
        anchors.fill: parent
        onClicked: root.hide()
    }

    // Note: Escape shortcut removed - window uses BypassWindowManagerHint
    // and doesn't receive keyboard focus on Wayland

}
