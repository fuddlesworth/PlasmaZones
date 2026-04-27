// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window

/**
 * Layout Picker Overlay Window — Wayland layer-shell host for LayoutPickerContent.
 *
 * Thin shell that owns the QQuickWindow lifecycle, exposes the C++ signal
 * contract (layoutSelected, dismissed), and forwards C++ writeQmlProperty
 * calls to the embedded content via aliases. All animations, dismiss state,
 * keyboard navigation, and the visible content tree (backdrop + popup frame
 * + grid of layout cards) live in LayoutPickerContent.qml so the same body
 * can be hosted by the unified NotificationOverlay (Loader-driven) when
 * Phase 2 of the surface-collapse refactor lands.
 *
 * Window configuration mirrors the pre-extraction file: the surface stays
 * Qt-visible across hide/show cycles to preserve the Wayland VkSwapchainKHR
 * (Qt's Vulkan backend can't reliably reinit one after a wl_surface tear-
 * down), and Qt.WindowTransparentForInput is folded into the window flags
 * while logically dismissed so the still-mapped layer surface doesn't eat
 * clicks at its screen position.
 */
Window {
    // Aliases let the C++ writeQmlProperty(window, "...", ...) callsites in
    // overlayservice/snapassist.cpp continue to address the same names
    // without knowing they're now stored on the inner content Item.
    // `_pickerDismissed` keeps its underscored name to match snapassist.cpp:417's
    // existing read; the inner content's property is `dismissed`, uniform
    // with the OSD content components.

    id: root

    property alias layouts: content.layouts
    property alias activeLayoutId: content.activeLayoutId
    property alias globalAutoAssign: content.globalAutoAssign
    property alias screenAspectRatio: content.screenAspectRatio
    property alias backgroundColor: content.backgroundColor
    property alias textColor: content.textColor
    property alias highlightColor: content.highlightColor
    property alias inactiveColor: content.inactiveColor
    property alias borderColor: content.borderColor
    property alias activeOpacity: content.activeOpacity
    property alias inactiveOpacity: content.inactiveOpacity
    property alias fontFamily: content.fontFamily
    property alias fontSizeScale: content.fontSizeScale
    property alias fontWeight: content.fontWeight
    property alias fontItalic: content.fontItalic
    property alias fontUnderline: content.fontUnderline
    property alias fontStrikeout: content.fontStrikeout
    property alias locked: content.locked
    property alias selectedIndex: content.selectedIndex
    // Legacy alias for snapassist.cpp:417's `m_layoutPickerWindow->property("_pickerDismissed")`
    // read. Underscore prefix preserved.
    property alias _pickerDismissed: content.dismissed

    // Public C++ signal contract — connected at snapassist.cpp:662-663.
    signal layoutSelected(string layoutId)
    signal dismissed()

    // Show with animation. Activates the layer surface so keyboard shortcuts
    // (Return/Enter/Arrow keys, all owned by the content Item) reach this
    // Window's focus chain.
    function show() {
        visible = true;
        content.show();
        requestActivate();
    }

    function hide() {
        content.hide();
    }

    flags: Qt.FramelessWindowHint | Qt.Tool | (content.dismissed ? Qt.WindowTransparentForInput : 0)
    color: "transparent"
    // Start hidden; show() flips this once and never back. The layer surface
    // stays mapped after first show so the next show() reuses the warmed
    // Vulkan swapchain — see file-level comment.
    visible: false

    LayoutPickerContent {
        id: content

        anchors.fill: parent
        // Forward the content's internal layoutSelected to the Window's
        // public signal — preserves snapassist.cpp's existing
        // SIGNAL(layoutSelected(QString)) connection target.
        onLayoutSelected: function(layoutId) {
            root.layoutSelected(layoutId);
        }
        // Re-emit dismissed() exactly once when the hide animation's
        // ScriptAction flips the content's dismissed back to true. Matches
        // the original file's behavior where dismissed() fired alongside
        // _pickerDismissed = true in the same ScriptAction. Initial value
        // (true) does not trigger this handler.
        onDismissedChanged: {
            if (content.dismissed)
                root.dismissed();

        }
    }

}
