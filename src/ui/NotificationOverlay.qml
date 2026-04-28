// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * Unified notification overlay — single Wayland layer-shell host that swaps
 * between LayoutOsd and NavigationOsd content via a Loader keyed on `mode`.
 *
 * Per effective screen the daemon previously kept LayoutOsd and NavigationOsd
 * warmed as two distinct QQuickWindows / QSGRenderThreads / Vulkan swapchains
 * / wl_surfaces. Both use the same protocol shape (PzRoles::OsdBase —
 * FullscreenOverlay layer, AnchorAll, no keyboard, click-through) and are
 * never visible simultaneously, so they collapse cleanly to a single surface
 * whose content swaps based on which message is being shown.
 *
 * LayoutPickerOverlay does NOT participate — it uses CenteredModal (exclusive
 * keyboard, distinct anchors) and wlr-layer-shell anchors are immutable
 * post-attach. It keeps its own surface.
 *
 * Phase 5 lifecycle:
 *   - C++ writes mode + data properties, then calls `surface->show()`
 *     which drives the SurfaceAnimator's beginShow (osd.show + osd.pop).
 *   - C++ also invokes the QML `restartDismissTimer()` function so the
 *     loaded content's auto-dismiss timer (re)starts.
 *   - When the timer fires (or the user clicks the OSD body), the loaded
 *     content emits `dismissRequested`; this host re-emits it as its own
 *     `dismissRequested(string)` signal which OverlayService wires to
 *     `Surface::hide()`. The library animator drives the fade out and
 *     flips Qt.WindowTransparentForInput on the still-mapped layer
 *     surface.
 *
 * Window.visible flips to true on the first Surface::show() and stays true
 * for the surface's lifetime (keepMappedOnHide=true) — Qt's Vulkan backend
 * on Wayland layer-shell doesn't reliably reinitialise the VkSwapchainKHR
 * after the wl_surface is torn down by hide.
 */
Window {
    // ── Mode selection ─────────────────────────────────────────────────────
    // Per-mode Component definitions. Each Component's content explicitly
    // wires only the root properties its content type understands —
    // LayoutOsdContent never sees `success`/`action`, NavigationOsdContent
    // never sees `layoutId`/`layoutName`. C++ writeQmlProperty calls flow
    // root → binding → loader.item.

    id: root

    // "layout-osd"      → LayoutOsdContent (zone preview + name + badge toast)
    // "navigation-osd"  → NavigationOsdContent (text-label keyboard-nav toast)
    // ""                → no content (Loader unloaded)
    property string mode: ""
    // ── Common properties (LayoutOsd + NavigationOsd) ─────────────────────
    property var zones: []
    // displayDuration is intentionally NOT propagated from this host — each
    // content type has its own default (1500 ms for layout-osd, 1000 ms for
    // navigation-osd) and the per-content binding is omitted in the
    // Components below so those defaults remain authoritative.
    // Theme defaults must mirror Kirigami so the Component bindings below
    // don't override the loaded content's matching Kirigami defaults with
    // hardcoded sentinels — neither showLayoutOsd nor showNavigationOsd
    // calls writeColorSettings, so without these the OSD body would render
    // against a literal white/black/blue palette.
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property color highlightColor: Kirigami.Theme.highlightColor
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
    property color errorColor: Kirigami.Theme.negativeTextColor
    // Re-export the loaded content's contentDesiredWidth/Height — the C++
    // OSD show paths read these via window->property("contentDesiredWidth")
    // to size the layer surface (and to compute matching layer-shell margins).
    // Fallback values cover the warm-up window where mode="" → no content
    // loaded; the surface is invisible at that point so the exact value
    // doesn't matter, but they MUST match osd.cpp's readOsdContentSize
    // fallbacks so a missed property read doesn't size the surface to one
    // value while QML measures another.
    readonly property int contentDesiredWidth: loader.item ? loader.item.contentDesiredWidth : 240
    readonly property int contentDesiredHeight: loader.item ? loader.item.contentDesiredHeight : 70

    /// Auto-dismiss request forwarded from the loaded content. C++ side
    /// connects this to Surface::hide() in OverlayService::createWarmedOsdSurface
    /// (string-based connect — QML-defined signals aren't addressable via
    /// `&Class::signal` pointers), so the dispatch chain
    ///   QML dismissTimer → loaded content dismissRequested → host dismissRequested
    ///     → Surface::hide() → SurfaceAnimator::beginHide
    /// closes around the unified surface. Parameter-less by design — the
    /// surface identity is implicit in the connection target.
    signal dismissRequested()

    /// Restart the loaded content's auto-dismiss timer. C++ invokes this
    /// after every Surface::show() so the timer (re)starts. No-op if no
    /// content is loaded (mode == "").
    function restartDismissTimer() {
        if (loader.item)
            loader.item.restartDismissTimer();

    }

    // Static flags — Phase 5 surface lifecycle owns Qt.WindowTransparentForInput
    // on the underlying QWindow during hide.
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    // Initial warm-up size only. C++ owns sizing imperatively post-warm-up:
    // every show path measures contentDesiredWidth/Height after writing
    // mode + per-mode properties, then calls QQuickWindow::setWidth/Height
    // before Surface::show(). A QML binding to contentDesiredWidth here
    // would be broken on the first imperative setWidth, leaving an
    // ambiguous "binding sometimes drives size, sometimes doesn't" model.
    // Keep these as plain literal initializers; surface is visible=false
    // during warm-up so the value is cosmetic.
    width: 240
    height: 70
    // Start hidden; first Surface::show() flips visible=true. Subsequent
    // hides keep the layer surface mapped (keepMappedOnHide=true) so the
    // warmed Vulkan swapchain survives across show cycles.
    visible: false
    // Catch typos in C++ mode writes ("layout-OSD" / "navigation_osd" / …)
    // before they degrade silently to "no content shown". Production
    // writers in osd.cpp use string constants, so this only fires under
    // a regression — cheap to leave in.
    onModeChanged: {
        if (mode !== "" && mode !== "layout-osd" && mode !== "navigation-osd")
            console.warn("NotificationOverlay: unknown mode =", mode);

    }

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
        // Forward dismissRequested from whichever content is loaded —
        // onLoaded fires after the new item finishes property
        // initialization, and Connections handles the case where the
        // sourceComponent flips to a different mode.
        onLoaded: {
            if (loader.item)
                loader.item.dismissRequested.connect(root.dismissRequested);

        }
    }

    // Caveat: the loaded content is rebuilt from scratch on every mode
    // flip, with bindings evaluated against root.* values current at item-
    // creation time. C++ overwrites the per-mode properties immediately
    // after the mode write, so the eventual visible state is correct —
    // BUT property-changed handlers inside content (`on<Name>Changed`)
    // will not fire if the new value matches the previous show's value.
    // Don't rely on Changed handlers for run-once-per-show side effects;
    // do that work via the C++ side (or via Component.onCompleted, which
    // does fire on every recreation).
    Component {
        id: layoutOsdComp

        LayoutOsdContent {
            // Common
            zones: root.zones
            backgroundColor: root.backgroundColor
            textColor: root.textColor
            highlightColor: root.highlightColor
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
            backgroundColor: root.backgroundColor
            textColor: root.textColor
            highlightColor: root.highlightColor
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
