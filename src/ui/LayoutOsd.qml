// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window

/**
 * Layout OSD Window — Wayland layer-shell host for LayoutOsdContent.
 *
 * Thin shell that owns the QQuickWindow lifecycle and forwards C++ property
 * writes to the embedded content via aliases. All animations, dismiss timing,
 * and the visible content tree (zone preview + name + category badge + lock
 * / disabled overlays) live in LayoutOsdContent.qml so the same body can be
 * hosted by the unified NotificationOverlay (Loader-driven) when Phase 2 of
 * the surface-collapse refactor lands.
 *
 * Window configuration mirrors the pre-extraction file: keep root.visible == true
 * after the first show() for the window's entire lifetime (Qt's Vulkan backend
 * on Wayland layer-shell can't reliably reinitialize the VkSwapchainKHR after
 * the wl_surface is torn down by hide), and toggle Qt.WindowTransparentForInput
 * via the content's `dismissed` flip so the invisible-but-Qt-visible layer
 * surface doesn't eat clicks at its screen position.
 */
Window {
    // Aliases let the C++ writeQmlProperty(window, "...", ...) callsites in
    // overlayservice/osd.cpp continue to address the same names without
    // knowing they're now stored on the inner content Item.

    id: root

    property alias layoutId: content.layoutId
    property alias layoutName: content.layoutName
    property alias zones: content.zones
    property alias category: content.category
    property alias autoAssign: content.autoAssign
    property alias globalAutoAssign: content.globalAutoAssign
    property alias showMasterDot: content.showMasterDot
    property alias masterCount: content.masterCount
    property alias producesOverlappingZones: content.producesOverlappingZones
    property alias zoneNumberDisplay: content.zoneNumberDisplay
    property alias screenAspectRatio: content.screenAspectRatio
    property alias aspectRatioClass: content.aspectRatioClass
    property alias displayDuration: content.displayDuration
    property alias backgroundColor: content.backgroundColor
    property alias textColor: content.textColor
    property alias highlightColor: content.highlightColor
    property alias fontFamily: content.fontFamily
    property alias fontSizeScale: content.fontSizeScale
    property alias fontWeight: content.fontWeight
    property alias fontItalic: content.fontItalic
    property alias fontUnderline: content.fontUnderline
    property alias fontStrikeout: content.fontStrikeout
    property alias locked: content.locked
    property alias disabled: content.disabled
    property alias disabledReason: content.disabledReason
    property alias fadeInDuration: content.fadeInDuration
    property alias fadeOutDuration: content.fadeOutDuration

    // Forward show/hide to the content. C++ already invokes via
    // QMetaObject::invokeMethod(window, "show") which finds the function on
    // the Window's meta-object.
    function show() {
        content.show();
    }

    function hide() {
        content.hide();
    }

    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus | (content.dismissed ? Qt.WindowTransparentForInput : 0)
    color: "transparent"
    width: content.contentDesiredWidth
    height: content.contentDesiredHeight
    // Start hidden; show() flips this once and never back (see the file-level
    // comment for why). Don't set Window.opacity — the inner content drives
    // visibility via Item.opacity. QWaylandWindow::setOpacity() is unimplemented.
    visible: false

    LayoutOsdContent {
        id: content

        anchors.fill: parent
    }

}
