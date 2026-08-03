// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * The passive shell's three modal popup slots (snap assist, layout picker,
 * shortcut cheatsheet), extracted from PassiveOverlayShell.qml by concern:
 * the three are the "popup tier" (z=2 within the shell's sibling stack,
 * mutually exclusive in practice), each following the same shape — a
 * property surface C++ writes before each show, a synchronous Loader gated
 * on `loaded`, and a SurfaceDecoration sibling.
 *
 * This container carries the whole tier's z; the slots inside share it.
 * The shell re-exposes each slot Item through its own root aliases so the
 * C++ wire-up (shellhost_bridge's window->property("...SlotItem")) is
 * unchanged, and reads `anyModalVisible` for the osdSlot z drop.
 *
 * @p shellRoot is the hosting PassiveOverlayShell window root; the modal
 * contents' selection/dismiss signals connect to its signal surface.
 */
Item {
    id: modalSlotsRoot

    required property var shellRoot

    readonly property alias snapAssistSlotItem: snapAssistSlot
    readonly property alias layoutPickerSlotItem: layoutPickerSlot
    readonly property alias cheatsheetSlotItem: cheatsheetSlot
    // The osdSlot drops from z=3 to 1.5 whenever a modal slot is visible;
    // computed here so the shell does not reach into the slots' internals.
    readonly property bool anyModalVisible: snapAssistSlot.visible || layoutPickerSlot.visible || cheatsheetSlot.visible

    Item {
        id: snapAssistSlot

        // Snap-assist data properties — C++ writes these before each
        // show; SnapAssistContent picks them up via QML lexical scope.
        property var emptyZones: []
        property var candidates: []
        property int screenWidth: 1920
        property int screenHeight: 1080
        property color highlightColor: QFZCommon.ZoneColorDefaults.activeZoneColor
        property color inactiveColor: QFZCommon.ZoneColorDefaults.inactiveZoneColor
        property color borderColor: QFZCommon.ZoneColorDefaults.zoneBorderColor
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

        anchors.fill: parent
        opacity: 0
        visible: false

        Loader {
            id: snapAssistLoader

            anchors.fill: parent
            active: snapAssistSlot.loaded
            // SYNCHRONOUS by contract: the C++ show path toggles `loaded`
            // and calls SurfaceAnimator::beginShow in the SAME tick, and
            // beginShow resolves the shaderAnchor from the live item tree.
            // An asynchronous load loses that race intermittently — no
            // anchor exists yet, the animator falls back to the bare slot
            // (no capture, no sibling hiding), the shader leg snaps opacity
            // to 1.0, and the content + decoration then mount mid-leg as a
            // STATIC fully-decorated surface that pops at completion. The
            // mount jank a sync load costs is the OSD loader's long-proven
            // behaviour; a correct entrance animation outranks it.
            sourceComponent: snapAssistContentComp
            onLoaded: {
                if (snapAssistLoader.item) {
                    snapAssistLoader.item.windowSelected.connect(modalSlotsRoot.shellRoot.snapAssistWindowSelected);
                    snapAssistLoader.item.dismissRequested.connect(modalSlotsRoot.shellRoot.snapAssistDismissRequested);
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
            decorationChain: snapAssistSlot.decorationChain
            decorationOuterPadding: snapAssistSlot.decorationOuterPadding
            audioSpectrum: snapAssistSlot.audioSpectrum
        }
    }

    Item {
        id: layoutPickerSlot

        // Picker data properties — C++ writes these before each show.
        property var layouts: []
        property string activeLayoutId: ""
        property real screenAspectRatio: 16 / 9
        // Card corner radius the surface decoration rounds to (see osdSlot).
        property real cardCornerRadius: Kirigami.Units.largeSpacing * 2
        property bool globalAutoAssign: false
        property bool locked: false
        property color backgroundColor: Kirigami.Theme.backgroundColor
        property color textColor: Kirigami.Theme.textColor
        property color highlightColor: QFZCommon.ZoneColorDefaults.previewActiveZoneColor
        property color inactiveColor: QFZCommon.ZoneColorDefaults.previewInactiveZoneColor
        property color borderColor: QFZCommon.ZoneColorDefaults.previewZoneBorderColor
        property real activeOpacity: 0.5
        property real inactiveOpacity: 0.3
        property string fontFamily: ""
        property real fontSizeScale: 1
        property int fontWeight: Font.Bold
        property bool fontItalic: false
        property bool fontUnderline: false
        property bool fontStrikeout: false
        // No labelFontColor here: picker previews deliberately don't wire label color, consistent with the selector and OSD slots.
        // OSD-style content lifecycle gate. C++ toggles false→true around
        // each show so LayoutPickerContent is re-instantiated.
        property bool loaded: false

        // Surface-shader decoration (Stage d). C++ OverlayService::applyDecoration
        // resolves the "popup.layoutPicker" pack and writes these before each
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
            // SYNCHRONOUS by contract — see snapAssistLoader: beginShow
            // resolves the shaderAnchor in the same tick as the `loaded`
            // toggle; an async mount races it and the entrance animation
            // intermittently degrades to a static surface + end pop.
            sourceComponent: layoutPickerContentComp
            onLoaded: {
                if (layoutPickerLoader.item) {
                    layoutPickerLoader.item.layoutSelected.connect(modalSlotsRoot.shellRoot.layoutPickerSelected);
                    layoutPickerLoader.item.dismissRequested.connect(modalSlotsRoot.shellRoot.layoutPickerDismissRequested);
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
            decorationChain: layoutPickerSlot.decorationChain
            decorationOuterPadding: layoutPickerSlot.decorationOuterPadding
            audioSpectrum: layoutPickerSlot.audioSpectrum
        }
    }

    Item {
        id: cheatsheetSlot

        // Cheatsheet data properties — C++ writes these before each show
        // (and re-pushes on live mode/rebind refresh).
        property var shortcuts: []
        property string currentMode: "snapping"
        property bool autotileAvailable: true
        property bool scrollingAvailable: true
        property bool layoutsAvailable: true
        // Card corner radius the surface decoration rounds to (see osdSlot).
        property real cardCornerRadius: Kirigami.Units.largeSpacing * 2
        property string fontFamily: ""
        property real fontSizeScale: 1
        // Declared-but-unforwarded contract (same as zoneSelectorSlot):
        // fontWeight/fontItalic/fontUnderline/fontStrikeout are written by
        // C++ writeFontProperties but CheatsheetContent doesn't consume
        // them — the sheet keeps the theme's row weight/decoration on
        // purpose. The declarations MUST stay: deleting one silently
        // demotes the C++ write to a dead dynamic property.
        property int fontWeight: Font.Bold
        property bool fontItalic: false
        property bool fontUnderline: false
        property bool fontStrikeout: false
        // OSD-style content lifecycle gate. C++ toggles false→true around
        // each show so CheatsheetContent is re-instantiated.
        property bool loaded: false

        // Surface-shader decoration (Stage d) — same declare-and-forward
        // contract as the picker slot: C++ writes these with setProperty,
        // an undeclared name silently becomes a dead dynamic property.
        property var decorationChain: []
        property real decorationOuterPadding: 0
        property var audioSpectrum: []

        anchors.fill: parent
        opacity: 0
        visible: false

        Loader {
            id: cheatsheetLoader

            anchors.fill: parent
            active: cheatsheetSlot.loaded
            // SYNCHRONOUS by contract — see snapAssistLoader: beginShow
            // resolves the shaderAnchor in the same tick as the `loaded`
            // toggle; an async mount races the entrance animation.
            sourceComponent: cheatsheetContentComp
            onLoaded: {
                if (cheatsheetLoader.item)
                    cheatsheetLoader.item.dismissRequested.connect(modalSlotsRoot.shellRoot.cheatsheetDismissRequested);
            }
        }

        Component {
            id: cheatsheetContentComp

            CheatsheetContent {
                shortcuts: cheatsheetSlot.shortcuts
                currentMode: cheatsheetSlot.currentMode
                autotileAvailable: cheatsheetSlot.autotileAvailable
                scrollingAvailable: cheatsheetSlot.scrollingAvailable
                layoutsAvailable: cheatsheetSlot.layoutsAvailable
                fontFamily: cheatsheetSlot.fontFamily
                fontSizeScale: cheatsheetSlot.fontSizeScale
            }
        }

        // Surface-shader decoration (Stage d). SIBLING of cheatsheetLoader.
        // Captures the loaded content's PopupFrame shaderAnchor and
        // re-renders it through the resolved "popup.cheatsheet" surface
        // pack. Inert when the source is empty.
        SurfaceDecoration {
            anchors.fill: parent
            contentItem: cheatsheetLoader.item
            decorationChain: cheatsheetSlot.decorationChain
            decorationOuterPadding: cheatsheetSlot.decorationOuterPadding
            audioSpectrum: cheatsheetSlot.audioSpectrum
        }
    }
}
