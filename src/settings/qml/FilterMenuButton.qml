// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQml.Models
import org.kde.kirigami as Kirigami

/**
 * @brief A "view-filter" tool button whose menu is groups of multi-select
 *        checkboxes, modeled on the Layouts page filter button.
 *
 * Data-driven via `groups`: an array of groups, each an array of
 * `{ key, label, count? }` entries. Each group is rendered as a run of
 * checkable items, with a separator between groups and a separator + Reset
 * action at the bottom — matching LayoutFilterBar's grouped filter menu.
 *
 * `excluded` holds the keys the user has unchecked (empty = everything shown),
 * so options default to checked and a newly-appearing option shows up checked
 * rather than silently hidden. The button reflects an active state while any
 * option is unchecked.
 *
 * Every row (item, separator, reset) flows through one Repeater + a
 * DelegateChooser so the menu order is purely model-driven — mixing a Repeater
 * with statically-declared trailing items does not order/render reliably.
 *
 *   FilterMenuButton {
 *       id: filterButton
 *       groups: [[{ key: "a", label: "Alpha", count: 3 }, …], …]
 *   }
 *   // consume via filterButton.isIncluded(key) / filterButton.excluded
 */
ToolButton {
    id: root

    /// Grouped checkbox model: `[[{ key, label, count? }, …], …]`. `key` is any
    /// value comparable with ===; `count`, when present, renders as "label (n)".
    /// Groups are visually separated; empty groups are skipped.
    property var groups: []
    /// Keys the user has unchecked. Empty = everything shown. The consumer
    /// reads this (or isIncluded/isExcluded) to drive its own filtering.
    property var excluded: []
    /// Title shown above the menu items.
    property string menuTitle: i18nc("@title:menu", "Filter")

    /// True while any option is unchecked — drives the button's active state.
    readonly property bool hasActiveFilters: excluded.length > 0

    // Flattened render model: checkbox rows ({type:"item"}), group separators
    // and a trailing separator ({type:"separator"}), then the reset action
    // ({type:"reset"}). Built so separators never lead or double up.
    readonly property var _rows: {
        var rows = [];
        for (var g = 0; g < groups.length; ++g) {
            var grp = groups[g];
            if (!grp || grp.length === 0)
                continue;
            if (rows.length > 0)
                rows.push({
                    "type": "separator"
                });
            for (var i = 0; i < grp.length; ++i) {
                var o = grp[i];
                rows.push({
                    "type": "item",
                    "key": o.key,
                    "label": o.label,
                    "count": o.count
                });
            }
        }
        if (rows.length > 0)
            rows.push({
                "type": "separator"
            });
        rows.push({
            "type": "reset"
        });
        return rows;
    }

    /// Whether `key` is currently shown (i.e. not unchecked).
    function isIncluded(key) {
        return excluded.indexOf(key) < 0;
    }
    function isExcluded(key) {
        return excluded.indexOf(key) >= 0;
    }
    function reset() {
        excluded = [];
    }
    // Reassign a fresh array (mutating in place would not notify bindings).
    function _setIncluded(key, included) {
        var next = excluded.slice();
        var idx = next.indexOf(key);
        if (included && idx >= 0)
            next.splice(idx, 1);
        else if (!included && idx < 0)
            next.push(key);
        excluded = next;
    }

    icon.name: "view-filter"
    // Active state is binding-driven, not a user toggle — checkable omitted.
    checked: root.hasActiveFilters
    onClicked: filterMenu.popup()
    Accessible.name: root.hasActiveFilters ? i18nc("@action:button", "Filter (active)") : i18nc("@action:button", "Filter")
    ToolTip.visible: hovered
    ToolTip.delay: Kirigami.Units.toolTipDelay
    ToolTip.text: root.hasActiveFilters ? i18nc("@info:tooltip", "Filters active. Click to change") : i18nc("@info:tooltip", "Filter")

    Menu {
        id: filterMenu

        title: root.menuTitle

        Repeater {
            model: root._rows

            delegate: DelegateChooser {
                role: "type"

                // Checkable filter item. `checked` is held by a Binding so it
                // survives the imperative toggle (checkable otherwise breaks the
                // declarative binding on user click — same idiom as
                // LayoutFilterBar.FilterMenuItem).
                DelegateChoice {
                    roleValue: "item"

                    MenuItem {
                        required property var modelData

                        checkable: true
                        text: (modelData.count !== undefined) ? i18nc("@option:check filter item with item count", "%1 (%2)", modelData.label, modelData.count) : modelData.label
                        Accessible.name: modelData.label
                        onToggled: root._setIncluded(modelData.key, checked)

                        Binding on checked {
                            value: root.isIncluded(modelData.key)
                        }
                    }
                }

                DelegateChoice {
                    roleValue: "separator"
                    MenuSeparator {}
                }

                DelegateChoice {
                    roleValue: "reset"

                    MenuItem {
                        text: i18nc("@action reset all filter checkboxes", "Reset Filters")
                        icon.name: "edit-reset"
                        enabled: root.hasActiveFilters
                        onTriggered: root.reset()
                    }
                }
            }
        }
    }
}
