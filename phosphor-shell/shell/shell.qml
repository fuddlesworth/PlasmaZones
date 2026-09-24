// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// NO `pragma ComponentBehavior: Bound` here, and it must not be added back.
// Inline Components in this file are instantiated from C++ with a context
// that is not their declaring one: PerScreenPanels builds each BarHost with a
// fresh QQmlContext carrying `modelData`, and LayerPopoutTransport builds
// powerMenuComponent against the engine's root context. Bound rejects exactly
// that, with "Cannot instantiate bound component outside its creation context"
// — every bar and the power menu silently fail to create.

import Phosphor.Bar
import Phosphor.ControlCenter
import Phosphor.Dashboard
import Phosphor.Ipc
import Phosphor.Launcher
import Phosphor.Lock
import Phosphor.Notifications
import Phosphor.OSD
import Phosphor.Picker
import Phosphor.Polkit
import Phosphor.Popout
import Phosphor.Power
import Phosphor.Service.Mpris
import Phosphor.Service.UPower
import Phosphor.Shell
import Phosphor.Theme
import QtQuick
import org.phosphor.surface as PhosphorSurface

// Composition root for the floating bar, Navigator, Stage and service panels.
// BarRegistry mounts the built-in and extension widgets. PerScreenPanels
// gives ShellEngine ownership of one bar per output; cross-service actions
// remain here so the reusable surfaces need only their injected models.
Item {
    id: root

    // The session's reduced-motion preference, read by the host from the
    // settings portal (ShellMotion), drives the theme's motion switch:
    // release tails halve, enter loses its overshoot, the gleam stops.
    Binding {
        target: Motion
        property: "reducedMotion"
        value: ShellMotion.reducedMotion || !Appearance.settings.motion
    }

    // ShellChrome resolves the daemon's effective decoration tree, including
    // appearance defaults and user overrides, for each surface path. Revision
    // updates follow tree, palette and pack changes across engine reloads.
    Component {
        id: chromeDecoration

        PhosphorSurface.SurfaceDecoration {
            property string surfacePath: ""
            property bool focused: true

            decorationChain: ShellChrome.revision >= 0 && surfacePath !== "" ? ShellChrome.chainFor(surfacePath) : []
            decorationOuterPadding: ShellChrome.revision >= 0 && surfacePath !== "" ? ShellChrome.outerPaddingFor(surfacePath) : 0
            surfaceFocused: focused
            animationsPaused: !Appearance.motion
            // The chrome sits in transformed and clipped hosts (a settling
            // toast, a scaling dashboard), which an unlayered stage ignores.
            layeredStages: true
        }
    }
    Component.onCompleted: ShellChrome.decorationComponent = chromeDecoration

    // Touchpad gestures (A3, the per-surface Gesture rows). Only the
    // compositor sees them; the KWin effect reports each completed one
    // through the daemon and ShellGestures relays it here. Three fingers
    // are the launcher's, four the dashboard's.
    Connections {
        target: ShellGestures

        function onSwiped(direction: string, fingers: int): void {
            if (fingers === 3 && direction === "up" && !Popouts.isOpen("launcher"))
                root.toggleLauncher();
            else if (fingers === 3 && direction === "down")
                Popouts.close(Popouts.handleFor("launcher"));
            else if (fingers === 4 && direction === "up" && !Popouts.isOpen("dashboard"))
                root.toggleDashboard();
        }

        function onPinched(direction: string, fingers: int): void {
            if (fingers === 4 && direction === "expanding")
                Popouts.close(Popouts.handleFor("dashboard"));
        }
    }

    // The wallpaper, one surface per output, and the first of the
    // per-screen surfaces because it is the bottom layer: a Background
    // panel that draws what WallpaperService resolves for its output.
    // The shell owns the wallpaper, so the picker's hover preview and
    // its Apply both land here (through the service, a crossfade each).
    // The delegate reads only the PhosphorShell context property, for the
    // delegate-context reason the bar gives.
    PerScreenPanels {
        id: wallpapers

        model: PhosphorShell.screens

        delegate: WallpaperSurface {
            fallbackWallpaper: Component {
                DefaultWallpaper {}
            }
            service: PhosphorShell.wallpaper
        }
    }

    PerScreenPanels {
        id: bars

        model: PhosphorShell.screens

        // Quick settings uses a layer popup. The optional pane transport keeps
        // its bar-hosted fallback here, with ownership from its controller.
        delegate: BarHost {
            id: bar

            decoration: ShellChrome.decorationComponent
            paneAnchor: "controlcenter"
            paneWidth: Tokens.pane_width
            paneDepth: Tokens.pane_depth
            // Open only on the screen the registry says owns it, so a
            // multi-head setup shows one pane, on the bar that summoned it.
            //
            // Read through PanelWindow.screen, NOT modelData.screen:
            // PerScreenPanels deliberately withholds the screen role from
            // modelData because that map snapshots a raw QScreen* which
            // dangles on hot-unplug, while this property is QPointer-backed
            // and simply reads null once the output dies. Hence the guard.
            paneOpen: bar.screen ? ControlCenterRegistry.openScreen === bar.screen.name : false
            // The optional external pane uses a tether instead of inline content.
            paneExternal: ControlCenterRegistry.paneExternal

            // The bar is the only thing that can locate the pane (a
            // Wayland client is never told where its toplevel went), so it
            // reports the frame back for the pane's own top band.
            onOverviewRequested: root.toggleDashboard(bar.screen)
            onPaneScreenRectChanged: ControlCenterRegistry.reportPaneRect(bar.paneScreenRect)
            // And where the chip itself is, so the pane lands in the zone
            // nearest it (A2 §4.2).
            function reportChip(): void {
                if (bar.screen)
                    ControlCenterRegistry.reportChipRect(bar.screen.name, bar.paneAnchorRect);
            }
            onPaneAnchorRectChanged: reportChip()

            // The pane transport asks which mode this output runs before
            // opening; none means the floating fallback.
            readonly property int screenMode: bar.placementMap ? bar.placementMap.mode : -1
            function reportMode(): void {
                if (bar.screen)
                    ControlCenterRegistry.reportScreenMode(bar.screen.name, bar.screenMode);
            }
            onScreenModeChanged: reportMode()
            onScreenChanged: reportMode()

            // Keep the compositor's blur region aligned to the visible material.
            readonly property string blurMaterial: Appearance.settings.material
            onBlurMaterialChanged: applyBlur()
            readonly property var bandWindow: bar.bandItem ? bar.bandItem.Window.window : null
            readonly property real bandBlurRadius: bar.bandItem ? bar.bandItem.radius : 0
            readonly property real mapBlurRadius: Appearance.radius
            function applyBlur(): void {
                if (bar.bandWindow)
                    ShellEffects.setBlurBehind(bar.bandItem, Appearance.settings.material !== "solid" ? bar.bandRect : Qt.rect(0, 0, 0, 0), Appearance.settings.material !== "solid" ? bar.mapBlurRect : Qt.rect(0, 0, 0, 0), bar.bandBlurRadius, bar.mapBlurRadius);
            }
            onBandWindowChanged: applyBlur()
            onBandRectChanged: applyBlur()
            onMapBlurRectChanged: applyBlur()
            onBandBlurRadiusChanged: applyBlur()
            onMapBlurRadiusChanged: applyBlur()
            Component.onCompleted: {
                reportMode();
                reportChip();
                applyBlur();
            }

            // ONE CONTROL CENTER PER SCREEN for the fallback, built on that
            // screen's first open and kept: the pane Loader latches active,
            // and the tiles hold live service connections that would be
            // torn down and re-enumerated on every close.
            //
            // The pane depth is a constant on the bar, and the pane
            // CLIPS: a rail list taller than the depth is cut off, so
            // adding rails means raising paneDepth to match, since the
            // surface reservation cannot grow after materialization.
            paneContent: Component {
                QuickSettingsSurface {
                    decoration: ShellChrome.decorationComponent
                    onCloseRequested: Popouts.close(Popouts.handleFor("control-center"))
                }
            }
        }
    }

    // The OSD overlay, one per output. OSDHost places a band on any screen
    // edge (a volume band on the focused window's bottom edge, a
    // brightness band down the right), so it needs the WHOLE screen: a
    // Top panel whose thickness is the screen's height, on the Overlay
    // layer so it draws above the bar, with no exclusive zone (an overlay
    // cannot reserve one, and this one must not) and an EMPTY input
    // region, which is PanelWindow's click-through case. Nothing here is
    // ever clickable; the OSD is pure feedback.
    //
    // Same context rule as the bar above: the delegate reads OsdRegistry
    // (a context property) and never an id from this file. The host
    // attaches itself to the registry so the `osd` IpcTarget below can
    // reach every screen's host through it; OSDHost's own targetScreen
    // filter decides which one draws.
    PerScreenPanels {
        id: osdOverlays

        model: PhosphorShell.screens

        delegate: PanelWindow {
            id: osdSurface

            edge: PanelWindow.Top
            alignment: PanelWindow.Fill
            thickness: modelData.height
            panelLayer: PanelWindow.LayerOverlay
            exclusiveZoneEnabled: false
            keyboardFocus: PanelWindow.None
            inputRegion: []

            OSDHost {
                id: osdHost

                anchors.fill: parent
                provider: OsdRegistry
                edgeMargin: 36
                bottomInset: 54
                decoration: ShellChrome.decorationComponent
                // Through PanelWindow.screen, not modelData.screen, for the
                // hot-unplug reason the bar delegate gives.
                screenName: osdSurface.screen ? osdSurface.screen.name : ""
                topInset: Tokens.bar_thickness
                Component.onCompleted: OsdRegistry.attachHost(osdHost)
                Component.onDestruction: OsdRegistry.detachHost(osdHost)
            }
        }
    }

    NotificationSurfaces {
        locked: sessionCoordinator.lock.state !== 0
        onOpenCenterRequested: root.toggleWidgetPanel("notification", null)
    }

    AppearanceSurfaces {
        locked: sessionCoordinator.lock.state !== 0
    }

    // The layer popup and the optional pane transport share this content.
    Component {
        id: paneComponent

        QuickSettingsSurface {
            decoration: ShellChrome.decorationComponent
            onCloseRequested: Popouts.close(Popouts.handleFor("control-center"))
        }
    }

    SessionLockCoordinator {
        id: sessionCoordinator
    }

    SessionLockScreens {
        coordinator: sessionCoordinator
        notificationCount: NotificationRegistry.unreadCount
    }

    // The session menu, per docs/phosphor-shell-design/mockups-v2/power-menu.svg:
    // a screen-centred Modal popout over a dimmed scrim, which is what
    // PhosphorPopout.ExclusiveMode.Modal means to the controller. It closes
    // every cooperative popout and suppresses new ones while it is up.
    //
    // `session` is NOT bound here. LayerPopoutTransport builds this
    // component against the ENGINE'S ROOT CONTEXT, where `sessionCoordinator`
    // — an id belonging to this file's scope — does not resolve, so the
    // binding threw "ReferenceError: sessionCoordinator is not defined" on
    // every open and the menu ran with an undefined session. It is handed
    // in through the request's `props` instead (see togglePowerMenu), which
    // the transport applies with setInitialProperties inside its
    // beginCreate/completeCreate window. That path also warns on a name the
    // delegate does not declare, where a stray binding fails silently.
    //
    // The same constraint is why this file must never gain
    // `pragma ComponentBehavior: Bound` (see the header): inline
    // Components here are instantiated from C++ against a foreign context.
    Component {
        id: powerMenuComponent

        PowerMenu {}
    }

    function openQuickSettingsPanel(panelId: string): void {
        const source = root._lastPanelSource;
        const screenName = root.controlCenterScreen || ControlCenterRegistry.screenOf(source)?.name || "";
        Popouts.close(Popouts.handleFor("control-center"));
        if (panelId === "appearance")
            PickerRegistry.show(screenName);
        else
            root.toggleWidgetPanel(panelId, source);
    }

    Connections {
        target: ControlCenterRegistry
        function onPanelRequested(panelId: string): void {
            root.openQuickSettingsPanel(panelId);
        }
        function onControlCenterRequested(panelId: string): void {
            const handle = Popouts.handleFor("bar.panel." + panelId);
            if (handle !== "")
                Popouts.close(handle);
            root.toggleControlCenter(root._lastPanelSource);
        }
    }

    Connections {
        target: PickerRegistry
        function onOpeningFailed(error: string): void {
            NotificationRegistry.send(qsTr("Could not open Appearance"), error);
        }
    }

    // Track the actual layer popup, independently of the optional pane route.
    property string controlCenterScreen: ""
    property string controlCenterHandle: ""

    // Quick settings and the launcher share a Cooperative scope. Opening one
    // closes the other; a Modal surface closes both and prevents new opens.
    function toggleControlCenter(source: Item): void {
        // screenOf hands back a QScreen the C++ side owns; the controller
        // marks it CppOwnership before returning, so the JS GC cannot
        // delete the live screen when this wrapper is collected. Do not
        // reach for a QScreen any other way from QML.
        root._lastPanelSource = source;
        const target = ControlCenterRegistry.screenOf(source);
        const centre = BarRegistry.anchorCenterFor(source);
        const anchored = centre >= 0;
        const railT = anchored && target && target.geometry.width > 0 ? Math.max(0, Math.min(1, centre / target.geometry.width)) : 0.5;
        const request = {
            "popoutId": "control-center",
            "content": paneComponent,
            "targetScreen": target,
            "anchor": PhosphorPopout.Anchor.BarRight,
            "customAnchor": Qt.point(anchored ? centre : 0, 0),
            "exclusive": PhosphorPopout.ExclusiveMode.Cooperative,
            "dismissOnFocusLoss": true,
            "keyboardFocus": true,
            "exclusiveKeyboard": true,
            "props": {
                "railT": railT,
                "panelWidth": Appearance.panelWidth
            }
        };
        // Repeated activation toggles on one output and transfers across outputs.
        const previous = Popouts.handleFor("control-center");
        if (previous !== "") {
            const move = target && root.controlCenterScreen !== target.name;
            Popouts.close(previous);
            if (!move)
                return;
        }
        const handle = Popouts.open(request);
        if (handle !== "") {
            root.controlCenterHandle = handle;
            root.controlCenterScreen = target ? target.name : "";
        }
    }

    // `source` is the bar button that summoned it, or null for a keybind:
    // the column lands on the button's side of the screen, so the pointer
    // that pressed it does not cross the whole screen to the words.
    function togglePowerMenu(source: Item): void {
        let alignRight = false;
        if (source && source.width > 0) {
            // The bar spans its screen from x 0, so window coordinates are
            // screen coordinates. The screen comes from the controller,
            // which marks it C++-owned: reading a QScreen through a
            // window's attached property hands it to the JS collector.
            const p = source.mapToItem(null, 0, 0);
            const screen = ControlCenterRegistry.screenOf(source);
            // A QScreen carries `geometry`, not a bare `width`.
            const screenWidth = screen ? screen.geometry.width : Screen.width;
            alignRight = p.x + source.width / 2 > screenWidth / 2;
        }
        // Nothing to do about the control center here: it is a Cooperative
        // popout on the same controller, so opening this Modal one closes
        // it through the controller's own arbitration.
        // toggle rather than open: pressing the bar button (or Ctrl+Alt+Del)
        // a second time should put the menu away, and the controller rejects
        // a plain open for an id that is already showing.
        // Anchor and ExclusiveMode live on the PhosphorPopout namespace
        // element, so they are reached through it. QML_ELEMENT on a
        // Q_NAMESPACE publishes the namespace itself as the type name;
        // it does not put the enums in scope unqualified.
        Popouts.toggle({
            "popoutId": "power",
            "content": powerMenuComponent,
            // The session the menu acts on, passed as a prop rather than
            // bound in the Component: ids from this file do not resolve in
            // the root context the transport builds the delegate against.
            "props": {
                "session": sessionCoordinator.session,
                "alignRight": alignRight
            },
            "anchor": PhosphorPopout.Anchor.ScreenCenter,
            "exclusive": PhosphorPopout.ExclusiveMode.Modal,
            "keyboardFocus": true,
            "dismissOnFocusLoss": true
        });
    }

    // The status chips' panels, keyed by the bar-widget id BarController
    // registers (barcontroller.cpp), NOT by the IPC target names below.
    //
    // One map rather than a chain of else-ifs, because every entry opens
    // the same shape in the same way: a Cooperative popout hanging under
    // the chip that was pressed. A widget missing from this map simply has
    // no panel, which is how focusedapp stays inert
    // without needing a case of their own.
    //
    // `keyboard` is per panel and not a shared default, because the two
    // answers are both wrong for the other case. A panel that takes the
    // keyboard pulls focus off whatever the user was typing in, which is
    // unacceptable for a volume slider or a device list. But the network
    // panel has a passphrase field, and a layer surface that was never
    // granted keyboard focus cannot receive a keystroke at all — the field
    // would look editable and silently swallow everything typed into it.
    readonly property var widgetPanels: ({
            "tray": {
                "component": trayPanelComponent,
                "keyboard": true
            },
            "systemmetrics": {
                "component": statsPanelComponent,
                "keyboard": true
            },
            "network": {
                "component": networkPanelComponent,
                "keyboard": true
            },
            "bluetooth": {
                "component": bluetoothPanelComponent,
                "keyboard": true
            },
            "audio": {
                "component": audioPanelComponent,
                "keyboard": true
            },
            "battery": {
                "component": batteryPanelComponent,
                "keyboard": false
            },
            "media": {
                "component": mediaPanelComponent,
                "keyboard": false
            },
            "notification": {
                "component": notificationPanelComponent,
                "keyboard": true
            },
            "clock": {
                "component": calendarPanelComponent,
                "keyboard": true
            }
        })

    Component {
        id: networkPanelComponent

        NetworkPanel {
            onBackRequested: {
                Popouts.close(Popouts.handleFor("bar.panel.network"));
                ControlCenterRegistry.requestControlCenter("network");
            }
            onCloseRequested: Popouts.close(Popouts.handleFor("bar.panel.network"))
        }
    }

    Component {
        id: bluetoothPanelComponent

        BluetoothPanel {
            onBackRequested: {
                Popouts.close(Popouts.handleFor("bar.panel.bluetooth"));
                ControlCenterRegistry.requestControlCenter("bluetooth");
            }
            onCloseRequested: Popouts.close(Popouts.handleFor("bar.panel.bluetooth"))
        }
    }

    Component {
        id: audioPanelComponent

        AudioPanel {
            onBackRequested: {
                Popouts.close(Popouts.handleFor("bar.panel.audio"));
                ControlCenterRegistry.requestControlCenter("audio");
            }
            onCloseRequested: Popouts.close(Popouts.handleFor("bar.panel.audio"))
        }
    }

    Component {
        id: batteryPanelComponent

        BatteryPanel {}
    }

    Component {
        id: mediaPanelComponent

        MediaPanel {}
    }

    Component {
        id: notificationPanelComponent

        NotificationPanel {
            onCloseRequested: Popouts.close(Popouts.handleFor("bar.panel.notification"))
        }
    }

    Component {
        id: statsPanelComponent
        StatsPanel {
            onCloseRequested: Popouts.close(Popouts.handleFor("bar.panel.systemmetrics"))
            onNetworkSettingsRequested: {
                Popouts.close(Popouts.handleFor("bar.panel.systemmetrics"));
                ControlCenterRegistry.requestControlCenter("systemmetrics");
            }
        }
    }

    Component {
        id: trayPanelComponent
        TrayPanel {
            onCloseRequested: Popouts.close(Popouts.handleFor("bar.panel.tray"))
        }
    }

    Component {
        id: calendarPanelComponent

        CalendarPanel {
            onCloseRequested: Popouts.close(Popouts.handleFor("bar.panel.clock"))
        }
    }

    // Open (or close) the panel belonging to bar widget `id`, hanging under
    // the chip that fired.
    //
    // The popout id is prefixed so each chip's panel is its own logical
    // popout: with one shared id, pressing network while bluetooth was open
    // would TOGGLE the open one shut and never open the one that was asked
    // for. Distinct ids plus the shared Cooperative scope give the wanted
    // behaviour instead — the previous panel closes, this one opens.
    function toggleWidgetPanel(id: string, source: Item): void {
        const panel = root.widgetPanels[id];
        if (!panel)
            return;
        root._lastPanelSource = source;
        // BarItem needs the chip's centre in its screen's pixels.
        // anchorCenterFor returns -1 when it cannot resolve one (a widget
        // with no window yet), which is NOT a coordinate: fall back to the
        // bar-centre anchor rather than pinning the panel to the left edge.
        const anchorSource = id === "tray" && source?.requestAnchor ? source.requestAnchor : source;
        const centre = BarRegistry.anchorCenterFor(anchorSource);
        const anchored = centre >= 0;
        // Where the panel sits along the screen, 0..1. This is what binds
        // the panel's stroke and top band to the one screen-wide gradient
        // rather than giving each panel a private colour: a chip on the
        // left opens a cyan-leaning surface, one on the right a
        // rose-leaning one. Falls back to centre when the chip's position
        // could not be resolved.
        const screen = BarRegistry.screenOf(source);
        const railT = anchored && screen && screen.geometry.width > 0 ? Math.max(0, Math.min(1, centre / screen.geometry.width)) : 0.5;
        const directMenu = id === "tray" ? source?.requestedMenuKey || "" : "";
        if (directMenu && Popouts.isOpen("bar.panel.tray"))
            Popouts.close(Popouts.handleFor("bar.panel.tray"));
        if (directMenu) {
            const entry = TrayModel.lookup(directMenu);
            if (!entry.menuPath || entry.menuPath === "/") {
                const point = anchorSource.mapToGlobal(anchorSource.width / 2, anchorSource.height / 2);
                source.requestedMenuKey = "";
                Qt.callLater(() => TrayModel.context(entry, point));
                return;
            }
        }
        const rightAligned = id === "systemmetrics" || id === "tray";
        Popouts.toggle({
            "popoutId": "bar.panel." + id,
            "content": panel.component,
            // screenOf hands back a QScreen the C++ side owns; the
            // controller marks it CppOwnership before returning, so the JS
            // GC cannot delete the live screen when this wrapper is
            // collected. Do not reach for a QScreen any other way from QML.
            "targetScreen": screen,
            "anchor": id === "notification" ? PhosphorPopout.Anchor.BarRight : anchored ? (rightAligned ? PhosphorPopout.Anchor.BarItemRight : PhosphorPopout.Anchor.BarItem) : PhosphorPopout.Anchor.BarCenter,
            "customAnchor": Qt.point(anchored ? centre + (rightAligned ? anchorSource.width / 2 : 0) : 0, 0),
            "exclusive": PhosphorPopout.ExclusiveMode.Cooperative,
            // Per panel; see widgetPanels above for why this is not one
            // shared value.
            "keyboardFocus": panel.keyboard,
            "exclusiveKeyboard": panel.keyboard,
            // Transients close on outside click or focus loss (A2 §4.7).
            // That is the line between this class and a pane: a pane is a
            // tile and does not vanish when you look elsewhere, and these
            // are glances.
            "dismissOnFocusLoss": true,
            "props": id === "tray" ? {
                "railT": railT,
                "sourceWidget": source,
                "initialMenuKey": directMenu
            } : {
                "railT": railT
            }
        });
        if (id === "tray" && source)
            source.requestedMenuKey = "";
    }

    // The bar cannot see a transient — it is a layer surface the shell
    // composes — so it is told which chip owns the open one. That drives
    // the chip's lit state and the tether down to the surface (A2 §4.3).
    // Both edges are needed: `popoutClosed` fires however the popout went,
    // including an outside click the shell never hears about otherwise.
    Connections {
        target: Popouts

        function onPopoutOpened(popoutId: string, handle: string): void {
            if (popoutId === "control-center")
                BarRegistry.setOpenPanel("controlcenter", root._lastPanelSource);
            else if (popoutId.startsWith("bar.panel."))
                BarRegistry.setOpenPanel(popoutId.substring("bar.panel.".length), root._lastPanelSource);
        }

        function onPopoutClosed(popoutId: string, handle: string): void {
            if (popoutId === "control-center" && handle === root.controlCenterHandle) {
                root.controlCenterHandle = "";
                root.controlCenterScreen = "";
            }
            if (popoutId === "control-center" || popoutId.startsWith("bar.panel."))
                BarRegistry.setOpenPanel("", null);
        }
    }

    // The chip the in-flight open was summoned from. Set immediately before
    // the request goes out, read by the handler above: PopoutController's
    // signal carries the id and the handle but not the source item.
    property Item _lastPanelSource: null

    Connections {
        target: BarRegistry

        // `source` is the bar widget that fired. The session menu is
        // screen-centred and ignores it; the control center is anchored to
        // a bar, so it uses the widget's window to pick which output's
        // capsule to grow out of; a status chip's panel hangs under the
        // chip itself.
        function onWidgetActivated(id: string, source: Item): void {
            if (id !== "placementmap")
                Popouts.close(Popouts.handleFor("dashboard"));
            // A Cooperative open is refused outright while a Modal popout is
            // up, and the refusal is silent: the user would press the button
            // and see nothing happen, with nothing logged. The power menu is
            // itself Modal and toggles, so it stays reachable.
            if (Popouts.modalActive && id !== "power")
                return;
            if (id === "placementmap" && source)
                source.expandRequested(false);
            else if (id === "power")
                root.togglePowerMenu(source);
            else if (id === "appearance")
                PickerRegistry.toggle(ControlCenterRegistry.screenOf(source)?.name || "");
            else if (id === "launcher")
                root.toggleLauncher();
            else if (id === "controlcenter" || id === "media")
                // "controlcenter" is the bar widget's registered id
                // (barcontroller.cpp), not the IPC target name below.
                root.toggleControlCenter(source);
            else
                // Date and service panels open from their own bar control.
                root.toggleWidgetPanel(id, source);
        }
    }

    // Typed commands also drive the nested harness through its private socket.
    IpcTarget {
        target: "appearance"
        function show(): bool {
            return PickerRegistry.show();
        }
        function presentation(name: string): bool {
            return AppearanceStore.setValue("presentation", name);
        }

        function preset(name: string): bool {
            return AppearanceStore.applyPreset(name);
        }
        function font(kind: string, family: string): bool {
            if (kind !== "uiFont" && kind !== "monoFont")
                return false;
            return AppearanceStore.setValue(kind, family);
        }
        function widget(id: string, region: string, index: int): bool {
            return AppearanceStore.moveWidget(id, region, index);
        }
        function visualizer(name: string): bool {
            return AppearanceStore.setValue("visualizer", name);
        }
        function motion(enabled: bool): bool {
            return AppearanceStore.setValue("motion", enabled);
        }
    }

    IpcTarget {
        target: "bar"

        function activate(id: string): bool {
            return BarRegistry.activateWidget(id);
        }
    }

    IpcTarget {
        target: "power"

        function show(): void {
            if (!Popouts.isOpen("power"))
                root.togglePowerMenu(null);
        }

        function toggle(): void {
            root.togglePowerMenu(null);
        }
    }

    // The control center's wire surface, per the mockup's
    // `phosphorctl call control-center.show`. Same show/toggle split as the
    // power menu, and the same reason: bind a compositor key to toggle, and
    // call show from a script that wants the panel up regardless.
    //
    // All three are argument-free, which is the form a keybind uses:
    // IpcTarget arity is strict, so a `toggle(screen)` would reject a bare
    // `phosphorctl call control-center.toggle` with "argument count
    // mismatch". A null source resolves to the primary output's bar.
    IpcTarget {
        target: "control-center"

        function show(): void {
            if (!Popouts.isOpen("control-center"))
                root.toggleControlCenter(null);
        }

        function toggle(): void {
            root.toggleControlCenter(null);
        }

        function hide(): void {
            // close() on an empty handle is a no-op, so this is safe when
            // nothing is open.
            Popouts.close(Popouts.handleFor("control-center"));
        }
    }

    // Launcher placement is independent of the workspace overview style.
    Component {
        id: launcherComponent

        Launcher {
            results: LauncherResults
            catalog: LauncherCatalog
            decoration: ShellChrome.decorationComponent
            // The viewfinder: this output's placement map, keyed by the
            // output the transport's layer window landed on.
            map: Screen.name ? PlacementMap.forScreen(Screen.name) : null
            // Built fresh on every open by the transport, so the reset that
            // clears the query and takes focus belongs here; the providers
            // behind the model are process-global and keep their state.
            Component.onCompleted: reset()
            onActivated: Popouts.close(Popouts.handleFor("launcher"))
            onDismissed: Popouts.close(Popouts.handleFor("launcher"))
        }
    }

    function toggleLauncher(): void {
        Popouts.toggle({
            "popoutId": "launcher",
            "content": launcherComponent,
            "anchor": PhosphorPopout.Anchor.ScreenCenter,
            "exclusive": PhosphorPopout.ExclusiveMode.Cooperative,
            "keyboardFocus": true,
            "exclusiveKeyboard": true,
            "dismissOnFocusLoss": true
        });
    }

    // The OSD's wire surface, per the OSD demo:
    // `phosphorctl call osd.show --arg kind=volume --arg value=62`. This is
    // how a compositor keybind for a volume or brightness key reaches the
    // band. Broadcast to every screen (the demo's "" target), which is what
    // a hardware key means. For the stateful kinds (mic, caps) a value of 0
    // reads as off and non-zero as on; the value-based kinds ignore that.
    // Returns false for a kind no factory serves, so a typo reports failure
    // over the wire rather than vanishing.
    IpcTarget {
        target: "osd"

        function show(kind: string, value: int): bool {
            return OsdRegistry.show(kind, value, "");
        }
    }

    // The launcher's wire surface, per the mockup's
    // `phosphorctl call launcher.toggle`. Argument-free, the form a
    // compositor keybind uses (Meta / Alt+Space bound to
    // `phosphorctl call launcher.toggle`).
    IpcTarget {
        target: "launcher"

        function show(): void {
            if (!Popouts.isOpen("launcher"))
                root.toggleLauncher();
        }

        function toggle(): void {
            root.toggleLauncher();
        }

        function hide(): void {
            Popouts.close(Popouts.handleFor("launcher"));
        }
    }

    // The lock's wire surface: `phosphorctl call lock.lock`, the form a
    // compositor keybind (Super+L) uses. Argument-free like the others.
    // Only lock, never unlock: the way out of a lock is the password, and
    // a wire call that unlocked a session would be a hole in it.
    // lock() is a no-op while already locked (A3 §6 d: Super+L re-locks
    // to nothing), through LockService's own guard.
    IpcTarget {
        target: "lock"

        function lock(): void {
            sessionCoordinator.lock.lock();
        }
    }

    // The picker's wire surface: `phosphorctl call picker.toggle`. An
    // empty screen means the focused output; a keybind uses the
    // argument-free form. hide() puts the previous palette back, the same
    // as Escape.
    IpcTarget {
        target: "picker"

        function show(): bool {
            return PickerRegistry.show("");
        }

        function toggle(): bool {
            return PickerRegistry.toggle("");
        }

        function hide(): void {
            PickerRegistry.hide();
        }
    }

    // Each modal belongs to the request captured in its initial properties.
    // Closing an older popup must not cancel a newer PolicyKit conversation.
    property string polkitHandle: ""
    property var polkitRequest: null

    Connections {
        target: Popouts
        function onPopoutClosed(popoutId: string, handle: string): void {
            if (popoutId !== "polkit" || handle !== root.polkitHandle)
                return;
            const request = root.polkitRequest;
            root.polkitHandle = "";
            root.polkitRequest = null;
            // Also covers a lost output or failed surface before the card exists.
            if (request && PolkitRegistry.activeRequest === request)
                PolkitRegistry.cancel();
        }
    }

    Component {
        id: polkitComponent
        PolkitSurface {
            agent: PolkitRegistry
            decoration: ShellChrome.decorationComponent
            surfaceEffects: ShellEffects
            keyboard: LockKeyboard {}
            errorText: PolkitRegistry.lastError
            Component.onDestruction: {
                if (request && PolkitRegistry.activeRequest === request)
                    PolkitRegistry.cancel();
            }
        }
    }

    // Prefer the requesting window's output; the card itself is centered.
    function openPolkitPrompt(): void {
        const request = PolkitRegistry.activeRequest;
        const pid = PolkitRegistry.requesterPid;
        let windowId = "";
        if (pid > 0 && typeof WindowTracking !== "undefined" && WindowTracking && typeof WindowTracking.findWindowByPid === "function") {
            const found = WindowTracking.findWindowByPid(pid);
            windowId = found === undefined || found === null ? "" : String(found);
        }
        let screenName = "";
        if (windowId !== "") {
            const names = PolkitRegistry.screenNames();
            for (let i = 0; i < names.length; ++i) {
                const map = PlacementMap.forScreen(names[i]);
                if (!map)
                    continue;
                const r = map.cellRect(windowId);
                if (r && r.width > 0 && r.height > 0) {
                    screenName = names[i];
                    break;
                }
            }
        }
        // screenNamed marks the QScreen CppOwnership before returning it,
        // the same guard ControlCenterRegistry.screenOf relies on.
        const screen = PolkitRegistry.screenNamed(screenName);
        if (!screen) {
            PolkitRegistry.cancel();
            return;
        }
        PolkitRegistry.setPlacement(screen.name, Qt.rect(0, 0, 0, 0));
        const handle = Popouts.open({
            "popoutId": "polkit",
            "content": polkitComponent,
            "targetScreen": screen,
            "anchor": PhosphorPopout.Anchor.ScreenCenter,
            "exclusive": PhosphorPopout.ExclusiveMode.Modal,
            "keyboardFocus": true,
            "exclusiveKeyboard": true,
            "dismissOnFocusLoss": false,
            "props": {
                "request": request,
                "requester": PolkitRegistry.requesterName,
                "requesterProgram": PolkitRegistry.requesterProgram,
                "resourceText": PolkitRegistry.requesterResource
            }
        });
        if (handle === "") {
            if (PolkitRegistry.activeRequest === request)
                PolkitRegistry.cancel();
        } else {
            root.polkitHandle = handle;
            root.polkitRequest = request;
        }
    }

    Connections {
        target: sessionCoordinator.lock
        function onStateChanged(): void {
            if (sessionCoordinator.lock.state !== 0)
                PolkitRegistry.cancel();
        }
    }

    Connections {
        target: PolkitRegistry

        function onActiveRequestChanged(): void {
            if (PolkitRegistry.activeRequest) {
                if (sessionCoordinator.lock.state === 0)
                    root.openPolkitPrompt();
                else
                    PolkitRegistry.cancel();
            } else
                // close() on an empty handle is a no-op.
                Popouts.close(Popouts.handleFor("polkit"));
        }
    }

    // Stage owns one output while its modal popout is open. Release the
    // native transform before the closing surface leaves the controller.
    Component {
        id: dashboardComponent

        StageOverview {
            implicitWidth: Screen.width
            implicitHeight: Screen.height
            screenName: Screen.name
            workspaces: Workspaces
            surfaceEffects: ShellEffects
            mapFor: index => Screen.name ? PlacementMap.forScreenDesktop(Screen.name, index) : null
            open: true
            onCloseRequested: open = false
            onReleased: Popouts.close(Popouts.handleFor("dashboard"))
        }
    }

    function toggleDashboard(target = null): void {
        const request = {
            "popoutId": "dashboard",
            "content": dashboardComponent,
            "anchor": PhosphorPopout.Anchor.ScreenCenter,
            "exclusive": PhosphorPopout.ExclusiveMode.Modal,
            "keyboardFocus": true,
            "dismissOnFocusLoss": true
        };
        if (target)
            request.targetScreen = target;
        Popouts.toggle(request);
    }

    // The dashboard's wire surface: `phosphorctl call dashboard.toggle`
    // is what a compositor keybind (or the four-finger swipe's handler)
    // binds. Same show/toggle/hide split as the launcher.
    IpcTarget {
        target: "dashboard"

        function show(): void {
            if (!Popouts.isOpen("dashboard"))
                root.toggleDashboard();
        }

        function toggle(): void {
            root.toggleDashboard();
        }

        function hide(): void {
            Popouts.close(Popouts.handleFor("dashboard"));
        }
    }

    CheatsheetSurface {
        locked: sessionCoordinator.lock.state !== 0
    }
}
