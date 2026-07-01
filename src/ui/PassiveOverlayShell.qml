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
    /// Zone-selector slot Item — SurfaceAnimator target for selector
    /// show/hide. Per-VS positioning via the slot's anchors.fill: parent
    /// + the shell being sized to the VS rect.
    readonly property alias zoneSelectorSlotItem: zoneSelectorSlot
    /// Main zone overlay slot Item — displays zones during window drag.
    readonly property alias mainOverlaySlotItem: mainOverlaySlot

    /// Forwarded from the loaded OSD content. C++ side connects this to
    /// the slot-hide animation start (not Surface::hide() — the shell
    /// stays mapped permanently, only the slot's opacity animates).
    signal osdDismissRequested
    /// Forwarded from snap-assist's `windowSelected` signal — host wires
    /// to onSnapAssistWindowSelected.
    signal snapAssistWindowSelected(string windowId, string zoneId, string geometryJson)
    /// Forwarded from snap-assist's backdrop click / dismiss request.
    signal snapAssistDismissRequested
    /// Forwarded from picker's `layoutSelected`.
    signal layoutPickerSelected(string layoutId)
    /// Forwarded from picker's `dismissRequested` (backdrop click /
    /// supplemental dismiss path; primary Escape goes via global accel).
    signal layoutPickerDismissRequested

    // Qt::WindowTransparentForInput is driven imperatively by C++ from
    // syncPassiveShellSurfaceState (via Surface::show()/hide() with
    // keepMappedOnHide=true) — when no slot is visible, the shell's
    // wl_surface input region is set empty so clicks pass through.
    // Driving it from a QML flags binding here would race the C++
    // path: a slot visibility change triggers BOTH a binding
    // re-evaluation that calls QQuickWindow::setFlags AND the C++
    // syncPassiveShellSurfaceState, and the order is undefined.
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    // No explicit width/height — `OverlayService::createWarmedOsdSurface`
    // passes `initialSize = screenGeom.size()` to `createLayerSurface`,
    // and `surface.cpp::computeWarmupGeometry` calls `setGeometry` with
    // that screen-sized rect BEFORE `completeCreate` fires QML
    // evaluation. A QML `width: …` / `height: …` binding here would
    // re-evaluate during `completeCreate` and OVERWRITE the C++-set
    // screen-sized geometry — committing the wl_surface at the QML
    // binding's value (15×4 gridUnits ≈ 270×72 px) and forcing the
    // compositor to configure the first frame at that small size. The
    // first OSD show on login then rendered with a wl_surface still
    // sized to ~270×72 while the QML internal layout was at the
    // (eventually-re-asserted) screen size — `container.centerIn` math
    // produced negative scene Y, surfaceAnimator pushed
    // `iSurfaceScreenPos.xy` with that negative Y, and fly-in's
    // `cardCenterClip.y` landed above clip-y = -1, rendering the OSD
    // card above the screen top with the bottom partially cut off.
    // Leaving the size to C++ entirely closes the race.
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

        // Surface-shader decoration (Stage d). C++ OverlayService::applyDecoration
        // resolves the "osd" pack from DecorationProfileTree and writes these
        // before each show; empty source = no decoration (card draws natively).
        // Consumed by the SurfaceDecoration sibling below, which captures the
        // loaded card's PopupFrame shaderAnchor and re-renders it rounded.
        property url decorationShaderSource
        property string decorationParamPreamble: ""
        property var decorationShaderParams: ({})

        /// Restart the loaded OSD content's auto-dismiss timer. C++
        /// invokes this after every OSD show via QMetaObject::invokeMethod.
        function restartDismissTimer() {
            if (osdLoader.item)
                osdLoader.item.restartDismissTimer();
            else
                console.warn("PassiveOverlayShell.osdSlot.restartDismissTimer: no OSD content loaded (mode =", JSON.stringify(osdSlot.mode), ") — auto-dismiss will not run");
        }

        anchors.fill: parent
        // Topmost slot: notifications/OSDs paint above every other content
        // type so a layout-OSD or nav-OSD reads cleanly even when stacked
        // over an active zone overlay or open snap-assist.
        z: 3
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
                success: osdSlot.success
                action: osdSlot.action
                reason: osdSlot.reason
                highlightedZoneIds: osdSlot.highlightedZoneIds
                sourceZoneId: osdSlot.sourceZoneId
                windowCount: osdSlot.windowCount
                errorColor: osdSlot.errorColor
            }
        }

        // Surface-shader decoration (Stage d). SIBLING of osdLoader (never an
        // ancestor of the captured card — a feedback loop). Captures the loaded
        // card's PopupFrame shaderAnchor and re-renders it through the resolved
        // "osd" surface pack (rounded corners + border), suppressing the card's
        // own square-cornered direct draw via the snapshot's hideSource. Inert
        // when decorationShaderSource is empty — the card then draws natively.
        SurfaceDecoration {
            anchors.fill: parent
            contentItem: osdLoader.item
            decorationShaderSource: osdSlot.decorationShaderSource
            decorationParamPreamble: osdSlot.decorationParamPreamble
            decorationShaderParams: osdSlot.decorationShaderParams
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

        // Surface-shader decoration (Stage d). C++ OverlayService::applyDecoration
        // resolves the "popup.snapAssist" pack and writes these before each show;
        // empty source = no decoration (card draws natively). Consumed by the
        // SurfaceDecoration sibling below.
        property url decorationShaderSource
        property string decorationParamPreamble: ""
        property var decorationShaderParams: ({})

        anchors.fill: parent
        // Popup tier — modal pickers paint above the zone selector and
        // main overlay but below notifications/OSDs (z=3).
        z: 2
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

        // Surface-shader decoration (Stage d). SIBLING of snapAssistLoader.
        // Captures the loaded content's shaderAnchor (the SnapAssistContent root
        // itself carries `shaderAnchor: true`) and re-renders it through the
        // resolved "popup.snapAssist" surface pack. Inert when the source is empty.
        SurfaceDecoration {
            anchors.fill: parent
            contentItem: snapAssistLoader.item
            decorationShaderSource: snapAssistSlot.decorationShaderSource
            decorationParamPreamble: snapAssistSlot.decorationParamPreamble
            decorationShaderParams: snapAssistSlot.decorationShaderParams
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

        // Surface-shader decoration (Stage d). C++ OverlayService::applyDecoration
        // resolves the "popup.layoutPicker" pack and writes these before each
        // show; empty source = no decoration. Consumed by the SurfaceDecoration
        // sibling below.
        property url decorationShaderSource
        property string decorationParamPreamble: ""
        property var decorationShaderParams: ({})

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
        // Popup tier — same z as snap-assist (the two are mutually
        // exclusive at any given moment) and below OSDs (z=3).
        z: 2
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

        // Surface-shader decoration (Stage d). SIBLING of layoutPickerLoader.
        // Captures the loaded content's PopupFrame shaderAnchor and re-renders it
        // through the resolved "popup.layoutPicker" surface pack. Inert when the
        // source is empty.
        SurfaceDecoration {
            anchors.fill: parent
            contentItem: layoutPickerLoader.item
            decorationShaderSource: layoutPickerSlot.decorationShaderSource
            decorationParamPreamble: layoutPickerSlot.decorationParamPreamble
            decorationShaderParams: layoutPickerSlot.decorationShaderParams
        }
    }

    Item {
        id: zoneSelectorSlot

        // Selector data properties — C++ writes these per-show. The
        // ZoneSelectorContent inside the Loader picks them up via QML
        // lexical scope.
        property var layouts: []
        property string activeLayoutId: ""
        property string hoveredLayoutId: ""
        property bool globalAutoAssign: false
        property string selectedLayoutId: ""
        property int selectedZoneIndex: -1
        property int minZoneSize: 8
        property int cursorX: -1
        property int cursorY: -1
        property real screenAspectRatio: 16 / 9
        property int screenWidth: 1920
        property int selectorPosition: 0
        property int selectorLayoutMode: 1
        property int selectorGridColumns: 5
        property int previewWidth: 180
        property int previewHeight: 101
        property bool previewLockAspect: true
        property bool positionIsVertical: false
        property bool loaded: false
        property int indicatorWidth: 180
        property int indicatorHeight: 101
        property int indicatorSpacing: 18
        property int layoutColumns: 1
        property int layoutRows: 1
        property int contentWidth: 180
        property int contentHeight: 129
        property int containerPadding: 36
        property int containerPaddingSide: 18
        property int containerTopMargin: 10
        property int containerSideMargin: 10
        property int containerRadius: 12
        property int labelTopMargin: 8
        property int labelHeight: 20
        property int labelSpace: 28
        property int cardPadding: 26
        property int cardSidePadding: 18
        property int containerWidth: 216
        property int containerHeight: 165
        property int barHeight: 175
        property int barWidth: 216
        property int totalRows: 1
        property int scrollContentHeight: 129
        property int scrollContentWidth: 180
        property bool needsScrolling: false
        property bool needsHorizontalScrolling: false
        property real previewScale: 0.09375
        property int zonePadding: 0
        property int zoneBorderWidth: 2
        property int zoneBorderRadius: 8
        property int scaledPadding: 1
        property int scaledBorderWidth: 1
        property int scaledBorderRadius: 2
        property bool locked: false
        property color highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
        property color inactiveColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)
        property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.9)
        property string fontFamily: ""
        property real fontSizeScale: 1
        property int fontWeight: Font.Bold
        property bool fontItalic: false
        property bool fontUnderline: false
        property bool fontStrikeout: false
        property color backgroundColor: Kirigami.Theme.backgroundColor
        property color textColor: Kirigami.Theme.textColor
        property real activeOpacity: 0.5
        property real inactiveOpacity: 0.3

        // Surface-shader decoration (Stage d). C++ OverlayService::applyDecoration
        // resolves the "popup.zoneSelector" pack and writes these before each
        // show; empty source = no decoration. Consumed by the SurfaceDecoration
        // sibling below.
        property url decorationShaderSource
        property string decorationParamPreamble: ""
        property var decorationShaderParams: ({})

        function applyScrollDelta(angleDeltaY) {
            if (zoneSelectorLoader.item)
                zoneSelectorLoader.item.applyScrollDelta(angleDeltaY);
        }

        function resetCursorState() {
            if (zoneSelectorLoader.item)
                zoneSelectorLoader.item.resetCursorState();

            zoneSelectorSlot.cursorX = -1;
            zoneSelectorSlot.cursorY = -1;
        }

        anchors.fill: parent
        // Mid tier — paints above the main zone overlay (z=0) and below
        // popups (z=2) / OSDs (z=3). Drag-time selector card sits in
        // front of the zone-overlay layer the user sees during the drag.
        z: 1
        opacity: 0
        visible: false

        Loader {
            id: zoneSelectorLoader

            anchors.fill: parent
            active: zoneSelectorSlot.loaded
            asynchronous: true
            sourceComponent: zoneSelectorContentComp
            // No signal wiring: the zone-selector slot is input-transparent by
            // design (see ZoneSelectorContent's `interactive: false`). Cursor
            // tracking and commit both go through C++ (updateSelectorPosition
            // + drop.cpp), so QML never needs to forward a selection event.
        }

        Component {
            id: zoneSelectorContentComp

            ZoneSelectorContent {
                layouts: zoneSelectorSlot.layouts
                activeLayoutId: zoneSelectorSlot.activeLayoutId
                hoveredLayoutId: zoneSelectorSlot.hoveredLayoutId
                globalAutoAssign: zoneSelectorSlot.globalAutoAssign
                selectedLayoutId: zoneSelectorSlot.selectedLayoutId
                selectedZoneIndex: zoneSelectorSlot.selectedZoneIndex
                minZoneSize: zoneSelectorSlot.minZoneSize
                cursorX: zoneSelectorSlot.cursorX
                cursorY: zoneSelectorSlot.cursorY
                screenAspectRatio: zoneSelectorSlot.screenAspectRatio
                screenWidth: zoneSelectorSlot.screenWidth
                selectorPosition: zoneSelectorSlot.selectorPosition
                selectorLayoutMode: zoneSelectorSlot.selectorLayoutMode
                selectorGridColumns: zoneSelectorSlot.selectorGridColumns
                previewWidth: zoneSelectorSlot.previewWidth
                previewHeight: zoneSelectorSlot.previewHeight
                previewLockAspect: zoneSelectorSlot.previewLockAspect
                positionIsVertical: zoneSelectorSlot.positionIsVertical
                indicatorWidth: zoneSelectorSlot.indicatorWidth
                indicatorHeight: zoneSelectorSlot.indicatorHeight
                indicatorSpacing: zoneSelectorSlot.indicatorSpacing
                layoutColumns: zoneSelectorSlot.layoutColumns
                layoutRows: zoneSelectorSlot.layoutRows
                contentWidth: zoneSelectorSlot.contentWidth
                contentHeight: zoneSelectorSlot.contentHeight
                containerPadding: zoneSelectorSlot.containerPadding
                containerPaddingSide: zoneSelectorSlot.containerPaddingSide
                containerTopMargin: zoneSelectorSlot.containerTopMargin
                containerSideMargin: zoneSelectorSlot.containerSideMargin
                containerRadius: zoneSelectorSlot.containerRadius
                labelTopMargin: zoneSelectorSlot.labelTopMargin
                labelHeight: zoneSelectorSlot.labelHeight
                labelSpace: zoneSelectorSlot.labelSpace
                cardPadding: zoneSelectorSlot.cardPadding
                cardSidePadding: zoneSelectorSlot.cardSidePadding
                containerWidth: zoneSelectorSlot.containerWidth
                containerHeight: zoneSelectorSlot.containerHeight
                barHeight: zoneSelectorSlot.barHeight
                barWidth: zoneSelectorSlot.barWidth
                totalRows: zoneSelectorSlot.totalRows
                scrollContentHeight: zoneSelectorSlot.scrollContentHeight
                scrollContentWidth: zoneSelectorSlot.scrollContentWidth
                needsScrolling: zoneSelectorSlot.needsScrolling
                needsHorizontalScrolling: zoneSelectorSlot.needsHorizontalScrolling
                previewScale: zoneSelectorSlot.previewScale
                zonePadding: zoneSelectorSlot.zonePadding
                zoneBorderWidth: zoneSelectorSlot.zoneBorderWidth
                zoneBorderRadius: zoneSelectorSlot.zoneBorderRadius
                scaledPadding: zoneSelectorSlot.scaledPadding
                scaledBorderWidth: zoneSelectorSlot.scaledBorderWidth
                scaledBorderRadius: zoneSelectorSlot.scaledBorderRadius
                locked: zoneSelectorSlot.locked
                highlightColor: zoneSelectorSlot.highlightColor
                inactiveColor: zoneSelectorSlot.inactiveColor
                borderColor: zoneSelectorSlot.borderColor
                fontFamily: zoneSelectorSlot.fontFamily
                fontSizeScale: zoneSelectorSlot.fontSizeScale
                fontWeight: zoneSelectorSlot.fontWeight
                fontItalic: zoneSelectorSlot.fontItalic
                fontUnderline: zoneSelectorSlot.fontUnderline
                fontStrikeout: zoneSelectorSlot.fontStrikeout
                backgroundColor: zoneSelectorSlot.backgroundColor
                textColor: zoneSelectorSlot.textColor
                activeOpacity: zoneSelectorSlot.activeOpacity
                inactiveOpacity: zoneSelectorSlot.inactiveOpacity
            }
        }

        // Surface-shader decoration (Stage d). SIBLING of zoneSelectorLoader.
        // Captures the loaded content's PopupFrame shaderAnchor and re-renders it
        // through the resolved "popup.zoneSelector" surface pack. Inert when the
        // source is empty.
        SurfaceDecoration {
            anchors.fill: parent
            contentItem: zoneSelectorLoader.item
            decorationShaderSource: zoneSelectorSlot.decorationShaderSource
            decorationParamPreamble: zoneSelectorSlot.decorationParamPreamble
            decorationShaderParams: zoneSelectorSlot.decorationShaderParams
        }
    }

    Item {
        id: mainOverlaySlot

        // Mode flag: false → ZoneOverlayContent (rectangles); true →
        // RenderNodeOverlayContent (shader). C++ side flips on
        // per-screen layout's shader settings before each show.
        property bool useShader: false
        // Common properties — both modes consume these.
        property var zones: []
        property string highlightedZoneId: ""
        property var highlightedZoneIds: []
        property bool showNumbers: true
        property var previewZones: []
        property color highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
        property color inactiveColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)
        property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.9)
        property color labelFontColor: Kirigami.Theme.textColor
        property string fontFamily: ""
        property real fontSizeScale: 1
        property int fontWeight: Font.Bold
        property bool fontItalic: false
        property bool fontUnderline: false
        property bool fontStrikeout: false
        property real activeOpacity: 0.5
        property real inactiveOpacity: 0.3
        property int borderWidth: Kirigami.Units.smallSpacing
        property int borderRadius: Kirigami.Units.gridUnit
        property bool _idled: false
        property bool loaded: false
        // Shader-mode properties.
        property url shaderSource
        property string paramPreamble: ""
        property string bufferShaderPath: ""
        property var bufferShaderPaths: []
        property bool bufferFeedback: false
        property real bufferScale: 1
        property string bufferWrap: "clamp"
        property int zoneCount: 0
        property int highlightedCount: 0
        property var shaderParams: ({})
        property int zoneDataVersion: 0
        property real iTime: 0
        property real iTimeDelta: 0
        property int iFrame: 0
        property point mousePosition: Qt.point(0, 0)
        property var labelsTexture
        property var audioSpectrum: []
        property var wallpaperTexture: null
        property bool useWallpaper: false
        property bool useDepthBuffer: false
        property var bufferWraps: []
        property string bufferFilter: "linear"
        property var bufferFilters: []

        function flash() {
            if (mainOverlayLoader.item && mainOverlayLoader.item.flash)
                mainOverlayLoader.item.flash();
        }

        function highlightZone(zoneId) {
            if (mainOverlayLoader.item && mainOverlayLoader.item.highlightZone)
                mainOverlayLoader.item.highlightZone(zoneId);
        }

        function highlightZones(zoneIds) {
            if (mainOverlayLoader.item && mainOverlayLoader.item.highlightZones)
                mainOverlayLoader.item.highlightZones(zoneIds);
        }

        function clearHighlight() {
            if (mainOverlayLoader.item && mainOverlayLoader.item.clearHighlight)
                mainOverlayLoader.item.clearHighlight();
        }

        function reloadShader() {
            if (mainOverlayLoader.item && mainOverlayLoader.item.reloadShader)
                mainOverlayLoader.item.reloadShader();
        }

        anchors.fill: parent
        // Bottom tier — zone overlay during a window drag is the
        // backdrop content; selector (z=1), popups (z=2), and OSDs
        // (z=3) all paint over it.
        z: 0
        opacity: 0
        visible: false

        Loader {
            id: mainOverlayLoader

            anchors.fill: parent
            active: mainOverlaySlot.loaded
            asynchronous: true
            sourceComponent: mainOverlaySlot.useShader ? renderNodeContentComp : zoneOverlayContentComp
        }

        Component {
            id: zoneOverlayContentComp

            ZoneOverlayContent {
                zones: mainOverlaySlot.zones
                highlightedZoneId: mainOverlaySlot.highlightedZoneId
                highlightedZoneIds: mainOverlaySlot.highlightedZoneIds
                showNumbers: mainOverlaySlot.showNumbers
                previewZones: mainOverlaySlot.previewZones
                highlightColor: mainOverlaySlot.highlightColor
                inactiveColor: mainOverlaySlot.inactiveColor
                borderColor: mainOverlaySlot.borderColor
                labelFontColor: mainOverlaySlot.labelFontColor
                fontFamily: mainOverlaySlot.fontFamily
                fontSizeScale: mainOverlaySlot.fontSizeScale
                fontWeight: mainOverlaySlot.fontWeight
                fontItalic: mainOverlaySlot.fontItalic
                fontUnderline: mainOverlaySlot.fontUnderline
                fontStrikeout: mainOverlaySlot.fontStrikeout
                activeOpacity: mainOverlaySlot.activeOpacity
                inactiveOpacity: mainOverlaySlot.inactiveOpacity
                borderWidth: mainOverlaySlot.borderWidth
                borderRadius: mainOverlaySlot.borderRadius
                _idled: mainOverlaySlot._idled
            }
        }

        Component {
            id: renderNodeContentComp

            RenderNodeOverlayContent {
                shaderSource: mainOverlaySlot.shaderSource
                paramPreamble: mainOverlaySlot.paramPreamble
                bufferShaderPath: mainOverlaySlot.bufferShaderPath
                bufferShaderPaths: mainOverlaySlot.bufferShaderPaths
                bufferFeedback: mainOverlaySlot.bufferFeedback
                bufferScale: mainOverlaySlot.bufferScale
                bufferWrap: mainOverlaySlot.bufferWrap
                zones: mainOverlaySlot.zones
                zoneCount: mainOverlaySlot.zoneCount
                highlightedCount: mainOverlaySlot.highlightedCount
                highlightedZoneId: mainOverlaySlot.highlightedZoneId
                highlightedZoneIds: mainOverlaySlot.highlightedZoneIds
                shaderParams: mainOverlaySlot.shaderParams
                zoneDataVersion: mainOverlaySlot.zoneDataVersion
                iTime: mainOverlaySlot.iTime
                iTimeDelta: mainOverlaySlot.iTimeDelta
                iFrame: mainOverlaySlot.iFrame
                mousePosition: mainOverlaySlot.mousePosition
                showNumbers: mainOverlaySlot.showNumbers
                labelFontColor: mainOverlaySlot.labelFontColor
                fontFamily: mainOverlaySlot.fontFamily
                fontSizeScale: mainOverlaySlot.fontSizeScale
                fontWeight: mainOverlaySlot.fontWeight
                fontItalic: mainOverlaySlot.fontItalic
                fontUnderline: mainOverlaySlot.fontUnderline
                fontStrikeout: mainOverlaySlot.fontStrikeout
                labelsTexture: mainOverlaySlot.labelsTexture
                audioSpectrum: mainOverlaySlot.audioSpectrum
                wallpaperTexture: mainOverlaySlot.wallpaperTexture
                useWallpaper: mainOverlaySlot.useWallpaper
                useDepthBuffer: mainOverlaySlot.useDepthBuffer
                bufferWraps: mainOverlaySlot.bufferWraps
                bufferFilter: mainOverlaySlot.bufferFilter
                bufferFilters: mainOverlaySlot.bufferFilters
                highlightColor: mainOverlaySlot.highlightColor
                inactiveColor: mainOverlaySlot.inactiveColor
                borderColor: mainOverlaySlot.borderColor
                activeOpacity: mainOverlaySlot.activeOpacity
                inactiveOpacity: mainOverlaySlot.inactiveOpacity
                borderWidth: mainOverlaySlot.borderWidth
                borderRadius: mainOverlaySlot.borderRadius
                _idled: mainOverlaySlot._idled
            }
        }
    }
}
