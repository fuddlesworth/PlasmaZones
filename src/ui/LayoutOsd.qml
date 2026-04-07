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
    // contentWrapper
    // Note: Escape shortcut removed - layer-shell overlay windows do not
    // receive keyboard focus on Wayland (KeyboardInteractivityNone)

    id: root

    // Layout data
    property string layoutId: ""
    property string layoutName: ""
    property var zones: []
    // Layout category: 0=Manual (matches LayoutCategory in C++)
    property int category: 0
    property bool autoAssign: false
    // Autotile algorithm metadata
    property bool showMasterDot: false
    property int masterCount: 1
    property bool producesOverlappingZones: false
    property string zoneNumberDisplay: "all"
    // Screen info for aspect ratio (bounded to prevent layout issues)
    property real screenAspectRatio: 16 / 9
    readonly property real safeAspectRatio: Math.max(0.5, Math.min(4, screenAspectRatio))
    // Layout's intended aspect ratio class (set from C++)
    property string aspectRatioClass: "any"
    // Resolved preview AR: use layout's class if set, fall back to screen's AR
    readonly property real previewAspectRatio: {
        switch (aspectRatioClass) {
        case "standard":
            return 16 / 9;
        case "ultrawide":
            return 21 / 9;
        case "super-ultrawide":
            return 32 / 9;
        case "portrait":
            return 9 / 16;
        default:
            return safeAspectRatio;
        }
    }
    // Timing
    property int displayDuration: 1500
    // ms before auto-hide
    property int fadeInDuration: 150
    property int fadeOutDuration: 200
    // Theme colors
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property color highlightColor: Kirigami.Theme.highlightColor
    // Font properties for zone number labels
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false
    property bool locked: false
    property bool disabled: false
    property string disabledReason: ""

    // Signals
    signal dismissed()

    // Show the OSD with animation
    function show() {
        // Stop any running animations to prevent conflicts
        showAnimation.stop();
        hideAnimation.stop();
        dismissTimer.stop();
        // Reset state for fresh animation (animate wrapper, not window)
        contentWrapper.opacity = 0;
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
        if (root.visible)
            hideAnimation.start();

    }

    // Window configuration - QPA layer-shell plugin handles overlay behavior on Wayland
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    // Size based on container (which is inside contentWrapper)
    width: container.width + Math.round(Kirigami.Units.gridUnit * 2.5)
    height: container.height + Math.round(Kirigami.Units.gridUnit * 2.5)
    // Start hidden, will be shown with animation
    // Note: Don't set Window.opacity - use contentWrapper.opacity instead
    // QWaylandWindow::setOpacity() is not implemented and logs warnings
    visible: false

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
            target: contentWrapper
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
                target: contentWrapper
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

    // Content wrapper - animates opacity instead of Window
    // This avoids "This plugin does not support setting window opacity" on Wayland
    Item {
        id: contentWrapper

        Accessible.name: i18n("Layout indicator")
        anchors.fill: parent
        opacity: 0

        // Shadow effect
        MultiEffect {
            source: container
            anchors.fill: container
            shadowEnabled: true
            shadowColor: Qt.rgba(0, 0, 0, 0.5)
            shadowBlur: 1
            shadowVerticalOffset: 4
            shadowHorizontalOffset: 0
        }

        // Main container
        Rectangle {
            id: container

            anchors.centerIn: parent
            width: previewContainer.width + Kirigami.Units.gridUnit * 3
            height: previewContainer.height + nameLabelRow.height + Kirigami.Units.gridUnit * 3
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
                width: Kirigami.Units.gridUnit * 11
                height: Math.round(Kirigami.Units.gridUnit * 11 / root.previewAspectRatio)

                // Background for preview area
                Rectangle {
                    anchors.fill: parent
                    color: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.08)
                    radius: Kirigami.Units.smallSpacing
                }

                // Zone preview using shared component
                QFZCommon.ZonePreview {
                    id: zonePreview

                    anchors.fill: parent
                    anchors.margins: 4
                    zones: root.zones
                    isHovered: false
                    isActive: true
                    zonePadding: 2
                    edgeGap: 2
                    minZoneSize: 12
                    showZoneNumbers: true
                    producesOverlappingZones: root.producesOverlappingZones
                    zoneNumberDisplay: root.zoneNumberDisplay
                    inactiveOpacity: 0.3
                    activeOpacity: 0.6
                    fontFamily: root.fontFamily
                    fontSizeScale: root.fontSizeScale
                    fontWeight: root.fontWeight
                    fontItalic: root.fontItalic
                    fontUnderline: root.fontUnderline
                    showMasterDot: root.showMasterDot
                    masterCount: root.masterCount
                    fontStrikeout: root.fontStrikeout
                    animationDuration: 150
                }

            }

            // Lock overlay (shown on top of preview when locked)
            Rectangle {
                anchors.fill: previewContainer
                visible: root.locked
                color: Qt.rgba(0, 0, 0, 0.5)
                radius: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    anchors.centerIn: parent
                    source: "object-locked"
                    width: Kirigami.Units.iconSizes.large
                    height: Kirigami.Units.iconSizes.large
                    color: Kirigami.Theme.highlightedTextColor
                }

            }

            // Disabled overlay (shown when context is disabled for this desktop/screen)
            Rectangle {
                anchors.fill: previewContainer
                visible: root.disabled
                color: Qt.rgba(0, 0, 0, 0.55)
                radius: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    anchors.centerIn: parent
                    source: "dialog-cancel"
                    width: Kirigami.Units.iconSizes.large
                    height: Kirigami.Units.iconSizes.large
                    color: Kirigami.Theme.neutralTextColor
                }

            }

            // Layout name with category badge
            Row {
                id: nameLabelRow

                anchors.top: previewContainer.bottom
                anchors.topMargin: Kirigami.Units.gridUnit
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottomMargin: Kirigami.Units.gridUnit * 1.5
                spacing: Kirigami.Units.smallSpacing

                // Category badge (layout type)
                QFZCommon.CategoryBadge {
                    id: categoryBadge

                    anchors.verticalCenter: parent.verticalCenter
                    category: root.category
                    autoAssign: root.autoAssign === true
                }

                Label {
                    id: nameLabel

                    anchors.verticalCenter: parent.verticalCenter
                    text: root.disabled ? root.disabledReason : root.locked ? i18n("%1 (Locked)", root.layoutName) : root.layoutName
                    font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.2
                    font.weight: Font.Medium
                    color: textColor
                    horizontalAlignment: Text.AlignHCenter
                }

            }

        }

        // Click to dismiss
        MouseArea {
            anchors.fill: parent
            onClicked: root.hide()
        }

    }

}
