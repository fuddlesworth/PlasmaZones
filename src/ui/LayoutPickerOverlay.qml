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

    // OSD-style content lifecycle gate — see ZoneSelectorWindow's
    // `loaded` property for the rationale. C++ toggles this false→true
    // around each surface->show() so the LayoutPickerContent inside the
    // Loader is re-instantiated and its inner shaderAnchor is a fresh
    // QQuickItem per show. Without this, the persistent shaderAnchor's
    // QQuickItemLayer state survives across shows and subsequent
    // vertex-shader transitions sample stale FBO content.
    property bool loaded: false
    // Data properties live on root (not aliased to the inner content)
    // because the Loader-driven LayoutPickerContent is destroyed +
    // recreated on every show — aliases would break each time the
    // content is unloaded. The inner content's bindings reach for
    // root.* via QML lexical scope inside the Component below.
    property var layouts: []
    property string activeLayoutId: ""
    property bool globalAutoAssign: false
    property real screenAspectRatio: 16 / 9
    property color backgroundColor: "white"
    property color textColor: "black"
    property color highlightColor: "blue"
    property color inactiveColor: "gray"
    property color borderColor: "black"
    property real activeOpacity: 0.7
    property real inactiveOpacity: 0.3
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Normal
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false
    property bool locked: false
    property int selectedIndex: -1
    /// Logically-shown gate — written by C++ alongside Surface::show/hide
    /// so a logically-hidden picker (still Qt-visible under
    /// keepMappedOnHide=true) doesn't silently respond to stray
    /// accelerator deliveries.
    property bool _shortcutsActive: false
    /// Idempotency latch for `dismissRequested`. Reset by C++ on every
    /// show (QML's `on<Name>Changed` handler form does not work for
    /// underscore-prefixed properties, so the reset can't be tied to
    /// `_shortcutsActive`'s change signal).
    property bool _dismissed: false

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

    Loader {
        anchors.fill: parent
        active: root.loaded
        sourceComponent: contentComp
    }

    Component {
        id: contentComp

        LayoutPickerContent {
            anchors.fill: parent
            // Bind data from root via QML lexical scope (the Component
            // is declared inside ZoneSelectorWindow / LayoutPickerOverlay,
            // so `root` resolves at component-instantiation time).
            layouts: root.layouts
            activeLayoutId: root.activeLayoutId
            globalAutoAssign: root.globalAutoAssign
            screenAspectRatio: root.screenAspectRatio
            backgroundColor: root.backgroundColor
            textColor: root.textColor
            highlightColor: root.highlightColor
            inactiveColor: root.inactiveColor
            borderColor: root.borderColor
            activeOpacity: root.activeOpacity
            inactiveOpacity: root.inactiveOpacity
            fontFamily: root.fontFamily
            fontSizeScale: root.fontSizeScale
            fontWeight: root.fontWeight
            fontItalic: root.fontItalic
            fontUnderline: root.fontUnderline
            fontStrikeout: root.fontStrikeout
            locked: root.locked
            selectedIndex: root.selectedIndex
            _shortcutsActive: root._shortcutsActive
            _dismissed: root._dismissed
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

}
