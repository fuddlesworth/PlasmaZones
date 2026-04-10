// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import PlasmaZones 1.0
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
    // Animation profile properties (set from C++ before show)
    property int animInDuration: 150
    property string animInStyle: "scalein"
    property real animInStyleParam: 0.8
    property int animOutDuration: 200
    property string animOutStyle: "scalein"
    property real animOutStyleParam: 0.9
    // Shader source URLs (set from C++ via AnimationShaderRegistry)
    property url animInShaderSource: ""
    property var animInShaderParams: ({
    })
    property url animOutShaderSource: ""
    property var animOutShaderParams: ({
    })
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
    property string disabledReason

    // Signals
    signal dismissed()

    // Show the OSD with animation (style-adaptive, supports shader transitions)
    function show() {
        showAnimation.stop();
        hideAnimation.stop();
        shaderShowAnim.stop();
        shaderHideAnim.stop();
        dismissTimer.stop();
        if (animInShaderSource.toString() !== "") {
            // ── Shader transition path (loaded from AnimationShaderRegistry) ──
            contentWrapper.opacity = 1;
            container.scale = 1;
            contentWrapper.layer.enabled = true;
            shaderTransition.shaderSource = animInShaderSource;
            shaderTransition.direction = 0;
            shaderTransition.duration = animInDuration;
            shaderTransition.styleParam = animInStyleParam;
            shaderTransition.shaderParams = animInShaderParams || {
            };
            shaderTransition.progress = 0;
            shaderTransition.visible = true;
            root.visible = true;
            shaderShowAnim.duration = animInDuration;
            shaderShowAnim.start();
        } else {
            // ── Built-in QML animation path ──
            contentWrapper.opacity = 0;
            container.scale = (animInStyle === "scalein") ? animInStyleParam : 1;
            container.y = 0;
            showOpacityAnim.duration = animInDuration;
            showTransformAnim.duration = (animInStyle === "fadein" || animInStyle === "none") ? 0 : animInDuration;
            if (animInStyle === "scalein") {
                showTransformAnim.target = container;
                showTransformAnim.property = "scale";
                showTransformAnim.from = animInStyleParam;
                showTransformAnim.to = 1;
                showTransformAnim.easing.type = Easing.OutBack;
                showTransformAnim.easing.overshoot = 1.2;
            } else if (animInStyle === "slideup") {
                showTransformAnim.target = container;
                showTransformAnim.property = "y";
                showTransformAnim.from = animInStyleParam;
                showTransformAnim.to = 0;
                showTransformAnim.easing.type = Easing.OutCubic;
                showTransformAnim.easing.overshoot = 0;
            } else {
                showTransformAnim.duration = 0;
            }
            root.visible = true;
            showAnimation.start();
        }
        dismissTimer.restart();
    }

    // Hide the OSD with animation (style-adaptive, supports shader transitions)
    function hide() {
        showAnimation.stop();
        shaderShowAnim.stop();
        dismissTimer.stop();
        if (!root.visible)
            return ;

        if (animOutShaderSource.toString() !== "") {
            // ── Shader transition path (loaded from AnimationShaderRegistry) ──
            contentWrapper.opacity = 1;
            contentWrapper.layer.enabled = true;
            shaderTransition.shaderSource = animOutShaderSource;
            shaderTransition.direction = 1;
            shaderTransition.duration = animOutDuration;
            shaderTransition.styleParam = animOutStyleParam;
            shaderTransition.shaderParams = animOutShaderParams || {
            };
            shaderTransition.progress = 1;
            shaderTransition.visible = true;
            shaderHideAnim.duration = animOutDuration;
            shaderHideAnim.start();
        } else {
            // ── Built-in QML animation path ──
            hideOpacityAnim.duration = animOutDuration;
            hideTransformAnim.duration = (animOutStyle === "fadein" || animOutStyle === "none") ? 0 : animOutDuration;
            if (animOutStyle === "scalein") {
                hideTransformAnim.target = container;
                hideTransformAnim.property = "scale";
                hideTransformAnim.to = animOutStyleParam;
                hideTransformAnim.easing.type = Easing.InCubic;
            } else if (animOutStyle === "slideup") {
                hideTransformAnim.target = container;
                hideTransformAnim.property = "y";
                hideTransformAnim.to = animOutStyleParam;
                hideTransformAnim.easing.type = Easing.InCubic;
            } else {
                hideTransformAnim.duration = 0;
            }
            hideAnimation.start();
        }
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

    // Show animation (configured dynamically in show())
    ParallelAnimation {
        id: showAnimation

        NumberAnimation {
            id: showOpacityAnim

            target: contentWrapper
            property: "opacity"
            from: 0
            to: 1
            duration: root.animInDuration
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            id: showTransformAnim

            target: container
            property: "scale"
            from: root.animInStyleParam
            to: 1
            duration: root.animInDuration
            easing.type: Easing.OutBack
            easing.overshoot: 1.2
        }

    }

    // Hide animation (configured dynamically in hide())
    SequentialAnimation {
        id: hideAnimation

        ParallelAnimation {
            NumberAnimation {
                id: hideOpacityAnim

                target: contentWrapper
                property: "opacity"
                to: 0
                duration: root.animOutDuration
                easing.type: Easing.InCubic
            }

            NumberAnimation {
                id: hideTransformAnim

                target: container
                property: "scale"
                to: root.animOutStyleParam
                duration: root.animOutDuration
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

    // ── Shader transition animations ──
    NumberAnimation {
        id: shaderShowAnim

        target: shaderTransition
        property: "progress"
        from: 0
        to: 1
        duration: root.animInDuration
        easing.type: Easing.OutCubic
        onFinished: {
            // Disable layer after animation for performance
            contentWrapper.layer.enabled = false;
            shaderTransition.visible = false;
        }
    }

    SequentialAnimation {
        id: shaderHideAnim

        NumberAnimation {
            target: shaderTransition
            property: "progress"
            from: 1
            to: 0
            duration: root.animOutDuration
            easing.type: Easing.InCubic
        }

        ScriptAction {
            script: {
                contentWrapper.layer.enabled = false;
                shaderTransition.visible = false;
                root.visible = false;
                root.dismissed();
            }
        }

    }

    // Shader transition renderer — renders contentWrapper through a shader effect
    AnimationShaderItem {
        id: shaderTransition

        anchors.fill: contentWrapper
        source: contentWrapper
        visible: false
        progress: 0
    }

    // Content wrapper - animates opacity instead of Window
    // This avoids "This plugin does not support setting window opacity" on Wayland
    Item {
        id: contentWrapper

        Accessible.name: root.disabled ? root.disabledReason : i18n("Layout indicator")
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

            // Lock overlay (shown on top of preview when locked — mutually exclusive with disabled)
            Rectangle {
                anchors.fill: previewContainer
                visible: root.locked && !root.disabled
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
                color: Qt.rgba(0, 0, 0, 0.5)
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

                // Category badge (layout type) — hidden when disabled
                QFZCommon.CategoryBadge {
                    id: categoryBadge

                    visible: !root.disabled
                    anchors.verticalCenter: parent.verticalCenter
                    category: root.category
                    autoAssign: root.autoAssign === true
                }

                Label {
                    id: nameLabel

                    anchors.verticalCenter: parent.verticalCenter
                    text: root.disabled ? root.disabledReason : (root.locked ? i18n("%1 (Locked)", root.layoutName) : root.layoutName)
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
