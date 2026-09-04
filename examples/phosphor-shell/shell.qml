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
import Phosphor.Ipc
import Phosphor.Popout
import Phosphor.Power
import Phosphor.Shell
import Phosphor.Theme
import QtQuick

// Top-level composer for the dogfood shell. Phase 4.1 replaces the old
// single TopPanel + pushed-in data sources with the production bar:
// BarHost mounts one connected-corner bar per output, and each bar widget
// owns its own data source (Clock its SystemClock, Battery its UPowerHost,
// Tray its StatusNotifierHost, ...), so this file no longer wires
// clock/CPU/memory/battery into a panel.
//
// BarHost reads the BarRegistry context property (the IBarWidgetFactory
// owner, set by src/shell/main.cpp) to mount its widgets.
//
// PerScreenPanels rather than a Repeater: ShellEngine discovers panels by
// walking QObject children and then takes ownership of each one, so the
// instantiator has to parent what it builds and must never destroy it
// afterwards. See PerScreenPanels' class docs.
//
// This file is also the composition root for anything that spans several
// services. The bar's trailing buttons are only triggers; what they open
// lives elsewhere, and BarRegistry.widgetActivated is the seam that keeps
// Phosphor.Bar from depending on every surface it can summon.
//
// The legacy panel/popup/settings demo components (TopPanel, PanelPopupHost,
// SettingsWindow, ...) still ship in this module but are no longer composed
// here; their replacements are the Phase 4 surfaces (control center,
// notification center, power menu) reached through that seam.
Item {
    id: root

    PerScreenPanels {
        id: bars

        model: PhosphorShell.screens

        // The control center grows out of THIS bar's capsule as one
        // continuous painted surface (the connected-corner design), rather
        // than opening as a separate centred popout.
        //
        // Everything the delegate reads must come from a CONTEXT PROPERTY,
        // never an id in this file: PerScreenPanels builds each delegate
        // with a fresh QQmlContext carrying `modelData`, so shell.qml's ids
        // do not resolve inside one. Hence the open state lives on
        // ControlCenterRegistry and the socket content is declared inline
        // here, in the delegate's own scope.
        delegate: BarHost {
            id: bar

            socketWidth: 380
            socketDepth: 460
            // Open only on the screen the registry says owns it, so a
            // multi-head setup shows one panel, on the bar that summoned it.
            //
            // Read through PanelWindow.screen, NOT modelData.screen:
            // PerScreenPanels deliberately withholds the screen role from
            // modelData because that map snapshots a raw QScreen* which
            // dangles on hot-unplug, while this property is QPointer-backed
            // and simply reads null once the output dies. Hence the guard.
            socketOpen: bar.screen ? ControlCenterRegistry.openScreen === bar.screen.name : false

            socketContent: Component {
                ControlCenter {
                    provider: ControlCenterRegistry
                    tileIds: ControlCenterRegistry.tileIds
                    columns: 2
                }
            }
        }
    }

    // The one live SessionHost in the process. It must be a singleton in
    // practice even though the type is instantiable: each instance opens its
    // own logind connection and takes two real inhibitors (a sleep
    // delay-lock, and a grab on the power/suspend/hibernate/lid keys), so a
    // second one would double them. Everything that needs session actions
    // takes this one by reference.
    //
    // Instantiating it here also starts the lock-before-sleep handshake,
    // which had never actually run: the coordinator shipped in this module
    // but nothing ever created it.
    SessionLockCoordinator {
        id: sessionCoordinator
    }

    // The session menu, per docs/phosphor-shell-design/mockups/power-menu.svg:
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

    // The control center grows out of the bar's own capsule (the
    // connected-corner socket), so it is NOT a popout: there is no separate
    // surface to place, and BarHost paints the body as part of the bar's
    // Shape. The bar delegate above mounts the content; all this file owns
    // is the open/close verb.
    //
    // That also means it does not go through PopoutController, so the
    // Modal power menu does not close it automatically the way it would a
    // Cooperative popout. Opening the power menu closes it explicitly
    // below, which is the same outcome by a different road.
    function toggleControlCenter(screenName: string): void {
        ControlCenterRegistry.toggleOnScreen(screenName);
    }

    function togglePowerMenu(): void {
        // The control center is painted into a bar capsule, not opened
        // through PopoutController, so the Modal power menu's automatic
        // "close every cooperative popout" does not reach it. Close it by
        // hand so the two are never up together.
        ControlCenterRegistry.close();
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
                "session": sessionCoordinator.session
            },
            "anchor": PhosphorPopout.Anchor.ScreenCenter,
            "exclusive": PhosphorPopout.ExclusiveMode.Modal,
            "keyboardFocus": true,
            "dismissOnFocusLoss": true
        });
    }

    Connections {
        target: BarRegistry

        // `source` is the bar widget that fired. The session menu is
        // screen-centred and ignores it; the control center is anchored to
        // a bar, so it uses the widget's window to pick which output's
        // capsule to grow out of.
        function onWidgetActivated(id: string, source: Item): void {
            if (id === "power")
                root.togglePowerMenu();
            else if (id === "controlcenter")
                // "controlcenter" is the bar widget's registered id
                // (barcontroller.cpp), not the IPC target name below.
                root.toggleControlCenter(ControlCenterRegistry.screenNameOf(source));
        }
    }

    // The wire surface, per the mockup's `phosphorctl call power.show`. The
    // bar button is one way in; this is the one a compositor keybind uses,
    // which is how Ctrl+Alt+Del reaches the menu without the shell claiming a
    // global shortcut of its own.
    //
    // `show` and `toggle` are separate because a method named show that hides
    // on the second call is a trap for anything scripting it. Bind a key to
    // toggle; call show from a script that wants the menu up regardless.
    IpcTarget {
        target: "power"

        function show(): void {
            if (!Popouts.isOpen("power"))
                root.togglePowerMenu();
        }

        function toggle(): void {
            root.togglePowerMenu();
        }
    }

    // The control center's wire surface, per the mockup's
    // `phosphorctl call control-center.open`. Same show/toggle split as the
    // power menu, and the same reason: bind a compositor key to toggle, and
    // call show from a script that wants the panel up regardless.
    //
    // The no-argument forms are the ones a keybind uses, so they must stay
    // callable as `phosphorctl call control-center.toggle` — IpcTarget
    // arity is strict, and a lone `toggle(screen)` would reject that with
    // "argument count mismatch". The *OnScreen variants take the explicit
    // output, for a keybind that should follow the pointer.
    IpcTarget {
        target: "control-center"

        function show(): void {
            showOnScreen(ControlCenterRegistry.screenNameOf(null));
        }

        function toggle(): void {
            // Open anywhere means close; nothing open means open on the
            // primary. Routing through toggleOnScreen with the primary name
            // would instead MOVE a panel that is open on another output,
            // which is not what a bare toggle means.
            if (ControlCenterRegistry.openScreen !== "")
                ControlCenterRegistry.close();
            else
                ControlCenterRegistry.toggleOnScreen(ControlCenterRegistry.screenNameOf(null));
        }

        function showOnScreen(screen: string): void {
            // toggleOnScreen would CLOSE it if that screen already has it,
            // which is not what a caller asking to show it means.
            if (ControlCenterRegistry.openScreen !== screen)
                ControlCenterRegistry.toggleOnScreen(screen);
        }

        function toggleOnScreen(screen: string): void {
            root.toggleControlCenter(screen);
        }

        function hide(): void {
            ControlCenterRegistry.close();
        }
    }
}
