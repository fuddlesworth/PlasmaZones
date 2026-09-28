// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Display-only fixtures. No ShortcutCatalog, daemon proxy, or process action
// is instantiated. Catalog changes never register or invoke a displayed binding.

import QtQuick
import Phosphor.Shell
import Phosphor.Dashboard
import Phosphor.Theme
import Phosphor.Ipc

Item {
    id: root

    property var sheet: null
    property int modeValue: 1
    property string profileName: "defaults"
    property string catalogStatus: "ready"
    property var catalogRows: []
    property bool openValue: true
    property bool surfaceVisible: true
    property int viewportWidth: 0
    property int viewportHeight: 0
    property int retryCount: 0
    property int closeCount: 0
    property int releaseCount: 0
    property var layoutsOverride: null
    readonly property string selectedOutput: Environment.get("PZ_SHORTCUTS_SCREEN")
    readonly property bool layoutsSupported: layoutsOverride === null ? modeValue !== 1 : layoutsOverride
    readonly property string workspaceLabel: modeValue === 2 ? qsTr("Listen") : modeValue === 0 ? qsTr("Build") : qsTr("Develop")

    Component.onCompleted: selectProfile("defaults")

    function defaults(): var {
        const rows = [];
        function add(id, label, mode, chord, category, categoryOrder, description) {
            rows.push({
                id: id,
                label: label,
                description: description || "",
                mode: mode,
                category: category,
                categoryOrder: categoryOrder,
                rowOrder: rows.length,
                triggers: chord ? [chord] : [],
                assigned: !!chord
            });
        }
        // Representative effective defaults from configdefaults.h and
        // configdefaults_scrolling_shortcuts.h. Individual catalog rows retain
        // their real IDs, including directional and numbered families.
        add("open_editor", qsTr("Open layout editor"), "all", "Meta+Shift+E", qsTr("General"), 0);
        add("open_settings", qsTr("Open settings"), "all", "Meta+Shift+P", qsTr("General"), 0);
        add("toggle_cheatsheet", qsTr("Keyboard shortcuts"), "all", "Meta+Alt+/", qsTr("General"), 0);
        add("toggle_autotile", qsTr("Cycle placement mode"), "all", "Meta+Shift+T", qsTr("General"), 0);
        add("retile", qsTr("Reapply layout"), "managed", "Meta+Ctrl+T", qsTr("General"), 0);
        add("toggle_window_float", qsTr("Toggle floating"), "all", "Meta+F", qsTr("General"), 0);
        add("scroll_switch_focus_float_tiling", qsTr("Switch floating and placed focus"), "all", "Meta+Alt+X", qsTr("General"), 0);
        add("previous_layout", qsTr("Previous layout"), "layouts", "Meta+Alt+[", qsTr("Layouts"), 1);
        add("next_layout", qsTr("Next layout"), "layouts", "Meta+Alt+]", qsTr("Layouts"), 1);
        add("layout_picker", qsTr("Choose layout"), "layouts", "Meta+Alt+Space", qsTr("Layouts"), 1);
        add("toggle_layout_lock", qsTr("Lock layout"), "layouts", "Meta+Ctrl+L", qsTr("Layouts"), 1);
        add("restore_window_size", qsTr("Restore window size"), "all", "Meta+Alt+Escape", qsTr("Zones"), 3);
        add("push_to_empty_zone", qsTr("Move to an empty zone"), "snapping", "Meta+Alt+Return", qsTr("Zones"), 3);
        const directions = [
            {
                id: "left",
                key: "Left",
                label: qsTr("left")
            },
            {
                id: "right",
                key: "Right",
                label: qsTr("right")
            },
            {
                id: "up",
                key: "Up",
                label: qsTr("up")
            },
            {
                id: "down",
                key: "Down",
                label: qsTr("down")
            }
        ];
        for (const direction of directions) {
            add("focus_zone_" + direction.id, qsTr("Move focus %1").arg(direction.label), "all", "Alt+Shift+" + direction.key, qsTr("Windows"), 4);
            add("move_window_" + direction.id, qsTr("Move window %1").arg(direction.label), "all", "Meta+Alt+Shift+" + direction.key, qsTr("Windows"), 4);
            add("swap_window_" + direction.id, qsTr("Swap window %1").arg(direction.label), "all", "Meta+Ctrl+Alt+" + direction.key, qsTr("Windows"), 4);
            add("span_window_" + direction.id, qsTr("Extend window %1").arg(direction.label), "snapping", "Ctrl+Alt+" + direction.key, qsTr("Windows"), 4);
        }
        add("focus_master", qsTr("Focus master"), "autotile", "Meta+Shift+M", qsTr("Autotile"), 9);
        add("swap_master", qsTr("Swap with master"), "autotile", "Meta+Shift+Return", qsTr("Autotile"), 9);
        add("increase_master_ratio", qsTr("Increase master width"), "autotile", "Meta+Shift+L", qsTr("Autotile"), 9);
        add("decrease_master_ratio", qsTr("Decrease master width"), "autotile", "Meta+Shift+H", qsTr("Autotile"), 9);
        add("increase_master_count", qsTr("Add a master window"), "autotile", "Meta+Ctrl+=", qsTr("Autotile"), 9);
        add("decrease_master_count", qsTr("Remove a master window"), "autotile", "Meta+Ctrl+-", qsTr("Autotile"), 9);
        add("scroll_focus_column_first", qsTr("Focus first column"), "scrolling", "Meta+Alt+Home", qsTr("Scrolling"), 10);
        add("scroll_focus_column_last", qsTr("Focus last column"), "scrolling", "Meta+Alt+End", qsTr("Scrolling"), 10);
        add("scroll_focus_column_left_or_last", qsTr("Focus previous column with wrapping"), "scrolling", "", qsTr("Scrolling"), 10);
        add("scroll_focus_column_right_or_first", qsTr("Focus next column with wrapping"), "scrolling", "", qsTr("Scrolling"), 10);
        add("scroll_consume_window", qsTr("Consume window"), "scrolling", "Meta+Alt+I", qsTr("Scrolling"), 10, qsTr("Pulls a window from the next column into the focused column, stacking them."));
        add("scroll_expel_window", qsTr("Expel window"), "scrolling", "Meta+Alt+Shift+I", qsTr("Scrolling"), 10, qsTr("Moves the focused window out of a shared column into a new column after it."));
        add("scroll_center_column", qsTr("Center column"), "scrolling", "Meta+Alt+C", qsTr("Scrolling"), 10);
        add("scroll_toggle_column_tabbed", qsTr("Toggle column tabs"), "scrolling", "Meta+Alt+T", qsTr("Scrolling"), 10);
        add("scroll_cycle_tab", qsTr("Next tab"), "scrolling", "Meta+Alt+Tab", qsTr("Scrolling"), 10);
        add("scroll_cycle_tab_back", qsTr("Previous tab"), "scrolling", "Meta+Alt+Shift+Tab", qsTr("Scrolling"), 10);
        add("scroll_increase_column_width", qsTr("Increase column width"), "scrolling", "Meta+Alt+W", qsTr("Scrolling"), 10);
        add("scroll_decrease_column_width", qsTr("Decrease column width"), "scrolling", "Meta+Alt+Shift+W", qsTr("Scrolling"), 10);
        add("scroll_cycle_column_width", qsTr("Cycle column width"), "scrolling", "Meta+Alt+PgUp", qsTr("Scrolling"), 10);
        add("scroll_cycle_column_width_back", qsTr("Cycle column width backward"), "scrolling", "Meta+Alt+PgDown", qsTr("Scrolling"), 10);
        add("scroll_cycle_window_height", qsTr("Cycle window height"), "scrolling", "Meta+Alt+Shift+PgUp", qsTr("Scrolling"), 10);
        add("scroll_increase_window_height", qsTr("Increase window height"), "scrolling", "Meta+Alt+H", qsTr("Scrolling"), 10);
        add("scroll_move_to_floating", qsTr("Move window to floating"), "scrolling", "", qsTr("Scrolling"), 10);
        add("scroll_move_to_tiling", qsTr("Move window to columns"), "scrolling", "", qsTr("Scrolling"), 10);
        for (let number = 1; number <= 9; ++number) {
            add("snap_to_zone_" + number, qsTr("Move to position %1").arg(number), "all", "Meta+Ctrl+" + number, qsTr("Zones"), 3);
            add("quick_layout_" + number, qsTr("Quick layout %1").arg(number), "layouts", "Meta+Alt+" + number, qsTr("Layouts"), 1);
        }
        return rows;
    }

    function selectProfile(name: string): bool {
        if (["defaults", "custom", "unassigned", "empty"].indexOf(name) < 0)
            return false;
        const custom = {
            focus_zone_left: ["Meta+H", "Alt+Shift+Left"],
            focus_zone_right: ["Meta+L", "Alt+Shift+Right"],
            focus_zone_up: ["Meta+K", "Alt+Shift+Up"],
            focus_zone_down: ["Meta+J", "Alt+Shift+Down"],
            toggle_window_float: ["Meta+Shift+F"],
            scroll_focus_column_left_or_last: ["Meta+Ctrl+Alt+Shift+Home"],
            scroll_focus_column_right_or_first: ["Meta+Ctrl+Alt+Shift+End"],
            scroll_increase_column_width: []
        };
        profileName = name;
        catalogRows = name === "empty" ? [] : defaults().map(row => {
            if (name === "unassigned")
                row.triggers = [];
            else if (name === "custom" && custom[row.id] !== undefined)
                row.triggers = custom[row.id];
            row.assigned = row.triggers.length > 0;
            return row;
        });
        return true;
    }

    function findNamedItem(item: Item, name: string): Item {
        if (item.objectName === name)
            return item;
        for (const child of item.children) {
            const match = findNamedItem(child, name);
            if (match)
                return match;
        }
        return null;
    }

    function metrics(name: string): var {
        if (!root.sheet || !root.sheet.Window.window)
            return null;
        const window = root.sheet.Window.window;
        const item = findNamedItem(window.contentItem, name);
        if (!item)
            return null;
        const rect = item.mapToItem(window.contentItem, 0, 0, item.width, item.height);
        return {
            x: rect.x,
            y: rect.y,
            w: rect.width,
            h: rect.height,
            visible: window.visible && item.visible,
            enabled: item.enabled
        };
    }

    PerScreenPanels {
        model: PhosphorShell.screens

        delegate: PanelWindow {
            id: panel
            readonly property bool selected: root.selectedOutput ? modelData.name === root.selectedOutput : modelData.isPrimary

            edge: PanelWindow.Top
            alignment: PanelWindow.Fill
            thickness: modelData.height
            panelLayer: PanelWindow.LayerOverlay
            exclusiveZoneEnabled: false
            keyboardFocus: selected ? PanelWindow.Exclusive : PanelWindow.None
            inputRegion: selected && root.surfaceVisible ? [Qt.rect(0, 0, width, height)] : []

            Connections {
                target: root
                function onSurfaceVisibleChanged(): void {
                    if (panel.Window.window)
                        panel.Window.window.visible = panel.selected && root.surfaceVisible;
                }
            }

            QtObject {
                id: fixtureMap
                property int mode: root.modeValue
                property int currentDesktop: mode === 2 ? 2 : mode === 0 ? 1 : 0
                property rect workArea: Qt.rect(0, 0, reference.width, reference.height)
                property var cells: mode === 1 ? [
                    {
                        id: "master",
                        x: 0,
                        y: 0,
                        w: .58,
                        h: 1,
                        focused: true,
                        occupied: true,
                        zoneNumber: 1
                    },
                    {
                        id: "upper",
                        x: .6,
                        y: 0,
                        w: .4,
                        h: .49,
                        occupied: true,
                        zoneNumber: 2
                    },
                    {
                        id: "lower",
                        x: .6,
                        y: .51,
                        w: .4,
                        h: .49,
                        occupied: true,
                        zoneNumber: 3
                    }
                ] : [
                    {
                        id: "first",
                        x: 0,
                        y: 0,
                        w: .32,
                        h: 1,
                        focused: true,
                        occupied: true,
                        zoneNumber: 1
                    },
                    {
                        id: "second",
                        x: .34,
                        y: 0,
                        w: .32,
                        h: 1,
                        occupied: true,
                        zoneNumber: 2
                    },
                    {
                        id: "third",
                        x: .68,
                        y: 0,
                        w: .32,
                        h: 1,
                        occupied: true,
                        zoneNumber: 3
                    }
                ]
                signal changed
                onModeChanged: changed()
                onWorkAreaChanged: changed()
                onCellsChanged: changed()

                function focusedCellId(): string {
                    return mode === 1 ? "master" : "first";
                }
            }

            Cheatsheet {
                id: reference
                anchors.centerIn: parent
                width: root.viewportWidth > 0 ? Math.min(parent.width, root.viewportWidth) : parent.width
                height: root.viewportHeight > 0 ? Math.min(parent.height, root.viewportHeight) : parent.height
                map: fixtureMap
                screenName: modelData.name
                workspaceName: root.workspaceLabel
                catalog: root.catalogStatus === "ready" ? root.catalogRows : []
                catalogAvailable: root.catalogStatus !== "unavailable"
                catalogLoading: root.catalogStatus === "loading"
                catalogError: root.catalogStatus === "error" ? qsTr("The shortcut catalog could not be loaded.") : ""
                layoutsAvailable: root.layoutsSupported
                open: panel.selected && root.openValue

                Component.onCompleted: {
                    if (panel.selected)
                        root.sheet = reference;
                }
                onCloseRequested: {
                    root.closeCount++;
                    root.openValue = false;
                }
                onReleased: {
                    root.releaseCount++;
                    root.surfaceVisible = false;
                }
                onRetryRequested: {
                    root.retryCount++;
                    root.catalogStatus = "ready";
                }
            }
        }
    }

    IpcTarget {
        target: "preview"

        function mode(name: string): bool {
            const value = ["snapping", "tiling", "scrolling"].indexOf(name);
            if (value < 0)
                return false;
            root.modeValue = value;
            root.layoutsOverride = null;
            if (root.sheet)
                root.sheet.scope = name;
            return true;
        }
        function profile(name: string): bool {
            return root.selectProfile(name);
        }
        function query(text: string): bool {
            if (!root.sheet)
                return false;
            root.sheet.filter = text;
            return true;
        }
        function scope(name: string): bool {
            if (!root.sheet || ["tiling", "scrolling", "snapping", "general", "shell", "all"].indexOf(name) < 0)
                return false;
            root.sheet.scope = name;
            return true;
        }
        function assigned(enabled: bool): bool {
            if (!root.sheet)
                return false;
            root.sheet.assignedOnly = enabled;
            return true;
        }
        function guide(enabled: bool): bool {
            if (!root.sheet)
                return false;
            root.sheet.showGuide = enabled;
            return true;
        }
        function layouts(available: bool): void {
            root.layoutsOverride = available;
        }
        function status(name: string): bool {
            if (["ready", "loading", "unavailable", "error"].indexOf(name) < 0)
                return false;
            root.catalogStatus = name;
            return true;
        }
        function geometry(width: int, height: int): bool {
            if (!((width === 0 && height === 0) || (width >= 320 && width <= 7680 && height >= 240 && height <= 4320)))
                return false;
            root.viewportWidth = width;
            root.viewportHeight = height;
            return true;
        }
        function preset(name: string): bool {
            return AppearanceStore.applyPreset(name);
        }
        function textScale(percent: int): bool {
            return AppearanceStore.setValue("textScale", percent);
        }
        function motion(enabled: bool): bool {
            return AppearanceStore.setValue("motion", enabled);
        }
        function open(): void {
            root.surfaceVisible = true;
            root.openValue = true;
        }
        function close(): void {
            root.openValue = false;
        }
        function item(name: string): string {
            return JSON.stringify(root.metrics(name));
        }
        function state(): string {
            const sheet = root.sheet;
            const window = sheet ? sheet.Window.window : null;
            return JSON.stringify({
                ready: sheet !== null,
                screen: sheet ? sheet.screenName : "",
                mode: root.modeValue,
                profile: root.profileName,
                catalogStatus: root.catalogStatus,
                catalogCount: root.catalogRows.length,
                layoutsAvailable: root.layoutsSupported,
                open: root.openValue,
                visible: window ? window.visible : false,
                scope: sheet ? sheet.scope : "",
                filter: sheet ? sheet.filter : "",
                assignedOnly: sheet ? sheet.assignedOnly : false,
                showGuide: sheet ? sheet.showGuide : false,
                width: sheet ? sheet.width : 0,
                height: sheet ? sheet.height : 0,
                activeFocusItem: window && window.activeFocusItem ? window.activeFocusItem.objectName : "",
                retryCount: root.retryCount,
                closeCount: root.closeCount,
                releaseCount: root.releaseCount
            });
        }
    }
}
