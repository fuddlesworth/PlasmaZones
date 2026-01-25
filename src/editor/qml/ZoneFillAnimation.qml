// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief Fill animation for zone expansion operations
 *
 * Handles animated fill when expanding zones to fill space.
 * Extracted from EditorZone.qml to reduce file size.
 */
Item {
    id: fillAnimator

    // Required references
    required property Item zoneRoot
    required property var controller
    required property real canvasWidth
    required property real canvasHeight
    // Animation targets
    property real targetX: 0
    property real targetY: 0
    property real targetWidth: 0
    property real targetHeight: 0
    property bool externalAnimation: false
    // Store zone center for consistent algorithm usage
    property real fillCenterX: 0.5
    property real fillCenterY: 0.5

    // Start fill animation from current geometry to target
    function startFillAnimation(newTargetX, newTargetY, newTargetWidth, newTargetHeight) {
        if (fillAnimation.running)
            fillAnimation.stop();

        zoneRoot.isAnimatingFill = true;
        externalAnimation = true;
        targetX = newTargetX;
        targetY = newTargetY;
        targetWidth = newTargetWidth;
        targetHeight = newTargetHeight;
        fillAnimation.start();
    }

    // Trigger animated fill operation
    function animatedExpandToFill() {
        if (!controller || !zoneRoot.zoneId)
            return ;

        // Guard against re-entrancy - don't start if already animating
        if (fillAnimation.running || zoneRoot.isAnimatingFill)
            return ;

        // Calculate the fill region using zone center
        var zoneX = zoneRoot.zoneData ? zoneRoot.zoneData.x : 0;
        var zoneY = zoneRoot.zoneData ? zoneRoot.zoneData.y : 0;
        var zoneW = zoneRoot.zoneData ? zoneRoot.zoneData.width : 0.25;
        var zoneH = zoneRoot.zoneData ? zoneRoot.zoneData.height : 0.25;
        var centerX = zoneX + zoneW / 2;
        var centerY = zoneY + zoneH / 2;
        // Store center coords for use in expandToFillRequested
        fillCenterX = centerX;
        fillCenterY = centerY;
        var region = controller.calculateFillRegion(zoneRoot.zoneId, centerX, centerY);
        if (region && region.width !== undefined && region.width > 0) {
            zoneRoot.isAnimatingFill = true;
            externalAnimation = false;
            targetX = region.x * canvasWidth;
            targetY = region.y * canvasHeight;
            targetWidth = region.width * canvasWidth;
            targetHeight = region.height * canvasHeight;
            fillAnimation.start();
        } else {
            // No valid region - call expand directly with zone center coords
            zoneRoot.expandToFillWithCoords(fillCenterX, fillCenterY);
        }
    }

    ParallelAnimation {
        // Pass zone center coords to use smartFillZone algorithm (same as calculateFillRegion)

        id: fillAnimation

        onFinished: {
            zoneRoot.isAnimatingFill = false;
            if (!fillAnimator.externalAnimation)
                zoneRoot.expandToFillWithCoords(fillAnimator.fillCenterX, fillAnimator.fillCenterY);

            fillAnimator.externalAnimation = false;
        }

        NumberAnimation {
            target: zoneRoot
            property: "visualX"
            to: fillAnimator.targetX
            duration: 150
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: zoneRoot
            property: "visualY"
            to: fillAnimator.targetY
            duration: 150
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: zoneRoot
            property: "visualWidth"
            to: fillAnimator.targetWidth
            duration: 150
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: zoneRoot
            property: "visualHeight"
            to: fillAnimator.targetHeight
            duration: 150
            easing.type: Easing.OutCubic
        }

    }

}
