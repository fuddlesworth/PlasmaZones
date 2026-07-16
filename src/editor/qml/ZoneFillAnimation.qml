// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.phosphor.animation

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

    // Abort a running fill animation without committing its target geometry.
    // Drag/resize handlers call this before capturing start geometry so a
    // press mid-animation neither races the animation's property writes nor
    // gets a stale commit from onFinished underneath the new operation.
    function stopFillAnimation() {
        if (!fillAnimation.running && !zoneRoot.isAnimatingFill)
            return;

        // Mark as external before stopping: should stop() ever deliver
        // onFinished, the commit branch (expandToFillWithCoords) must not run.
        externalAnimation = true;
        fillAnimation.stop();
        zoneRoot.isAnimatingFill = false;
        externalAnimation = false;
    }

    // Trigger animated fill operation
    function animatedExpandToFill() {
        if (!controller || !zoneRoot.zoneId)
            return;

        // Guard against re-entrancy - don't start if already animating
        if (fillAnimation.running || zoneRoot.isAnimatingFill)
            return;

        // Calculate the fill region using the zone center in normalized canvas
        // coords. Derive it from the visual geometry rather than zoneData:
        // fixed-mode zones store screen pixels in zoneData.x/y/width/height, so
        // treating those as normalized would point at a bogus center.
        var centerX = (canvasWidth > 0) ? (zoneRoot.visualX + zoneRoot.visualWidth / 2) / canvasWidth : 0.5;
        var centerY = (canvasHeight > 0) ? (zoneRoot.visualY + zoneRoot.visualHeight / 2) / canvasHeight : 0.5;
        // Store center coords for use in expandToFillWithCoords
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

        PhosphorMotionAnimation {
            target: zoneRoot
            properties: "visualX"
            to: fillAnimator.targetX
            profile: "editor.snapIn"
        }

        PhosphorMotionAnimation {
            target: zoneRoot
            properties: "visualY"
            to: fillAnimator.targetY
            profile: "editor.snapIn"
        }

        PhosphorMotionAnimation {
            target: zoneRoot
            properties: "visualWidth"
            to: fillAnimator.targetWidth
            profile: "editor.snapIn"
        }

        PhosphorMotionAnimation {
            target: zoneRoot
            properties: "visualHeight"
            to: fillAnimator.targetHeight
            profile: "editor.snapIn"
        }
    }
}
