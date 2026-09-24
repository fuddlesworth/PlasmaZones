// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme

FocusScope {
    id: root
    property real railT: 0.8
    property Item sourceWidget: null
    property string initialMenuKey: ""
    property string initialPage: "overflow"
    property string selectedKey: initialMenuKey
    property bool directMenu: initialMenuKey.length > 0
    property bool settingsOpen: initialPage === "settings"
    property real menuOffset: 0
    readonly property var selectedEntry: TrayModel.lookup(selectedKey)
    readonly property bool hasMenu: !!selectedEntry.instanceKey
    readonly property var shownItems: sourceWidget ? sourceWidget.shownItems : TrayModel.barItems
    readonly property var barKeys: shownItems.map(entry => entry.instanceKey)
    readonly property var overflowItems: TrayModel.items.filter(entry => entry.visibility !== "hidden" && !barKeys.includes(entry.instanceKey))
    readonly property real maximumHeight: Math.max(240, (Screen.height || 900) - Appearance.barHeight - 72)
    readonly property real maximumWidth: Math.max(1, (Screen.width || 1440) - 48)
    readonly property bool singlePane: width < drawer.implicitWidth + 312
    readonly property rect menuRect: hasMenu ? Qt.rect(menuLoader.x, menuLoader.y, menuLoader.width, menuLoader.height) : Qt.rect(0, 0, 0, 0)
    readonly property rect popoutBlurRect: drawer.visible ? Qt.rect(drawer.x, drawer.y, drawer.width, drawer.height) : menuRect
    readonly property rect popoutSecondaryBlurRect: drawer.visible ? menuRect : Qt.rect(0, 0, 0, 0)
    signal closeRequested
    implicitWidth: Math.min(maximumWidth, directMenu ? 300 : drawer.implicitWidth + (hasMenu && maximumWidth >= drawer.implicitWidth + 312 ? 312 : 0))
    implicitHeight: Math.min(maximumHeight, Math.max(drawer.visible ? drawer.implicitHeight : 0, hasMenu ? (singlePane ? 0 : menuOffset) + menuLoader.implicitHeight : 0))
    focus: true
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true
    function closeMenu(): void {
        if (directMenu) {
            closeRequested();
            return;
        }
        const previousKey = selectedKey;
        selectedKey = "";
        Qt.callLater(() => drawer.focusEntry(previousKey));
    }
    function showSettings(open: bool): void {
        selectedKey = "";
        directMenu = false;
        settingsOpen = open;
        menuOffset = 0;
        Qt.callLater(() => drawer.focusFirst());
    }
    function openMenu(entry, anchor: Item): void {
        if (!entry.menuPath || entry.menuPath === "/") {
            openNativeMenu(entry, anchor.mapToGlobal(anchor.width / 2, anchor.height / 2));
            return;
        }
        selectedKey = entry.instanceKey;
        menuOffset = Math.max(0, Math.min(maximumHeight - 300, anchor.mapToItem(drawer, 0, 0).y));
    }
    function openNativeMenu(entry, point): void {
        closeRequested();
        Qt.callLater(() => TrayModel.context(entry, point));
    }
    Keys.onEscapePressed: event => {
        if (hasMenu && menuLoader.item && menuLoader.item.canGoBack)
            menuLoader.item.back();
        else if (hasMenu)
            closeMenu();
        else if (settingsOpen)
            showSettings(false);
        else
            closeRequested();
        event.accepted = true;
    }
    onSelectedEntryChanged: if (selectedKey && !selectedEntry.instanceKey) {
        if (directMenu)
            closeRequested();
        else
            selectedKey = "";
    }
    Component.onCompleted: {
        if (!directMenu)
            Qt.callLater(() => drawer.focusFirst());
        else if (!hasMenu)
            closeRequested();
    }
    TrayDrawer {
        id: drawer
        visible: !root.directMenu && (!root.hasMenu || !root.singlePane)
        x: root.hasMenu && !root.singlePane ? 312 : 0
        y: Appearance.bottom ? root.height - height : 0
        width: Math.min(implicitWidth, root.width)
        height: Math.min(implicitHeight, root.height)
        maximumHeight: root.maximumHeight
        railT: root.railT
        entries: root.overflowItems
        barCount: root.shownItems.length
        shownItems: root.shownItems
        selectedKey: root.selectedKey
        settingsOpen: root.settingsOpen
        onCloseRequested: root.closeRequested()
        onActivated: root.closeRequested()
        onSettingsRequested: open => root.showSettings(open)
        onMenuRequested: (entry, anchor) => root.openMenu(entry, anchor)
    }
    Loader {
        id: menuLoader
        active: root.hasMenu
        x: 0
        y: Appearance.bottom ? Math.max(0, root.height - height) : root.singlePane ? 0 : Math.min(root.menuOffset, root.maximumHeight - height)
        width: Math.min(300, root.width)
        height: implicitHeight
        sourceComponent: Component {
            TrayMenu {
                entry: root.selectedEntry
                railT: root.railT
                maximumHeight: root.maximumHeight
                onCloseRequested: root.closeMenu()
                onSettingsRequested: root.showSettings(true)
                onNativeMenuRequested: (entry, point) => root.openNativeMenu(entry, point)
            }
        }
    }
}
