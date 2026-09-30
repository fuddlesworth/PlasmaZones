// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Phosphor.Bar
import Phosphor.Ipc
import Phosphor.Popout
import Phosphor.Theme

Item {
    id: root
    property bool locked: false
    property Item sourceWidget: null
    property Item sourceAnchor: null
    property string openPage: ""
    property string screenName: ""
    property string handle: ""
    signal detailRequested(string panelId, Item source, string screenName)

    function close(): void {
        Popouts.close(Popouts.handleFor("status-icons"));
    }

    function show(page: string, source: Item, output: string): bool {
        if (root.locked || Popouts.modalActive)
            return false;
        if (page !== "settings" && page !== "overflow" && !StatusIcons.icons.some(icon => icon.id === page))
            return false;
        if (PickerRegistry.openScreen !== "") {
            PickerRegistry.requestPanel("status-icons");
            return true;
        }
        const anchor = source?.requestAnchor || source;
        const target = output ? BarRegistry.screenNamed(output) : BarRegistry.screenOf(anchor);
        const previous = Popouts.handleFor("status-icons");
        const repeated = previous !== "" && root.openPage === page && root.sourceWidget === source && root.screenName === (target?.name || "");
        if (previous !== "")
            Popouts.close(previous);
        if (repeated)
            return true;
        root.sourceWidget = source;
        root.sourceAnchor = anchor;
        root.openPage = page;
        root.screenName = target?.name || "";
        const centre = BarRegistry.anchorCenterFor(anchor);
        root.handle = Popouts.open({
            popoutId: "status-icons",
            content: panelComponent,
            targetScreen: target,
            anchor: centre >= 0 ? PhosphorPopout.Anchor.BarItemRight : PhosphorPopout.Anchor.BarRight,
            customAnchor: Qt.point(centre >= 0 ? centre + anchor.width / 2 : 0, 0),
            exclusive: PhosphorPopout.ExclusiveMode.Cooperative,
            keyboardFocus: true,
            exclusiveKeyboard: true,
            dismissOnFocusLoss: true,
            props: {
                routing: root,
                page: page,
                railT: centre >= 0 && target?.geometry.width > 0 ? Math.max(0, Math.min(1, centre / target.geometry.width)) : 0.5
            }
        });
        if (!root.handle) {
            root.openPage = "";
            root.sourceWidget = null;
            root.sourceAnchor = null;
            return false;
        }
        BarRegistry.setOpenPanel("status-icons", anchor);
        return true;
    }

    Binding {
        target: root.sourceWidget
        property: "expandedStatusPage"
        value: root.openPage
        when: target !== null && root.handle !== ""
        restoreMode: Binding.RestoreBindingOrValue
    }

    // The transport creates this content in the engine root context. Pass
    // its routing owner explicitly so callbacks survive that context change.
    Component {
        id: panelComponent
        StatusIconsPanel {
            required property QtObject routing
            controller: StatusIcons
            decoration: ShellChrome.decorationComponent
            onPageChanged: {
                if (routing)
                    routing.openPage = page;
            }
            onCloseRequested: routing.close()
            onDetailRequested: panelId => {
                const source = routing.sourceWidget;
                const output = routing.screenName;
                routing.close();
                routing.detailRequested(panelId, source, output);
            }
        }
    }

    Connections {
        target: Popouts
        function onPopoutClosed(popoutId: string, closedHandle: string): void {
            if (popoutId !== "status-icons" || root.handle !== closedHandle)
                return;
            root.handle = "";
            root.openPage = "";
            root.sourceWidget = null;
            root.sourceAnchor = null;
            BarRegistry.setOpenPanel("", null);
        }
    }
    Connections {
        target: PickerRegistry
        function onPanelRequested(panelId: string, output: string): void {
            if (panelId === "status-icons")
                root.show("settings", null, output);
        }
    }
    onLockedChanged: {
        if (locked)
            root.close();
    }
    IpcTarget {
        target: "status-icons"
        function show(page: string): bool {
            return root.show(page || "settings", null, "");
        }
        function hide(): void {
            root.close();
        }
    }
}
