// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtTest
import Phosphor.Dashboard
import Phosphor.Theme

Item {
    id: root
    width: 1440
    height: 900
    property var savedSettings: ({})

    TextInput {
        id: outside
        objectName: "outsideReference"
        x: 1350
        y: 850
        width: 80
        height: 24
        activeFocusOnTab: true
    }
    Component {
        id: mapComponent
        QtObject {
            property int mode: 1
            property int currentDesktop: 0
            property rect workArea: Qt.rect(0, 0, 1440, 900)
            property var cells: []
            signal changed
        }
    }
    Component {
        id: sheetComponent
        Cheatsheet {
            width: 1440
            height: 900
            layoutsAvailable: false
            workspaceName: qsTr("Develop")
        }
    }
    Component {
        id: effectsComponent
        QtObject {
            property var target: null
            property rect region: Qt.rect(0, 0, 0, 0)
            property rect excluded: Qt.rect(0, 0, 0, 0)
            property real radius: 0
            property int callCount: 0
            function setBlurBehind(item, bounds, exclusion, cornerRadius) {
                target = item;
                region = bounds;
                excluded = exclusion;
                radius = cornerRadius;
                callCount++;
            }
        }
    }

    TestCase {
        id: tests
        name: "ShortcutReference"
        when: windowShown

        SignalSpy {
            id: closeSpy
            signalName: "closeRequested"
        }
        SignalSpy {
            id: retrySpy
            signalName: "retryRequested"
        }
        SignalSpy {
            id: releaseSpy
            signalName: "released"
        }

        function initTestCase() {
            root.savedSettings = JSON.parse(JSON.stringify(AppearanceStore.values));
        }
        function init() {
            verify(AppearanceStore.applyPreset("phosphor"));
            verify(AppearanceStore.setValue("motion", false));
            verify(AppearanceStore.setValue("textScale", 100));
            closeSpy.clear();
            retrySpy.clear();
            releaseSpy.clear();
        }
        function cleanup() {
            closeSpy.target = null;
            retrySpy.target = null;
            releaseSpy.target = null;
            verify(AppearanceStore.setValues(root.savedSettings));
        }
        function row(id, label, mode, triggers, description) {
            return {
                id: id,
                label: label,
                mode: mode,
                triggers: triggers,
                assigned: triggers.length > 0,
                category: qsTr("Test actions"),
                categoryOrder: 4,
                rowOrder: 0,
                description: description || ""
            };
        }
        function catalog() {
            return [row("focus_zone_left", qsTr("Focus left"), "all", ["Alt+Shift+Left"], qsTr("Focus the neighboring window on the left.")), row("focus_zone_right", qsTr("Focus right"), "all", ["Alt+Shift+Right"]), row("focus_zone_up", qsTr("Focus up"), "all", ["Alt+Shift+Up"]), row("focus_zone_down", qsTr("Focus down"), "all", ["Alt+Shift+Down"]), row("focus_master", qsTr("Focus master"), "autotile", ["Meta+Shift+M"]), row("swap_master", qsTr("Swap with master"), "autotile", []), row("scroll_center_column", qsTr("Center column"), "scrolling", ["Meta+Alt+C"]), row("scroll_increase_column_width", qsTr("Increase column width"), "scrolling", []), row("span_window_left", qsTr("Extend left"), "snapping", ["Ctrl+Alt+Left"]), row("next_layout", qsTr("Next layout"), "layouts", ["Meta+Alt+]"]), row("open_settings", qsTr("Open settings"), "all", ["Meta+Shift+P"])];
        }
        function child(owner, name) {
            const item = findChild(owner, name);
            verify(!!item, "Object exists");
            return item;
        }
        function scene(properties, rows) {
            const map = createTemporaryObject(mapComponent, root);
            verify(!!map, "Object exists");
            const sheet = createTemporaryObject(sheetComponent, root, Object.assign({
                map: map,
                catalog: rows || catalog(),
                open: true
            }, properties || {}));
            verify(!!sheet, "Component exists");
            tryCompare(sheet, "progress", 1);
            verify(waitForRendering(sheet));
            return sheet;
        }
        function click(owner, name) {
            const item = child(owner, name);
            verify(waitForRendering(item));
            mouseClick(item, item.width / 2, item.height / 2);
            return item;
        }
        function actionIds(sheet) {
            const ids = [];
            for (const group of sheet.groups) {
                for (const entry of group.rows) {
                    if (entry.family) {
                        for (const member of entry.children)
                            ids.push(member.id);
                    } else {
                        ids.push(entry.id);
                    }
                }
            }
            return ids;
        }
        function belongsTo(item, ancestor) {
            while (item && item !== ancestor)
                item = item.parent;
            return item === ancestor;
        }
        function focusDescription(item) {
            return item ? String(item) + " objectName=" + JSON.stringify(item.objectName) : "null";
        }
        function typeSearchKeys(keys) {
            for (const key of keys) {
                keyPress(key);
                keyRelease(key);
            }
        }

        function test_openFocusesSearchAndUsesCurrentMode() {
            const sheet = scene();
            const search = child(sheet, "shortcutSearch");
            tryCompare(search, "activeFocus", true);
            compare(sheet.scope, "tiling");
            verify(actionIds(sheet).includes("focus_master"));
            verify(!actionIds(sheet).includes("scroll_center_column"));
            compare(sheet.filter, "");
        }

        function test_searchEditingClearAndEscape() {
            const sheet = scene();
            closeSpy.target = sheet;
            const search = child(sheet, "shortcutSearch");
            search.focus = true;
            sheet.focusSearch();
            tryCompare(search, "activeFocus", true);
            typeSearchKeys([Qt.Key_C, Qt.Key_O, Qt.Key_L, Qt.Key_1, Qt.Key_Slash]);
            tryCompare(sheet, "filter", "col1/");
            click(sheet, "shortcutClearSearch");
            tryCompare(sheet, "filter", "");
            tryCompare(search, "activeFocus", true);
            typeSearchKeys([Qt.Key_M]);
            tryCompare(sheet, "filter", "m");
            keyClick(Qt.Key_Escape);
            tryCompare(sheet, "filter", "");
            compare(closeSpy.count, 0);
            keyClick(Qt.Key_Escape);
            tryCompare(closeSpy, "count", 1);
        }

        function test_browsingScopesDoesNotChangeWorkspaceMode() {
            const sheet = scene();
            for (const scope of ["scrolling", "snapping", "general", "shell", "all", "tiling"]) {
                click(sheet, "shortcutScope:" + scope);
                tryCompare(sheet, "scope", scope);
                compare(sheet.map.mode, 1);
                compare(sheet.map.currentDesktop, 0);
            }
            tryCompare(sheet, "scope", "tiling");
            verify(actionIds(sheet).includes("focus_master"));
            verify(!actionIds(sheet).includes("scroll_center_column"));
        }

        function test_assignedFilterAndRecovery() {
            const rows = [row("focus_master", qsTr("Focus master"), "autotile", ["Meta+M"]), row("swap_master", qsTr("Swap master"), "autotile", [])];
            const sheet = scene({
                showGuide: false
            }, rows);
            compare(sheet.actionCount, 2);
            click(sheet, "shortcutAssignedOnly");
            tryCompare(sheet, "assignedOnly", true);
            tryCompare(sheet, "actionCount", 1);
            compare(actionIds(sheet), ["focus_master"]);
            sheet.catalog = rows.map(entry => Object.assign({}, entry, {
                    triggers: [],
                    assigned: false
                }));
            tryCompare(sheet, "actionCount", 0);
            tryCompare(child(sheet, "shortcutEmptyTitle"), "text", qsTr("No assigned shortcuts here."));
            click(sheet, "shortcutRecover");
            tryCompare(sheet, "assignedOnly", false);
            tryCompare(sheet, "actionCount", 2);
        }

        function test_unavailableRetryLoadingAndValidEmpty() {
            const sheet = scene({
                catalogAvailable: false,
                showGuide: false
            });
            retrySpy.target = sheet;
            tryCompare(child(sheet, "shortcutEmptyTitle"), "text", qsTr("Shortcuts aren’t available yet."));
            click(sheet, "shortcutRecover");
            tryCompare(retrySpy, "count", 1);
            sheet.catalogAvailable = true;
            sheet.catalogLoading = true;
            tryCompare(child(sheet, "shortcutEmptyTitle"), "text", qsTr("Loading your shortcuts…"));
            tryCompare(child(sheet, "shortcutRecover"), "visible", false);
            sheet.catalogLoading = false;
            sheet.catalogError = qsTr("The service returned an invalid catalog.");
            tryCompare(sheet, "unavailable", true);
            tryCompare(child(sheet, "shortcutRecover"), "visible", true);
            sheet.catalogError = "";
            sheet.catalog = [];
            tryCompare(sheet, "unavailable", false);
            tryCompare(sheet, "actionCount", 0);
            tryCompare(child(sheet, "shortcutEmptyTitle"), "text", qsTr("No shortcuts in this section."));
            tryCompare(child(sheet, "shortcutRecover"), "visible", false);
        }

        function test_familyExpansionDetailsAndLiveCatalogUpdate() {
            const sheet = scene({
                showGuide: false
            }, catalog().slice(0, 4));
            const familyId = "family:focus_zone";
            const family = child(sheet, "shortcutEntry:" + familyId);
            compare(family.expanded, false);
            click(sheet, "shortcutSummary:" + familyId);
            tryCompare(family, "expanded", true);
            tryVerify(() => !!findChild(sheet, "shortcutSummary:focus_zone_left"));
            click(sheet, "shortcutSummary:focus_zone_left");
            const details = child(sheet, "shortcutDetails:focus_zone_left");
            tryCompare(details, "active", true);
            tryCompare(details, "status", Loader.Ready);
            tryVerify(() => details.item && details.item.height > 0);
            verify(sheet.expandedRows[familyId]);
            verify(sheet.expandedRows.focus_zone_left);
            sheet.catalog = sheet.catalog.map(entry => Object.assign({}, entry, {
                    description: qsTr("Updated shortcut description.")
                }));
            verify(waitForRendering(sheet));
            tryVerify(() => findChild(sheet, "shortcutEntry:focus_zone_left")?.entry.description === qsTr("Updated shortcut description."));
            tryCompare(child(sheet, "shortcutEntry:" + familyId), "expanded", true);
            tryCompare(child(sheet, "shortcutDetails:focus_zone_left"), "active", true);
            compare(child(sheet, "shortcutEntry:focus_zone_left").entry.description, qsTr("Updated shortcut description."));
        }

        function test_alternateBindingsReachKeyRenderer() {
            const rows = [row("focus_zone_left", qsTr("Focus left"), "all", ["Meta+H", "Alt+Shift+Left"])];
            const sheet = scene({
                showGuide: false
            }, rows);
            const keys = child(sheet, "shortcutBindings:focus_zone_left");
            compare(JSON.stringify(keys.bindings), JSON.stringify([["Meta", "H"], ["Alt", "Shift", "←"]]));
            verify(keys.visible);
            verify(keys.width > 0);
            verify(keys.implicitHeight >= keys.keyHeight);
            sheet.filter = "meta shift h";
            tryCompare(sheet, "actionCount", 0);
            sheet.filter = "super h";
            tryCompare(sheet, "actionCount", 1);
            compare(actionIds(sheet), ["focus_zone_left"]);
        }

        function test_keycapsResizeWithLiveTextScale() {
            const sheet = scene({
                showGuide: false
            }, [row("focus_master", qsTr("Focus master"), "autotile", ["Meta+Shift+M"])]);
            const keys = child(sheet, "shortcutBindings:focus_master");
            const originalWidth = keys.naturalWidth;
            for (const percent of [115, 110, 90, 100]) {
                verify(AppearanceStore.setValue("textScale", percent));
                verify(waitForRendering(sheet));
                tryVerify(() => Math.abs(keys.implicitHeight - keys.keyHeight) < 1);
                if (percent > 100)
                    verify(keys.naturalWidth > originalWidth);
            }
        }

        function test_keyAliasQueries_data() {
            return [
                {
                    tag: "return",
                    id: "swap_master",
                    trigger: "Meta+Shift+Return",
                    query: "windows key shift enter"
                },
                {
                    tag: "escape",
                    id: "restore_window_size",
                    trigger: "Meta+Alt+Escape",
                    query: "super alt esc"
                },
                {
                    tag: "page-down",
                    id: "scroll_cycle_column_width_back",
                    trigger: "Meta+Alt+PgDown",
                    query: "meta option page down"
                },
                {
                    tag: "plus",
                    id: "increase_master_count",
                    trigger: "Meta+Ctrl++",
                    query: "meta control plus"
                }
            ];
        }
        function test_keyAliasQueries(data) {
            const sheet = scene({
                showGuide: false
            }, [row(data.id, qsTr("Fixture action"), "all", [data.trigger])]);
            sheet.scope = "all";
            sheet.filter = data.query;
            tryCompare(sheet, "actionCount", 1);
            compare(actionIds(sheet), [data.id]);
            tryVerify(() => !!findChild(sheet, "shortcutBindings:" + data.id));
            compare(child(sheet, "shortcutBindings:" + data.id).bindings.length, 1);
        }

        function test_themeAndViewportContainment_data() {
            return [
                {
                    tag: "phosphor",
                    preset: "phosphor",
                    width: 1440,
                    height: 900,
                    scale: 100
                },
                {
                    tag: "ember-large",
                    preset: "ember",
                    width: 1024,
                    height: 600,
                    scale: 115
                },
                {
                    tag: "paper-small",
                    preset: "paper",
                    width: 640,
                    height: 480,
                    scale: 115
                }
            ];
        }
        function test_themeAndViewportContainment(data) {
            verify(AppearanceStore.applyPreset(data.preset));
            verify(AppearanceStore.setValue("textScale", data.scale));
            const sheet = scene({
                width: data.width,
                height: data.height
            });
            const card = child(sheet, "shortcutSheet");
            verify(card.width > 0 && card.height > 0);
            verify(card.x >= 0 && card.y >= 0);
            verify(card.x + card.width <= sheet.width + 1);
            verify(card.y + card.height <= sheet.height + 1);
            for (const name of ["shortcutSearch", "shortcutClose", "shortcutAssignedOnly", "shortcutViewport"]) {
                const item = child(sheet, name);
                const rect = item.mapToItem(card, 0, 0, item.width, item.height);
                verify(rect.width > 0 && rect.height > 0);
                verify(rect.x >= -1 && rect.y >= -1);
                verify(rect.x + rect.width <= card.width + 1);
                verify(rect.y + rect.height <= card.height + 1);
            }
            fuzzyCompare(child(sheet, "shortcutSearch").font.pixelSize, 12 * data.scale / 100, 1);
        }

        function test_tabAndBacktabRemainInsideReference() {
            const sheet = scene({
                showGuide: false
            }, [row("focus_master", qsTr("Focus master"), "autotile", ["Meta+M"])]);
            const search = child(sheet, "shortcutSearch");
            sheet.focusSearch();
            tryCompare(search, "activeFocus", true);
            for (const backward of [false, true]) {
                let steps = 0;
                do {
                    const previous = root.Window.window.activeFocusItem;
                    keyClick(backward ? Qt.Key_Backtab : Qt.Key_Tab, backward ? Qt.ShiftModifier : Qt.NoModifier);
                    const context = (backward ? "Backtab" : "Tab") + " step " + steps + "; previous=" + focusDescription(previous) + "; current=" + focusDescription(root.Window.window.activeFocusItem);
                    tryVerify(() => root.Window.window.activeFocusItem !== previous && belongsTo(root.Window.window.activeFocusItem, sheet), 5000, context);
                    steps++;
                } while (!search.activeFocus && steps < 80)
                verify(steps > 3 && steps < 80, (backward ? "Backtab" : "Tab") + " did not complete a full focus cycle in " + steps + " steps; current=" + focusDescription(root.Window.window.activeFocusItem));
                compare(search.activeFocus, true);
            }
            compare(outside.activeFocus, false);
        }

        function test_materialTracksSheetGeometryAndClearsWhenHidden() {
            const effects = createTemporaryObject(effectsComponent, root);
            verify(!!effects, "Blur target exists");
            const sheet = scene({
                surfaceEffects: effects,
                x: 23,
                y: 17,
                width: 1100,
                height: 700
            });
            const card = child(sheet, "shortcutSheet");
            tryCompare(sheet, "materialBlurred", true);
            tryVerify(() => effects.callCount > 0);
            compare(effects.target, sheet);
            compare(sheet.materialRect, Qt.rect(card.x, card.y, card.width, card.height));
            tryCompare(effects, "region", card.mapToItem(null, 0, 0, card.width, card.height));
            compare(effects.excluded, Qt.rect(0, 0, 0, 0));
            compare(effects.radius, sheet.materialRadius);
            verify(effects.region.width > 0 && effects.region.width < sheet.width);
            verify(effects.region.height > 0 && effects.region.height < sheet.height);

            const beforeResize = effects.callCount;
            sheet.width = 800;
            sheet.height = 540;
            tryCompare(effects, "region", card.mapToItem(null, 0, 0, card.width, card.height));
            verify(effects.callCount > beforeResize);
            compare(sheet.materialRect, Qt.rect(card.x, card.y, card.width, card.height));

            verify(AppearanceStore.setValue("material", "solid"));
            tryCompare(sheet, "materialBlurred", false);
            tryCompare(effects, "region", Qt.rect(0, 0, 0, 0));
            verify(AppearanceStore.setValue("material", "glass"));
            tryCompare(sheet, "materialBlurred", true);
            tryCompare(effects, "region", card.mapToItem(null, 0, 0, card.width, card.height));
            sheet.open = false;
            tryCompare(sheet, "visible", false);
            tryCompare(sheet, "materialBlurred", false);
            tryCompare(effects, "region", Qt.rect(0, 0, 0, 0));
        }

        function test_focusedRowsScrollIntoView() {
            const rows = [];
            for (let index = 0; index < 24; ++index)
                rows.push(row("fixture_action_" + index, qsTr("Action %1").arg(index), "all", ["Meta+Ctrl+F1"]));
            const sheet = scene({
                width: 900,
                height: 500,
                showGuide: false
            }, rows);
            sheet.scope = "general";
            tryCompare(sheet, "actionCount", 24);
            const viewport = child(sheet, "shortcutViewport");
            tryVerify(() => viewport.contentHeight > viewport.height);
            const last = child(sheet, "shortcutSummary:fixture_action_23");
            verify(waitForRendering(sheet));
            last.focus = true;
            last.forceActiveFocus(Qt.TabFocusReason);
            tryCompare(last, "activeFocus", true);
            tryVerify(() => viewport.contentY > 0);
            const point = last.mapToItem(viewport, 0, 0);
            verify(point.y >= -1);
            verify(point.y + last.height <= viewport.height + 1);
            const first = child(sheet, "shortcutSummary:fixture_action_0");
            first.focus = true;
            first.forceActiveFocus(Qt.BacktabFocusReason);
            tryCompare(first, "activeFocus", true);
            tryVerify(() => first.mapToItem(viewport, 0, 0).y >= -1 && first.mapToItem(viewport, 0, 0).y + first.height <= viewport.height + 1);
            compare(first.activeFocus, true);
        }

        function test_closeButtonRequestsDismissal() {
            const sheet = scene();
            closeSpy.target = sheet;
            click(sheet, "shortcutClose");
            tryCompare(closeSpy, "count", 1);
        }

        function test_closingEmitsReleasedAfterAnimation() {
            verify(AppearanceStore.setValue("motion", true));
            const sheet = scene();
            releaseSpy.target = sheet;
            sheet.open = false;
            tryCompare(releaseSpy, "count", 1);
            compare(sheet.progress, 0);
            compare(sheet.visible, false);
            sheet.open = true;
            tryCompare(sheet, "progress", 1);
            sheet.open = false;
            tryCompare(releaseSpy, "count", 2);
            compare(sheet.visible, false);
        }
    }
}
