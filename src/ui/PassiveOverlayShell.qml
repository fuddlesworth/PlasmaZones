// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

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
 *   - The shell wl_surface is kept mapped across hides while shaders or
 *     animations are enabled (keepMappedOnHide is effects-gated in
 *     createWarmedOsdSurface), so the Vulkan swapchain + RHI pipelines
 *     warm once and stay hot for every subsequent per-content show —
 *     no per-show wl_surface map/unmap, and the cold-pipeline
 *     first-paint cost is paid once at daemon start. With both effects
 *     off, syncPassiveShellSurfaceState unmaps the wl_surface when all
 *     slots are hidden.
 *
 * This shell is the kbd-None grouping. Modal kbd-Exclusive overlays
 * (snap-assist, layout picker) historically lived in their own per-show
 * wl_surfaces because layer-shell binds keyboard interactivity at first
 * commit and KWin doesn't re-evaluate it on already-mapped surfaces.
 * The unified shell hosts them in THIS same shell with kbd routed via
 * global accelerators (KGlobalAccel) instead — see the modal slots hosted
 * in PassiveOverlayModalSlots.qml (instantiated below as `modalSlots`).
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
    // Sibling tiers below: the modalSlots container (z=2, hosting the
    // snap-assist / layout-picker / cheatsheet slots in
    // PassiveOverlayModalSlots.qml), zoneSelectorSlot (z=1),
    // scrollDropIndicatorSlot (z=0.6), mainOverlaySlot (z=0). The osdSlot's
    // z is dynamic (3 normally, 1.5 while a modal slot is visible — see the
    // binding on osdSlot). Each is a sibling Item with its own properties +
    // Loader, animated independently via the SurfaceAnimator's
    // per-(Surface, target) keying.

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
    readonly property Item snapAssistSlotItem: modalSlots.snapAssistSlotItem
    /// Layout-picker slot Item — SurfaceAnimator target for picker
    /// show/hide. Modal kbd (Return/Enter/arrows/Escape) routes via
    /// KGlobalAccel ad-hoc registrations made by start.cpp on the
    /// matching show/dismiss signals — the shell is kbd-None so QML
    /// Shortcuts can't fire here.
    readonly property Item layoutPickerSlotItem: modalSlots.layoutPickerSlotItem
    /// Zone-selector slot Item — SurfaceAnimator target for selector
    /// show/hide. Per-VS positioning via the slot's anchors.fill: parent
    /// + the shell being sized to the VS rect.
    readonly property alias zoneSelectorSlotItem: zoneSelectorSlot
    /// Main zone overlay slot Item — displays zones during window drag.
    readonly property alias mainOverlaySlotItem: mainOverlaySlot
    /// Cheatsheet slot Item — SurfaceAnimator target for the shortcut
    /// cheatsheet's show/hide. Display-only modal card; Escape routes via
    /// a dedicated KGlobalAccel ad-hoc grab made by start.cpp on the
    /// matching show/dismiss signals — the shell is kbd-None so QML
    /// Shortcuts can't fire here.
    readonly property Item cheatsheetSlotItem: modalSlots.cheatsheetSlotItem
    /// Scroll drag drop-indicator slot Item — outlines the slot a dragged
    /// window would land in while a scrolling drag re-insert is armed.
    /// Display-only: it declares no pointer handlers and contributes no input
    /// region, because it is drawn underneath a cursor that is mid-drag.
    /// Content updates are plain property writes (no re-instantiation).
    readonly property alias scrollDropIndicatorSlotItem: scrollDropIndicatorSlot

    /// Forwarded from the loaded OSD content. C++ side connects this to
    /// the slot-hide animation start (not Surface::hide() — only the
    /// slot's opacity animates; the shell stays mapped across hides
    /// while shaders or animations are enabled).
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
    /// Forwarded from the cheatsheet's `dismissRequested` (backdrop click;
    /// primary Escape goes via the dedicated ad-hoc grab).
    signal cheatsheetDismissRequested

    // Qt::WindowTransparentForInput is driven imperatively by C++ from
    // syncPassiveShellSurfaceState (via Surface::show()/hide();
    // keepMappedOnHide is effects-gated, see createWarmedOsdSurface,
    // so the surface is kept mapped on hide only while shaders or
    // animations are enabled) — when no slot is visible, the shell's
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
    // surface stays mapped across hides while shaders or animations
    // are enabled (effects-gated keepMappedOnHide) so swapchain + RHI
    // pipelines stay warm across show cycles; with both effects off
    // the wl_surface unmaps once all slots are hidden.
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
        // Card corner radius the surface decoration rounds to. Read by
        // OverlayService::applyDecoration and injected as the border/shadow
        // cornerRadius, so the card silhouette follows the card rather than a
        // per-pack value. gridUnit * 1.5 is the OSD card's design radius.
        property real cardCornerRadius: Kirigami.Units.gridUnit * 1.5
        property var zones: []
        property color backgroundColor: Kirigami.Theme.backgroundColor
        property color textColor: Kirigami.Theme.textColor
        property color highlightColor: QFZCommon.ZoneColorDefaults.previewActiveZoneColor
        // MUST be declared: the daemon pushes these with setProperty
        // (osd.cpp pushLayoutOsdContent) and the layoutOsdComp bindings
        // below forward them — an undeclared name silently becomes a
        // dynamic property no binding observes, so the forwarding would
        // bind undefined and the OSD preview would never recolor.
        property color inactiveColor: QFZCommon.ZoneColorDefaults.previewInactiveZoneColor
        property color borderColor: QFZCommon.ZoneColorDefaults.previewZoneBorderColor
        // Zone fill opacities for the OSD preview. Same declare-and-forward
        // contract as the colors above: osd.cpp pushLayoutOsdContent writes
        // the settings/override-resolved pair; the defaults match
        // LayoutOsdContent's own.
        property real activeOpacity: 0.6
        property real inactiveOpacity: 0.3
        property string layoutId: ""
        property string layoutName: ""
        property int category: 0
        property bool autoAssign: false
        property bool globalAutoAssign: false
        property bool showMasterDot: false
        property int masterCount: 1
        property bool producesOverlappingZones: false
        property string zoneNumberDisplay: "all"
        // Strip axis ticks and the empty-strip caption for the scrolling
        // strip card. Same declare-and-forward contract as the colors above:
        // osd.cpp pushLayoutOsdContent writes both with setProperty on every
        // show, and an UNDECLARED name silently becomes a dynamic property no
        // binding observes — the ticks would never reach LayoutOsdContent.
        property string stripAxisHint: "none"
        property string stripEmptyCaption: ""
        property real screenAspectRatio: 16 / 9
        property string aspectRatioClass: "any"
        property string fontFamily: ""
        property real fontSizeScale: 1
        property int fontWeight: Font.Bold
        property bool fontItalic: false
        property bool fontUnderline: false
        property bool fontStrikeout: false
        property bool locked: false
        // Captions the card as a native scrolling template rather than a zone
        // layout. Same declare-and-forward contract as the colors above:
        // pushLayoutOsdContent writes it on every layout-osd show.
        property bool isTemplate: false
        property bool disabled: false
        property string disabledReason: ""
        // Overlay glyph for the disabled-style card. The card is
        // refusal-only (overlayservice.h documents the design), so the
        // daemon restates this same literal per show; the QML default only
        // covers the never-shown pre-first-write state. One of four copies
        // of the literal: LayoutOsdContent.qml's default, the write in
        // src/daemon/overlayservice/osd.cpp, and daemon/osd.cpp's text-OSD
        // fallback for the same message — change one and change all four.
        property string disabledIcon: "dialog-cancel"
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
        // Resolved decoration chain: ordered stage list ({source,
        // vertexSource, preamble, params, animated} per pack), plus the
        // chain's largest declared outer margin (logical px, e.g. glow's
        // glowSize) the decoration host inflates its capture by. MUST be
        // declared + forwarded: C++ writes them with setProperty, and an
        // undeclared name silently becomes a dynamic property no binding
        // observes — the decoration would never update.
        property var decorationChain: []
        property real decorationOuterPadding: 0
        // Live CAVA audio spectrum, forwarded to the SurfaceDecoration below.
        // Same declare-and-forward contract as decorationChain: C++ writes it
        // with setProperty, so an undeclared name would silently become a dead
        // dynamic property no binding observes and audio would never reach the
        // decoration shader.
        property var audioSpectrum: []

        /// Restart the loaded OSD content's auto-dismiss timer. C++
        /// invokes this after every OSD show via QMetaObject::invokeMethod.
        function restartDismissTimer() {
            if (osdLoader.item)
                osdLoader.item.restartDismissTimer();
            else
                console.warn("PassiveOverlayShell.osdSlot.restartDismissTimer: no OSD content loaded (mode =", JSON.stringify(osdSlot.mode), ") — auto-dismiss will not run");
        }

        anchors.fill: parent
        // Topmost slot while no modal is up: notifications/OSDs paint
        // above the passive content types (main overlay z=0, zone
        // selector z=1) so a layout-OSD or nav-OSD reads cleanly over an
        // active zone overlay or drag-time selector. While a MODAL slot
        // (snap-assist, layout picker, or the shortcut cheatsheet, hosted in
        // the z=2 modal container) is visible the OSD
        // drops to 1.5 — still above the passive tiers, but below the
        // modal — so a concurrently-fired OSD card neither occludes
        // modal content for its ~1.5s display nor lets its
        // click-to-dismiss MouseArea eat clicks meant for the modal
        // (the shell grabs input only while a modal is up, so an
        // OSD-above-modal card would otherwise sit first in hit-test
        // order over its rect). `visible` is the right predicate: it
        // flips true at modal show and back to false only when the
        // hide animation completes (onSnapAssistSlotHideCompleted /
        // the picker equivalent), covering the modal's full on-screen
        // span; `loaded` blips false→true on every re-show.
        z: modalSlots.anyModalVisible ? 1.5 : 3
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
                inactiveColor: osdSlot.inactiveColor
                borderColor: osdSlot.borderColor
                activeOpacity: osdSlot.activeOpacity
                inactiveOpacity: osdSlot.inactiveOpacity
                layoutId: osdSlot.layoutId
                layoutName: osdSlot.layoutName
                category: osdSlot.category
                autoAssign: osdSlot.autoAssign
                globalAutoAssign: osdSlot.globalAutoAssign
                showMasterDot: osdSlot.showMasterDot
                masterCount: osdSlot.masterCount
                producesOverlappingZones: osdSlot.producesOverlappingZones
                zoneNumberDisplay: osdSlot.zoneNumberDisplay
                stripAxisHint: osdSlot.stripAxisHint
                stripEmptyCaption: osdSlot.stripEmptyCaption
                screenAspectRatio: osdSlot.screenAspectRatio
                aspectRatioClass: osdSlot.aspectRatioClass
                fontFamily: osdSlot.fontFamily
                fontSizeScale: osdSlot.fontSizeScale
                fontWeight: osdSlot.fontWeight
                fontItalic: osdSlot.fontItalic
                fontUnderline: osdSlot.fontUnderline
                fontStrikeout: osdSlot.fontStrikeout
                locked: osdSlot.locked
                isTemplate: osdSlot.isTemplate
                disabled: osdSlot.disabled
                disabledReason: osdSlot.disabledReason
                disabledIcon: osdSlot.disabledIcon
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
                fontFamily: osdSlot.fontFamily
                fontSizeScale: osdSlot.fontSizeScale
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
            decorationChain: osdSlot.decorationChain
            decorationOuterPadding: osdSlot.decorationOuterPadding
            audioSpectrum: osdSlot.audioSpectrum
        }
    }

    // The popup tier's three modal slots (snap assist, layout picker,
    // cheatsheet), extracted by concern into PassiveOverlayModalSlots.qml.
    // The container carries the tier's z=2 the slots used to declare
    // individually — modals paint above the zone selector and main overlay,
    // and above OSDs while visible (the osdSlot z binding above reads
    // modalSlots.anyModalVisible). The root ...SlotItem properties re-expose
    // each slot Item so the C++ wire-up is unchanged.
    PassiveOverlayModalSlots {
        id: modalSlots

        shellRoot: root
        anchors.fill: parent
        z: 2
    }

    Item {
        id: scrollDropIndicatorSlot

        // Drop-target rect in shell-window coordinates — C++ converts from
        // absolute compositor space before writing. An empty rect never
        // arrives here: the daemon hides the slot instead.
        property rect indicatorRect: Qt.rect(0, 0, 0, 0)
        // Indicator colour (Scrolling.DropIndicator/Color), pushed by C++ on
        // every rect update, always CONCRETE — the follow-the-theme sentinel is
        // resolved in Settings before it reaches the overlay. Must be declared
        // here AND forwarded below: setProperty on an undeclared name silently
        // creates a dynamic property that no binding ever sees (see the
        // zoneSelectorSlot contract note). The initialisers are placeholders
        // that are never painted: C++ writes both colours before it writes
        // `loaded`, so the content item is instantiated with the real values.
        property color indicatorColor: Kirigami.Theme.highlightColor
        property color indicatorBorderColor: Kirigami.Theme.highlightColor
        property real indicatorOpacity: 0.25
        property int indicatorBorderWidth: 2
        property int indicatorBorderRadius: 8
        // Whether the content should tween a rect change: false for the
        // FIRST rect of a (re)show so it snaps into place instead of
        // stretching in from the stale rect of the previous drag, true for
        // cursor-driven target changes. Written by C++ BEFORE the rect.
        property bool animateMoves: true
        // Content lifecycle gate, toggled by C++ on show/hide. Unlike the
        // OSD-style slots the content is NOT re-instantiated per update — the
        // rect changes as the drag moves and flows through the
        // `indicatorRect` binding.
        property bool loaded: false

        anchors.fill: parent
        // Indicator tier: above the main overlay (z=0) and below the zone
        // selector (z=1), the OSDs and the modals.
        z: 0.6
        opacity: 0
        visible: false

        Loader {
            id: scrollDropIndicatorLoader

            anchors.fill: parent
            active: scrollDropIndicatorSlot.loaded
            // SYNCHRONOUS by contract — see snapAssistLoader.
            sourceComponent: scrollDropIndicatorContentComp
        }

        Component {
            id: scrollDropIndicatorContentComp

            ScrollDropIndicatorContent {
                indicatorRect: scrollDropIndicatorSlot.indicatorRect
                indicatorColor: scrollDropIndicatorSlot.indicatorColor
                indicatorBorderColor: scrollDropIndicatorSlot.indicatorBorderColor
                indicatorOpacity: scrollDropIndicatorSlot.indicatorOpacity
                indicatorBorderWidth: scrollDropIndicatorSlot.indicatorBorderWidth
                indicatorBorderRadius: scrollDropIndicatorSlot.indicatorBorderRadius
                animateMoves: scrollDropIndicatorSlot.animateMoves
            }
        }
    }

    Item {
        id: zoneSelectorSlot

        // Selector data properties — C++ writes these per-show. The
        // ZoneSelectorContent inside the Loader picks them up via QML
        // lexical scope.
        //
        // Declared-but-unforwarded contract: 16 of these properties are
        // written by C++ (selector.cpp / selector_update.cpp) but NOT
        // forwarded to ZoneSelectorContent below, whose consumers were
        // removed when the content derives those values itself:
        //   screenAspectRatio, screenWidth, selectorLayoutMode,
        //   selectorGridColumns, previewWidth, previewHeight,
        //   previewLockAspect, positionIsVertical, layoutRows, barHeight,
        //   barWidth, totalRows, previewScale, zonePadding,
        //   zoneBorderWidth, zoneBorderRadius
        // They MUST stay declared: C++ pushes them with setProperty, and
        // deleting a declaration would silently demote the write to a
        // dynamic property (masking the contract) while C++-side reads of
        // the slot's current values would break. Do not remove them
        // without also removing the corresponding C++ writes.
        property var layouts: []
        property string activeLayoutId: ""
        property bool globalAutoAssign: false
        property string selectedLayoutId: ""
        property int selectedZoneIndex: -1
        // Strip-mode selector state (scrolling screens). Same
        // declare-and-forward contract as decorationChain below: C++ writes
        // these with setProperty (selector_update.cpp pushes stripMode /
        // stripColumns / stripVerticalAxis per update; selector_strip.cpp
        // writes the selectedStrip* triple per hit-test), so an undeclared
        // name would silently become a dead dynamic property and the content
        // would never leave layout mode.
        property bool stripMode: false
        property var stripColumns: []
        // Undeclared until #923's audit: the C++ push landed on a dynamic
        // property no binding observed, so every axis branch in
        // ZoneSelectorContent and ZoneSelectorStripCard was dead while the
        // C++ hit-test transposed anyway. The picture and the drop target
        // disagreed with nothing to show for it.
        property bool stripVerticalAxis: false
        property int selectedStripColumn: -1
        property int selectedStripGap: -1
        property int selectedStripHalf: -1
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
        property int containerTopMargin: 10
        property int containerSideMargin: 10
        // Card corner radius the surface decoration rounds to (see osdSlot).
        property real cardCornerRadius: Kirigami.Units.largeSpacing * 1.5
        property int labelTopMargin: 8
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
        property color highlightColor: QFZCommon.ZoneColorDefaults.previewActiveZoneColor
        property color inactiveColor: QFZCommon.ZoneColorDefaults.previewInactiveZoneColor
        property color borderColor: QFZCommon.ZoneColorDefaults.previewZoneBorderColor
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
        // Resolved decoration chain: ordered stage list ({source,
        // vertexSource, preamble, params, animated} per pack), plus the
        // chain's largest declared outer margin (logical px, e.g. glow's
        // glowSize) the decoration host inflates its capture by. MUST be
        // declared + forwarded: C++ writes them with setProperty, and an
        // undeclared name silently becomes a dynamic property no binding
        // observes — the decoration would never update.
        property var decorationChain: []
        property real decorationOuterPadding: 0
        // Live CAVA audio spectrum, forwarded to the SurfaceDecoration below.
        // Same declare-and-forward contract as decorationChain: C++ writes it
        // with setProperty, so an undeclared name would silently become a dead
        // dynamic property no binding observes and audio would never reach the
        // decoration shader.
        property var audioSpectrum: []

        function applyScrollDelta(angleDeltaY) {
            if (zoneSelectorLoader.item)
                zoneSelectorLoader.item.applyScrollDelta(angleDeltaY);
        }

        // Clears slot-level cursor state only. The content's cursorX /
        // cursorY are bindings onto these slot properties (forwarded in
        // zoneSelectorContentComp below), so writing the slot props is
        // sufficient — assigning on the content item would sever those
        // bindings for the rest of the content's lifetime.
        function resetCursorState() {
            zoneSelectorSlot.cursorX = -1;
            zoneSelectorSlot.cursorY = -1;
        }

        anchors.fill: parent
        // Mid tier — paints above the main zone overlay (z=0) and below
        // popups (z=2) / OSDs (z=3, or 1.5 while a modal is visible —
        // still above this slot). Drag-time selector card sits in
        // front of the zone-overlay layer the user sees during the drag.
        z: 1
        opacity: 0
        visible: false

        Loader {
            id: zoneSelectorLoader

            anchors.fill: parent
            active: zoneSelectorSlot.loaded
            // SYNCHRONOUS by contract — see snapAssistLoader: beginShow
            // resolves the shaderAnchor in the same tick as the `loaded`
            // toggle; an async mount races it and the entrance animation
            // intermittently degrades to a static surface + end pop.
            sourceComponent: zoneSelectorContentComp
            // No signal wiring: the zone-selector slot is input-transparent by
            // design (its zone previews declare no pointer handlers). Cursor
            // tracking and commit both go through C++ (updateSelectorPosition
            // + drop.cpp), so QML never needs to forward a selection event.
        }

        Component {
            id: zoneSelectorContentComp

            ZoneSelectorContent {
                layouts: zoneSelectorSlot.layouts
                activeLayoutId: zoneSelectorSlot.activeLayoutId
                globalAutoAssign: zoneSelectorSlot.globalAutoAssign
                selectedLayoutId: zoneSelectorSlot.selectedLayoutId
                selectedZoneIndex: zoneSelectorSlot.selectedZoneIndex
                stripMode: zoneSelectorSlot.stripMode
                stripColumns: zoneSelectorSlot.stripColumns
                stripVerticalAxis: zoneSelectorSlot.stripVerticalAxis
                selectedStripColumn: zoneSelectorSlot.selectedStripColumn
                selectedStripGap: zoneSelectorSlot.selectedStripGap
                selectedStripHalf: zoneSelectorSlot.selectedStripHalf
                minZoneSize: zoneSelectorSlot.minZoneSize
                cursorX: zoneSelectorSlot.cursorX
                cursorY: zoneSelectorSlot.cursorY
                selectorPosition: zoneSelectorSlot.selectorPosition
                indicatorWidth: zoneSelectorSlot.indicatorWidth
                indicatorHeight: zoneSelectorSlot.indicatorHeight
                indicatorSpacing: zoneSelectorSlot.indicatorSpacing
                layoutColumns: zoneSelectorSlot.layoutColumns
                contentWidth: zoneSelectorSlot.contentWidth
                contentHeight: zoneSelectorSlot.contentHeight
                containerTopMargin: zoneSelectorSlot.containerTopMargin
                containerSideMargin: zoneSelectorSlot.containerSideMargin
                labelTopMargin: zoneSelectorSlot.labelTopMargin
                labelSpace: zoneSelectorSlot.labelSpace
                cardPadding: zoneSelectorSlot.cardPadding
                cardSidePadding: zoneSelectorSlot.cardSidePadding
                containerWidth: zoneSelectorSlot.containerWidth
                containerHeight: zoneSelectorSlot.containerHeight
                scrollContentHeight: zoneSelectorSlot.scrollContentHeight
                scrollContentWidth: zoneSelectorSlot.scrollContentWidth
                needsScrolling: zoneSelectorSlot.needsScrolling
                needsHorizontalScrolling: zoneSelectorSlot.needsHorizontalScrolling
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
            decorationChain: zoneSelectorSlot.decorationChain
            decorationOuterPadding: zoneSelectorSlot.decorationOuterPadding
            audioSpectrum: zoneSelectorSlot.audioSpectrum
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
        property color highlightColor: QFZCommon.ZoneColorDefaults.activeZoneColor
        property color inactiveColor: QFZCommon.ZoneColorDefaults.inactiveZoneColor
        property color borderColor: QFZCommon.ZoneColorDefaults.zoneBorderColor
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
        property bool halfFloatBuffers: true
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

        function reloadShader() {
            if (mainOverlayLoader.item && mainOverlayLoader.item.reloadShader)
                mainOverlayLoader.item.reloadShader();
        }

        // Idle-quiesce hook (OverlayService::scheduleIdleQuiesce): frees the
        // shader item's GPU resources after the idle grace window. No-op for
        // the rectangle content, which has no releaseIdleGraphicsResources.
        function releaseIdleGraphicsResources() {
            if (mainOverlayLoader.item && mainOverlayLoader.item.releaseIdleGraphicsResources)
                mainOverlayLoader.item.releaseIdleGraphicsResources();
            else if (mainOverlayLoader.item && mainOverlaySlot.useShader)
                // Same warn-on-broken-chain convention as osdSlot.restartDismissTimer:
                // a shader-mode slot whose content lacks the hook means the
                // installed QML is out of step with the daemon, and the idle
                // memory release silently stops working.
                console.warn("PassiveOverlayShell.mainOverlaySlot.releaseIdleGraphicsResources: shader content loaded without the hook — idle GPU release skipped");
        }

        anchors.fill: parent
        // Bottom tier — zone overlay during a window drag is the
        // backdrop content; selector (z=1), popups (z=2), and OSDs
        // (z=3, or 1.5 under a visible modal) all paint over it.
        z: 0
        opacity: 0
        visible: false

        Loader {
            id: mainOverlayLoader

            anchors.fill: parent
            active: mainOverlaySlot.loaded
            // SYNCHRONOUS by contract — see snapAssistLoader: beginShow
            // resolves the shaderAnchor in the same tick as the `loaded`
            // toggle; an async mount races it and the entrance animation
            // intermittently degrades to a static surface + end pop.
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
                halfFloatBuffers: mainOverlaySlot.halfFloatBuffers
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
