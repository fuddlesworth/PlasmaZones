// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable button that opens a cascading category menu.
 *
 * Drives the editor/settings shader choosers and the rule editor's match-
 * field / action-type pickers. The host owns the data: pass in a flat
 * `items` list (each entry: `{ id, name, category? }`), bind `currentId`,
 * and react to `selected(id)`.
 *
 * ## Visual style
 *
 * The picker derives from `ComboBox` so it inherits the desktop style's
 * standard combo chrome (filled background, theme-coloured indicator,
 * rounded border) and lines up next to other settings-page combos
 * (e.g. the timing-mode picker). We replace the default flat-list popup
 * with our cascading category menu by zero-sizing the built-in `popup`
 * and opening `categoryMenu` from `onPressedChanged`.
 *
 * ## Categories
 *
 * The `category` field is split on `/` to build a two-level tree
 * (`"Audio/Reactive"` → `Audio` submenu containing a `Reactive` submenu).
 * Items without a category land in a flat tail below the category
 * submenus.
 *
 * ## "None" entry
 *
 * For hosts where clearing the selection is a first-class action (animation
 * settings), set `includeNoneEntry: true` to prepend an explicit `noneText`
 * row. Selecting it emits `selected("")`. The editor leaves it off
 * because its dialog uses a separate "Enable effect" checkbox.
 *
 * ## Menu lifecycle (Qt 6 use-after-free workaround)
 *
 * Qt 6's QQuickMenu's transition pipeline races with QQmlData destruction
 * when MenuItems are torn down mid-event. We use ItemDelegate (not MenuItem)
 * to bypass Qt's internal `onItemTriggered → dismiss()` cascade, build the
 * menu once and keep it alive across opens, and defer selection through
 * `Qt.callLater` so the click handler returns before the menu hides.
 *
 * Required:
 *   - `items`: var — list of `{ id, name, category?, categoryOrder?,
 *     categoryGroup?, dimmed?, dimReason? }` maps. A `dimmed` item renders
 *     greyed with a warning icon + `dimReason` tooltip (still selectable —
 *     the host surfaces the consequence). `categoryOrder` (int) sorts
 *     top-level categories; uncategorised items sort by name.
 *     `categoryGroup` (string) is used for group separators between
 *     top-level categories.
 *
 * Optional:
 *   - `currentId`: string — drives the checkmark and button label
 *   - `noneId`: string — id treated as "no effect"; checkmark is shown
 *      on this entry when `currentId` matches
 *   - `includeNoneEntry`: bool — prepend a "None" item that selects ""
 *   - `noneText`: string — label for the "None" entry
 *   - `placeholderText`: string — button label when nothing is selected
 */
ComboBox {
    // No `Component.onDestruction` cleanup — Qt's parent-child ownership
    // tears the menu and its dynamically-created children down for us, in
    // the right order. Calling `markDirty()` here would race that
    // cascade: by the time the handler fires, some `_owned` children may
    // already be partially destroyed, and the explicit `child.destroy()`
    // would crash. `markDirty()` is for the in-flight rebuild path
    // (registry reload while the picker stays alive), not for teardown.

    id: root

    required property var items
    property string currentId: ""
    property string noneId: ""
    property bool includeNoneEntry: false
    property string noneText: i18nc("@item:inlistbox", "None")
    property string placeholderText: i18nc("@action:button", "Select…")
    // Icon size for the leaf ItemDelegates' checkmark. The org.kde.desktop
    // MenuItem sizes its content icon from a fixed style metric (this exact
    // expression) and ignores `icon.width`, so the category-submenu checkmark
    // is that size no matter what. Mirror the expression here so the leaf
    // ItemDelegates — whose IconLabel DOES honour `icon.width` — render their
    // checkmark at the identical size in both pointer and touch modes.
    readonly property real _menuIconSize: Kirigami.Settings.hasTransientTouchInput ? Kirigami.Units.iconSizes.smallMedium : Kirigami.Units.iconSizes.small
    // Re-entry guard against rapid press / Space-down events queueing two
    // `showMenu` callbacks before the first one returns. `_opening` is set
    // when we hand off to `Qt.callLater` and cleared on `aboutToShow`.
    property bool _opening: false
    readonly property var _currentItemInfo: {
        if (!items || !currentId)
            return null;

        for (var i = 0; i < items.length; i++) {
            if (items[i] && items[i].id === currentId)
                return items[i];
        }
        return null;
    }
    // The category path ({ top, sub }) of the currently-selected item, or null
    // when nothing is selected or the selection is uncategorised (those items
    // live in the flat tail and carry their own checkmark). Drives the
    // "your selection is in here" checkmark icon on the category submenu that
    // contains it, so the user can tell which submenu holds the current value
    // without hovering into each one. `sub` is "" for a single-level category.
    readonly property var _selectedCategoryPath: {
        if (!items || !currentId)
            return null;

        for (var i = 0; i < items.length; i++) {
            if (!items[i] || items[i].id !== currentId)
                continue;

            var cat = (items[i].category || "").trim();
            if (cat === "")
                return null;

            var slashIdx = cat.indexOf("/");
            var top = slashIdx >= 0 ? cat.substring(0, slashIdx).trim() : cat;
            // Mirror _categoryTree: a leading-slash / empty-top category is
            // treated as uncategorised (flat tail), so it has no submenu to mark.
            if (top === "")
                return null;

            return {
                "top": top,
                "sub": slashIdx >= 0 ? cat.substring(slashIdx + 1).trim() : ""
            };
        }
        return null;
    }
    readonly property var _sortedItems: {
        if (!items || items.length === 0)
            return [];

        var arr = [];
        for (var i = 0; i < items.length; i++) {
            if (items[i])
                arr.push(items[i]);
        }
        arr.sort(function (a, b) {
            var na = (a && a.name !== undefined) ? String(a.name) : "";
            var nb = (b && b.name !== undefined) ? String(b.name) : "";
            return na.localeCompare(nb);
        });
        return arr;
    }
    // { categories: [{name, items, subcategories: [{name, items}]}],
    //   uncategorized: [...] }
    readonly property var _categoryTree: {
        var sorted = root._sortedItems;
        var tree = {};
        var uncategorized = [];
        for (var i = 0; i < sorted.length; i++) {
            var s = sorted[i];
            var cat = (s.category || "").trim();
            if (cat === "") {
                uncategorized.push(s);
                continue;
            }
            var slashIdx = cat.indexOf("/");
            var top = slashIdx >= 0 ? cat.substring(0, slashIdx).trim() : cat;
            var sub = slashIdx >= 0 ? cat.substring(slashIdx + 1).trim() : "";
            if (top === "") {
                uncategorized.push(s);
                continue;
            }
            if (!tree[top])
                tree[top] = {
                    "direct": [],
                    "subcats": {},
                    "order": Infinity,
                    "group": (s.categoryGroup || "")
                };

            // The bucket creator may lack a categoryGroup while a later item
            // in the same top category carries one — backfill so the group is
            // captured regardless of name-sort order.
            if (tree[top].group === "" && s.categoryGroup)
                tree[top].group = s.categoryGroup;

            // Track the smallest explicit `categoryOrder` seen for this top
            // category — hosts that supply it (the rule editor's field/action
            // pickers) get that order; hosts that don't (shaders) fall back to
            // alphabetical via the Infinity default.
            var ord = (s.categoryOrder !== undefined && s.categoryOrder !== null) ? Number(s.categoryOrder) : Infinity;
            if (ord < tree[top].order)
                tree[top].order = ord;

            if (sub === "") {
                tree[top].direct.push(s);
            } else {
                if (!tree[top].subcats[sub])
                    tree[top].subcats[sub] = [];

                tree[top].subcats[sub].push(s);
            }
        }
        var keys = Object.keys(tree);
        keys.sort(function (a, b) {
            // Explicit categoryOrder first (rule editor pickers), then
            // alphabetical (shaders, and ties).
            var oa = tree[a].order, ob = tree[b].order;
            if (oa !== ob)
                return oa - ob;
            return a.localeCompare(b);
        });
        var categories = [];
        for (var k = 0; k < keys.length; k++) {
            var node = tree[keys[k]];
            var subKeys = Object.keys(node.subcats);
            subKeys.sort(function (a, b) {
                return a.localeCompare(b);
            });
            var subcategories = [];
            for (var si = 0; si < subKeys.length; si++) {
                subcategories.push({
                    "name": subKeys[si],
                    "items": node.subcats[subKeys[si]]
                });
            }
            categories.push({
                "name": keys[k],
                "items": node.direct,
                "subcategories": subcategories,
                "group": node.group
            });
        }
        return {
            "categories": categories,
            "uncategorized": uncategorized
        };
    }

    signal selected(string id)

    function _requestOpenMenu() {
        if (_opening || categoryMenu.visible)
            return;

        _opening = true;
        Qt.callLater(categoryMenu.showMenu);
    }

    // Empty model — the click opens our cascading menu instead of the
    // default flat-list popup. ComboBox refuses to render its chrome
    // when `model` is undefined, so an empty array is the canonical
    // "no items, but I'm still a ComboBox" form.
    model: []
    // Layout attachment is the caller's call — the shader choosers want
    // `Layout.fillWidth: true`; the rule editor's field/action pickers want a
    // content-sized width. So this component does NOT force fillWidth.
    // We borrow ComboBox visuals but the user-facing semantics — and the
    // accessibility role — are "button that opens a categorised menu", not
    // "select-from-flat-list combo". Setting role explicitly stops AT-SPI
    // from announcing "combobox 0 of 0" against our zero-sized internal
    // popup; instead screen readers announce the picker as a button whose
    // label is the currently-selected item's name.
    Accessible.role: Accessible.Button
    Accessible.name: displayText
    ToolTip.text: i18nc("@info:tooltip", "Choose from the categorized list")
    ToolTip.visible: hovered && !categoryMenu.visible
    ToolTip.delay: Kirigami.Units.toolTipDelay
    displayText: {
        var info = root._currentItemInfo;
        if (info && info.name)
            return info.name;

        if (root.includeNoneEntry && (root.currentId === "" || root.currentId === root.noneId))
            return root.noneText;

        // Non-empty `currentId` that doesn't match any entry in the
        // items list — pack uninstalled out from under us. Make it
        // visible rather than silently rendering the placeholder.
        if (root.currentId !== "" && root.currentId !== root.noneId)
            return i18nc("@info item missing", "(missing: %1)", root.currentId);

        return root.placeholderText;
    }
    onPressedChanged: {
        if (pressed)
            _requestOpenMenu();
    }
    // Keyboard activation — the default ComboBox path opens its (suppressed)
    // popup on Space / Return / Enter; without an explicit handler, keyboard
    // users would focus the picker but be unable to open it. Mirror the
    // mouse path through `_requestOpenMenu`.
    Keys.onSpacePressed: function (event) {
        _requestOpenMenu();
        event.accepted = true;
    }
    Keys.onReturnPressed: function (event) {
        _requestOpenMenu();
        event.accepted = true;
    }
    Keys.onEnterPressed: function (event) {
        _requestOpenMenu();
        event.accepted = true;
    }
    // Force rebuild on items change (registry reload) — _built guard would
    // otherwise pin the menu to whatever was loaded at first popup. While the
    // menu is OPEN, markDirty() would destroy the children the user is looking
    // at (the menu visibly empties); defer the rebuild to onClosed instead.
    onItemsChanged: {
        if (categoryMenu.visible)
            categoryMenu._pendingDirty = true;
        else
            categoryMenu.markDirty();
    }

    // Use ItemDelegate (not MenuItem) so Qt's onItemTriggered → dismiss()
    // cascade never fires; the menu hides via explicit Qt.callLater below.
    Component {
        id: menuItemComponent

        ItemDelegate {
            property string itemId
            property bool isSelected: false
            // Optional per-item "incompatible" state — dimmed + a warning icon
            // + an explanatory tooltip. The item stays selectable (the host
            // surfaces the consequence elsewhere, e.g. the rule editor's row
            // chip); the picker just signals the mismatch up front. The
            // checkmark wins on the current selection so it's still findable.
            property bool itemDimmed: false
            property string dimReason: ""

            opacity: itemDimmed ? 0.45 : 1
            icon.name: isSelected ? "checkmark" : (itemDimmed ? "dialog-warning" : "")
            icon.width: root._menuIconSize
            icon.height: root._menuIconSize
            ToolTip.text: dimReason
            ToolTip.visible: itemDimmed && hovered && dimReason !== ""
            ToolTip.delay: Kirigami.Units.toolTipDelay
            onClicked: categoryMenu.selectItem(itemId)
        }
    }

    Component {
        id: subMenuComponent

        Menu {
            // Opaque palette via window role — `background:` crashes in
            // finalizeExitTransition.
            palette.window: Kirigami.Theme.backgroundColor

            // Empty transitions keep finalizeExitTransition synchronous.
            enter: Transition {}

            exit: Transition {}
        }
    }

    Component {
        id: menuSeparatorComponent

        MenuSeparator {}
    }

    Menu {
        id: categoryMenu

        // Two buckets: `_allItems` is the flat list of selectable
        // ItemDelegates (the picker iterates it on every open to refresh
        // checkmarks). `_owned` is the union of every dynamically-created
        // child, kind-tagged so markDirty() / selectItem can dispatch
        // explicitly instead of inferring child kind from runtime
        // properties. Each entry: `{ obj, owner, kind }` where `owner`
        // is the Menu the child was added to. `Menu.addMenu(submenu)`
        // reparents the submenu QML object to an auto-created MenuItem
        // placeholder, so `entry.obj.parent` points at the placeholder,
        // NOT the containing menu — reading it as the detach target
        // fails the `typeof owner.removeMenu === "function"` test, the
        // submenu stays attached, and a subsequent rebuild appends
        // duplicate entries. Tracking the owner at insertion time
        // avoids the parent-pointer ambiguity entirely.
        property var _allItems: []
        property var _owned: []
        // Category/subcategory submenu items tracked with their identity
        // (`{ menuItem, top, sub }`) so updateChecks() can (re)apply the
        // selected-category checkmark icon on every open. `menuItem` is the
        // MenuItem the parent menu auto-creates for the submenu — we set its
        // `icon.name` to the same themed "checkmark" the leaf ItemDelegates
        // use, so the "your selection is in here" mark is visually identical at
        // both levels. The menu is built once and kept alive, so a selection
        // change between opens must refresh the marks without a full rebuild.
        property var _categorySubmenus: []
        property bool _built: false
        // Set when `items` changes while the menu is open; consumed by
        // onClosed to rebuild against a hidden tree (see onItemsChanged).
        property bool _pendingDirty: false

        function markDirty() {
            // Qt 6 differentiates Menu.removeItem() (works for items +
            // separators) from Menu.removeMenu() (required for nested Menus
            // added via addMenu()). Kind-tagging the owned list lets us
            // dispatch the right detach call without sniffing for
            // `typeof child.addMenu === "function"`, which is fragile to
            // future QML imports that introduce other types with `addMenu`.
            for (var i = _owned.length - 1; i >= 0; --i) {
                var entry = _owned[i];
                if (!entry || !entry.obj || !entry.owner)
                    continue;

                if (entry.kind === "submenu" && typeof entry.owner.removeMenu === "function")
                    entry.owner.removeMenu(entry.obj);
                else if (typeof entry.owner.removeItem === "function")
                    entry.owner.removeItem(entry.obj);
                entry.obj.destroy();
            }
            _owned = [];
            _allItems = [];
            _categorySubmenus = [];
            _built = false;
            root._opening = false;
        }

        function selectItem(id) {
            // Defer everything so the click handler returns before
            // setting visible = false. Setting visible mid-event triggers
            // finalizeExitTransition on a still-active event pipeline.
            Qt.callLater(function () {
                for (var i = 0; i < _owned.length; i++) {
                    var entry = _owned[i];
                    if (entry && entry.kind === "submenu" && entry.obj)
                        entry.obj.visible = false;
                }
                categoryMenu.visible = false;
                // Emit AFTER closing so any host-side reaction (registry
                // reload, refresh) sees a settled menu state. If the host
                // mutates `items` synchronously from the handler, the
                // resulting `markDirty()` runs against a fully-hidden tree.
                root.selected(id);
            });
        }

        // Request the shared menu icon size on an auto-created submenu
        // MenuItem. Styles that size a MenuItem's icon from `icon.width` pick
        // this up; styles that use a fixed metric (org.kde.desktop) ignore it,
        // which is why the leaf ItemDelegates are pinned to that same metric
        // (`_menuIconSize`) instead of the submenu being pinned to theirs. The
        // size is constant; updateChecks() only toggles icon.name on / off.
        function _sizeSubmenuIcon(menuItem) {
            if (!menuItem)
                return;

            menuItem.icon.width = root._menuIconSize;
            menuItem.icon.height = root._menuIconSize;
        }

        function updateChecks() {
            for (var i = 0; i < _allItems.length; i++) {
                var it = _allItems[i];
                if (!it)
                    continue;

                var sel;
                if (it.itemId === "")
                    sel = (root.currentId === "" || root.currentId === root.noneId);
                else
                    sel = (it.itemId === root.currentId);
                it.isSelected = sel;
            }
            // Mark the category (and subcategory) submenu that holds the
            // current selection with the same "checkmark" icon the leaf items
            // use, so the user can see where their value lives without hovering
            // into each submenu. Recomputed on every open because the menu
            // persists across selection changes. A top-level submenu is marked
            // whenever the selection is anywhere inside it (including a nested
            // subcategory); the subcategory submenu is marked only on an exact
            // top+sub match.
            var path = root._selectedCategoryPath;
            for (var k = 0; k < _categorySubmenus.length; k++) {
                var cs = _categorySubmenus[k];
                if (!cs || !cs.menuItem)
                    continue;

                var holdsSelection = false;
                if (path) {
                    if (cs.sub === "")
                        holdsSelection = (path.top === cs.top);
                    else
                        holdsSelection = (path.top === cs.top && path.sub === cs.sub);
                }
                cs.menuItem.icon.name = holdsSelection ? "checkmark" : "";
            }
        }

        // Helpers — every dynamically-created child is tracked in `_owned`
        // with the owning Menu and a `kind` tag so markDirty() can
        // dispatch the right detach call (removeItem vs removeMenu) on
        // the right target. Storing the owner here, rather than reading
        // it back from `child.parent` later, avoids the
        // `Menu.addMenu` reparent-to-placeholder ambiguity that was
        // letting old submenus persist across markDirty.
        function _addItem(menu, props) {
            var item = menuItemComponent.createObject(menu, props);
            menu.addItem(item);
            _allItems.push(item);
            _owned.push({
                "obj": item,
                "owner": menu,
                "kind": "item"
            });
            return item;
        }

        function _addSubmenu(menu, props) {
            var sub = subMenuComponent.createObject(menu, props);
            menu.addMenu(sub);
            _owned.push({
                "obj": sub,
                "owner": menu,
                "kind": "submenu"
            });
            return sub;
        }

        function _addSeparator(menu) {
            var sep = menuSeparatorComponent.createObject(menu);
            menu.addItem(sep);
            _owned.push({
                "obj": sep,
                "owner": menu,
                "kind": "separator"
            });
            return sep;
        }

        // Build the delegate props for one item — carries the optional
        // `dimmed` / `dimReason` fields the host may set (e.g. the action
        // picker marks context-domain actions incompatible with a
        // window-property match).
        function _itemProps(obj) {
            return {
                "text": obj.name,
                "itemId": obj.id,
                "itemDimmed": obj.dimmed === true,
                "dimReason": obj.dimReason || ""
            };
        }

        function showMenu() {
            if (!_built) {
                _built = true;
                if (root.includeNoneEntry) {
                    _addItem(categoryMenu, {
                        "text": root.noneText,
                        "itemId": ""
                    });
                    _addSeparator(categoryMenu);
                }
                var categories = root._categoryTree.categories;
                // Draw a divider where the category group changes (hosts that
                // tag items with `categoryGroup` — e.g. the action picker's
                // context vs window domains). Empty groups never divide.
                var lastGroup = "";
                for (var c = 0; c < categories.length; c++) {
                    var cat = categories[c];
                    var catItems = cat.items || [];
                    var subcats = cat.subcategories || [];
                    // Skip empty top-level categories — produces an empty
                    // submenu the user can hover into for no reason.
                    if (catItems.length === 0 && subcats.length === 0)
                        continue;

                    var group = cat.group || "";
                    if (lastGroup !== "" && group !== "" && group !== lastGroup)
                        _addSeparator(categoryMenu);
                    if (group !== "")
                        lastGroup = group;

                    var subMenu = _addSubmenu(categoryMenu, {
                        "title": cat.name
                    });
                    // The MenuItem the parent auto-creates for the submenu is
                    // the item just appended — capture it so updateChecks() can
                    // toggle its checkmark icon, and pin its icon size to match
                    // the leaf ItemDelegates.
                    var topMenuItem = categoryMenu.itemAt(categoryMenu.count - 1);
                    _sizeSubmenuIcon(topMenuItem);
                    _categorySubmenus.push({
                        "menuItem": topMenuItem,
                        "top": cat.name,
                        "sub": ""
                    });
                    for (var s = 0; s < catItems.length; s++)
                        _addItem(subMenu, _itemProps(catItems[s]));
                    for (var sc = 0; sc < subcats.length; sc++) {
                        var subItems = subcats[sc].items || [];
                        if (subItems.length === 0)
                            continue;

                        var subSubMenu = _addSubmenu(subMenu, {
                            "title": subcats[sc].name
                        });
                        var subMenuItem = subMenu.itemAt(subMenu.count - 1);
                        _sizeSubmenuIcon(subMenuItem);
                        _categorySubmenus.push({
                            "menuItem": subMenuItem,
                            "top": cat.name,
                            "sub": subcats[sc].name
                        });
                        for (var ss = 0; ss < subItems.length; ss++)
                            _addItem(subSubMenu, _itemProps(subItems[ss]));
                    }
                }
                var uncategorized = root._categoryTree.uncategorized;
                if (uncategorized.length > 0 && categories.length > 0)
                    _addSeparator(categoryMenu);

                for (var u = 0; u < uncategorized.length; u++)
                    _addItem(categoryMenu, _itemProps(uncategorized[u]));
            }
            updateChecks();
            categoryMenu.popup(root, 0, root.height);
        }

        onAboutToShow: root._opening = false
        // Apply a deferred items-change rebuild now that the tree is hidden
        // (see onItemsChanged) — markDirty() destroys children safely here.
        onClosed: {
            if (_pendingDirty) {
                _pendingDirty = false;
                markDirty();
            }
        }
        palette.window: Kirigami.Theme.backgroundColor

        enter: Transition {}

        exit: Transition {}
    }

    // Suppress the default popup — `visible: false` plus zero size keeps
    // it inert while still satisfying ComboBox's invariant that
    // `popup` is a real Popup. Our cascading menu is `categoryMenu`
    // below; it opens from `onPressedChanged` (mouse press) and from the
    // Keys handler (Space / Return / Enter on focused ComboBox).
    popup: T.Popup {
        visible: false
        width: 0
        height: 0
    }
}
