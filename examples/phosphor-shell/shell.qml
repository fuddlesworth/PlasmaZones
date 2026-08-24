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
import Phosphor.Ipc
import Phosphor.Popout
import Phosphor.Power
import Phosphor.Shell
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

        delegate: BarHost {}
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
    Component {
        id: powerMenuComponent

        PowerMenu {
            session: sessionCoordinator.session
        }
    }

    function togglePowerMenu(): void {
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
            "anchor": PhosphorPopout.Anchor.ScreenCenter,
            "exclusive": PhosphorPopout.ExclusiveMode.Modal,
            "keyboardFocus": true,
            "dismissOnFocusLoss": true
        });
    }

    Connections {
        target: BarRegistry

        // `source` is the bar widget that fired. Unused here because the
        // session menu is screen-centred, but a bar-anchored surface needs it
        // to pick which output's bar it hangs from.
        function onWidgetActivated(id: string, source: Item): void {
            if (id === "power")
                root.togglePowerMenu();
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
}
