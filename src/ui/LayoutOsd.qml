// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.phosphor.animation
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
    // Dismiss state used for input gating. True while the OSD is logically
    // hidden (pre-first-show or after a full hide animation). When true, we
    // bind Qt.WindowTransparentForInput into the window flags so the
    // invisible-but-still-mapped layer surface doesn't eat clicks at its
    // screen position. Toggled on discrete show/dismiss events — NOT tied
    // to opacity — so the flag doesn't churn during the fade animation
    // (which would cause repeated QWindow::setFlags() calls and potential
    // input-region reconfiguration on every animation tick).
    // (No signals — previously emitted dismissed() to a C++ slot that did
    // nothing; both were removed in L3 v2. The flip of _osdDismissed at the
    // end of hideAnimation's ScriptAction is the entire dismiss mechanism.)
    // Hide the OSD with animation.
    // Window configuration - QPA layer-shell plugin handles overlay behavior on Wayland.
    // We keep root.visible == true after the first show() for the window's entire
    // lifetime (never cycling back to false in the hide animation), because Qt's
    // Vulkan backend on Wayland layer-shell doesn't reliably reinitialize the
    // VkSwapchainKHR after the wl_surface is torn down by a hide. The OSD's
    // visual "hidden" state is entirely opacity-driven (contentWrapper.opacity
    // goes to 0), not Qt-window-visibility-driven.

    id: root

    // Layout data
    property string layoutId: ""
    property string layoutName: ""
    property var zones: []
    // Layout category: 0=Manual (matches LayoutCategory in C++)
    property int category: 0
    // Per-layout autoAssign flag (raw, not yet OR'd with the global master
    // toggle). CategoryBadge folds in `globalAutoAssign` to display effective
    // state — see selector_update.cpp / snapassist.cpp / osd.cpp where these
    // properties are written.
    property bool autoAssign: false
    // Mirrors the global "Auto-assign for all layouts" master toggle (#370).
    // Forwarded into CategoryBadge so the badge shows effective state even
    // when the per-layout flag is off.
    property bool globalAutoAssign: false
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
    // ms before auto-hide. Show/hide fade shapes are driven by the
    // "osd.show" / "osd.hide" / "osd.pop" profile JSONs — tune those
    // to adjust the appear/disappear feel rather than re-introducing
    // per-window duration overrides here.
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
    // Initial value is true because the window is first created via
    // OverlayService::warmUpLayoutOsd() with visible:false — at that point
    // the layer surface isn't live yet, but setting the flag here is
    // harmless and ensures the very first frame after show() has the
    // correct input-accepting state.
    property bool _osdDismissed: true

    // Show the OSD with animation
    function show() {
        // Stop any running animations to prevent conflicts
        showAnimation.stop();
        hideAnimation.stop();
        dismissTimer.stop();
        // Reset state for fresh animation (animate wrapper, not window)
        contentWrapper.opacity = 0;
        container.scale = 0.8;
        root._osdDismissed = false;
        root.visible = true;
        showAnimation.start();
        dismissTimer.restart();
    }

    // After L3 v2, root.visible stays true for the window's entire lifetime
    // once show() has been called; the original `if (root.visible)` guard
    // became dead code after first show. We now gate on _osdDismissed
    // instead: if we're already in the dismissed state, the hide animation
    // is a no-op (another hide() mid-dismiss would just re-run the same
    // fade-out against already-zero opacity). Also guards the pre-first-show
    // case: warm-upped windows start with _osdDismissed == true, so an
    // errant hide() before any show() short-circuits cleanly.
    function hide() {
        if (root._osdDismissed)
            return ;

        showAnimation.stop();
        dismissTimer.stop();
        hideAnimation.start();
    }

    // To keep an invisible-but-Qt-visible window from eating clicks at its
    // position, we toggle Qt.WindowTransparentForInput via a property binding on
    // _osdDismissed. Qt Wayland translates this flag to wl_surface.set_input_region
    // updates on the live surface — no surface recreation required.
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus | (root._osdDismissed ? Qt.WindowTransparentForInput : 0)
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

    // Show animation. Opacity uses osd.show (plain OutCubic decel).
    // Scale uses osd.pop (OutBack overshoot) to preserve the subtle
    // "pop" the original design used — matching the pre-PhosphorMotion
    // NumberAnimation { easing.type: Easing.OutBack; overshoot: 1.2 }
    // shape. Do not collapse both onto a single profile.
    ParallelAnimation {
        id: showAnimation

        PhosphorMotionAnimation {
            target: contentWrapper
            properties: "opacity"
            from: 0
            to: 1
            profile: "osd.show"
        }

        PhosphorMotionAnimation {
            target: container
            properties: "scale"
            from: 0.8
            to: 1
            profile: "osd.pop"
        }

    }

    // Hide animation
    SequentialAnimation {
        id: hideAnimation

        ParallelAnimation {
            PhosphorMotionAnimation {
                target: contentWrapper
                properties: "opacity"
                to: 0
                profile: "osd.hide"
            }

            PhosphorMotionAnimation {
                target: container
                properties: "scale"
                to: 0.9
                profile: "osd.hide"
            }

        }

        ScriptAction {
            script: {
                // Do NOT set root.visible = false — see the Window flags
                // comment at the top of this file for why. We flip
                // _osdDismissed instead, which binds Qt.WindowTransparentForInput
                // into the window flags so the (still Qt-visible) layer
                // surface stops eating input at its screen position.
                root._osdDismissed = true;
            }
        }

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
                    globalAutoAssign: root.globalAutoAssign === true
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

        // Click to dismiss. Gated on _osdDismissed so that in the post-hide
        // transparent state we don't consume events at the Qt Quick level
        // even if the wl_surface input region hasn't been updated yet.
        MouseArea {
            anchors.fill: parent
            enabled: !root._osdDismissed
            onClicked: root.hide()
        }

    }

}
