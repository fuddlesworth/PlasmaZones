// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window

/**
 * Scroll tab-indicator shell — a per-screen wlr-layer-shell host that
 * carries nothing but the scrolling tab indicators.
 *
 * Every other kbd-None overlay shares one surface per screen (see
 * PassiveOverlayShell.qml, and the polishAndSync argument there for why
 * grouping is the default). The indicators are the deliberate exception,
 * because the compositor SLIDES this surface: the KWin effect adds the
 * scrolling strip's view offset to it on every frame of a scroll, so the
 * indicators travel with the columns they label instead of being hidden
 * for the length of every leg. A surface translates as a whole, so
 * anything sharing it would slide too — and the passive shell hosts the
 * navigation OSD, which fires on the very action that scrolls.
 *
 * Nothing here mirrors the compositor's motion. That was tried and cannot
 * be made to work: the daemon renders and commits this surface for KWin to
 * composite on a LATER frame, so a second spring on this side starts a
 * frame behind and falls further behind as the scene grows. The offset is
 * applied where the columns' own offset is applied, in the same paint
 * pass, from the same spring.
 *
 * C++ reaches the slot through the `scrollTabsSlotItem` alias and writes
 * its properties directly; SurfaceAnimator targets the same Item for the
 * show/hide legs.
 */
Window {
    id: root

    /// Scroll tab-strip slot Item — per-screen tab indicators for tabbed
    /// scrolling columns. Each tab is a click target that activates its
    /// window; the surface is click-through everywhere OUTSIDE the indicator
    /// rects, which is what the daemon's per-screen input region gives it.
    /// Content updates are plain property writes (no re-instantiation).
    readonly property alias scrollTabsSlotItem: scrollTabsSlot

    /// Forwarded from the tab indicator's `tabActivated` — the host wires
    /// this to onScrollTabActivated and focuses that window.
    signal scrollTabActivated(string windowId)

    // Input state is driven imperatively by C++ (syncScrollTabShellSurfaceState),
    // never by a QML flags binding — see PassiveOverlayShell.qml for why the
    // binding form races the C++ path.
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    // No explicit width/height: C++ commits the screen-sized warm-up geometry
    // before QML evaluates, and a binding here would overwrite it. Same
    // rationale as PassiveOverlayShell.qml, at length there.
    visible: false

    Item {
        id: scrollTabsSlot

        // Tab-indicator model — C++ writes a list of strip entries.
        // Each entry: {x, y, width, height (shell-window coordinates),
        // position (0 left, 1 right, 2 top, 3 bottom), tabs: [{windowId,
        // title, active, urgent, colors?}]}. `position` decides the whole
        // vertical/horizontal branch in the content item, and `windowId` is
        // what a tab click relays back.
        property var strips: []
        // Content lifecycle gate, toggled by C++ on show/hide. Unlike the
        // OSD-style slots the content is NOT re-instantiated per update —
        // strip changes are frequent (every relayout) and flow through the
        // `strips` binding.
        property bool loaded: false
        // User overlay font, pushed by C++ writeFontProperties (same
        // pipeline as every other slot).
        property string fontFamily: ""
        property real fontSizeScale: 1
        property int fontWeight: Font.Normal
        property bool fontItalic: false
        property bool fontUnderline: false
        property bool fontStrikeout: false
        // Tab-indicator PAINT settings (Scrolling.TabIndicator), pushed by C++
        // on every strip update. They must be declared here AND forwarded
        // below: setProperty on an undeclared name silently creates a dynamic
        // property that no binding ever sees, so the control looks wired and
        // does nothing (see the zoneSelectorSlot contract note in
        // PassiveOverlayShell.qml).
        property int tabStyle: 0
        property int gapsBetweenTabs: 0
        property int cornerRadius: -1
        property string activeColor: ""
        property string inactiveColor: ""
        property string urgentColor: ""

        anchors.fill: parent
        opacity: 0
        visible: false

        Loader {
            id: scrollTabsLoader

            anchors.fill: parent
            active: scrollTabsSlot.loaded
            // SYNCHRONOUS by contract: the daemon writes `loaded` and then
            // drives the show leg in the same call, so an async load would
            // animate an empty slot.
            sourceComponent: scrollTabsContentComp
            onLoaded: {
                if (scrollTabsLoader.item)
                    scrollTabsLoader.item.tabActivated.connect(root.scrollTabActivated);
            }
        }

        Component {
            id: scrollTabsContentComp

            ScrollTabStripContent {
                strips: scrollTabsSlot.strips
                fontFamily: scrollTabsSlot.fontFamily
                fontSizeScale: scrollTabsSlot.fontSizeScale
                fontWeight: scrollTabsSlot.fontWeight
                fontItalic: scrollTabsSlot.fontItalic
                fontUnderline: scrollTabsSlot.fontUnderline
                fontStrikeout: scrollTabsSlot.fontStrikeout
                style: scrollTabsSlot.tabStyle
                gapsBetweenTabs: scrollTabsSlot.gapsBetweenTabs
                cornerRadius: scrollTabsSlot.cornerRadius
                activeColor: scrollTabsSlot.activeColor
                inactiveColor: scrollTabsSlot.inactiveColor
                urgentColor: scrollTabsSlot.urgentColor
            }
        }
    }
}
