// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// NO `pragma ComponentBehavior: Bound` here, and it must not be added back.
// Both inline Components in this file are instantiated from C++ with a context
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
import org.plasmazones.common as PZCommon

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

    // Surface packs on the chrome (A1 §2.4): the one decoration host every
    // surface instantiates in its DecorationSlot. ShellChrome resolves the
    // chain for the slot's surface path from the same decoration tree the
    // Decoration pages edit; `revision` is what re-resolves it on a tree,
    // palette or pack change. Handed to ShellChrome rather than referenced
    // by id, because a per-screen delegate cannot see an id in this file.
    Component {
        id: chromeDecoration

        PZCommon.SurfaceDecoration {
            property string surfacePath: ""
            property bool focused: true

            decorationChain: Appearance.surfacePacks && ShellChrome.revision >= 0 && surfacePath !== "" ? ShellChrome.chainFor(surfacePath) : []
            decorationOuterPadding: ShellChrome.revision >= 0 && surfacePath !== "" ? ShellChrome.outerPaddingFor(surfacePath) : 0
            surfaceFocused: focused
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

        // The control center is tethered to THIS bar's chip (05 §8). Phase
        // 2 opens it as an ENGINE-PLACED PANE, a real toplevel the daemon
        // positions (A2 §4), and the bar draws only the tether to it; the
        // phase-1 pane painted inside the bar's surface stays as the
        // floating fallback for an output with no placement engine.
        //
        // Everything the delegate reads must come from a CONTEXT PROPERTY,
        // never an id in this file: PerScreenPanels builds each delegate
        // with a fresh QQmlContext carrying `modelData`, so shell.qml's ids
        // do not resolve inside one. Hence the open state lives on
        // ControlCenterRegistry and the fallback pane content is declared
        // inline here, in the delegate's own scope (the engine-placed
        // pane's content is `paneComponent` below, built by the transport
        // against the root context).
        delegate: BarHost {
            id: bar

            decoration: ShellChrome.decorationComponent
            paneAnchor: "controlcenter"
            paneWidth: 380
            paneDepth: 460
            // Open only on the screen the registry says owns it, so a
            // multi-head setup shows one pane, on the bar that summoned it.
            //
            // Read through PanelWindow.screen, NOT modelData.screen:
            // PerScreenPanels deliberately withholds the screen role from
            // modelData because that map snapshots a raw QScreen* which
            // dangles on hot-unplug, while this property is QPointer-backed
            // and simply reads null once the output dies. Hence the guard.
            paneOpen: bar.screen ? ControlCenterRegistry.openScreen === bar.screen.name : false
            // External: the open pane is a toplevel, so no inline pane,
            // only the tether routed to where the placement map says it is.
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
            function applyBlur(): void {
                if (bar.bandWindow)
                    ShellEffects.setBlurBehind(bar.bandItem, Appearance.settings.material !== "solid" ? bar.bandRect : Qt.rect(0, 0, 0, 0), Appearance.settings.material !== "solid" ? bar.mapBlurRect : Qt.rect(0, 0, 0, 0), Appearance.radius);
            }
            onBandWindowChanged: applyBlur()
            onBandRectChanged: applyBlur()
            onMapBlurRectChanged: applyBlur()
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
                ControlCenter {
                    MprisHost {
                        id: controlsMpris
                    }
                    mediaPlayer: controlsMpris.playerCount > 0 ? controlsMpris.playerAt(0) : null
                    UPowerHost {
                        id: controlsPower
                    }
                    focusEnabled: NotificationRegistry.doNotDisturb
                    focusAvailable: NotificationRegistry.serverActive
                    nightLightEnabled: QuickSettings.nightLightEnabled
                    nightLightAvailable: QuickSettings.nightLightAvailable
                    batterySummary: controlsPower.displayDevice && controlsPower.displayDevice.isPresent ? Math.round(controlsPower.displayDevice.percentage) + "%" : ""
                    powerSummary: QuickSettings.powerProfile === "balanced" ? qsTr("Balanced power") : QuickSettings.powerProfile === "power-saver" ? qsTr("Power saver") : QuickSettings.powerProfile === "performance" ? qsTr("Performance") : ""
                    notificationSummary: NotificationRegistry.unreadCount ? qsTr("%1 unread notifications").arg(NotificationRegistry.unreadCount) : qsTr("No pending notifications")
                    onFocusToggled: NotificationRegistry.doNotDisturb = !NotificationRegistry.doNotDisturb
                    onNightLightToggled: QuickSettings.toggleNightLight()
                    onCloseRequested: Popouts.close(Popouts.handleFor("control-center"))
                    provider: ControlCenterRegistry
                    tileIds: ControlCenterRegistry.tileIds.filter(id => id !== "idle")
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

    // The toast overlay, one per output, on its own surface rather than
    // the OSD's because the two want different input: OSD bands never
    // take a click, while a toast card needs hover (which pauses its
    // timer) and a close button. Same full-screen Overlay panel, but the
    // input region follows the cards: ToastHost publishes their rects and
    // PanelWindow opens input over exactly those, so everything around
    // them stays click-through and an empty stack passes every click to
    // the window beneath.
    PerScreenPanels {
        id: toastOverlays

        model: PhosphorShell.screens

        delegate: PanelWindow {
            id: toastSurface

            edge: PanelWindow.Top
            alignment: PanelWindow.Fill
            thickness: modelData.height
            panelLayer: PanelWindow.LayerOverlay
            exclusiveZoneEnabled: false
            keyboardFocus: PanelWindow.None
            inputRegion: toastHost.inputRects

            ToastHost {
                id: toastHost

                anchors.fill: parent
                screenName: toastSurface.screen ? toastSurface.screen.name : ""
                decoration: ShellChrome.decorationComponent
                // The `notify` IpcTarget below lands on the primary
                // output's host, which is why the flag travels with the
                // attachment.
                Component.onCompleted: ToastRegistry.attachHost(toastHost, toastHost.screenName, modelData.isPrimary)
                Component.onDestruction: ToastRegistry.detachHost(toastHost)
            }
        }
    }

    // The wallpaper and theme picker, one strip per output (A3 §8): a
    // 96 px Bottom panel on the Overlay layer with no exclusive zone, so
    // it lies over the windows it covers and reserves nothing. Closed, its
    // input region is empty and it is click-through; open, the region is
    // the whole strip. Keyboard is OnDemand, the only interactivity a
    // panel can change nothing about after creation: the compositor gives
    // the strip focus on the first click into it, from which point Escape
    // returns and Enter applies. Which output's strip is open lives on
    // PickerRegistry (a context property, for the delegate-context reason
    // the bar gives); the strip restores the palette and the wallpaper
    // preview (on the `wallpapers` surfaces above) when it closes.
    PerScreenPanels {
        id: pickerStrips

        model: PhosphorShell.screens

        delegate: PanelWindow {
            id: pickerSurface

            edge: PanelWindow.Bottom
            alignment: PanelWindow.Fill
            thickness: 96
            panelLayer: PanelWindow.LayerOverlay
            exclusiveZoneEnabled: false
            keyboardFocus: PanelWindow.OnDemand
            inputRegion: pickerStrip.open ? [Qt.rect(0, 0, pickerSurface.width, pickerSurface.height)] : []

            Picker {
                id: pickerStrip

                anchors.fill: parent
                decoration: ShellChrome.decorationComponent
                screenName: pickerSurface.screen ? pickerSurface.screen.name : ""
                wallpaper: PhosphorShell.wallpaper
                targetCount: PickerRegistry.targetCount
                open: screenName !== "" && PickerRegistry.openScreen === screenName
                opacity: open ? 1 : 0
                visible: opacity > 0
                onClosed: PickerRegistry.hide()

                Behavior on opacity {
                    NumberAnimation {
                        duration: pickerStrip.open ? Motion.duration_enter_content : Motion.duration_release_long
                        easing: pickerStrip.open ? Motion.reveal : Motion.release
                    }
                }
            }
        }
    }

    // The polkit dim, one per output (A3 §9c): while a request is open the
    // screen it was placed on dims 20%, with the requester's window left
    // clear. Pure feedback on a full-screen click-through Overlay panel,
    // like the OSD's. The card itself is a popout (below), because a
    // panel cannot take keyboard focus on demand after creation and the
    // prompt must land focus in its field the moment it appears.
    PerScreenPanels {
        id: polkitDims

        model: PhosphorShell.screens

        delegate: PanelWindow {
            id: polkitDimSurface

            edge: PanelWindow.Top
            alignment: PanelWindow.Fill
            thickness: modelData.height
            panelLayer: PanelWindow.LayerOverlay
            exclusiveZoneEnabled: false
            keyboardFocus: PanelWindow.None
            inputRegion: []

            PolkitDim {
                anchors.fill: parent
                readonly property string screenName: polkitDimSurface.screen ? polkitDimSurface.screen.name : ""
                active: PolkitRegistry.activeRequest !== null && screenName !== "" && PolkitRegistry.promptScreen === screenName
                hole: active && PolkitRegistry.anchorRect.width > 0 ? PolkitRegistry.anchorRect : null
            }
        }
    }

    // The engine-placed pane's content, built FRESH on every open by
    // PanePopoutTransport against the engine's root context (so only
    // context properties, never ids from this file), and destroyed with
    // the toplevel on close. Same declaration as the fallback above; the
    // two cannot share one Component because the delegate's is in a
    // PerScreenPanels context the transport cannot reach.
    Component {
        id: paneComponent

        ControlCenter {
            MprisHost {
                id: controlsMpris
            }
            mediaPlayer: controlsMpris.playerCount > 0 ? controlsMpris.playerAt(0) : null
            UPowerHost {
                id: controlsPower
            }
            focusEnabled: NotificationRegistry.doNotDisturb
            focusAvailable: NotificationRegistry.serverActive
            nightLightEnabled: QuickSettings.nightLightEnabled
            nightLightAvailable: QuickSettings.nightLightAvailable
            batterySummary: controlsPower.displayDevice && controlsPower.displayDevice.isPresent ? Math.round(controlsPower.displayDevice.percentage) + "%" : ""
            powerSummary: QuickSettings.powerProfile === "balanced" ? qsTr("Balanced power") : QuickSettings.powerProfile === "power-saver" ? qsTr("Power saver") : QuickSettings.powerProfile === "performance" ? qsTr("Performance") : ""
            notificationSummary: NotificationRegistry.unreadCount ? qsTr("%1 unread notifications").arg(NotificationRegistry.unreadCount) : qsTr("No pending notifications")
            onFocusToggled: NotificationRegistry.doNotDisturb = !NotificationRegistry.doNotDisturb
            onNightLightToggled: QuickSettings.toggleNightLight()
            onCloseRequested: Popouts.close(Popouts.handleFor("control-center"))
            provider: ControlCenterRegistry
            tileIds: ControlCenterRegistry.tileIds.filter(id => id !== "idle")
            // A card drilling in opens the bar panel that card names, which
            // is the same surface its chip on the bar opens. The control
            // center closes first: two Cooperative popouts in one scope
            // would otherwise have the arbiter close this one anyway, and
            // doing it here makes the hand-off deliberate rather than a
            // side effect of arbitration.
            onPanelRequested: panelId => {
                Popouts.close(Popouts.handleFor("control-center"));
                root.toggleWidgetPanel(panelId, root._lastPanelSource);
            }
        }
    }

    SessionLockCoordinator {
        id: sessionCoordinator
    }

    // One controller drives authentication for every lock surface.
    LockController {
        id: lockController

        lock: sessionCoordinator.lock
    }

    // The battery figure in the lock's bottom-left eyebrow. One host for
    // every screen's LockScreen rather than one per surface, and declared
    // here rather than inside Phosphor.Lock so that module stays free of
    // Phosphor.Service.* imports (its tests run without the services).
    UPowerHost {
        id: lockBattery
    }

    PerScreen {
        id: lockSurfaces

        model: PhosphorShell.screens

        delegate: LockSurface {
            id: lockSurface

            // PerScreen hands these in as initial properties.
            required property var phosphorScreen
            property string name: ""
            property int index: 0
            property bool isPrimary: false

            screen: lockSurface.phosphorScreen
            visible: lockController.surfacesWanted

            LockScreen {
                anchors.fill: parent
                controller: lockController
                decoration: ShellChrome.decorationComponent
                map: lockSurface.name.length > 0 ? PlacementMap.forScreen(lockSurface.name) : null
                wallpaperPath: PhosphorShell.wallpaper.path
                battery: lockBattery
                isPrimary: lockSurface.isPrimary
            }
        }
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
    // `pragma ComponentBehavior: Bound` (see the header): both inline
    // Components here are instantiated from C++ against a foreign context.
    Component {
        id: powerMenuComponent

        PowerMenu {}
    }

    // The control center is a pane tethered to the bar. main.cpp routes
    // the "control-center" id to a PanePopoutTransport, which opens it as
    // a toplevel the placement engine places (A2 §4), with the bar-socket
    // transport behind it as the floating fallback (an output with no
    // engine, or a toplevel that could not be built); both drive
    // ControlCenterRegistry.openScreen. Going through Popouts rather than
    // writing that property directly is what makes the Modal power menu
    // close it, refuses it while a modal is up, and drains it on reload,
    // all without this file remembering to.
    //
    // `content` is the pane's content for the toplevel; the fallback
    // ignores it and the delegate above mounts its own. `targetScreen` is
    // the output whose bar button fired, so a multi-head setup tethers the
    // pane to that bar. Keyboard focus: a toplevel takes it as a window,
    // which is what lets Escape close the pane (A2 §4.7). No dismiss on
    // focus loss: a pane is a tile, and tiles do not vanish when you look
    // elsewhere.
    // The control center and the launcher share the default popout
    // scope, so opening one CLOSES the other. That is the intended
    // behaviour for two full-attention surfaces triggered from the same
    // bar, and it is worth stating because nothing at either call site
    // hints at it: give one of them its own scope and they would happily
    // sit open together. The same scope is the A2 §4.7 arbitration for
    // panes: a second pane id opened here replaces the first.
    function toggleControlCenter(source: Item): void {
        // screenOf hands back a QScreen the C++ side owns; the controller
        // marks it CppOwnership before returning, so the JS GC cannot
        // delete the live screen when this wrapper is collected. Do not
        // reach for a QScreen any other way from QML.
        root._lastPanelSource = source;
        const target = ControlCenterRegistry.screenOf(source);
        const centre = BarRegistry.anchorCenterFor(source);
        const anchored = centre >= 0;
        const railT = anchored && target && target.width > 0 ? Math.max(0, Math.min(1, centre / target.width)) : 0.5;
        const request = {
            "popoutId": "control-center",
            "content": paneComponent,
            "targetScreen": target,
            "anchor": Appearance.stage ? PhosphorPopout.Anchor.BottomCenter : PhosphorPopout.Anchor.BarRight,
            "customAnchor": Qt.point(anchored ? centre : 0, 0),
            "exclusive": PhosphorPopout.ExclusiveMode.Cooperative,
            "dismissOnFocusLoss": true,
            // No keyboard for the same reason the other panels take none:
            // it is a pointer surface, and holding focus takes it off
            // whatever the user was typing in. It also kept the surface
            // alive by holding the grab.
            "keyboardFocus": true,
            "props": {
                "railT": railT,
                "panelWidth": Appearance.stage && target ? Math.max(364, target.geometry.width - 190) : Appearance.panelWidth
            }
        };
        // The arbiter keys on the popout id alone, which is right for the
        // launcher and the power menu but not for a pane that belongs to
        // one bar. Without this check, pressing the button on a SECOND
        // monitor while the panel is open on the first just closes it, and
        // the screen this call went to the trouble of resolving is thrown
        // away. Move it instead: close there, open here.
        const openOn = ControlCenterRegistry.openScreen;
        if (openOn !== "" && target && openOn !== target.name) {
            const handle = Popouts.handleFor("control-center");
            if (handle !== "")
                Popouts.close(handle);
            Popouts.open(request);
            return;
        }
        Popouts.toggle(request);
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
    // no panel, which is how systemmetrics and focusedapp stay inert
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
            "appearance": {
                "component": appearancePanelComponent,
                "keyboard": true
            },
            "network": {
                "component": networkPanelComponent,
                "keyboard": true
            },
            "bluetooth": {
                "component": bluetoothPanelComponent,
                "keyboard": false
            },
            "audio": {
                "component": audioPanelComponent,
                "keyboard": false
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
                "keyboard": false
            },
            "clock": {
                "component": calendarPanelComponent,
                "keyboard": true
            }
        })

    Component {
        id: networkPanelComponent

        NetworkPanel {}
    }

    Component {
        id: bluetoothPanelComponent

        BluetoothPanel {}
    }

    Component {
        id: audioPanelComponent

        AudioPanel {}
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

        NotificationPanel {}
    }

    Component {
        id: appearancePanelComponent
        AppearancePanel {
            sessionState: AppearanceSession
            availableWidgets: BarRegistry.factoryIds
            onCloseRequested: Popouts.close(Popouts.handleFor("bar.panel.appearance"))
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
        const centre = BarRegistry.anchorCenterFor(source);
        const anchored = centre >= 0;
        // Where the panel sits along the screen, 0..1. This is what binds
        // the panel's stroke and top band to the one screen-wide gradient
        // rather than giving each panel a private colour: a chip on the
        // left opens a cyan-leaning surface, one on the right a
        // rose-leaning one. Falls back to centre when the chip's position
        // could not be resolved.
        const screen = BarRegistry.screenOf(source);
        const railT = anchored && screen && screen.width > 0 ? Math.max(0, Math.min(1, centre / screen.width)) : 0.5;
        Popouts.toggle({
            "popoutId": "bar.panel." + id,
            "content": panel.component,
            // screenOf hands back a QScreen the C++ side owns; the
            // controller marks it CppOwnership before returning, so the JS
            // GC cannot delete the live screen when this wrapper is
            // collected. Do not reach for a QScreen any other way from QML.
            "targetScreen": screen,
            "anchor": anchored ? PhosphorPopout.Anchor.BarItem : PhosphorPopout.Anchor.BarCenter,
            "customAnchor": Qt.point(anchored ? centre : 0, 0),
            "exclusive": PhosphorPopout.ExclusiveMode.Cooperative,
            // Per panel; see widgetPanels above for why this is not one
            // shared value.
            "keyboardFocus": panel.keyboard,
            // Transients close on outside click or focus loss (A2 §4.7).
            // That is the line between this class and a pane: a pane is a
            // tile and does not vanish when you look elsewhere, and these
            // are glances.
            "dismissOnFocusLoss": true,
            "props": {
                "railT": railT
            }
        });
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
        function show(): void {
            root.toggleWidgetPanel("appearance", null);
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
    // `phosphorctl call control-center.open`. Same show/toggle split as the
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

    // The launcher owns its Navigator panel or Stage shelf geometry.
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

    // A toast over the wire:
    // `phosphorctl call notify.send --arg summary=Hi --arg body=There`. It
    // lands on the primary output's stack and returns the toast id the host
    // assigned (-1 when suppressed or when no host is up). This is the
    // shell's own path, for scripts and for seeing the stack at all; the
    // org.freedesktop.Notifications server is not run by this process yet,
    // so notify-send does not arrive here.
    IpcTarget {
        target: "notify"

        function send(summary: string, body: string): int {
            return ToastRegistry.send(summary, body);
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

        function show(): void {
            PickerRegistry.show("");
        }

        function toggle(): void {
            PickerRegistry.toggle("");
        }

        function hide(): void {
            PickerRegistry.hide();
        }
    }

    // The polkit prompt (A3 §9): a card hanging from the requesting
    // window's top edge, built as a popout so it can appear on any output
    // with keyboard focus landing in its field, and torn down with the
    // request. Detached: it opens even while the power menu holds a
    // modal, draws no scrim of its own (the per-screen PolkitDim above is
    // the 20% dim with the requester left clear), and no other popout
    // closes it. Same root-context rule as the launcher: everything it
    // reads is a context property.
    //
    // Closing the popout any other way (the host's Escape, a hot reload)
    // destroys this content; a request still open at that point is
    // cancelled so polkit never waits on a card nobody can see.
    Component {
        id: polkitComponent

        PolkitPrompt {
            agent: PolkitRegistry
            decoration: ShellChrome.decorationComponent
            request: PolkitRegistry.activeRequest
            requester: PolkitRegistry.requesterName
            errorText: PolkitRegistry.lastError
            Component.onCompleted: forceActiveFocus()
            Component.onDestruction: {
                if (PolkitRegistry.activeRequest)
                    PolkitRegistry.cancel();
            }
        }
    }

    // Where the prompt goes: the requester's pid through the window
    // tracker (a capability the dashboard work adds; probed, never
    // assumed) to a window id, then across every output's placement map
    // to the cell that shows it. Found: the band spans that window's top
    // edge on that output. Not found: the screen's top edge, centred,
    // under the bar, on the primary output.
    function openPolkitPrompt(): void {
        const pid = PolkitRegistry.requesterPid;
        let windowId = "";
        if (pid > 0 && typeof WindowTracking !== "undefined" && WindowTracking && typeof WindowTracking.findWindowByPid === "function") {
            const found = WindowTracking.findWindowByPid(pid);
            windowId = found === undefined || found === null ? "" : String(found);
        }
        let screenName = "";
        let rect = Qt.rect(0, 0, 0, 0);
        if (windowId !== "") {
            const names = PolkitRegistry.screenNames();
            for (let i = 0; i < names.length; ++i) {
                const map = PlacementMap.forScreen(names[i]);
                if (!map)
                    continue;
                const r = map.cellRect(windowId);
                if (r && r.width > 0 && r.height > 0) {
                    screenName = names[i];
                    rect = r;
                    break;
                }
            }
        }
        // screenNamed marks the QScreen CppOwnership before returning it,
        // the same guard ControlCenterRegistry.screenOf relies on.
        const screen = PolkitRegistry.screenNamed(screenName);
        if (!screen)
            return;
        const anchored = rect.width > 0;
        // PopoutHost caps a frame at the surface width minus its margins,
        // so a band on a full-width window is centred within that cap.
        const bandWidth = anchored ? Math.min(rect.width, screen.geometry.width - 2 * Tokens.spacing_l) : 360;
        const x = anchored ? rect.x + (rect.width - bandWidth) / 2 : (screen.geometry.width - 360) / 2;
        const y = anchored ? rect.y : Tokens.bar_thickness + Tokens.spacing_l;
        PolkitRegistry.setPlacement(screen.name, anchored ? rect : Qt.rect(0, 0, 0, 0));
        Popouts.open({
            "popoutId": "polkit",
            "content": polkitComponent,
            "targetScreen": screen,
            "anchor": PhosphorPopout.Anchor.Custom,
            "customAnchor": Qt.point(x, y),
            "exclusive": PhosphorPopout.ExclusiveMode.Detached,
            "keyboardFocus": true,
            // A password prompt holds the keyboard until it closes; Detached
            // keeps it free of the Modal scrim, the flag adds the grab.
            "exclusiveKeyboard": true,
            "dismissOnFocusLoss": false,
            "props": {
                "anchored": anchored,
                "bandWidth": bandWidth
            }
        });
    }

    Connections {
        target: PolkitRegistry

        function onActiveRequestChanged(): void {
            if (PolkitRegistry.activeRequest)
                root.openPolkitPrompt();
            else
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

    // The keybind cheatsheet (A3 §10): the daemon's chords drawn on this
    // output's placement map. A Cooperative popout with OnDemand keyboard
    // focus, so Escape and the type-to-filter keys land on the sheet while
    // every real chord still reaches the compositor's global-shortcut
    // filter first: press one and the map moves under the labels. Not
    // Modal: the desktop stays live behind it and nothing is dimmed by
    // the compositor.
    //
    // The sheet's own ShortcutCatalog is declared inside the Component,
    // where its id resolves; it reads Control.getShortcutsJson and follows
    // shortcutsChanged.
    Component {
        id: cheatsheetComponent

        Cheatsheet {
            implicitWidth: Screen.width
            implicitHeight: Screen.height
            screenName: Screen.name
            map: Screen.name ? PlacementMap.forScreen(Screen.name) : null
            catalog: chords.rows
            open: true
            onCloseRequested: open = false
            onReleased: Popouts.close(Popouts.handleFor("cheatsheet"))

            ShortcutCatalog {
                id: chords
            }
        }
    }

    function toggleCheatsheet(): void {
        Popouts.toggle({
            "popoutId": "cheatsheet",
            "content": cheatsheetComponent,
            "anchor": PhosphorPopout.Anchor.ScreenCenter,
            "exclusive": PhosphorPopout.ExclusiveMode.Cooperative,
            "keyboardFocus": true,
            "dismissOnFocusLoss": false
        });
    }

    // `phosphorctl call cheatsheet.toggle`, for the compositor keybind
    // (the daemon's own toggle_cheatsheet chord drives its KWin-overlay
    // sheet, not this one).
    IpcTarget {
        target: "cheatsheet"

        function toggle(): void {
            root.toggleCheatsheet();
        }
    }
}
