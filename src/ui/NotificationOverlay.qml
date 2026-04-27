// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window

/**
 * Unified notification overlay — single Wayland layer-shell host that swaps
 * between LayoutOsd and NavigationOsd content via a Loader keyed on `mode`.
 *
 * Phase 2 of the surface-reduction plan: per effective screen the daemon
 * previously kept LayoutOsd and NavigationOsd warmed as two distinct
 * QQuickWindows / QSGRenderThreads / Vulkan swapchains / wl_surfaces. Both
 * use the same protocol shape (PzRoles::OsdBase — FullscreenOverlay layer,
 * AnchorAll, no keyboard, click-through), and the two modes are never
 * visible simultaneously, so they collapse cleanly to a single surface
 * whose content swaps based on which message is being shown.
 *
 * LayoutPickerOverlay does NOT participate — it uses CenteredModal
 * (exclusive keyboard, distinct anchors) and wlr-layer-shell anchors are
 * immutable post-attach. It keeps its own surface.
 *
 * Window configuration mirrors the per-mode standalone files: keep
 * root.visible == true after the first show() for the window's entire
 * lifetime (Qt's Vulkan backend on Wayland layer-shell can't reliably
 * reinitialize the VkSwapchainKHR after the wl_surface is torn down by
 * hide), and toggle Qt.WindowTransparentForInput via the loaded content's
 * `dismissed` flip so the invisible-but-Qt-visible layer surface doesn't
 * eat clicks at its screen position.
 *
 * C++ usage (osd.cpp):
 *   writeQmlProperty(window, "mode", "layout-osd");
 *   writeQmlProperty(window, "layoutId", ...);
 *   writeQmlProperty(window, "layoutName", ...);
 *   ...
 *   QMetaObject::invokeMethod(window, "show");
 *
 * Setting `mode` first triggers the Loader to instantiate the matching
 * content; subsequent property writes flow through bindings to the loaded
 * item. The bindings are declared per-Component below — each Component
 * only references the subset of root properties its content type uses.
 */
Window {
    // ── Mode selection ─────────────────────────────────────────────────────

    id: root

    // "layout-osd"      → LayoutOsdContent (zone preview + name + badge toast)
    // "navigation-osd"  → NavigationOsdContent (text-label keyboard-nav toast)
    // ""                → no content (Loader unloaded)
    property string mode: ""
    // ── Common properties (LayoutOsd + NavigationOsd) ─────────────────────
    property var zones: []
    property int displayDuration: 1500 // overridden per-mode in show() if needed
    property color backgroundColor: "white"
    property color textColor: "black"
    property color highlightColor: "blue"
    property int fadeInDuration: -1
    property int fadeOutDuration: -1
    // ── LayoutOsd-only properties ──────────────────────────────────────────
    property string layoutId: ""
    property string layoutName: ""
    property int category: 0
    property bool autoAssign: false
    property bool globalAutoAssign: false
    property bool showMasterDot: false
    property int masterCount: 1
    property bool producesOverlappingZones: false
    property string zoneNumberDisplay: "all"
    property real screenAspectRatio: 16 / 9
    property string aspectRatioClass: "any"
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false
    property bool locked: false
    property bool disabled: false
    property string disabledReason: ""
    // ── NavigationOsd-only properties ──────────────────────────────────────
    property bool success: true
    property string action: ""
    property string reason: ""
    property var highlightedZoneIds: []
    property string sourceZoneId: ""
    property int windowCount: 1
    property color errorColor: "red"
    // Re-export the loaded content's contentDesiredWidth/Height so
    // overlayservice/osd.cpp's per-show resize path (which currently reads
    // these on NavigationOsd's Window root) keeps working unchanged.
    readonly property int contentDesiredWidth: loader.item ? loader.item.contentDesiredWidth : 1
    readonly property int contentDesiredHeight: loader.item ? loader.item.contentDesiredHeight : 1

    // Forward show / hide to whichever content is currently loaded. C++
    // invokes via QMetaObject::invokeMethod(window, "show") — same path as
    // the per-mode standalone Windows, so osd.cpp's call site is unchanged.
    function show() {
        if (loader.item)
            loader.item.show();

    }

    function hide() {
        if (loader.item)
            loader.item.hide();

    }

    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus | ((loader.item && !loader.item.dismissed) ? 0 : Qt.WindowTransparentForInput)
    color: "transparent"
    width: contentDesiredWidth
    height: contentDesiredHeight
    // Start hidden; show() flips this once and never back. The layer surface
    // stays mapped after first show so the next show() reuses the warmed
    // Vulkan swapchain — see file-level comment.
    visible: false

    Loader {
        id: loader

        anchors.fill: parent
        sourceComponent: {
            switch (root.mode) {
            case "layout-osd":
                return layoutOsdComp;
            case "navigation-osd":
                return navigationOsdComp;
            default:
                return null;
            }
        }
    }

    // Per-mode Component definitions. Each Component's content explicitly
    // wires only the root properties its content type understands —
    // LayoutOsdContent never sees `success`/`action`, NavigationOsdContent
    // never sees `layoutId`/`layoutName`. C++ writeQmlProperty calls flow
    // root → binding → loader.item.
    Component {
        id: layoutOsdComp

        LayoutOsdContent {
            // Common
            zones: root.zones
            displayDuration: root.displayDuration
            backgroundColor: root.backgroundColor
            textColor: root.textColor
            highlightColor: root.highlightColor
            fadeInDuration: root.fadeInDuration
            fadeOutDuration: root.fadeOutDuration
            // LayoutOsd-specific
            layoutId: root.layoutId
            layoutName: root.layoutName
            category: root.category
            autoAssign: root.autoAssign
            globalAutoAssign: root.globalAutoAssign
            showMasterDot: root.showMasterDot
            masterCount: root.masterCount
            producesOverlappingZones: root.producesOverlappingZones
            zoneNumberDisplay: root.zoneNumberDisplay
            screenAspectRatio: root.screenAspectRatio
            aspectRatioClass: root.aspectRatioClass
            fontFamily: root.fontFamily
            fontSizeScale: root.fontSizeScale
            fontWeight: root.fontWeight
            fontItalic: root.fontItalic
            fontUnderline: root.fontUnderline
            fontStrikeout: root.fontStrikeout
            locked: root.locked
            disabled: root.disabled
            disabledReason: root.disabledReason
        }

    }

    Component {
        id: navigationOsdComp

        NavigationOsdContent {
            // Common
            zones: root.zones
            displayDuration: root.displayDuration
            backgroundColor: root.backgroundColor
            textColor: root.textColor
            highlightColor: root.highlightColor
            fadeInDuration: root.fadeInDuration
            fadeOutDuration: root.fadeOutDuration
            // NavigationOsd-specific
            success: root.success
            action: root.action
            reason: root.reason
            highlightedZoneIds: root.highlightedZoneIds
            sourceZoneId: root.sourceZoneId
            windowCount: root.windowCount
            errorColor: root.errorColor
        }

    }

}
