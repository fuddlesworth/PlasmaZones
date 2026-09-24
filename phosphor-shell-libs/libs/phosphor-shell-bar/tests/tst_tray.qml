// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtTest
import Phosphor.Bar
import Phosphor.Theme

Item {
    id: root
    width: 1100
    height: 900
    property var savedSettings: ({})

    Component {
        id: panelComponent
        TrayPanel {
            width: implicitWidth
            height: implicitHeight
            property int closeCount: 0
            onCloseRequested: closeCount++
        }
    }
    Component {
        id: drawerComponent
        TrayDrawer {
            width: implicitWidth
            height: implicitHeight
            entries: TrayModel.presentation.overflowItems
        }
    }
    Component {
        id: barHostComponent
        BarHost {
            width: 1100
            height: 90
            visible: false
        }
    }
    Component {
        id: trayWidgetComponent
        Tray {}
    }
    QtObject {
        id: trayRegistry
        function createWidgetFor(id, parent) {
            return id === "tray" ? trayWidgetComponent.createObject(parent) : null;
        }
    }
    Component {
        id: trayRegionComponent
        BarRegion {
            property var host: null
            groups: host ? host.rightGroups : []
            registry: trayRegistry
            maximumWidth: 75
        }
    }

    TestCase {
        id: tests
        name: "TrayInteractions"
        when: windowShown

        function initTestCase() {
            root.savedSettings = JSON.parse(JSON.stringify(AppearanceStore.values));
            verify(trayFixture.start());
        }

        function cleanupTestCase() {
            trayFixture.stop();
        }

        function init() {
            TrayModel.reset();
            seed("everyday", 8);
            verify(trayFixture.call("ClearEvents", []));
        }

        function cleanup() {
            AppearanceStore.setValues(root.savedSettings);
        }

        function seed(scenario, count) {
            verify(!!trayFixture.call("SetScenario", [scenario]));
            const expected = trayFixture.status().apps;
            compare(expected.length, count);
            tryVerify(() => TrayModel.items.length === count && expected.every(app => TrayModel.items.some(entry => entry.dbusService === app.unique && entry.title === app.title)));
        }

        function entry(id) {
            return TrayModel.items.find(item => item.itemId === id);
        }

        function fixtureApp(id) {
            return trayFixture.status().apps.find(app => app.id === id);
        }

        function recorded(kind, app, action) {
            return trayFixture.events().filter(event => event.kind === kind && event.app === app && (action === undefined || event.action === action));
        }

        function control(panel, name) {
            const item = findChild(panel, name);
            verify(!!item, "Object exists");
            return item;
        }

        function panelFor(id) {
            const panel = createTemporaryObject(panelComponent, root, id ? {
                initialMenuKey: entry(id).instanceKey
            } : {});
            verify(!!panel, "Component exists");
            verify(waitForRendering(panel));
            if (id)
                tryVerify(() => !!findChild(panel, "trayMenuItem-1"));
            return panel;
        }

        function fitsHorizontally(item, panel) {
            const point = item.mapToItem(panel, 0, 0);
            return point.x >= -1 && point.x + item.width <= panel.width + 1;
        }

        function test_singlePaneKeepsMenusAndSettingsReachable() {
            const panel = createTemporaryObject(panelComponent, root, {
                width: 340
            });
            verify(!!panel, "Component exists");
            verify(waitForRendering(panel));
            const close = control(panel, "trayClose");
            verify(fitsHorizontally(close, panel));
            const phone = control(panel, "trayApp-" + entry("kdeconnect").preferenceKey);
            mouseClick(phone);
            tryCompare(panel, "hasMenu", true);
            tryCompare(panel, "singlePane", true);
            tryCompare(close, "visible", false);
            tryVerify(() => !!findChild(panel, "trayMenuItem-2"));
            const action = control(panel, "trayMenuItem-2");
            tryVerify(() => action.visible && fitsHorizontally(action, panel));

            panel.width = 720;
            tryCompare(panel, "singlePane", false);
            tryCompare(close, "visible", true);
            verify(fitsHorizontally(close, panel));
            panel.width = 340;
            tryCompare(panel, "singlePane", true);
            tryCompare(close, "visible", false);
            action.forceActiveFocus();
            keyClick(Qt.Key_Escape);
            tryCompare(panel, "hasMenu", false);
            tryCompare(close, "visible", true);
            tryCompare(phone, "activeFocus", true);

            mouseClick(control(panel, "traySettings"));
            tryCompare(panel, "settingsOpen", true);
            tryVerify(() => fitsHorizontally(close, panel));
            const limit = control(panel, "trayLimit");
            tryVerify(() => limit.visible && fitsHorizontally(limit, panel));
            tryCompare(panel, "width", 340);
        }

        function test_trayPreferenceChangesPreserveTheBoundedBarSource() {
            verify(AppearanceStore.setValue("barLayout", {
                left: [],
                center: [],
                right: [["tray"]]
            }));
            verify(AppearanceStore.setValue("trayLimit", 4));
            const pins = ["nextcloud", "steam", "discord"];
            for (const id of pins)
                TrayModel.setVisibility(entry(id).preferenceKey, "pinned");
            tryVerify(() => TrayModel.barItems.length === 3);
            const host = createTemporaryObject(barHostComponent, root);
            verify(!!host, "Component exists");
            const region = createTemporaryObject(trayRegionComponent, root, {
                host: host
            });
            verify(!!region, "Component exists");
            tryCompare(region, "mountedCount", 1);
            const source = region.cellFor("tray").widget;
            verify(!!source, "Object exists");
            tryCompare(source, "capacity", 1);
            const panel = createTemporaryObject(panelComponent, root, {
                sourceWidget: source
            });
            verify(!!panel, "Component exists");
            tryVerify(() => panel.shownItems.length === 1 && panel.overflowItems.length === 7);
            panel.showSettings(true);
            const settings = control(panel, "traySettingsPage");
            tryVerify(() => settings.shownItems.length === 1);
            const originalGroups = host.rightGroups;

            for (const change of [["trayIcons", "color"], ["trayAttention", false], ["trayLimit", 3]]) {
                verify(AppearanceStore.setValue(change[0], change[1]));
                tryVerify(() => Appearance.settings[change[0]] === change[1]);
                compare(host.rightGroups, originalGroups);
                verify(host.rightGroups === originalGroups);
                compare(region.cellFor("tray").widget, source);
                compare(panel.sourceWidget, source);
                compare(panel.shownItems.length, 1);
                compare(panel.overflowItems.length, 7);
                for (const item of TrayModel.barItems.slice(1))
                    verify(panel.overflowItems.some(overflow => overflow.instanceKey === item.instanceKey));
            }
            region.maximumWidth = 160;
            tryCompare(source, "capacity", 3);
            tryVerify(() => panel.shownItems.length === 3 && panel.overflowItems.length === 5);
            tryVerify(() => settings.shownItems.length === 3);
            compare(region.cellFor("tray").widget, source);
        }

        function test_drawerSettingsAndEmptyState() {
            const panel = panelFor();
            compare(panel.overflowItems.length, 8);
            mouseClick(control(panel, "traySettings"));
            tryCompare(panel, "settingsOpen", true);
            const attention = control(panel, "trayAttention");
            verify(waitForRendering(attention));
            mouseClick(attention);
            tryVerify(() => Appearance.settings.trayAttention === false);
            const limit = control(panel, "trayLimit");
            limit.forceActiveFocus();
            keyClick(Qt.Key_End);
            tryVerify(() => Appearance.settings.trayLimit === 4);
            const visibility = control(panel, "trayVisibility-" + entry("nextcloud").preferenceKey);
            visibility.forceActiveFocus();
            keyClick(Qt.Key_Home);
            tryVerify(() => entry("nextcloud").visibility === "pinned");
            panel.showSettings(false);
            tryCompare(panel, "settingsOpen", false);
            tryVerify(() => panel.shownItems.some(item => item.itemId === "nextcloud"));
            seed("empty", 0);
            tryCompare(panel, "overflowItems", []);
            compare(panel.shownItems.length, 0);
        }

        function test_checkboxDispatchUpdatesTheOpenMenu() {
            const panel = panelFor("discord");
            const mute = control(panel, "trayMenuItem-10");
            compare(mute.Accessible.checked, false);
            mouseClick(mute);
            tryVerify(() => recorded("action", "discord", "mute").length === 1);
            tryCompare(mute.Accessible, "checked", true);
            compare(fixtureApp("discord").tooltip, "Notifications muted");
            compare(panel.closeCount, 0);
            mouseClick(mute);
            tryCompare(mute.Accessible, "checked", false);
            tryVerify(() => recorded("action", "discord", "mute").length === 2);
            compare(panel.closeCount, 0);
        }

        function test_keyboardSubmenuRadioAndBack() {
            const panel = panelFor("steam");
            const submenu = control(panel, "trayMenuItem-20");
            submenu.forceActiveFocus();
            keyClick(Qt.Key_Right);
            const model = control(panel, "trayMenuModel");
            tryCompare(model, "rootId", 20);
            tryCompare(model, "count", 4);
            tryVerify(() => !!findChild(panel, "trayMenuItem-22"));
            const online = control(panel, "trayMenuItem-21");
            const away = control(panel, "trayMenuItem-22");
            online.forceActiveFocus();
            keyClick(Qt.Key_Down);
            tryCompare(away, "activeFocus", true);
            keyClick(Qt.Key_Return);
            tryVerify(() => fixtureApp("steam").tooltip === "Away");
            tryCompare(away.Accessible, "checked", true);
            tryCompare(online.Accessible, "checked", false);
            compare(panel.closeCount, 0);
            keyClick(Qt.Key_Left);
            tryCompare(model, "rootId", 0);
            keyClick(Qt.Key_Escape);
            tryCompare(panel, "closeCount", 1);
        }

        function test_disabledActionsAndLazyProjectorSubmenu() {
            verify(trayFixture.call("SetRecording", [true]));
            tryVerify(() => entry("obs").toolTipBody === "Recording · 02:18");
            const panel = panelFor("obs");
            const quit = control(panel, "trayMenuItem-99");
            compare(quit.enabled, false);
            mouseClick(quit);
            tryCompare(quit, "enabled", false);
            compare(recorded("action", "obs", "quit").length, 0);
            mouseClick(control(panel, "trayMenuItem-20"));
            const model = control(panel, "trayMenuModel");
            tryCompare(model, "rootId", 20);
            tryCompare(model, "count", 2);
            tryVerify(() => !!findChild(panel, "trayMenuItem-21"));
            const display = control(panel, "trayMenuItem-21");
            display.forceActiveFocus();
            keyClick(Qt.Key_Return);
            tryVerify(() => recorded("action", "obs", "display-1").length === 1);
            tryCompare(panel, "closeCount", 1);
            compare(fixtureApp("obs").recording, true);
        }

        function test_menuOnlyTileAndKeyboardContextMenu() {
            const panel = panelFor();
            const phone = control(panel, "trayApp-" + entry("kdeconnect").preferenceKey);
            mouseClick(phone);
            tryCompare(panel, "hasMenu", true);
            tryVerify(() => panel.selectedEntry.itemId === "kdeconnect");
            tryVerify(() => !!findChild(panel, "trayMenuItem-2"));
            compare(recorded("activate", "kdeconnect").length, 0);
            compare(panel.closeCount, 0);
            panel.closeMenu();
            tryCompare(panel, "hasMenu", false);
            verify(waitForRendering(phone));
            tryCompare(phone, "activeFocus", true);
            const discord = control(panel, "trayApp-" + entry("discord").preferenceKey);
            discord.forceActiveFocus();
            tryCompare(discord, "activeFocus", true);
            keyClick(Qt.Key_F10, Qt.ShiftModifier);
            tryCompare(panel, "hasMenu", true);
            tryVerify(() => panel.selectedEntry.itemId === "discord");
            compare(recorded("activate", "discord").length, 0);
        }

        function test_primaryTileDispatchesActivation() {
            const panel = panelFor();
            const discord = control(panel, "trayApp-" + entry("discord").preferenceKey);
            mouseClick(discord);
            tryCompare(panel, "closeCount", 1);
            tryVerify(() => recorded("activate", "discord").length === 1);
            compare(recorded("activate", "discord")[0].menuOnly, false);
            compare(panel.hasMenu, false);
        }

        function test_alwaysHiddenAttentionStaysPrivate() {
            const nextcloud = entry("nextcloud").preferenceKey;
            TrayModel.setVisibility(nextcloud, "hidden");
            const drawer = createTemporaryObject(drawerComponent, root);
            verify(!!drawer, "Component exists");
            verify(trayFixture.call("SetStatus", ["nextcloud", "NeedsAttention"]));
            tryVerify(() => entry("nextcloud").attention);
            compare(TrayModel.barItems.some(item => item.itemId === "nextcloud"), false);
            compare(drawer.entries.some(item => item.itemId === "nextcloud"), false);
            compare(!!drawer.attention, false);
            compare(findChild(drawer, "trayApp-" + nextcloud), null);
            TrayModel.setVisibility(nextcloud, "auto");
            tryVerify(() => TrayModel.barItems.some(item => item.itemId === "nextcloud"));
            tryVerify(() => drawer.attention && drawer.attention.itemId === "nextcloud");
            TrayModel.setVisibility(nextcloud, "hidden");
            tryVerify(() => !drawer.attention);
            compare(TrayModel.barItems.some(item => item.itemId === "nextcloud"), false);
        }

        function test_twentyAppsScrollWithinTheDrawer() {
            seed("many20", 20);
            const panel = panelFor();
            const scroll = control(panel, "trayScroll");
            compare(panel.overflowItems.length, 20);
            verify(panel.implicitHeight <= panel.maximumHeight);
            verify(panel.implicitHeight < 800);
            tryVerify(() => scroll.contentHeight > scroll.height);
            const firstButton = control(panel, "trayApp-" + panel.overflowItems[0].preferenceKey);
            mouseWheel(firstButton, firstButton.width / 2, firstButton.height / 2, 0, -240);
            tryVerify(() => scroll.contentY > 0);
            compare(recorded("scroll", panel.overflowItems[0].itemId).length, 0);
            const last = panel.overflowItems[19];
            const lastButton = control(panel, "trayApp-" + last.preferenceKey);
            lastButton.forceActiveFocus();
            tryVerify(() => scroll.contentY > 0);
            const point = lastButton.mapToItem(scroll, 0, 0);
            verify(point.y >= 0 && point.y + lastButton.height <= scroll.height + 1);
            keyClick(Qt.Key_Left);
            const previous = control(panel, "trayApp-" + panel.overflowItems[18].preferenceKey);
            tryCompare(previous, "activeFocus", true);
        }

        function test_removingTheAppClosesItsDirectMenu() {
            const panel = panelFor("steam");
            const key = entry("steam").instanceKey;
            verify(trayFixture.call("Remove", ["steam"]));
            tryVerify(() => !TrayModel.lookup(key).instanceKey);
            tryCompare(panel, "hasMenu", false);
            tryCompare(panel, "closeCount", 1);
            // Loader releases its menu through deferred QObject destruction.
            tryVerify(() => findChild(panel, "trayMenuModel") === null);
        }

        function test_switchingAppsResetsNestedMenuState() {
            const panel = panelFor();
            panel.openMenu(entry("steam"), panel);
            tryVerify(() => !!findChild(panel, "trayMenuItem-20"));
            const submenu = control(panel, "trayMenuItem-20");
            verify(waitForRendering(submenu));
            compare(panel.selectedEntry.itemId, "steam");
            mouseClick(submenu);
            tryCompare(control(panel, "trayMenuModel"), "rootId", 20);
            panel.openMenu(entry("nextcloud"), panel);
            tryVerify(() => panel.selectedEntry.itemId === "nextcloud");
            tryVerify(() => {
                const model = findChild(panel, "trayMenuModel");
                return model && model.valid && model.rootId === 0 && model.service === entry("nextcloud").dbusService;
            });
            tryVerify(() => !!findChild(panel, "trayMenuItem-11"));
            compare(control(panel, "trayMenuItem-11").enabled, true);
            tryVerify(() => trayFixture.events().some(event => event.kind === "menu-event" && event.app === "steam" && event.menu === 20 && event.event === "closed"));
            tryVerify(() => trayFixture.events().some(event => event.kind === "menu-event" && event.app === "nextcloud" && event.menu === 0 && event.event === "opened"));
            compare(panel.closeCount, 0);
        }
    }
}
