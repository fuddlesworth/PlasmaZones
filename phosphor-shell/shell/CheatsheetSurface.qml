// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Phosphor.Dashboard
import Phosphor.Ipc
import Phosphor.Popout
import Phosphor.Shell

Item {
    id: root
    property bool locked: false
    onLockedChanged: if (locked)
        hide()

    Component {
        id: content
        Cheatsheet {
            id: sheet
            property var _popoutHost: null
            implicitWidth: Screen.width
            implicitHeight: Screen.height
            screenName: Screen.name
            map: screenName ? PlacementMap.forScreen(screenName) : null
            catalog: chords.rows
            catalogAvailable: chords.available
            catalogLoading: chords.loading
            catalogError: chords.error
            layoutsAvailable: !!map && map.layoutsAvailable
            workspaceName: Workspaces.activeName
            decoration: ShellChrome.decorationComponent
            surfaceEffects: ShellEffects
            open: true
            onRetryRequested: chords.retry()
            onCloseRequested: {
                if (_popoutHost && _popoutHost.open)
                    Popouts.close(Popouts.handleFor("cheatsheet"));
            }
            onReleased: {
                // Close only this surface, never a newer instance with the same id.
                if (_popoutHost && _popoutHost.open)
                    _popoutHost.dismiss();
            }
            Connections {
                target: sheet._popoutHost
                function onOpenChanged(): void {
                    if (!sheet._popoutHost.open)
                        sheet.open = false;
                }
            }
            ShortcutCatalog {
                id: chords
            }
        }
    }

    function show(screenName = ""): bool {
        // A locked desktop or another modal intentionally consumes the request.
        // Returning false here would open the daemon fallback over that modal.
        if (root.locked || Popouts.modalActive)
            return true;
        if (Popouts.isOpen("cheatsheet"))
            return true;
        const target = screenName ? BarRegistry.screenNamed(screenName) : null;
        if (screenName && !target)
            return false;
        return Popouts.open({
            "popoutId": "cheatsheet",
            "content": content,
            "targetScreen": target,
            "anchor": PhosphorPopout.Anchor.ScreenCenter,
            "exclusive": PhosphorPopout.ExclusiveMode.Modal,
            "keyboardFocus": true,
            "exclusiveKeyboard": true,
            "dismissOnFocusLoss": false,
            "props": screenName ? {
                "screenName": screenName
            } : {}
        }) !== "";
    }
    function toggle(screenName = ""): bool {
        if (Popouts.isOpen("cheatsheet")) {
            root.hide();
            return true;
        }
        return root.show(screenName);
    }
    function hide(): void {
        Popouts.close(Popouts.handleFor("cheatsheet"));
    }
    IpcTarget {
        target: "cheatsheet"
        function show(): bool {
            return root.show();
        }
        function toggle(): bool {
            return root.toggle();
        }
        function hide(): void {
            root.hide();
        }
        function toggleForScreen(screenName: string): bool {
            return root.toggle(screenName);
        }
    }
}
