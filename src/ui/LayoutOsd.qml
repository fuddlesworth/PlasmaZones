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
    // Phase 5: surface lifecycle + show/hide animations are entirely library-
    // driven. PhosphorAnimationLayer::SurfaceAnimator (registered for
    // PzRoles::LayoutOsd) drives Window.contentItem opacity + scale via its
    // `osd.show` / `osd.pop` / `osd.hide` profiles; PhosphorLayer::Surface
    // handles `Qt.WindowTransparentForInput` on the underlying QWindow during
    // the hide cycle.
    // root.visible flips to true on the first Surface::show() and stays true
    // for the surface's lifetime (keepMappedOnHide=true). Qt's Vulkan backend
    // on Wayland layer-shell doesn't reliably reinitialise the VkSwapchainKHR
    // after the wl_surface is torn down, so the visual "hidden" state is
    // driven by Window.contentItem opacity going to 0, not by toggling
    // Qt-window visibility.
    // Auto-dismiss is QML-side: dismissTimer fires dismissRequested() which
    // C++ wires to Surface::hide(). Escape is not handled (layer-shell
    // overlay windows do not receive keyboard focus when keyboard
    // interactivity is None).

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
    /// Idempotency latch for `dismissRequested`. The timer-fire path and the
    /// MouseArea click path both want to dismiss; without this, a click
    /// landing during the brief window between dismissTimer triggering and
    /// `Qt.WindowTransparentForInput` actually flipping at the QWindow
    /// level (the lib sets it inside the hide dispatch, which is async wrt
    /// the QML event loop) double-fires. C++ then re-runs Surface::hide()
    /// on a Hidden surface and logs `qCWarning` for every spurious click.
    /// Reset on every restartDismissTimer() so a re-shown OSD dismisses
    /// normally.
    property bool _dismissed: false

    /// Auto-dismiss request emitted by the dismissTimer. C++ side hooks this
    /// to OverlayService → Surface::hide() so the library can drive the
    /// fade-out via the SurfaceAnimator.
    signal dismissRequested()

    /// Restart the auto-dismiss timer from C++ on every show. Replaces the
    /// QML-internal `dismissTimer.restart()` that the old show() used to
    /// call.
    function restartDismissTimer() {
        _dismissed = false;
        dismissTimer.restart();
    }

    /// Internal: emit dismissRequested at most once per show cycle.
    function _requestDismiss() {
        if (_dismissed)
            return ;

        _dismissed = true;
        root.dismissRequested();
    }

    // Window configuration. Static flags — Phase 5 surface lifecycle owns
    // `Qt.WindowTransparentForInput` on the underlying QWindow during hide.
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    // Size based on container (which is inside contentWrapper)
    width: container.width + Math.round(Kirigami.Units.gridUnit * 2.5)
    height: container.height + Math.round(Kirigami.Units.gridUnit * 2.5)
    // Start hidden; first Surface::show() flips visible=true.
    // Note: Don't set Window.opacity - use contentWrapper.opacity instead
    // QWaylandWindow::setOpacity() is not implemented and logs warnings
    visible: false

    // Auto-dismiss timer. Emits a signal that the C++ side hooks to
    // Surface::hide() (which drives the library animator's beginHide).
    Timer {
        id: dismissTimer

        interval: root.displayDuration
        onTriggered: root._requestDismiss()
    }

    // Content wrapper - opacity defaults to 1 now. Phase 5 SurfaceAnimator
    // drives Window.contentItem opacity for show/hide; this child stays at
    // 1 and inherits visibility from the parent fade.
    Item {
        id: contentWrapper

        Accessible.name: root.disabled ? root.disabledReason : i18n("Layout indicator")
        anchors.fill: parent

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

        // Click to dismiss. The QML-side `_dismissed` latch in
        // `_requestDismiss()` collapses overlapping dismiss sources
        // (timer-fire + click during the same show cycle) into a single
        // dismissRequested signal. C++ then runs Surface::hide() exactly
        // once, avoiding the spurious qCWarning storm a double-call would
        // otherwise produce.
        MouseArea {
            anchors.fill: parent
            onClicked: root._requestDismiss()
        }

    }

}
