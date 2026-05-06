// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * Passive overlay shell — single per-screen wlr-layer-shell host that
 * groups every kbd-None overlay role into one wl_surface / QQuickWindow
 * / QSGRenderThread / Vulkan swapchain. Each per-content slot is a
 * sibling QQuickItem inside the shell that exposes a `shaderAnchor` for
 * SurfaceAnimator's per-(Surface, target) keying.
 *
 * Why one shell instead of one Surface per content:
 *   - polishAndSync runs sequentially on the GUI thread across all
 *     QQuickWindows in a process. Two windows that animate concurrently
 *     contend for the GUI thread; the slower window's polishAndSync
 *     blocks the faster one's. With every passive overlay riding the
 *     shell's single window, all per-content animations share one
 *     polishAndSync — no inter-content contention.
 *   - The shell wl_surface is permanently mapped after first show, so
 *     the Vulkan swapchain + RHI pipelines warm once and stay hot for
 *     every subsequent per-content show. No per-show wl_surface
 *     map/unmap and the cold-pipeline first-paint cost is paid once at
 *     daemon start.
 *
 * This shell is the kbd-None grouping. Modal kbd-Exclusive overlays
 * (snap-assist, layout picker) historically lived in their own per-show
 * wl_surfaces because layer-shell binds keyboard interactivity at first
 * commit and KWin doesn't re-evaluate it on already-mapped surfaces.
 * The unified-shell migration pulls them into THIS same shell with kbd
 * routed via global accelerators (KGlobalAccel) instead — see the
 * matching `snap-assist` / `layout-picker` slots below (added in
 * subsequent migration steps).
 *
 * C++ side accesses each slot Item via the `osdSlotItem` (etc.) alias
 * exposed on this Window root; property writes target the slot Item
 * directly and SurfaceAnimator targets the slot Item for show/hide.
 */
Window {
    // ── OSD slot ──────────────────────────────────────────────────────────
    // Host for LayoutOsd + NavigationOsd content. Inner card (loaded via
    // the per-mode Component below) carries `property bool shaderAnchor:
    // true` so vertex shaders bind to the visible OSD body rather than
    // the fullscreen slot Item.
    // Future slots (subsequent migration steps): zoneSelector,
    // mainOverlay, snapAssist, layoutPicker. Each is a sibling Item with
    // its own properties + Loader, animated independently via the
    // SurfaceAnimator's per-(Surface, target) keying.

    id: root

    /// OSD slot Item — SurfaceAnimator target for OSD show/hide. C++
    /// writes `mode` / data properties on this Item directly (matching
    /// the previous standalone NotificationOverlay's root property
    /// surface), then invokes SurfaceAnimator::beginShow with this Item
    /// as the rootItem argument.
    readonly property alias osdSlotItem: osdSlot
    /// Snap-assist slot Item — SurfaceAnimator target for snap-assist
    /// show/hide. C++ writes data properties (emptyZones, candidates,
    /// screenWidth, etc.) directly on this Item. Modal kbd grab is gone
    /// (the shell is kbd-None); Escape routes via the daemon's
    /// KGlobalAccel cancel-overlay shortcut.
    readonly property alias snapAssistSlotItem: snapAssistSlot
    /// Layout-picker slot Item — SurfaceAnimator target for picker
    /// show/hide. Modal kbd (Return/Enter/arrows/Escape) routes via
    /// KGlobalAccel ad-hoc registrations made by start.cpp on the
    /// matching show/dismiss signals — the shell is kbd-None so QML
    /// Shortcuts can't fire here.
    readonly property alias layoutPickerSlotItem: layoutPickerSlot

    /// Forwarded from the loaded OSD content. C++ side connects this to
    /// the slot-hide animation start (not Surface::hide() — the shell
    /// stays mapped permanently, only the slot's opacity animates).
    signal osdDismissRequested()
    /// Forwarded from snap-assist's `windowSelected` signal — host wires
    /// to onSnapAssistWindowSelected.
    signal snapAssistWindowSelected(string windowId, string zoneId, string geometryJson)
    /// Forwarded from snap-assist's backdrop click / dismiss request.
    signal snapAssistDismissRequested()
    /// Forwarded from picker's `layoutSelected`.
    signal layoutPickerSelected(string layoutId)
    /// Forwarded from picker's `dismissRequested` (backdrop click /
    /// supplemental dismiss path; primary Escape goes via global accel).
    signal layoutPickerDismissRequested()

    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    // Initial size — the C++ side resizes to per-screen geometry on
    // creation. The shell wl_surface is screen-sized so vertex-shader
    // transitions have geometry runway equal to the screen.
    width: Kirigami.Units.gridUnit * 15
    height: Kirigami.Units.gridUnit * 4
    // Start hidden; first per-content show flips visible=true. The
    // surface stays mapped (keepMappedOnHide=true) for the daemon's
    // lifetime so swapchain + RHI pipelines stay warm across show
    // cycles.
    visible: false

    Item {
        // OSD properties — bindings inside the per-mode Components below
        // reach for these via QML lexical scope. C++ side writes these
        // directly on this Item before each show.

        id: osdSlot

        // "layout-osd"      → LayoutOsdContent (zone preview + name + badge toast)
        // "navigation-osd"  → NavigationOsdContent (text-label keyboard-nav toast)
        // ""                → no content (Loader unloaded)
        property string mode: ""
        property var zones: []
        property color backgroundColor: Kirigami.Theme.backgroundColor
        property color textColor: Kirigami.Theme.textColor
        property color highlightColor: Kirigami.Theme.highlightColor
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
        property bool success: true
        property string action: ""
        property string reason: ""
        property var highlightedZoneIds: []
        property string sourceZoneId: ""
        property int windowCount: 1
        property color errorColor: Kirigami.Theme.negativeTextColor
        property real shaderBoundsPadding: 0

        /// Restart the loaded OSD content's auto-dismiss timer. C++
        /// invokes this after every OSD show via QMetaObject::invokeMethod.
        function restartDismissTimer() {
            if (osdLoader.item)
                osdLoader.item.restartDismissTimer();
            else
                console.warn("PassiveOverlayShell.osdSlot.restartDismissTimer: no OSD content loaded (mode =", JSON.stringify(osdSlot.mode), ") — auto-dismiss will not run");
        }

        anchors.fill: parent
        // SurfaceAnimator drives this Item's opacity. Start at 0 so the
        // first paint pre-show doesn't flash the OSD at full opacity.
        opacity: 0
        // Toggled true on first show by C++ side. Stays true thereafter
        // (animator drives the visible fade via opacity). A QPointer<Item>
        // referencing this slot survives across show cycles.
        visible: false
        // Catch typos in C++ mode writes ("layout-OSD" / "navigation_osd" / …)
        // before they degrade silently to "no content shown".
        onModeChanged: {
            if (mode !== "" && mode !== "layout-osd" && mode !== "navigation-osd")
                console.warn("PassiveOverlayShell osdSlot: unknown mode =", mode);

        }

        Loader {
            id: osdLoader

            anchors.fill: parent
            sourceComponent: {
                switch (osdSlot.mode) {
                case "layout-osd":
                    return layoutOsdComp;
                case "navigation-osd":
                    return navigationOsdComp;
                default:
                    return null;
                }
            }
            // Forward dismissRequested from whichever content is loaded.
            // Mode flip destroys the previous item, severing this connect
            // automatically; fresh onLoaded re-wires.
            onLoaded: {
                if (osdLoader.item)
                    osdLoader.item.dismissRequested.connect(root.osdDismissRequested);

            }
        }

        Component {
            id: layoutOsdComp

            LayoutOsdContent {
                zones: osdSlot.zones
                backgroundColor: osdSlot.backgroundColor
                textColor: osdSlot.textColor
                highlightColor: osdSlot.highlightColor
                shaderBoundsPadding: osdSlot.shaderBoundsPadding
                layoutId: osdSlot.layoutId
                layoutName: osdSlot.layoutName
                category: osdSlot.category
                autoAssign: osdSlot.autoAssign
                globalAutoAssign: osdSlot.globalAutoAssign
                showMasterDot: osdSlot.showMasterDot
                masterCount: osdSlot.masterCount
                producesOverlappingZones: osdSlot.producesOverlappingZones
                zoneNumberDisplay: osdSlot.zoneNumberDisplay
                screenAspectRatio: osdSlot.screenAspectRatio
                aspectRatioClass: osdSlot.aspectRatioClass
                fontFamily: osdSlot.fontFamily
                fontSizeScale: osdSlot.fontSizeScale
                fontWeight: osdSlot.fontWeight
                fontItalic: osdSlot.fontItalic
                fontUnderline: osdSlot.fontUnderline
                fontStrikeout: osdSlot.fontStrikeout
                locked: osdSlot.locked
                disabled: osdSlot.disabled
                disabledReason: osdSlot.disabledReason
            }

        }

        Component {
            id: navigationOsdComp

            NavigationOsdContent {
                zones: osdSlot.zones
                backgroundColor: osdSlot.backgroundColor
                textColor: osdSlot.textColor
                highlightColor: osdSlot.highlightColor
                shaderBoundsPadding: osdSlot.shaderBoundsPadding
                success: osdSlot.success
                action: osdSlot.action
                reason: osdSlot.reason
                highlightedZoneIds: osdSlot.highlightedZoneIds
                sourceZoneId: osdSlot.sourceZoneId
                windowCount: osdSlot.windowCount
                errorColor: osdSlot.errorColor
            }

        }

    }

    Item {
        id: snapAssistSlot

        // Snap-assist data properties — C++ writes these before each
        // show; SnapAssistContent picks them up via QML lexical scope.
        property var emptyZones: []
        property var candidates: []
        property int screenWidth: 1920
        property int screenHeight: 1080
        property color highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
        property color inactiveColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)
        property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.9)
        property real activeOpacity: 0.5
        property real inactiveOpacity: 0.3
        property int borderWidth: Kirigami.Units.smallSpacing
        property int borderRadius: Kirigami.Units.gridUnit
        // OSD-style content lifecycle gate. C++ toggles false→true around
        // each show so SnapAssistContent is re-instantiated, producing a
        // fresh shaderAnchor QQuickItem per show — avoids stale FBO content
        // on subsequent vertex-shader transitions.
        property bool loaded: false

        anchors.fill: parent
        opacity: 0
        visible: false

        Loader {
            id: snapAssistLoader

            anchors.fill: parent
            active: snapAssistSlot.loaded
            // asynchronous: true keeps the GUI thread responsive while
            // the snap-assist body (Repeater of zones × Repeater of
            // candidate cards) is instantiated. Without async loading a
            // sibling slot's animation (e.g. an OSD fly-in) stalls
            // mid-flight while this content mounts.
            asynchronous: true
            sourceComponent: snapAssistContentComp
            onLoaded: {
                if (snapAssistLoader.item) {
                    snapAssistLoader.item.windowSelected.connect(root.snapAssistWindowSelected);
                    snapAssistLoader.item.dismissRequested.connect(root.snapAssistDismissRequested);
                }
            }
        }

        Component {
            id: snapAssistContentComp

            SnapAssistContent {
                emptyZones: snapAssistSlot.emptyZones
                candidates: snapAssistSlot.candidates
                screenWidth: snapAssistSlot.screenWidth
                screenHeight: snapAssistSlot.screenHeight
                highlightColor: snapAssistSlot.highlightColor
                inactiveColor: snapAssistSlot.inactiveColor
                borderColor: snapAssistSlot.borderColor
                activeOpacity: snapAssistSlot.activeOpacity
                inactiveOpacity: snapAssistSlot.inactiveOpacity
                borderWidth: snapAssistSlot.borderWidth
                borderRadius: snapAssistSlot.borderRadius
            }

        }

    }

    Item {
        id: layoutPickerSlot

        // Picker data properties — C++ writes these before each show.
        property var layouts: []
        property string activeLayoutId: ""
        property real screenAspectRatio: 16 / 9
        property bool globalAutoAssign: false
        property bool locked: false
        property color backgroundColor: Kirigami.Theme.backgroundColor
        property color textColor: Kirigami.Theme.textColor
        property color highlightColor: Kirigami.Theme.highlightColor
        property color inactiveColor: Kirigami.Theme.disabledTextColor
        property color borderColor: Kirigami.Theme.textColor
        property real activeOpacity: 0.5
        property real inactiveOpacity: 0.3
        property int borderWidth: Kirigami.Units.smallSpacing
        property int borderRadius: Kirigami.Units.gridUnit
        property string fontFamily: ""
        property real fontSizeScale: 1
        property int fontWeight: Font.Bold
        property bool fontItalic: false
        property bool fontUnderline: false
        property bool fontStrikeout: false
        property color labelFontColor: Kirigami.Theme.textColor
        // OSD-style content lifecycle gate. C++ toggles false→true around
        // each show so LayoutPickerContent is re-instantiated.
        property bool loaded: false

        // Forwards to LayoutPickerContent.moveSelection / confirmSelection
        // — invoked by C++ on global-accel callbacks since the shell is
        // kbd-None and the picker content's QML Shortcuts can't fire.
        function moveSelection(dx, dy) {
            if (layoutPickerLoader.item)
                layoutPickerLoader.item.moveSelection(dx, dy);

        }

        function confirmSelection() {
            if (layoutPickerLoader.item)
                layoutPickerLoader.item.confirmSelection();

        }

        anchors.fill: parent
        opacity: 0
        visible: false

        Loader {
            id: layoutPickerLoader

            anchors.fill: parent
            active: layoutPickerSlot.loaded
            asynchronous: true
            sourceComponent: layoutPickerContentComp
            onLoaded: {
                if (layoutPickerLoader.item) {
                    layoutPickerLoader.item.layoutSelected.connect(root.layoutPickerSelected);
                    layoutPickerLoader.item.dismissRequested.connect(root.layoutPickerDismissRequested);
                    // Seed _shortcutsActive so the LayoutPickerContent's
                    // backdrop dismiss + selection bindings are live (the
                    // content uses this to gate side-effects). The QML
                    // Shortcuts inside LayoutPickerContent stay defined
                    // but never fire (shell is kbd-None) — global accels
                    // drive the picker instead.
                    layoutPickerLoader.item._shortcutsActive = true;
                }
            }
        }

        Component {
            id: layoutPickerContentComp

            LayoutPickerContent {
                layouts: layoutPickerSlot.layouts
                activeLayoutId: layoutPickerSlot.activeLayoutId
                globalAutoAssign: layoutPickerSlot.globalAutoAssign
                screenAspectRatio: layoutPickerSlot.screenAspectRatio
                backgroundColor: layoutPickerSlot.backgroundColor
                textColor: layoutPickerSlot.textColor
                highlightColor: layoutPickerSlot.highlightColor
                inactiveColor: layoutPickerSlot.inactiveColor
                borderColor: layoutPickerSlot.borderColor
                activeOpacity: layoutPickerSlot.activeOpacity
                inactiveOpacity: layoutPickerSlot.inactiveOpacity
                fontFamily: layoutPickerSlot.fontFamily
                fontSizeScale: layoutPickerSlot.fontSizeScale
                fontWeight: layoutPickerSlot.fontWeight
                fontItalic: layoutPickerSlot.fontItalic
                fontUnderline: layoutPickerSlot.fontUnderline
                fontStrikeout: layoutPickerSlot.fontStrikeout
                locked: layoutPickerSlot.locked
            }

        }

    }

}
