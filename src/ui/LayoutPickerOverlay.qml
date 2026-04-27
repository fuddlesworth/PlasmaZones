// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window

/**
 * Layout Picker Overlay Window — Wayland layer-shell host for LayoutPickerContent.
 *
 * Thin shell that owns the QQuickWindow lifecycle, exposes the C++ signal
 * contract (layoutSelected, dismissRequested), and forwards C++
 * writeQmlProperty calls to the embedded content via aliases. All keyboard
 * navigation, dismiss state, and the visible content tree (backdrop +
 * popup frame + grid of layout cards) live in LayoutPickerContent.qml so
 * the body stays consistent with the OSD content components.
 *
 * Phase 5: surface lifecycle + show/hide animations are driven entirely by
 * PhosphorAnimationLayer::SurfaceAnimator (registered for
 * PzRoles::LayoutPicker). The library handles
 * `Qt.WindowTransparentForInput` on the underlying QWindow during the hide
 * cycle (Surface::Impl::drive sets the flag when keepMappedOnHide routes
 * through beginHide). Static flags here — a QML binding tied to a dismiss
 * state would fight the library's setFlag.
 */
Window {
    id: root

    // Aliases let the C++ writeQmlProperty(window, "...", ...) callsites in
    // overlayservice/snapassist.cpp continue to address the same names
    // without knowing they're now stored on the inner content Item.
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
    /// Logically-shown gate — written by C++ alongside Surface::show/hide
    /// so a logically-hidden picker (still Qt-visible under
    /// keepMappedOnHide=true) doesn't silently respond to stray
    /// accelerator deliveries.
    property alias _shortcutsActive: content._shortcutsActive
    /// Idempotency latch for `dismissRequested`. Reset by C++ on every
    /// show (QML's `on<Name>Changed` handler form does not work for
    /// underscore-prefixed properties, so the reset can't be tied to
    /// `_shortcutsActive`'s change signal).
    property alias _dismissed: content._dismissed

    // Public C++ signal contract — connected at snapassist.cpp via
    // SIGNAL(layoutSelected(QString)) / SIGNAL(dismissRequested()).
    signal layoutSelected(string layoutId)
    /// User-initiated dismiss request — backdrop click, Shortcut Escape
    /// (handled by C++ event filter and routed through hideLayoutPicker
    /// directly), or in-flight race during the fade-out window.
    /// OverlayService::createWarmedOsdSurface (or showLayoutPicker's
    /// connection in snapassist.cpp) wires this to Surface::hide().
    signal dismissRequested()

    // Static flags — Phase 5 surface lifecycle owns
    // Qt.WindowTransparentForInput during the hide cycle.
    flags: Qt.FramelessWindowHint | Qt.Tool
    color: "transparent"
    // Start hidden; first Surface::show() flips visible=true. Subsequent
    // hides keep the layer surface mapped (keepMappedOnHide=true) so the
    // warmed Vulkan swapchain survives across show cycles.
    visible: false

    LayoutPickerContent {
        id: content

        anchors.fill: parent
        // Forward content's internal signals to the Window's public ones
        // — preserves snapassist.cpp's existing
        // SIGNAL(layoutSelected(QString)) / SIGNAL(dismissRequested())
        // connection targets.
        onLayoutSelected: function(layoutId) {
            root.layoutSelected(layoutId);
        }
        onDismissRequested: root.dismissRequested()
    }

}
