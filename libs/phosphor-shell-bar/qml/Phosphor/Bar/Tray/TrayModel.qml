// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
pragma Singleton
import QtQuick
import Phosphor.Theme
import Phosphor.Service.Sni

QtObject {
    id: root
    property StatusNotifierHost host: StatusNotifierHost {}
    property StatusNotifierItemModel source: StatusNotifierItemModel {
        host: root.host
    }
    property TrayItems presentation: TrayItems {
        source: root.source
        order: Appearance.settings.trayOrder
        visibility: Appearance.settings.trayVisibility
        maximumBarIcons: Appearance.settings.trayLimit
        attentionPromotion: Appearance.settings.trayAttention
    }
    readonly property var items: presentation.items
    readonly property var barItems: presentation.barItems
    function lookup(key: string): var {
        void items;
        return presentation.itemByInstanceKey(key);
    }
    function title(entry): string {
        return entry.title || entry.toolTipTitle || entry.itemId || qsTr("Background app");
    }
    function status(entry): string {
        return entry.toolTipBody || entry.toolTipTitle || (entry.attention ? qsTr("Needs attention") : qsTr("Running"));
    }
    function setVisibility(key: string, value: string): void {
        const values = Object.assign({}, Appearance.settings.trayVisibility);
        values[key] = value;
        AppearanceStore.setValue("trayVisibility", values);
    }
    function move(key: string, before: string): void {
        if (key === before)
            return;
        const order = [...new Set(items.map(entry => entry.preferenceKey).concat(Appearance.settings.trayOrder))].filter(value => value !== key);
        const index = order.indexOf(before);
        order.splice(index < 0 ? order.length : index, 0, key);
        AppearanceStore.setValue("trayOrder", order);
    }
    function step(key: string, direction: int): void {
        const order = [...new Set(items.map(entry => entry.preferenceKey))];
        const index = order.indexOf(key), next = index + direction;
        if (index < 0 || next < 0 || next >= order.length)
            return;
        move(key, direction < 0 ? order[next] : order[next + 1] || "");
    }
    function reset(): void {
        const values = Object.assign({}, AppearanceStore.values);
        Object.assign(values, {
            trayIcons: "symbolic",
            trayLimit: 2,
            trayAttention: true,
            trayOrder: [],
            trayVisibility: {}
        });
        AppearanceStore.setValues(values);
    }
    function primary(entry, point): void {
        if (entry.item)
            entry.item.activate(Math.round(point.x), Math.round(point.y));
    }
    function secondary(entry, point): void {
        if (entry.item)
            entry.item.secondaryActivate(Math.round(point.x), Math.round(point.y));
    }
    function context(entry, point): void {
        if (entry.item)
            entry.item.contextMenu(Math.round(point.x), Math.round(point.y));
    }
}
