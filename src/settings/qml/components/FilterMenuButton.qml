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
 * action at the bottom. Used directly by the Rules page and shader browser,
 * and hosted invisibly by LayoutFilterBar for the Layouts page menus.
 *
 * `excluded` holds the keys the user has unchecked (empty = everything shown),
 * so options default to checked and a newly-appearing option shows up checked
 * rather than silently hidden. The button reflects an active state while any
 * option is unchecked. Toggling an option keeps the menu open
 * (StayOpenMenuItem); it closes on Escape, click-outside, or Reset.
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
    /// A host that owns its filter state elsewhere can instead drive this
    /// through a `Binding on excluded` value source (a plain binding would be
    /// severed by the internal reassignment on toggle) and listen to
    /// filterToggled to write its own state back.
    property var excluded: []
    /// Title shown above the menu items.
    property string menuTitle: i18nc("@title:menu", "Filter")
    /// Extra active-state contribution from the host (e.g. a search field
    /// that reset() is expected to clear via resetTriggered). ORed into
    /// hasActiveFilters so the badge and the Reset action reflect it.
    property bool externalFilterActive: false

    /// True while any option is unchecked (or the host reports an external
    /// filter) — drives the button's active state and the Reset action.
    readonly property bool hasActiveFilters: excluded.length > 0 || externalFilterActive

    /// A checkbox was toggled by the user. `excluded` has already been
    /// updated; hosts that own their filter state elsewhere write it here.
    signal filterToggled(var key, bool included)
    /// The Reset action ran. `excluded` has already been cleared; hosts
    /// reset any external state (search text, persisted bools) here.
    signal resetTriggered

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
        resetTriggered();
    }
    /// Open the filter menu without going through the button (for hosts that
    /// keep their own toolbar button and embed this one invisibly).
    function popup() {
        filterMenu.popup();
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
        filterToggled(key, included);
    }

    // Prune stale keys when the option set changes: an excluded key whose
    // option no longer exists in `groups` would keep the button stuck in the
    // active state with no visible row left to re-check. Reassign only when
    // something was actually pruned (same no-spurious-notify idiom as
    // _setIncluded).
    onGroupsChanged: {
        var live = [];
        for (var g = 0; g < groups.length; ++g) {
            var grp = groups[g];
            if (!grp)
                continue;
            for (var i = 0; i < grp.length; ++i)
                live.push(grp[i].key);
        }
        var next = excluded.filter(function (key) {
            return live.indexOf(key) >= 0;
        });
        if (next.length !== excluded.length)
            excluded = next;
    }

    icon.name: "view-filter"
    // Active state is binding-driven, not a user toggle — checkable omitted.
    checked: root.hasActiveFilters
    onClicked: popup()
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

                // Checkable filter item that keeps the menu open on toggle
                // (StayOpenMenuItem). `checked` is driven by a `Binding on`
                // that reads the filter set back, so a programmatic change to
                // it (Reset, a host-driven `excluded`) lands on the item.
                //
                // `Binding on checked` rather than a plain `checked:` binding,
                // and the difference is load-bearing. What StayOpenMenuItem
                // has to avoid is AbstractButton's activation path, which
                // emits triggered() and dismisses the menu; the way it does
                // that is to flip `checked` with a JS assignment and re-emit
                // toggled() by hand. A JS write severs a plain binding for
                // good, and `Binding on` is a value source that survives it.
                // Without it, Reset Filters would stop reaching any item the
                // user had clicked.
                DelegateChoice {
                    roleValue: "item"

                    StayOpenMenuItem {
                        required property var modelData

                        text: (modelData.count !== undefined) ? i18nc("@option:check filter item with item count", "%1 (%2)", modelData.label, modelData.count) : modelData.label
                        Accessible.name: modelData.label
                        onToggled: root._setIncluded(modelData.key, checked)

                        // Reads the filter set back onto the item. RestoreNone
                        // is stated rather than inherited: this binding
                        // carries no `when` and so never deactivates, and a
                        // delegate being torn down has nothing worth
                        // restoring onto.
                        Binding on checked {
                            value: root.isIncluded(modelData.key)
                            restoreMode: Binding.RestoreNone
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
