// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import "GroupSortLogic.js" as Core
import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Pack-agnostic shader browser page.
 *
 * Read-only listing with drop-zone install card, filter bar, grouped
 * card grid, and a detail dialog. Drives the Animations → Shaders page
 * (for `data/animations/` packs), the Snapping → Shaders page (for
 * `data/shaders/` overlay packs), and the Decoration → Shaders page (for
 * `data/surface/` packs).
 *
 * The host supplies a `bridge` QObject implementing the contract:
 *
 *   QVariantList availableShaderEffects()
 *   bool         installShaderPack(const QString& url)
 *   void         openUserShaderDirectory()
 *   QVariantList shaderEffectUsages(const QString& effectId)
 *
 *   signal shaderEffectsChanged()
 *   signal shaderProfileChanged(const QString& path)  // optional
 *
 * Plus a few labels the host can override to suit its domain (e.g.
 * "Browse installed snapping overlay shaders" vs the animation copy).
 *
 * ## Layout
 *
 *   • Optional info banner (`infoBannerText`).
 *   • User shaders card — drop zone for installing user shader packs +
 *     "Open Folder" button.
 *   • Search row — text search + a multi-select filter button (source
 *     toggles, one checkbox per capability type when the catalogue spans
 *     more than one, and one per category), modeled on the Layouts page.
 *   • Group / sort row — a GroupSortBar to group (Category / Type / Source
 *     / None) and sort (Name / Category / Type) the catalogue.
 *   • Grouped sections — each group renders as a collapsible card of shader
 *     thumbnails (count shown in the header). Card click opens
 *     ShaderBrowserDetailDialog.
 */
SettingsFlickable {
    id: root

    required property var bridge
    /// Distinct QtCore.Settings category so each host page (Animations /
    /// Snapping / Decoration shaders) remembers its own group/sort choice
    /// independently. Hosts should set this to a unique, stable string.
    property string settingsCategory: "ShaderBrowserPage"
    // ── Domain-tuned copy ────────────────────────────────────────────────
    property string infoBannerText: ""
    property string userShadersDescription: i18n("User-installed shader packs live under your data directory. Drop a shader pack folder here to make it available to PlasmaZones.")
    property string dropZoneIdleText: i18nc("@info drop-zone idle label", "Drop a shader pack folder here")
    property string dropZoneHoverText: i18nc("@info drop-zone hover label", "Release to install shader pack")
    property string emptyCatalogueText: i18n("No shader effects installed.")
    /// Closure plumbed through to the detail dialog. Hosts pass an
    /// `i18ncp(..., count)` closure so the plural form is selected with
    /// the LIVE count — pre-baking singular/plural strings here would
    /// break locales with >2 plural forms (Polish, Russian, Arabic, …)
    /// and would lie about `%n` (it would substitute the wrapper's
    /// hard-coded count instead of `_usages.length`).
    property var usageHeaderTextFn: function (count) {
        return i18ncp("@info shader usage section header", "Used in %n event", "Used in %n events", count);
    }
    /// Closure plumbed through to each ShaderBrowserCard's footer chip.
    /// Same plural-form / live-count rationale as `usageHeaderTextFn`,
    /// and lets the card chip stay consistent with the dialog header
    /// (animations: "event"; snapping: "layout"; default: generic "use").
    property var usageChipTextFn: function (count) {
        return i18ncp("@info shader usage count", "%n use", "%n uses", count);
    }
    // Loaded from a Q_INVOKABLE; Connections below manually refreshes it
    // on `shaderEffectsChanged`. Q_INVOKABLE results aren't reactive
    // across the QML binding boundary.
    property var effectList: bridge ? bridge.availableShaderEffects() : []
    // ── Filter state ────────────────────────────────────────────────────
    property string filterText: ""
    // Source + category filters are multi-select checkboxes in the filter
    // button (modeled on the Layouts page). These derive its unchecked-key set
    // into the booleans / predicate the effect filter below consumes:
    //   "src:builtin" / "src:user" gate source; "cat:<name>" gate a category.
    readonly property bool showBuiltIn: !shaderFilterButton.isExcluded("src:builtin")
    readonly property bool showUser: !shaderFilterButton.isExcluded("src:user")
    // ── Type axis (`appliesTo` event-class capability) ──────────────────
    // Ordered catalog of the known event-class capabilities. Universal (an
    // empty `appliesTo`) is the synthetic order-0 bucket resolved in the
    // helpers below. The whole axis — a Type filter group, a Type group-by /
    // sort option, and the card badge — only surfaces when the installed
    // packs actually span more than one type (`_hasTypeAxis`). Snapping and
    // decoration packs are all universal, so those pages look unchanged.
    readonly property var _typeCatalog: [
        {
            "key": "geometry",
            "label": i18nc("@item shader capability", "Geometry"),
            "order": 1
        },
        {
            "key": "move",
            "label": i18nc("@item shader capability (held interactive window drag)", "Drag motion"),
            "order": 2
        },
        {
            "key": "appearance",
            "label": i18nc("@item shader capability", "Appearance"),
            "order": 3
        },
        {
            "key": "desktop",
            "label": i18nc("@item shader capability", "Desktop"),
            "order": 4
        }
    ]
    readonly property string _universalKey: "universal"
    // Distinct type keys present across the catalogue, ordered by catalog
    // order, each with a count. Drives the Type filter group.
    readonly property var _allTypes: {
        var counts = {};
        for (var i = 0; i < effectList.length; i++) {
            if (!effectList[i])
                continue;

            var key = root._effectTypeKey(effectList[i]);
            counts[key] = (counts[key] || 0) + 1;
        }
        var keys = Object.keys(counts);
        keys.sort(function (a, b) {
            return root._typeOrder(a) - root._typeOrder(b);
        });
        var result = [];
        for (var k = 0; k < keys.length; k++)
            result.push({
                "key": keys[k],
                "label": root._typeLabel(keys[k]),
                "count": counts[keys[k]]
            });
        return result;
    }
    readonly property bool _hasTypeAxis: _allTypes.length > 1
    // ── Group / sort options (data-driven; dispatched by option id) ─────
    // The Type option only appears when the type axis is live. Grouping and
    // sorting dispatch on the option `id`, never a raw index, so a model that
    // grows or drops the Type option can never mis-dispatch.
    readonly property var _groupOptions: {
        var opts = [
            {
                "id": "category",
                "label": i18nc("@item:inlistbox group shaders by", "Category")
            }
        ];
        if (root._hasTypeAxis)
            opts.push({
                "id": "type",
                "label": i18nc("@item:inlistbox group shaders by", "Type")
            });

        opts.push({
            "id": "source",
            "label": i18nc("@item:inlistbox group shaders by", "Source")
        });
        opts.push({
            "id": "none",
            "label": i18nc("@item:inlistbox group shaders by", "None")
        });
        return opts;
    }
    readonly property var _sortOptions: {
        var opts = [
            {
                "id": "name",
                "label": i18nc("@item:inlistbox sort shaders by", "Name")
            },
            {
                "id": "category",
                "label": i18nc("@item:inlistbox sort shaders by", "Category")
            }
        ];
        if (root._hasTypeAxis)
            opts.push({
                "id": "type",
                "label": i18nc("@item:inlistbox sort shaders by", "Type")
            });

        return opts;
    }
    property int groupByIndex: 0
    property int sortByIndex: 0
    property bool sortAscending: true
    readonly property string _groupId: (_groupOptions[groupByIndex] || _groupOptions[0]).id
    readonly property string _sortId: (_sortOptions[sortByIndex] || _sortOptions[0]).id
    // ── Derived: category index (sorted, with counts) ───────────────────
    readonly property var _allCategories: {
        var counts = {};
        for (var i = 0; i < effectList.length; i++) {
            var cat = (effectList[i] && effectList[i].category) ? effectList[i].category : "";
            if (cat.length === 0)
                continue;

            counts[cat] = (counts[cat] || 0) + 1;
        }
        var keys = Object.keys(counts);
        keys.sort(function (a, b) {
            return a.localeCompare(b);
        });
        var result = [];
        for (var k = 0; k < keys.length; k++)
            result.push({
                "name": keys[k],
                "count": counts[keys[k]]
            });
        return result;
    }
    // ── Derived: filtered + grouped effects ─────────────────────────────
    readonly property var _filteredEffects: {
        var needle = root.filterText.trim().toLowerCase();
        var out = [];
        for (var i = 0; i < effectList.length; i++) {
            var e = effectList[i];
            if (!e)
                continue;

            var isUser = !!e.isUserEffect;
            if (isUser && !root.showUser)
                continue;

            if (!isUser && !root.showBuiltIn)
                continue;

            var cat = e.category || "";
            if (cat.length > 0 && shaderFilterButton.isExcluded("cat:" + cat))
                continue;

            if (root._hasTypeAxis && shaderFilterButton.isExcluded("type:" + root._effectTypeKey(e)))
                continue;

            if (needle.length > 0) {
                var hay = (String(e.name || "") + " " + String(e.id || "") + " " + String(e.description || "") + " " + String(e.category || "") + " " + String(e.author || "")).toLowerCase();
                if (hay.indexOf(needle) === -1)
                    continue;
            }
            out.push(e);
        }
        return out;
    }
    /// Filtered effects grouped and sorted per the toolbar selection, as an
    /// ordered `[{label, items}]` array ready for the section Repeater. Reuses
    /// the neutral GroupSortLogic primitives (Core); the groupers stay here
    /// because their bucket labels need the QML i18n context.
    readonly property var _displayGroups: {
        var groups = root._buildGroups(root._filteredEffects, root._groupId);
        Core.applySort(groups, root._comparatorFor(root._sortId), root.sortAscending);
        // Keep the header for real groupings (even when they collapse to one
        // section, matching the prior always-headed category cards); the
        // "None" grouping carries an empty label so it renders header-less.
        return Core.finalizeGroups(groups, root._groupId !== "none");
    }

    // ── Type-axis helpers ────────────────────────────────────────────────
    // Real packs declare at most one capability token, so the first token is
    // the effect's primary bucket; an empty `appliesTo` is universal.
    function _effectTypeKey(e) {
        if (!e || !e.appliesTo || e.appliesTo.length === 0)
            return root._universalKey;

        return String(e.appliesTo[0]);
    }
    function _typeLabel(key) {
        if (key === root._universalKey)
            return i18nc("@item shader capability (applies to every event)", "Universal");

        for (var i = 0; i < root._typeCatalog.length; i++)
            if (root._typeCatalog[i].key === key)
                return root._typeCatalog[i].label;

        // Unknown future token — show it capitalized rather than dropping it.
        return key.length > 0 ? key.charAt(0).toUpperCase() + key.slice(1) : key;
    }
    function _typeOrder(key) {
        if (key === root._universalKey)
            return 0;

        for (var i = 0; i < root._typeCatalog.length; i++)
            if (root._typeCatalog[i].key === key)
                return root._typeCatalog[i].order;

        return 99;
    }
    /// Non-empty capability label for a card badge; "" for universal (the
    /// default majority — no badge, to keep the grid uncluttered).
    function _typeBadgeLabel(e) {
        var key = root._effectTypeKey(e);
        return key === root._universalKey ? "" : root._typeLabel(key);
    }

    // ── Grouping / sorting ───────────────────────────────────────────────
    function _buildGroups(effects, groupId) {
        if (groupId === "none")
            return Core.ungrouped(effects, "");

        if (groupId === "source")
            return Core.groupByBoolKey(effects, function (e) {
                return !e.isUserEffect;
            }, "builtin", i18nc("@title:group built-in shaders", "Built-in"), "user", i18nc("@title:group user-installed shaders", "User"));

        if (groupId === "type")
            return Core.groupByKeyed(effects, function (e) {
                var key = root._effectTypeKey(e);
                return {
                    "key": key,
                    "order": root._typeOrder(key),
                    "label": root._typeLabel(key)
                };
            });

        // Default: category. Order groups alphabetically via the pre-sorted
        // category index, with "Uncategorised" pinned last.
        var uncategorised = i18nc("@title:group fallback for shaders without a category", "Uncategorised");
        var orderOf = {};
        for (var i = 0; i < root._allCategories.length; i++)
            orderOf[root._allCategories[i].name] = i;

        return Core.groupByKeyed(effects, function (e) {
            var cat = (e.category && e.category.length > 0) ? e.category : "";
            if (cat.length === 0)
                return {
                    "key": uncategorised,
                    "order": 1000000,
                    "label": uncategorised
                };

            return {
                "key": cat,
                "order": orderOf[cat] !== undefined ? orderOf[cat] : 999999,
                "label": cat
            };
        });
    }
    function _nameOf(e) {
        return String((e && (e.name || e.id)) || "");
    }
    function _comparatorFor(sortId) {
        if (sortId === "category")
            return function (a, b) {
                var c = String(a.category || "").localeCompare(String(b.category || ""));
                return c !== 0 ? c : root._nameOf(a).localeCompare(root._nameOf(b));
            };

        if (sortId === "type")
            return function (a, b) {
                var d = root._typeOrder(root._effectTypeKey(a)) - root._typeOrder(root._effectTypeKey(b));
                return d !== 0 ? d : root._nameOf(a).localeCompare(root._nameOf(b));
            };

        return function (a, b) {
            return root._nameOf(a).localeCompare(root._nameOf(b));
        };
    }
    // Per-row "Used in:" labels resolve via Q_INVOKABLE. QML can't
    // observe the result of an invokable across mutations, so each row
    // re-evaluates against this tick whenever any usage source mutates.
    property int _usagesRev: 0

    contentHeight: content.implicitHeight
    clip: true

    Connections {
        function onShaderEffectsChanged() {
            root.effectList = root.bridge ? root.bridge.availableShaderEffects() : [];
        }

        function onShaderProfileChanged(path) {
            ++root._usagesRev;
        }

        // Surface bridge-emitted toast requests through the shell
        // `window.showToast`. Without this, the controller's
        // toastRequested signal goes to /dev/null and the install
        // drop zone falls back to the generic InlineMessage above
        // (which can't carry the concrete failure reason).
        function onToastRequested(text) {
            if (typeof window !== "undefined" && window && window.showToast)
                window.showToast(text);
        }

        target: root.bridge
        // `ignoreUnknownSignals` is tolerated implicitly by Connections —
        // bridges that don't expose `shaderProfileChanged` /
        // `toastRequested` simply never fire them; the functions are
        // still defined so the signal handler index resolves correctly
        // when present.
        ignoreUnknownSignals: true
    }

    Timer {
        id: searchDebounce

        interval: 150
        onTriggered: root.filterText = searchField.text
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            visible: root.infoBannerText.length > 0
            text: root.infoBannerText
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("User shaders")
            collapsible: true
            searchAnchor: "userShaders"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    text: root.userShadersDescription
                    wrapMode: Text.WordWrap
                    color: Kirigami.Theme.disabledTextColor
                }

                FileDropZone {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 5
                    idleText: root.dropZoneIdleText
                    hoverText: root.dropZoneHoverText
                    idleIcon: "folder-download"
                    hoverIcon: "folder-add"
                    iconSize: Kirigami.Units.iconSizes.large
                    onFileDropped: function (url) {
                        var ok = root.bridge ? root.bridge.installShaderPack(url) : false;
                        installResult.show(ok, url);
                    }
                }

                Kirigami.InlineMessage {
                    id: installResult

                    function show(ok, url) {
                        // A dropped URL is percent-encoded, so decode before
                        // showing the name ("My%20Pack" is not what the user
                        // called it).
                        var basename = "";
                        try {
                            basename = decodeURIComponent(String(url).split("/").pop());
                        } catch (e) {
                            basename = String(url).split("/").pop();
                        }
                        if (basename.length === 0)
                            basename = String(url);

                        if (ok) {
                            type = Kirigami.MessageType.Positive;
                            text = i18nc("@info shader install success", "Installed shader pack “%1”.", basename);
                        } else {
                            type = Kirigami.MessageType.Error;
                            text = i18nc("@info shader install failure", "Could not install “%1”. The folder must contain a metadata.json and not collide with an existing pack.", basename);
                        }
                        visible = true;
                        autoHideTimer.restart();
                    }

                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    visible: false
                    showCloseButton: true

                    Timer {
                        id: autoHideTimer

                        interval: 6000
                        onTriggered: installResult.visible = false
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing

                    Item {
                        Layout.fillWidth: true
                    }

                    Button {
                        text: i18n("Open Folder")
                        icon.name: "folder-open"
                        flat: true
                        Accessible.name: i18n("Open user shader directory")
                        onClicked: {
                            if (root.bridge)
                                root.bridge.openUserShaderDirectory();
                        }
                    }
                }
            }
        }

        // ── Search row ──────────────────────────────────────────────────
        // Full-width search + a multi-select filter button (source +
        // categories), mirroring the Rules page's search row.
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.SearchField {
                id: searchField

                Layout.fillWidth: true
                placeholderText: i18nc("@info:placeholder shader search", "Search shaders…")
                Accessible.name: i18n("Search shaders")
                onTextChanged: searchDebounce.restart()
            }

            // One multi-select filter button (source + categories), modeled on
            // the Layouts page. Source toggles come first, then one checkbox per
            // discovered category; the button's `excluded` set drives showBuiltIn
            // / showUser and the per-category predicate above. Reset lives in the
            // menu; the search field clears via its own affordance.
            FilterMenuButton {
                id: shaderFilterButton

                menuTitle: i18nc("@title:menu", "Filter Shaders")
                // Groups (source, [type,] categories) — a divider between each,
                // plus the trailing divider + Reset, mirroring the Layouts menu.
                // The Type group is present only when the type axis is live.
                groups: {
                    var source = [
                        {
                            "key": "src:builtin",
                            "label": i18nc("@option:check", "Built-in")
                        },
                        {
                            "key": "src:user",
                            "label": i18nc("@option:check", "User-installed")
                        }
                    ];
                    var types = [];
                    if (root._hasTypeAxis) {
                        var allTypes = root._allTypes;
                        for (var t = 0; t < allTypes.length; t++)
                            types.push({
                                "key": "type:" + allTypes[t].key,
                                "label": allTypes[t].label,
                                "count": allTypes[t].count
                            });
                    }
                    var cats = [];
                    var all = root._allCategories;
                    for (var i = 0; i < all.length; i++)
                        cats.push({
                            "key": "cat:" + all[i].name,
                            "label": all[i].name,
                            "count": all[i].count
                        });
                    return root._hasTypeAxis ? [source, types, cats] : [source, cats];
                }
            }
        }

        // ── Group / sort row ────────────────────────────────────────────
        // On its own row beneath the search field, matching the Layouts page.
        // The host owns the selection and its persistence (see the Settings
        // block below); after loading persisted state we call syncFromState()
        // to re-point the combos.
        GroupSortBar {
            id: groupSortBar

            Layout.fillWidth: true
            groupModel: {
                var m = [];
                for (var i = 0; i < root._groupOptions.length; i++)
                    m.push(root._groupOptions[i].label);
                return m;
            }
            sortModel: {
                var m = [];
                for (var i = 0; i < root._sortOptions.length; i++)
                    m.push(root._sortOptions[i].label);
                return m;
            }
            // Initial-only bindings — syncFromState() re-syncs after the
            // host imperatively writes these (persisted-state load).
            groupByIndex: root.groupByIndex
            sortByIndex: root.sortByIndex
            sortAscending: root.sortAscending
            onChanged: {
                root.groupByIndex = groupByIndex;
                root.sortByIndex = sortByIndex;
                root.sortAscending = sortAscending;
                root._savePrefs();
            }
        }

        // ── Empty states ────────────────────────────────────────────────
        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.gridUnit * 2
            visible: root.effectList.length === 0
            icon.name: "image-missing"
            text: root.emptyCatalogueText
        }

        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.gridUnit * 2
            visible: root.effectList.length > 0 && root._filteredEffects.length === 0
            icon.name: "view-filter"
            text: i18n("No shaders match the current filter")
            explanation: i18n("Try a different filter or search term.")
        }

        // ── Grouped shader catalogue ────────────────────────────────────
        // Each group (per the group-by selection: category, type, source, or a
        // single "None" bucket) renders as its own collapsible SettingsCard —
        // the same grouped-section treatment as the Rules page — with the group
        // label as the header and the shader count as the trailing hint.
        Repeater {
            model: root._displayGroups

            delegate: SettingsCard {
                required property var modelData

                Layout.fillWidth: true
                headerText: modelData.label
                // "None" grouping yields an empty label — render it as a plain
                // header-less card rather than a collapsible section.
                collapsible: modelData.label.length > 0
                headerTrailingText: i18np("%n shader", "%n shaders", modelData.items.length)

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    // Cards in a Flow wrap to the next row when out of
                    // horizontal space — 3-4 cards per row at typical
                    // settings-window widths. The inner delegate declares its
                    // own `required property var modelData` so the section
                    // delegate's identically-named `modelData` doesn't shadow
                    // the Repeater's auto-injection.
                    Flow {
                        id: shaderFlow

                        Layout.fillWidth: true
                        // Standard card-content inset (matches SettingsRow / the
                        // section header), so the cards line up with everything
                        // else rather than hugging the category card edge.
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing

                        // Responsive columns: fit as many cards as the minimum
                        // card width allows, then stretch each card to fill the
                        // row evenly so there's no dead gap on the right edge.
                        readonly property real _minCardWidth: Kirigami.Units.gridUnit * 13
                        readonly property int _columns: Math.max(1, Math.floor((width + spacing) / (_minCardWidth + spacing)))
                        readonly property real _cardWidth: (width - spacing * (_columns - 1)) / _columns

                        Repeater {
                            model: modelData.items

                            delegate: ShaderBrowserCard {
                                required property var modelData

                                width: shaderFlow._cardWidth
                                effect: modelData
                                bridge: root.bridge
                                usagesRev: root._usagesRev
                                usageChipTextFn: root.usageChipTextFn
                                typeBadgeFn: function (e) {
                                    return root._hasTypeAxis ? root._typeBadgeLabel(e) : "";
                                }
                                onShowDetails: function (e) {
                                    detailDialog.effect = e;
                                    detailDialog.open();
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Persisted group / sort selection (per host page) ────────────────
    // Stored by option id, not index, so the choice survives a model whose
    // Type option appears or disappears between sessions.
    Settings {
        id: prefs

        category: root.settingsCategory
        property string groupId: "category"
        property string sortId: "name"
        property bool sortAscending: true
    }

    function _savePrefs() {
        prefs.groupId = root._groupId;
        prefs.sortId = root._sortId;
        prefs.sortAscending = root.sortAscending;
    }
    function _indexOfOption(options, id) {
        for (var i = 0; i < options.length; i++)
            if (options[i].id === id)
                return i;

        return 0;
    }

    Component.onCompleted: {
        root.groupByIndex = root._indexOfOption(root._groupOptions, prefs.groupId);
        root.sortByIndex = root._indexOfOption(root._sortOptions, prefs.sortId);
        root.sortAscending = prefs.sortAscending;
        groupSortBar.syncFromState();
    }

    ShaderBrowserDetailDialog {
        id: detailDialog

        bridge: root.bridge
        usagesRev: root._usagesRev
        usageHeaderTextFn: root.usageHeaderTextFn
    }
}
