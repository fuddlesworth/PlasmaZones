// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

ApplicationWindow {
    // Cached breadcrumb segment list for the current navigation
    // state. Rendered as `[top-parent] › [intermediate] › … ›
    // [current page]` with every non-leaf segment navigable; current-
    // mode segment is not clickable since it's where the user already
    // is. For 2-level navigation the chain has two entries (parent +
    // page); for the 3-level Animations layout it has three (top-parent
    // + intermediate-parent + page). In main mode the chain collapses
    // to just the current page name.

    id: window

    // Expose the layout context menu so Loader-loaded pages can connect to its signals
    readonly property alias layoutContextMenu: layoutContextMenu
    // Responsive sidebar: collapse to icon-only below 750px
    readonly property bool sidebarCompact: window.width < Kirigami.Units.gridUnit * 50
    // Track page navigation for transitions
    // Shortcut overlay visibility
    property bool _showShortcuts: false
    // Close-without-save guard
    property bool _closeConfirmed: false
    // ── Drill-down sidebar state ─────────────────────────────────────
    // "main" = top-level list; any other value names the currently-
    // displayed parent. Top-level parents (e.g. "snapping", "tiling",
    // "animations") and mid-level virtual parents registered in
    // `_parentMode` (e.g. "animations-surfaces", "animations-library")
    // are both valid values; `_drillOut` walks the lineage one level
    // at a time using `_parentMode`.
    property string _sidebarMode: "main"
    // Suppression flag for the sidebar ListView's accordion add/remove
    // transitions. Set true while `sidebarTransition` is rebuilding the
    // model so the drill-in/out cross-fade isn't fought by per-row
    // accordion animations. Toggling a collapsible category leaves this
    // false so the accordion plays as intended.
    property bool _suppressAccordion: false
    // Expanded state for inline collapsible categories. Every known
    // category is seeded to `true` so the sidebar opens with all
    // groups expanded — the user can collapse any of them with a
    // single click, and per-session state survives in this map.
    // Categories currently come in three flavours: top-level
    // "display" / "rules" groupings under the main sidebar, the
    // Snapping/Tiling sub-sidebar `*-cat` buckets, and the
    // Animations sub-sidebar "animations-surfaces" /
    // "animations-library" buckets. Mutated by replacing the whole
    // object so QML's binding system picks up the change.
    property var _expandedCategories: ({
        "display": true,
        "rules": true,
        "snapping-visual-cat": true,
        "snapping-behavior-cat": true,
        "snapping-config-cat": true,
        "tiling-visual-cat": true,
        "tiling-behavior-cat": true,
        "tiling-config-cat": true,
        "animations-surfaces": true,
        "animations-library": true
    })
    // Main sidebar items. "display" and "rules" are top-level inline
    // collapsible categories that group related leaves under a single
    // header. They behave exactly like Surfaces/Library inside the
    // Animations sub-sidebar: the header row toggles _expandedCategories
    // and renders its children inline (indented) immediately below.
    readonly property var _mainItems: [{
        "name": "overview",
        "label": i18n("Overview"),
        "iconName": "monitor",
        "hasChildren": false,
        "hasDividerAfter": true
    }, {
        "name": "display",
        "label": i18n("Display"),
        "iconName": "preferences-desktop-display",
        "hasChildren": true,
        "isCollapsible": true,
        "hasDividerAfter": true
    }, {
        "name": "snapping",
        "label": i18n("Snapping"),
        "iconName": "view-split-left-right",
        "hasChildren": true,
        "hasDividerAfter": false
    }, {
        "name": "tiling",
        "label": i18n("Tiling"),
        "iconName": "window-duplicate",
        "hasChildren": true,
        "hasDividerAfter": true
    }, {
        "name": "animations",
        "label": i18n("Animations"),
        "iconName": "media-playback-start",
        "hasChildren": true,
        "hasDividerAfter": false
    }, {
        "name": "rules",
        "label": i18n("Rules"),
        "iconName": "view-list-details",
        "hasChildren": true,
        "isCollapsible": true,
        "hasDividerAfter": true
    }, {
        "name": "editor",
        "label": i18n("Editor"),
        "iconName": "document-edit",
        "hasChildren": false,
        "hasDividerAfter": false
    }, {
        "name": "general",
        "label": i18n("General"),
        "iconName": "configure",
        "hasChildren": false,
        "hasDividerAfter": false
    }, {
        "name": "about",
        "label": i18n("About"),
        "iconName": "help-about",
        "hasChildren": false,
        "hasDividerAfter": false
    }]
    // Children for each parent
    readonly property var _childItems: ({
        "display": [{
            "name": "virtualscreens",
            "label": i18n("Virtual Screens"),
            "iconName": "virtual-desktops"
        }, {
            "name": "layouts",
            "label": i18n("Layouts"),
            "iconName": "view-grid"
        }],
        "rules": [{
            "name": "window-rules",
            "label": i18n("Window Rules"),
            "iconName": "view-list-details"
        }, {
            "name": "exclusions",
            "label": i18n("Exclusions"),
            "iconName": "dialog-cancel"
        }],
        "snapping": [{
            "name": "snapping-visual-cat",
            "label": i18n("Visual"),
            "iconName": "preferences-desktop-color",
            "hasChildren": true,
            "isCollapsible": true,
            "hasDividerAfter": true
        }, {
            "name": "snapping-behavior-cat",
            "label": i18n("Behavior"),
            "iconName": "preferences-system",
            "hasChildren": true,
            "isCollapsible": true,
            "hasDividerAfter": true
        }, {
            "name": "snapping-config-cat",
            "label": i18n("Configuration"),
            "iconName": "configure",
            "hasChildren": true,
            "isCollapsible": true
        }],
        "snapping-visual-cat": [{
            "name": "snapping-appearance",
            "label": i18n("Appearance"),
            "iconName": "preferences-desktop-color"
        }, {
            "name": "snapping-effects",
            "label": i18n("Effects"),
            "iconName": "preferences-desktop-effects"
        }, {
            "name": "snapping-shaders",
            "label": i18n("Shaders"),
            "iconName": "preferences-desktop-display"
        }],
        "snapping-behavior-cat": [{
            "name": "snapping-behavior",
            "label": i18n("Behavior"),
            "iconName": "preferences-system"
        }, {
            "name": "snapping-zoneselector",
            "label": i18n("Zone Selector"),
            "iconName": "view-choose"
        }],
        "snapping-config-cat": [{
            "name": "snapping-ordering",
            "label": i18n("Priority"),
            "iconName": "view-sort"
        }, {
            "name": "snapping-shortcuts",
            "label": i18n("Quick Shortcuts"),
            "iconName": "bookmark"
        }],
        "tiling": [{
            "name": "tiling-visual-cat",
            "label": i18n("Visual"),
            "iconName": "preferences-desktop-color",
            "hasChildren": true,
            "isCollapsible": true,
            "hasDividerAfter": true
        }, {
            "name": "tiling-behavior-cat",
            "label": i18n("Behavior"),
            "iconName": "preferences-system",
            "hasChildren": true,
            "isCollapsible": true,
            "hasDividerAfter": true
        }, {
            "name": "tiling-config-cat",
            "label": i18n("Configuration"),
            "iconName": "configure",
            "hasChildren": true,
            "isCollapsible": true
        }],
        "tiling-visual-cat": [{
            "name": "tiling-appearance",
            "label": i18n("Appearance"),
            "iconName": "preferences-desktop-color"
        }],
        "tiling-behavior-cat": [{
            "name": "tiling-behavior",
            "label": i18n("Behavior"),
            "iconName": "preferences-system"
        }, {
            "name": "tiling-algorithm",
            "label": i18n("Algorithms"),
            "iconName": "view-grid"
        }],
        "tiling-config-cat": [{
            "name": "tiling-ordering",
            "label": i18n("Priority"),
            "iconName": "view-sort"
        }, {
            "name": "tiling-shortcuts",
            "label": i18n("Quick Shortcuts"),
            "iconName": "bookmark"
        }],
        "animations": [{
            "name": "animations-general",
            "label": i18n("General"),
            "iconName": "configure",
            "hasDividerAfter": true
        }, {
            "name": "animations-surfaces",
            "label": i18n("Surfaces"),
            "iconName": "preferences-desktop-multimedia",
            "hasChildren": true,
            "isCollapsible": true
        }, {
            "name": "animations-library",
            "label": i18n("Library"),
            "iconName": "folder-open",
            "hasChildren": true,
            "isCollapsible": true
        }],
        "animations-surfaces": [{
            "name": "animations-windows",
            "label": i18n("Windows"),
            "iconName": "window-new"
        }, {
            "name": "animations-osds",
            "label": i18n("OSDs"),
            "iconName": "dialog-information"
        }, {
            "name": "animations-overlays",
            "label": i18n("Overlays"),
            "iconName": "view-presentation"
        }, {
            "name": "animations-side-panels",
            "label": i18n("Side Panels"),
            "iconName": "sidebar-collapse-symbolic"
        }, {
            "name": "animations-widgets",
            "label": i18n("Widgets"),
            "iconName": "preferences-desktop-theme"
        }, {
            "name": "animations-editor",
            "label": i18n("Layout Editor"),
            "iconName": "document-edit"
        }],
        "animations-library": [{
            "name": "animations-presets",
            "label": i18n("Presets"),
            "iconName": "bookmarks"
        }, {
            "name": "animations-motionsets",
            "label": i18n("Motion Sets"),
            "iconName": "color-palette"
        }, {
            "name": "animations-shaders",
            "label": i18n("Shaders"),
            "iconName": "preferences-desktop-display"
        }]
    })
    // Map from sub-mode → its parent mode. Modes not listed here drill
    // back to "main". Lets `_drillOut` pop one level instead of always
    // returning to the top. Surfaces and Library inside Animations are
    // inline collapsible categories (not drill-down sub-modes), so they
    // are intentionally absent here.
    readonly property var _parentMode: ({
    })
    // Page component map -- loaded on demand by Loader
    readonly property var _pageComponents: ({
        "overview": "MonitorStatePage.qml",
        "virtualscreens": "VirtualScreensPage.qml",
        "layouts": "LayoutsPage.qml",
        "snapping-appearance": "SnappingAppearancePage.qml",
        "snapping-behavior": "SnappingBehaviorPage.qml",
        "snapping-zoneselector": "SnappingZoneSelectorPage.qml",
        "snapping-effects": "SnappingEffectsPage.qml",
        "snapping-shaders": "SnappingShadersPage.qml",
        "tiling-appearance": "TilingAppearancePage.qml",
        "tiling-behavior": "TilingBehaviorPage.qml",
        "tiling-algorithm": "TilingAlgorithmPage.qml",
        "snapping-shortcuts": "SnappingQuickShortcutsPage.qml",
        "snapping-ordering": "SnappingOrderingPage.qml",
        "tiling-shortcuts": "TilingQuickShortcutsPage.qml",
        "tiling-ordering": "TilingOrderingPage.qml",
        "window-rules": "WindowRulesPage.qml",
        "exclusions": "ExclusionsPage.qml",
        "editor": "EditorPage.qml",
        "general": "GeneralPage.qml",
        "about": "AboutPage.qml",
        "animations-general": "AnimationsGeneralPage.qml",
        "animations-windows": "AnimationsWindowsPage.qml",
        "animations-editor": "AnimationsEditorPage.qml",
        "animations-osds": "AnimationsOsdsPage.qml",
        "animations-overlays": "AnimationsOverlaysPage.qml",
        "animations-side-panels": "AnimationsSidePanelsPage.qml",
        "animations-widgets": "AnimationsWidgetsPage.qml",
        "animations-presets": "AnimationsPresetsPage.qml",
        "animations-motionsets": "AnimationsMotionSetsPage.qml",
        "animations-shaders": "AnimationsShadersPage.qml"
    })
    // Shared aspect ratio labels (used in context menu + LayoutsPage section headers)
    readonly property var aspectRatioLabels: ({
        "any": i18n("All Monitors"),
        "standard": i18n("Standard (16:9)"),
        "ultrawide": i18n("Ultrawide (21:9)"),
        "super-ultrawide": i18n("Super-Ultrawide (32:9)"),
        "portrait": i18n("Portrait (9:16)")
    })
    // Flat name → label map computed once from `_mainItems` and
    // every `_childItems` bucket. Drives `_modeLabel` and the
    // breadcrumb so resolving a label is a single hash lookup
    // instead of nested-loop scans on every binding re-evaluation.
    // Property is `readonly` so QML caches the value at startup;
    // both source maps are also `readonly`, so the index never goes
    // stale.
    readonly property var _labelByName: {
        let out = ({
        });
        for (let i = 0; i < _mainItems.length; i++) {
            out[_mainItems[i].name] = _mainItems[i].label;
        }
        const buckets = Object.keys(_childItems);
        for (let b = 0; b < buckets.length; b++) {
            const entries = _childItems[buckets[b]];
            for (let e = 0; e < entries.length; e++) {
                out[entries[e].name] = entries[e].label;
            }
        }
        return out;
    }
    // Hard depth ceiling for any walk through `_parentMode` /
    // `_childItems` — guards against a malformed map (cyclic lineage,
    // self-referencing parent) producing an infinite loop. Today's
    // tree never exceeds 2 hops; 16 is an order of magnitude headroom
    // for any plausible future structure.
    readonly property int _maxNavDepth: 16
    // Cached as a `readonly property var` so the Repeater's `model`
    // and per-delegate separator-visibility binding share a single
    // computation. Reading `_sidebarMode` and `activePage` inside
    // the IIFE registers the binding's reactivity dependencies.
    readonly property var _breadcrumbModel: {
        if (_sidebarMode === "main") {
            const activeName = settingsController.activePage;
            return [{
                "name": activeName,
                "label": _modeLabel(activeName),
                "clickable": false
            }];
        }
        // Walk the parent chain top-down with an explicit depth
        // bound; `_parentMode[mode]` returning undefined terminates
        // the walk normally, the bound is belt-and-braces against a
        // future cyclic map.
        let chain = [];
        let mode = _sidebarMode;
        for (let depth = 0; depth < _maxNavDepth && mode !== undefined; depth++) {
            chain.unshift(mode);
            mode = _parentMode[mode];
        }
        let segments = [];
        for (let i = 0; i < chain.length; i++) {
            segments.push({
                "name": chain[i],
                "label": _modeLabel(chain[i]),
                "clickable": chain[i] !== _sidebarMode
            });
        }
        // If the active page lives inside an inline collapsible category
        // (e.g. animations-windows under animations-surfaces), splice the
        // category segment in between the mode and the page so the
        // breadcrumb reads "Animations / Surfaces / Windows".
        const activePage = settingsController.activePage;
        const cat = _categoryOf(activePage);
        if (cat.length > 0)
            segments.push({
            "name": cat,
            "label": _modeLabel(cat),
            "clickable": true
        });

        segments.push({
            "name": activePage,
            "label": _subPageLabel(activePage),
            "clickable": false
        });
        return segments;
    }

    /// Resolve the page QML source for @p pageName. An unmapped page name is
    /// a navigation bug — surface it with a console.warn rather than silently
    /// falling back, mirroring the sidebar's "show the gap visibly" stance.
    function _resolvePageSource(pageName) {
        var file = window._pageComponents[pageName];
        if (file === undefined) {
            console.warn("Main.qml: no page component mapped for activePage '" + pageName + "' — falling back to LayoutsPage.qml");
            file = "LayoutsPage.qml";
        }
        return Qt.resolvedUrl(file);
    }

    // Walk @p parentName's descendants and return every leaf whose label
    // matches @p searchText. Nested labels are prefixed with their
    // immediate parent ("Surfaces / Windows") so a search hit is
    // unambiguous when multiple intermediate categories live under the
    // same grandparent. When an intermediate parent's OWN label matches
    // the query (e.g. searching "surfaces"), every leaf under it is
    // included unfiltered — otherwise typing a category name would yield
    // empty results because the parent itself is a virtual non-leaf.
    function _collectMatchingDescendants(parentName, searchText) {
        let out = [];
        let children = _childItems[parentName] || [];
        for (let j = 0; j < children.length; j++) {
            let child = children[j];
            if (child.hasChildren) {
                let intermediateMatches = child.label.toLowerCase().indexOf(searchText) >= 0;
                let nestedQuery = intermediateMatches ? "" : searchText;
                let nested = _collectMatchingDescendants(child.name, nestedQuery);
                for (let k = 0; k < nested.length; k++) {
                    out.push({
                        "name": nested[k].name,
                        "label": child.label + " / " + nested[k].label,
                        "iconName": nested[k].iconName,
                        "hasDividerAfter": false
                    });
                }
            } else if (searchText === "" || child.label.toLowerCase().indexOf(searchText) >= 0) {
                out.push(child);
            }
        }
        return out;
    }

    function _rebuildSidebar() {
        sidebarModel.clear();
        let searchText = sidebarSearch.text.toLowerCase();
        if (_sidebarMode === "main") {
            for (let i = 0; i < _mainItems.length; i++) {
                let item = _mainItems[i];
                if (searchText) {
                    // Search the item label and walk every descendant
                    // (children + grand-children) so a Surfaces leaf
                    // like "Windows" still surfaces under the
                    // top-level Animations entry.
                    let itemMatches = item.label.toLowerCase().indexOf(searchText) >= 0;
                    let matchingChildren = item.hasChildren ? _collectMatchingDescendants(item.name, searchText) : [];
                    if (!itemMatches && matchingChildren.length === 0)
                        continue;

                    // Show parent (disable drill-in if showing matched children)
                    sidebarModel.append({
                        "name": item.name,
                        "label": item.label,
                        "iconName": item.iconName,
                        "hasChildren": matchingChildren.length === 0 && item.hasChildren,
                        "isBackButton": false,
                        "hasDividerAfter": false,
                        "isDivider": false,
                        "isCategory": false,
                        "categoryExpanded": false,
                        "isCategoryChild": false
                    });
                    // Show matching children inline
                    for (let j = 0; j < matchingChildren.length; j++) {
                        let childDivider = matchingChildren[j].hasDividerAfter || false;
                        sidebarModel.append({
                            "name": matchingChildren[j].name,
                            "label": matchingChildren[j].label,
                            "iconName": matchingChildren[j].iconName,
                            "hasChildren": false,
                            "isBackButton": false,
                            "hasDividerAfter": false,
                            "isDivider": false,
                            "isSearchChild": true,
                            "isCategory": false,
                            "categoryExpanded": false,
                            "isCategoryChild": false
                        });
                        if (childDivider)
                            sidebarModel.append({
                            "name": "__divider__",
                            "label": "",
                            "iconName": "",
                            "hasChildren": false,
                            "isBackButton": false,
                            "hasDividerAfter": false,
                            "isDivider": true,
                            "isCategory": false,
                            "categoryExpanded": false,
                            "isCategoryChild": false
                        });

                    }
                    continue;
                }
                // Top-level inline collapsible category (Display, Rules):
                // append the header row with isCategory:true and, when
                // expanded, every child immediately after with
                // isCategoryChild flagged so the delegate indents. The
                // chevron is driven by isCategory, so don't set
                // hasChildren on the header (that would make it drill).
                if (item.isCollapsible === true) {
                    const expanded = _expandedCategories[item.name] === true;
                    sidebarModel.append({
                        "name": item.name,
                        "label": item.label,
                        "iconName": item.iconName,
                        "hasChildren": false,
                        "isBackButton": false,
                        "hasDividerAfter": false,
                        "isDivider": false,
                        "isCategory": true,
                        "categoryExpanded": expanded,
                        "isCategoryChild": false
                    });
                    if (expanded) {
                        const groupChildren = _childItems[item.name] || [];
                        for (let g = 0; g < groupChildren.length; g++) {
                            const gc = groupChildren[g];
                            sidebarModel.append({
                                "name": gc.name,
                                "label": gc.label,
                                "iconName": gc.iconName,
                                "hasChildren": false,
                                "isBackButton": false,
                                "hasDividerAfter": false,
                                "isDivider": false,
                                "isCategory": false,
                                "categoryExpanded": false,
                                "isCategoryChild": true
                            });
                        }
                    }
                    if (item.hasDividerAfter)
                        sidebarModel.append({
                        "name": "__divider__",
                        "label": "",
                        "iconName": "",
                        "hasChildren": false,
                        "isBackButton": false,
                        "hasDividerAfter": false,
                        "isDivider": true,
                        "isCategory": false,
                        "categoryExpanded": false,
                        "isCategoryChild": false
                    });

                    continue;
                }
                sidebarModel.append({
                    "name": item.name,
                    "label": item.label,
                    "iconName": item.iconName,
                    "hasChildren": item.hasChildren,
                    "isBackButton": false,
                    "hasDividerAfter": false,
                    "isDivider": false,
                    "isCategory": false,
                    "categoryExpanded": false,
                    "isCategoryChild": false
                });
                if (item.hasDividerAfter)
                    sidebarModel.append({
                    "name": "__divider__",
                    "label": "",
                    "iconName": "",
                    "hasChildren": false,
                    "isBackButton": false,
                    "hasDividerAfter": false,
                    "isDivider": true,
                    "isCategory": false,
                    "categoryExpanded": false,
                    "isCategoryChild": false
                });

            }
        } else {
            // Back-row label is the CURRENT mode's display label
            // (so a sub-sidebar "Surfaces" reads "← Surfaces"). The
            // mode may live in `_mainItems` (top-level parents like
            // "animations") or inside another `_childItems` entry
            // (intermediate parents like "animations-surfaces").
            // Search-mode entries that have nested matches inlined
            // below have their drill-in disabled at append time so
            // the user clicks the leaves directly, mirroring the
            // main-mode search pattern.
            let parentLabel = _modeLabel(_sidebarMode);
            // Back button row (always visible)
            sidebarModel.append({
                "name": "__back__",
                "label": parentLabel,
                "iconName": "arrow-left",
                "hasChildren": false,
                "isBackButton": true,
                "hasDividerAfter": false,
                "isDivider": false,
                "isCategory": false,
                "categoryExpanded": false,
                "isCategoryChild": false
            });
            // Child items
            let children = _childItems[_sidebarMode] || [];
            for (let i = 0; i < children.length; i++) {
                let child = children[i];
                let childDivider = child.hasDividerAfter || false;
                if (searchText) {
                    // Search inside a sub-sidebar matches the entry's
                    // own label OR (when it's an intermediate parent)
                    // any grand-child label. Without the recursion,
                    // searching "windows" from inside the Animations
                    // sub-sidebar would silently miss it because
                    // Windows lives one level deeper under Surfaces —
                    // a regression compared with the prior flat layout.
                    let ownMatch = child.label.toLowerCase().indexOf(searchText) >= 0;
                    let nestedMatches = child.hasChildren ? _collectMatchingDescendants(child.name, searchText) : [];
                    if (!ownMatch && nestedMatches.length === 0)
                        continue;

                    sidebarModel.append({
                        "name": child.name,
                        "label": child.label,
                        "iconName": child.iconName,
                        "hasChildren": nestedMatches.length === 0 && (child.hasChildren || false),
                        "isBackButton": false,
                        "hasDividerAfter": false,
                        "isDivider": false,
                        "isCategory": false,
                        "categoryExpanded": false,
                        "isCategoryChild": false
                    });
                    for (let j = 0; j < nestedMatches.length; j++) {
                        sidebarModel.append({
                            "name": nestedMatches[j].name,
                            "label": nestedMatches[j].label,
                            "iconName": nestedMatches[j].iconName,
                            "hasChildren": false,
                            "isBackButton": false,
                            "hasDividerAfter": false,
                            "isDivider": false,
                            "isSearchChild": true,
                            "isCategory": false,
                            "categoryExpanded": false,
                            "isCategoryChild": false
                        });
                    }
                    continue;
                }
                // Inline collapsible category (Surfaces / Library inside
                // the Animations sub-sidebar). The header row never drills
                // — clicking toggles _expandedCategories. When expanded,
                // append every grand-child immediately after with
                // isCategoryChild flagged so the delegate indents.
                if (child.isCollapsible === true) {
                    const expanded = _expandedCategories[child.name] === true;
                    sidebarModel.append({
                        "name": child.name,
                        "label": child.label,
                        "iconName": child.iconName,
                        "hasChildren": false,
                        "isBackButton": false,
                        "hasDividerAfter": false,
                        "isDivider": false,
                        "isCategory": true,
                        "categoryExpanded": expanded,
                        "isCategoryChild": false
                    });
                    if (expanded) {
                        const grandChildren = _childItems[child.name] || [];
                        for (let g = 0; g < grandChildren.length; g++) {
                            const gc = grandChildren[g];
                            sidebarModel.append({
                                "name": gc.name,
                                "label": gc.label,
                                "iconName": gc.iconName,
                                "hasChildren": false,
                                "isBackButton": false,
                                "hasDividerAfter": false,
                                "isDivider": false,
                                "isCategory": false,
                                "categoryExpanded": false,
                                "isCategoryChild": true
                            });
                        }
                    }
                    if (childDivider)
                        sidebarModel.append({
                        "name": "__divider__",
                        "label": "",
                        "iconName": "",
                        "hasChildren": false,
                        "isBackButton": false,
                        "hasDividerAfter": false,
                        "isDivider": true,
                        "isCategory": false,
                        "categoryExpanded": false,
                        "isCategoryChild": false
                    });

                    continue;
                }
                sidebarModel.append({
                    "name": child.name,
                    "label": child.label,
                    "iconName": child.iconName,
                    "hasChildren": child.hasChildren || false,
                    "isBackButton": false,
                    "hasDividerAfter": false,
                    "isDivider": false,
                    "isCategory": false,
                    "categoryExpanded": false,
                    "isCategoryChild": false
                });
                if (childDivider)
                    sidebarModel.append({
                    "name": "__divider__",
                    "label": "",
                    "iconName": "",
                    "hasChildren": false,
                    "isBackButton": false,
                    "hasDividerAfter": false,
                    "isDivider": true,
                    "isCategory": false,
                    "categoryExpanded": false,
                    "isCategoryChild": false
                });

            }
        }
    }

    // Returns the display label for any node by name. Falls back to
    // the raw name so a missing entry shows up visibly in the UI
    // rather than silently collapsing to an empty string.
    function _modeLabel(name) {
        const label = _labelByName[name];
        return label !== undefined ? label : name;
    }

    // Toggle the expanded state of an inline collapsible category by
    // its item name (e.g. "animations-surfaces"). Replaces the whole
    // map so QML's binding system observes the mutation, then mutates
    // the sidebar model INCREMENTALLY — insert this category's child
    // rows directly after the header (expand), or remove them in place
    // (collapse). A full `_rebuildSidebar()` here would clear and
    // re-append every row in the model, firing the ListView's
    // `add`/`remove` transitions for every visible row instead of just
    // the toggled category's children — producing a sidebar-wide flash
    // rather than the local accordion reveal. The incremental path
    // keeps every other row in place so its `displaced` transition
    // animates them sliding to make room.
    function _toggleCategory(name) {
        let next = Object.assign({
        }, _expandedCategories);
        const willBeExpanded = !(next[name] === true);
        next[name] = willBeExpanded;
        _expandedCategories = next;
        _applyCategoryExpansion(name, willBeExpanded);
    }

    // Apply @p expanded to the named category's row in `sidebarModel`
    // by inserting or removing its children. Falls back to a full
    // rebuild when the category isn't visible as a category row in
    // the current model (e.g. during search mode, where matches are
    // inlined under a non-category parent).
    function _applyCategoryExpansion(name, expanded) {
        let headerIdx = -1;
        for (let i = 0; i < sidebarModel.count; i++) {
            const row = sidebarModel.get(i);
            if (row.name === name && row.isCategory === true) {
                headerIdx = i;
                break;
            }
        }
        if (headerIdx < 0) {
            // Not a category row in the current model — rebuild from
            // scratch (cheap path; the data structure walk is the
            // authoritative renderer for non-trivial state changes).
            _rebuildSidebar();
            return ;
        }
        sidebarModel.setProperty(headerIdx, "categoryExpanded", expanded);
        if (expanded) {
            const children = _childItems[name] || [];
            for (let j = 0; j < children.length; j++) {
                const child = children[j];
                sidebarModel.insert(headerIdx + 1 + j, {
                    "name": child.name,
                    "label": child.label,
                    "iconName": child.iconName,
                    "hasChildren": child.hasChildren === true,
                    "isBackButton": false,
                    "hasDividerAfter": false,
                    "isDivider": false,
                    "isCategory": false,
                    "categoryExpanded": false,
                    "isCategoryChild": true
                });
            }
        } else {
            // Walk forward while the row is a child of this category
            // (marked via `isCategoryChild`). Stop at the next
            // header / divider / non-child so we don't remove a
            // sibling category's contents.
            while (headerIdx + 1 < sidebarModel.count) {
                const r = sidebarModel.get(headerIdx + 1);
                if (r.isCategoryChild !== true)
                    break;

                sidebarModel.remove(headerIdx + 1);
            }
        }
    }

    // Smart-default expansion: if the active page lives inside an inline
    // collapsible category, ensure that category is expanded. Called at
    // startup and whenever activePage changes (via the Connections slot
    // below). Idempotent — bails when the category is already expanded
    // so it doesn't churn the sidebar model.
    function _expandActivePageCategory() {
        const cat = _categoryOf(settingsController.activePage);
        if (cat.length === 0)
            return ;

        if (_expandedCategories[cat] === true)
            return ;

        let next = Object.assign({
        }, _expandedCategories);
        next[cat] = true;
        _expandedCategories = next;
        // Incremental expand keeps the rest of the sidebar in place so
        // only the newly-revealed children animate in. See the same
        // rationale on `_toggleCategory`.
        _applyCategoryExpansion(cat, true);
    }

    /// If @p pageName lives inside an inline collapsible category, return
    /// that category's item name (e.g. "animations-surfaces" or "display").
    /// Otherwise returns "". Used by the breadcrumb chain to insert the
    /// category segment between the mode and the page AND by the
    /// smart-default-expansion logic that auto-expands the owning category
    /// whenever activePage lands inside it.
    function _categoryOf(pageName) {
        const candidates = ["animations-surfaces", "animations-library", "display", "rules", "snapping-visual-cat", "snapping-behavior-cat", "snapping-config-cat", "tiling-visual-cat", "tiling-behavior-cat", "tiling-config-cat"];
        for (let c = 0; c < candidates.length; c++) {
            const list = _childItems[candidates[c]] || [];
            for (let i = 0; i < list.length; i++) {
                if (list[i].name === pageName)
                    return candidates[c];

            }
        }
        return "";
    }

    // Navigate to the breadcrumb segment named @p name. Top-level
    // modes pop to main with that parent name as `activePage` (so the
    // sidebar highlights it); top-level inline collapsible categories
    // (e.g. "display", "rules") expand in place without a sidebar mode
    // switch; sub-sidebar inline categories (animations-surfaces /
    // animations-library) drill into their owning mode and ensure the
    // category is expanded; remaining drill-down sub-modes pop directly
    // into that intermediate sub-sidebar. All non-trivial transitions
    // run through sidebarTransition for visual continuity.
    function _navigateToBreadcrumbSegment(name) {
        for (let i = 0; i < _mainItems.length; i++) {
            if (_mainItems[i].name === name) {
                // Top-level inline collapsible category — already in
                // main mode, just guarantee the group is expanded.
                if (_mainItems[i].isCollapsible === true) {
                    let next = Object.assign({
                    }, _expandedCategories);
                    next[name] = true;
                    _expandedCategories = next;
                    // Incremental — see `_toggleCategory` for the
                    // rationale (avoids whole-sidebar flash).
                    _applyCategoryExpansion(name, true);
                    return ;
                }
                sidebarTransition.pendingMode = "main";
                sidebarTransition.pendingPage = name;
                sidebarTransition.restart();
                return ;
            }
        }
        // Inline collapsible category inside any sub-sidebar — find its
        // parent mode by scanning `_childItems` for the bucket that
        // contains this name as a collapsible entry, then drill into it.
        const parentMode = _parentModeForCategory(name);
        if (parentMode.length > 0) {
            let next = Object.assign({
            }, _expandedCategories);
            next[name] = true;
            _expandedCategories = next;
            sidebarTransition.pendingMode = parentMode;
            sidebarTransition.pendingPage = "";
            sidebarTransition.restart();
            return ;
        }
        // Drill-down sub-mode (Snapping / Tiling / Animations top-level).
        sidebarTransition.pendingMode = name;
        sidebarTransition.pendingPage = "";
        sidebarTransition.restart();
    }

    /// Reverse lookup: given an inline collapsible category's item name
    /// (e.g. "snapping-visual-cat"), return the sub-sidebar mode that
    /// hosts it (e.g. "snapping"). Returns "" when no `_childItems`
    /// bucket lists @p categoryName as a collapsible entry. Used by the
    /// breadcrumb navigator and the stale-`activePage` remap so the
    /// owning mode doesn't need to be hard-coded per category.
    function _parentModeForCategory(categoryName) {
        const keys = Object.keys(_childItems);
        for (let k = 0; k < keys.length; k++) {
            const list = _childItems[keys[k]];
            for (let i = 0; i < list.length; i++) {
                if (list[i].name === categoryName && list[i].isCollapsible === true)
                    return keys[k];

            }
        }
        return "";
    }

    // Helper: find a subpage label by name. Searches the current
    // mode's child list first (fast path) then falls back to the
    // whole tree so a stale `activePage` from a different mode still
    // resolves to a readable label in the breadcrumb.
    function _subPageLabel(pageName) {
        let children = _childItems[_sidebarMode];
        if (children) {
            for (let i = 0; i < children.length; i++) {
                if (children[i].name === pageName)
                    return children[i].label;

            }
        }
        return _modeLabel(pageName);
    }

    // Drill into a parent category and select the first reachable
    // leaf (recursing through intermediate categories so e.g.
    // drilling into "animations" lands on "General", and a future
    // bucket whose immediate children are ALL `hasChildren` still
    // resolves a real leaf instead of showing the LayoutsPage
    // fallback). The optional @p depth parameter is incremented on
    // each recursive call and short-circuits at `_maxNavDepth` to
    // guard against a cyclic `_childItems` map producing a stack
    // overflow.
    function _firstLeafOf(parentName, depth) {
        const next = depth === undefined ? 0 : depth;
        if (next >= _maxNavDepth)
            return "";

        let children = _childItems[parentName] || [];
        for (let i = 0; i < children.length; i++) {
            if (!children[i].hasChildren)
                return children[i].name;

            const nested = _firstLeafOf(children[i].name, next + 1);
            if (nested.length > 0)
                return nested;

        }
        return "";
    }

    function _drillIn(parentName) {
        const firstLeaf = _firstLeafOf(parentName);
        sidebarTransition.pendingMode = parentName;
        sidebarTransition.pendingPage = firstLeaf;
        sidebarTransition.restart();
    }

    function showWhatsNew() {
        whatsNewDialog.open();
    }

    // Show a toast notification from any child page
    function showToast(msg) {
        toast.show(msg);
    }

    // Show the layout context menu (called from LayoutsPage.qml via window.showLayoutContextMenu)
    function showLayoutContextMenu(layout) {
        layoutContextMenu.showForLayout(layout);
    }

    // Pop one level. Sub-modes registered in `_parentMode` (e.g.
    // `animations-surfaces` → `animations`) drill back to the
    // intermediate parent; everything else returns to "main" with the
    // current parent highlighted as `activePage`. When popping back
    // into an intermediate (still-virtual) parent, `activePage` is
    // left untouched (empty pendingPage) so the leaf the user came
    // from stays visible until they pick another — re-anchoring on
    // the virtual category they just stepped out of would be less
    // useful context.
    function _drillOut() {
        const target = _parentMode[_sidebarMode] || "main";
        const popToMain = target === "main";
        const pendingPage = popToMain ? (_sidebarMode !== "main" ? _sidebarMode : "overview") : "";
        sidebarTransition.pendingMode = target;
        sidebarTransition.pendingPage = pendingPage;
        sidebarTransition.restart();
    }

    title: i18n("PlasmaZones Settings")
    width: Kirigami.Units.gridUnit * 80
    height: Kirigami.Units.gridUnit * 48
    visible: true
    onClosing: function(close) {
        settingsController.saveWindowGeometry(window.x, window.y, window.width, window.height);
        if (settingsController.needsSave && !window._closeConfirmed) {
            close.accepted = false;
            unsavedChangesDialog.open();
        }
    }
    Component.onCompleted: {
        // Restore window geometry
        var geo = settingsController.loadWindowGeometry();
        if (geo.width > 0 && geo.height > 0) {
            window.width = geo.width;
            window.height = geo.height;
        }
        if (geo.hasPosition) {
            window.x = geo.x;
            window.y = geo.y;
        }
        // Seed smart-default expansion BEFORE the first build so the
        // owning category of a restored activePage is open from the
        // first paint instead of flashing closed → open.
        _expandActivePageCategory();
        // Build initial sidebar
        _rebuildSidebar();
        // If the active page is a child of a category, drill in. Skip
        // intermediate (`hasChildren: true`) entries so a stale
        // `activePage = "animations-surfaces"` doesn't masquerade as a
        // valid restored leaf — the virtual parent name only ever
        // means "show this sub-sidebar" (drill-down) or "show this
        // inline category" (no drill), and there's no QML component
        // for it in `_pageComponents`. The drill-in transition picks a
        // real leaf for the user when one of those names round-trips
        // through stored state.
        let page = settingsController.activePage;
        let parents = Object.keys(_childItems);
        for (let p = 0; p < parents.length; p++) {
            let children = _childItems[parents[p]];
            for (let c = 0; c < children.length; c++) {
                if (children[c].name === page && !children[c].hasChildren) {
                    // If the owning parent is an inline collapsible
                    // category, drill into its host mode (so the
                    // sub-sidebar opens) — except for top-level main-mode
                    // categories (Display/Rules), which live directly
                    // under main and need _sidebarMode left at "main".
                    // Inline categories are not sidebar modes themselves;
                    // they render expanded under their owner.
                    const cat = _categoryOf(page);
                    const subMode = cat.length > 0 ? _parentModeForCategory(cat) : "";
                    if (subMode.length > 0)
                        _sidebarMode = subMode;
                    else if (cat === "display" || cat === "rules")
                        _sidebarMode = "main";
                    else
                        _sidebarMode = parents[p];
                    _rebuildSidebar();
                    return ;
                }
            }
        }
        // Stale activePage is a virtual parent. Inline category names
        // need to be remapped to a real leaf so the user lands on
        // something other than the silent LayoutsPage fallback. The
        // owning sidebar mode is derived from the category's host
        // bucket: top-level main-mode categories (display/rules) live
        // under "main"; sub-sidebar categories (animations-*-cat,
        // snapping-*-cat, tiling-*-cat) live under their parent mode
        // returned by _parentModeForCategory.
        let staleMode = "";
        for (let m = 0; m < _mainItems.length; m++) {
            if (_mainItems[m].name === page && _mainItems[m].isCollapsible === true) {
                staleMode = "main";
                break;
            }
        }
        if (staleMode.length === 0)
            staleMode = _parentModeForCategory(page);

        if (staleMode.length > 0) {
            _sidebarMode = staleMode;
            const firstLeaf = _firstLeafOf(page);
            if (firstLeaf.length > 0)
                settingsController.activePage = firstLeaf;

            _rebuildSidebar();
            return ;
        }
        if (_childItems[page])
            _drillIn(page);

    }

    // Auto-drill-out if feature disabled while viewing its subpages
    Connections {
        function onSnappingEnabledChanged() {
            if (!appSettings.snappingEnabled && _sidebarMode === "snapping")
                _drillOut();

        }

        function onAutotileEnabledChanged() {
            if (!appSettings.autotileEnabled && _sidebarMode === "tiling")
                _drillOut();

        }

        target: appSettings
    }

    // Auto-expand the owning inline collapsible category when activePage
    // changes to a page inside one (e.g. external navigation lands on
    // animations-windows while Surfaces is collapsed, or on layouts
    // while Display is collapsed). The user should see the selected
    // leaf rather than a collapsed category hiding it.
    Connections {
        function onActivePageChanged() {
            const cat = window._categoryOf(settingsController.activePage);
            if (cat && window._expandedCategories[cat] !== true) {
                let next = Object.assign({
                }, window._expandedCategories);
                next[cat] = true;
                window._expandedCategories = next;
                // Rebuild only when the affected category is on screen.
                // Top-level categories (display/rules) live in main mode;
                // sub-sidebar categories live under the mode returned by
                // _parentModeForCategory (animations, snapping, tiling).
                const topLevel = (cat === "display" || cat === "rules");
                const subMode = window._parentModeForCategory(cat);
                const subVisible = subMode.length > 0 && window._sidebarMode === subMode;
                if ((topLevel && window._sidebarMode === "main") || subVisible)
                    window._rebuildSidebar();

            }
        }

        target: settingsController
    }

    // Visible sidebar model (rebuilt when _sidebarMode changes)
    ListModel {
        id: sidebarModel
    }

    // Sidebar drill-in/out transition animation. While this animation
    // runs, _suppressAccordion is held true so the ListView's accordion
    // add/remove transitions stay quiet — the cross-fade already covers
    // the visual change and per-row animations would fight it.
    SequentialAnimation {
        id: sidebarTransition

        property string pendingMode: ""
        property string pendingPage: ""

        ScriptAction {
            script: window._suppressAccordion = true
        }

        PhosphorMotionAnimation {
            target: sidebar
            properties: "opacity"
            to: 0
            profile: "panel.fadeOut"
        }

        ScriptAction {
            script: {
                window._sidebarMode = sidebarTransition.pendingMode;
                window._rebuildSidebar();
                if (sidebarTransition.pendingPage)
                    settingsController.activePage = sidebarTransition.pendingPage;

            }
        }

        PhosphorMotionAnimation {
            target: sidebar
            properties: "opacity"
            to: 1
            profile: "panel.fadeIn"
        }

        ScriptAction {
            script: window._suppressAccordion = false
        }

    }

    Shortcut {
        sequence: "Ctrl+PgUp"
        onActivated: {
            let idx = sidebar.currentIndex;
            for (let i = idx - 1; i >= 0; i--) {
                let item = sidebarModel.get(i);
                if (!item.isBackButton && !item.hasChildren && !item.isCategory && item.name !== "__divider__") {
                    settingsController.activePage = item.name;
                    return ;
                }
            }
            // At boundary — drill out if in a sub-category
            if (_sidebarMode !== "main")
                _drillOut();

        }
    }

    Shortcut {
        sequence: "Ctrl+PgDown"
        onActivated: {
            let idx = sidebar.currentIndex;
            for (let i = idx + 1; i < sidebarModel.count; i++) {
                let item = sidebarModel.get(i);
                if (!item.isBackButton && !item.hasChildren && !item.isCategory && item.name !== "__divider__") {
                    settingsController.activePage = item.name;
                    return ;
                }
            }
            // At boundary — drill out if in a sub-category
            if (_sidebarMode !== "main")
                _drillOut();

        }
    }

    Shortcut {
        sequence: "?"
        enabled: {
            var item = window.activeFocusItem;
            if (!item)
                return true;

            // A focused QtQuick.Controls TextField/TextArea reports the
            // control (not its internal TextInput/TextEdit) as the
            // activeFocusItem, so the instanceof checks alone miss it.
            // Also treat any item with an editable-text accessible role as
            // a text input so typing `?` into a field never toggles help.
            if (item instanceof TextInput || item instanceof TextEdit)
                return false;

            var role = item.Accessible.role;
            if (role === Accessible.EditableText || role === Accessible.PasswordText)
                return false;

            return true;
        }
        onActivated: window._showShortcuts = !window._showShortcuts
    }

    RowLayout {
        id: mainContent

        anchors.fill: parent
        spacing: 0

        // =================================================================
        // SIDEBAR
        // =================================================================
        Pane {
            id: sidebarPane

            Layout.fillHeight: true
            Layout.preferredWidth: window.sidebarCompact ? Kirigami.Units.gridUnit * 3 : Kirigami.Units.gridUnit * 12
            Layout.minimumWidth: window.sidebarCompact ? Kirigami.Units.gridUnit * 3 : Kirigami.Units.gridUnit * 12
            padding: 0

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Search field
                TextField {
                    id: sidebarSearch

                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    Accessible.name: i18n("Search settings pages")
                    placeholderText: i18n("Search...")
                    visible: !window.sidebarCompact
                    leftPadding: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing * 2
                    onTextChanged: _rebuildSidebar()

                    Kirigami.Icon {
                        source: "search"
                        anchors.left: parent.left
                        anchors.leftMargin: Kirigami.Units.smallSpacing
                        anchors.verticalCenter: parent.verticalCenter
                        width: Kirigami.Units.iconSizes.small
                        height: Kirigami.Units.iconSizes.small
                        opacity: 0.5
                    }

                }

                // Navigation list
                ListView {
                    id: sidebar

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: sidebarModel
                    currentIndex: {
                        for (var i = 0; i < sidebarModel.count; i++) {
                            if (sidebarModel.get(i).name === settingsController.activePage)
                                return i;

                        }
                        return -1;
                    }
                    clip: true

                    // Accordion add/remove/displaced transitions drive the
                    // collapsible category toggle (Display, Rules,
                    // Surfaces, Library). Gated by _suppressAccordion so
                    // they stay quiet during the drill-in/out cross-fade
                    // which already covers the visual change.
                    add: Transition {
                        enabled: !window._suppressAccordion

                        PhosphorMotionAnimation {
                            properties: "opacity"
                            from: 0
                            to: 1
                            profile: "widget.accordionExpand"
                        }

                        PhosphorMotionAnimation {
                            properties: "y"
                            profile: "widget.accordionExpand"
                        }

                    }

                    remove: Transition {
                        enabled: !window._suppressAccordion

                        PhosphorMotionAnimation {
                            properties: "opacity"
                            from: 1
                            to: 0
                            profile: "widget.accordionCollapse"
                        }

                    }

                    displaced: Transition {
                        enabled: !window._suppressAccordion

                        PhosphorMotionAnimation {
                            properties: "y"
                            profile: "widget.accordionExpand"
                        }

                    }

                    delegate: ItemDelegate {
                        id: navDelegate

                        required property int index
                        required property string name
                        required property string label
                        required property string iconName
                        required property bool hasChildren
                        required property bool isBackButton
                        required property bool hasDividerAfter
                        required property bool isDivider
                        required property bool isCategory
                        required property bool categoryExpanded
                        required property bool isCategoryChild
                        // `model` is the full row object — explicit
                        // `required` so strict QML resolution finds it
                        // (the ListView's implicit `model` context property
                        // is rejected in strict mode). We still go through
                        // `model.isSearchChild` rather than declaring the
                        // role as its own `required property bool` because
                        // not every append site sets the role — undefined
                        // coerces to false via the `=== true` check.
                        required property var model
                        readonly property bool isSearchChild: model.isSearchChild === true
                        readonly property bool isActive: {
                            if (isBackButton)
                                return false;

                            if (hasChildren)
                                return false;

                            if (isCategory)
                                return false;

                            return name === settingsController.activePage;
                        }

                        width: sidebar.width
                        height: isDivider ? Kirigami.Units.largeSpacing : (isBackButton ? Kirigami.Units.gridUnit * 2.6 : Kirigami.Units.gridUnit * 2.2)
                        enabled: !isDivider
                        highlighted: isActive
                        onClicked: {
                            if (isCategory) {
                                window._toggleCategory(name);
                                return ;
                            }
                            if (isBackButton) {
                                window._drillOut();
                                return ;
                            }
                            if (hasChildren) {
                                // Block drill-down if the feature is disabled
                                if (name === "snapping" && !appSettings.snappingEnabled)
                                    return ;

                                if (name === "tiling" && !appSettings.autotileEnabled)
                                    return ;

                                window._drillIn(name);
                                return ;
                            }
                            // If selecting an inline search result, clear
                            // the search, drill into the leaf's actual
                            // parent (could be a different sub-sidebar
                            // bucket), and activate the leaf. The lookup
                            // walks every `_childItems` bucket so it
                            // handles top-level search hits AND nested
                            // matches inlined inside a sub-sidebar.
                            if (sidebarSearch.text.length > 0) {
                                let parents = Object.keys(_childItems);
                                for (let p = 0; p < parents.length; p++) {
                                    let children = _childItems[parents[p]];
                                    for (let c = 0; c < children.length; c++) {
                                        if (children[c].name === name) {
                                            sidebarSearch.text = "";
                                            // Map inline-category parent
                                            // buckets to their owning mode —
                                            // inline categories are not
                                            // sidebar modes themselves.
                                            const owner = window._categoryOf(name);
                                            _sidebarMode = owner.length > 0 ? "animations" : parents[p];
                                            _rebuildSidebar();
                                            settingsController.activePage = name;
                                            return ;
                                        }
                                    }
                                }
                            }
                            settingsController.activePage = name;
                        }
                        leftPadding: {
                            const base = window.sidebarCompact ? 0 : Kirigami.Units.smallSpacing;
                            // Inline search-result rows AND inline
                            // collapsible-category children nest under
                            // their parent — bump leftPadding by an
                            // iconSize-equivalent so the indent reads
                            // as hierarchy. Theme-aware via
                            // `Kirigami.Units` and RTL-correct because
                            // `leftPadding` flips with layoutDirection.
                            if (navDelegate.isSearchChild || navDelegate.isCategoryChild)
                                return base + Kirigami.Units.iconSizes.small;

                            return base;
                        }
                        rightPadding: window.sidebarCompact ? 0 : Kirigami.Units.smallSpacing
                        // Strip the "Surfaces / Windows" prefix from
                        // the Accessible.name so screen readers
                        // announce the leaf cleanly without the
                        // visual-hierarchy decoration. The full
                        // composed label still drives the visible
                        // Label and ToolTip below.
                        Accessible.name: {
                            const slashIdx = navDelegate.label.lastIndexOf(" / ");
                            return slashIdx >= 0 ? navDelegate.label.substring(slashIdx + 3) : navDelegate.label;
                        }
                        ToolTip.visible: window.sidebarCompact && navDelegate.hovered
                        ToolTip.text: label
                        ToolTip.delay: 300

                        // Section divider rendering (when this delegate is a divider item)
                        Kirigami.Separator {
                            visible: navDelegate.isDivider
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: Kirigami.Units.largeSpacing
                            anchors.rightMargin: Kirigami.Units.largeSpacing
                        }

                        background: Rectangle {
                            color: {
                                if (navDelegate.isActive)
                                    return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.12);

                                if (navDelegate.hovered)
                                    return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06);

                                return "transparent";
                            }
                            radius: Kirigami.Units.smallSpacing

                            // Left accent bar for active item
                            Rectangle {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.round(Kirigami.Units.devicePixelRatio * 2.5)
                                height: parent.height * 0.5
                                radius: width / 2
                                color: Kirigami.Theme.highlightColor
                                visible: navDelegate.isActive
                            }

                            // Bottom separator after back button
                            Rectangle {
                                visible: navDelegate.isBackButton
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: Kirigami.Units.smallSpacing
                                anchors.rightMargin: Kirigami.Units.smallSpacing
                                height: Math.round(Kirigami.Units.devicePixelRatio)
                                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                            }

                            Behavior on color {
                                PhosphorMotionAnimation {
                                    profile: "widget.tint.fast"
                                }

                            }

                        }

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                source: navDelegate.iconName
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                Layout.fillWidth: window.sidebarCompact
                                Layout.alignment: Qt.AlignVCenter
                                opacity: {
                                    if (navDelegate.isBackButton)
                                        return 0.7;

                                    if (navDelegate.isActive)
                                        return 1;

                                    return 0.7;
                                }

                                Behavior on opacity {
                                    PhosphorMotionAnimation {
                                        profile: "widget.hover"
                                        durationOverride: 120
                                    }

                                }

                            }

                            Label {
                                text: navDelegate.label
                                Layout.fillWidth: true
                                font.weight: {
                                    if (navDelegate.isBackButton)
                                        return Font.DemiBold;

                                    if (navDelegate.isActive)
                                        return Font.DemiBold;

                                    return Font.Normal;
                                }
                                opacity: {
                                    if (navDelegate.isBackButton)
                                        return 0.8;

                                    if (navDelegate.isActive)
                                        return 1;

                                    return 0.7;
                                }
                                visible: !window.sidebarCompact

                                Behavior on opacity {
                                    PhosphorMotionAnimation {
                                        profile: "widget.hover"
                                        durationOverride: 120
                                    }

                                }

                            }

                            // Unsaved changes badge — per-page tracking.
                            // Reference dirtyPages once so QML tracks it as a
                            // binding dependency, then delegate the actual
                            // lookup (including parent-category traversal) to
                            // the controller's isPageDirty().
                            Rectangle {
                                id: dirtyBadge

                                width: Kirigami.Units.smallSpacing * 1.5
                                height: Kirigami.Units.smallSpacing * 1.5
                                radius: width / 2
                                color: Kirigami.Theme.neutralTextColor
                                visible: {
                                    if (navDelegate.isDivider || navDelegate.isBackButton || navDelegate.isCategory)
                                        return false;

                                    settingsController.dirtyPages; // binding dependency
                                    return settingsController.isPageDirty(navDelegate.name);
                                }
                                Layout.alignment: Qt.AlignVCenter

                                SequentialAnimation {
                                    id: dirtyBadgePulse

                                    loops: Animation.Infinite
                                    running: dirtyBadge.visible
                                    onRunningChanged: {
                                        if (!running)
                                            dirtyBadge.opacity = 1;

                                    }

                                    PhosphorMotionAnimation {
                                        target: dirtyBadge
                                        properties: "opacity"
                                        from: 1
                                        to: 0.4
                                        profile: "widget.pulse"
                                    }

                                    PhosphorMotionAnimation {
                                        target: dirtyBadge
                                        properties: "opacity"
                                        from: 0.4
                                        to: 1
                                        profile: "widget.pulse"
                                    }

                                }

                            }

                            // Enable/disable toggle for snapping and tiling.
                            // Wraps the assignment in begin/endExternalEdit so
                            // the dirty marker lands on snapping/tiling rather
                            // than whatever page the user is currently viewing.
                            SettingsSwitch {
                                visible: (navDelegate.name === "snapping" || navDelegate.name === "tiling") && !window.sidebarCompact
                                checked: navDelegate.name === "snapping" ? appSettings.snappingEnabled : appSettings.autotileEnabled
                                accessibleName: navDelegate.label
                                onToggled: function(newValue) {
                                    settingsController.beginExternalEdit(navDelegate.name);
                                    if (navDelegate.name === "snapping")
                                        appSettings.snappingEnabled = newValue;
                                    else
                                        appSettings.autotileEnabled = newValue;
                                    settingsController.endExternalEdit();
                                }
                            }

                            // Right chevron for drill-down items AND inline
                            // collapsible category headers. Rotates 90° to
                            // point down when an inline category is expanded.
                            Kirigami.Icon {
                                source: "go-next"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                Layout.alignment: Qt.AlignVCenter
                                opacity: {
                                    if (navDelegate.name === "snapping" && !appSettings.snappingEnabled)
                                        return 0.15;

                                    if (navDelegate.name === "tiling" && !appSettings.autotileEnabled)
                                        return 0.15;

                                    return 0.3;
                                }
                                visible: (navDelegate.hasChildren || navDelegate.isCategory) && !window.sidebarCompact
                                // Keep the same icon so the visual stays
                                // familiar; rotation is animatable.
                                rotation: navDelegate.categoryExpanded ? 90 : 0

                                Behavior on rotation {
                                    PhosphorMotionAnimation {
                                        profile: "widget.hover"
                                        durationOverride: 150
                                    }

                                }

                            }

                        }

                    }

                }

                // Daemon status
                Pane {
                    Layout.fillWidth: true
                    padding: Kirigami.Units.smallSpacing * 1.5
                    topPadding: Kirigami.Units.smallSpacing * 2
                    bottomPadding: Kirigami.Units.smallSpacing * 2

                    RowLayout {
                        anchors.fill: parent
                        spacing: Kirigami.Units.smallSpacing

                        Rectangle {
                            id: daemonDot

                            width: Kirigami.Units.smallSpacing * 1.5
                            height: Kirigami.Units.smallSpacing * 1.5
                            radius: width / 2
                            Layout.alignment: Qt.AlignVCenter
                            color: settingsController.daemonRunning ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor

                            // Pulsing glow when running
                            SequentialAnimation {
                                id: daemonPulse

                                loops: settingsController.daemonRunning ? Animation.Infinite : 0
                                running: settingsController.daemonRunning
                                onRunningChanged: {
                                    if (!running)
                                        daemonDot.opacity = 1;

                                }

                                PhosphorMotionAnimation {
                                    target: daemonDot
                                    properties: "opacity"
                                    from: 1
                                    to: 0.4
                                    profile: "widget.pulse.slow"
                                }

                                PhosphorMotionAnimation {
                                    target: daemonDot
                                    properties: "opacity"
                                    from: 0.4
                                    to: 1
                                    profile: "widget.pulse.slow"
                                }

                            }

                        }

                        Label {
                            text: settingsController.daemonRunning ? i18n("Running") : i18n("Stopped")
                            opacity: 0.7
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            visible: !window.sidebarCompact
                        }

                        SettingsSwitch {
                            Layout.alignment: Qt.AlignVCenter
                            checked: settingsController.daemonRunning
                            enabled: !settingsController.daemonController.busy
                            accessibleName: i18n("Toggle daemon")
                            onToggled: function(newValue) {
                                settingsController.daemonController.setEnabled(newValue);
                            }
                        }

                    }

                    background: Rectangle {
                        color: "transparent"

                        Rectangle {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: Math.round(Kirigami.Units.devicePixelRatio)
                            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                        }

                    }

                }

            }

            Behavior on Layout.preferredWidth {
                PhosphorMotionAnimation {
                    // Direction is taken from the same `sidebarCompact` flag
                    // that drives `Layout.preferredWidth` above, so the leg
                    // is decided synchronously when the nav rail is toggled.
                    // Reading `Layout.preferredWidth` directly would re-
                    // evaluate during the Behavior and converge to the wrong
                    // leg as the value approaches its target.
                    profile: !window.sidebarCompact ? "panel.slideIn" : "panel.slideOut"
                }

            }

            Behavior on Layout.minimumWidth {
                PhosphorMotionAnimation {
                    // Same `sidebarCompact` driver as above — the rail's
                    // minimumWidth tracks preferredWidth in lockstep.
                    profile: !window.sidebarCompact ? "panel.slideIn" : "panel.slideOut"
                }

            }

            background: Rectangle {
                color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 1)

                // Subtle right edge shadow
                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: Math.round(Kirigami.Units.devicePixelRatio)
                    color: Kirigami.Theme.separatorColor !== undefined ? Kirigami.Theme.separatorColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
                }

            }

        }

        // =================================================================
        // CONTENT AREA
        // =================================================================
        ColumnLayout {
            // Aspect ratio submenu — present unless the right-clicked
            // layout is autotile (showForLayout reconciles its insertion
            // state in lockstep with the layout kind). Rows are driven
            // by an Instantiator over `_aspectRatioOptions` so each
            // ItemDelegate's lifecycle is Qt-managed.

            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // -- Breadcrumb header ----------------------------------------
            // Breadcrumb — always visible (essential in compact mode for context)
            Pane {
                Layout.fillWidth: true
                padding: Kirigami.Units.largeSpacing
                topPadding: Kirigami.Units.smallSpacing * 2
                bottomPadding: Kirigami.Units.smallSpacing * 2

                RowLayout {
                    anchors.fill: parent
                    spacing: Kirigami.Units.smallSpacing

                    Row {
                        spacing: Kirigami.Units.smallSpacing

                        // Render every lineage segment + a separator
                        // between them. The `_breadcrumbModel` cached
                        // property holds the precomputed segment list
                        // and re-evaluates only when its inputs
                        // (`_sidebarMode`, `activePage`) change — far
                        // cheaper than calling `_breadcrumbSegments()`
                        // once per delegate per binding fire.
                        Repeater {
                            model: window._breadcrumbModel

                            delegate: Row {
                                required property int index
                                required property var modelData

                                spacing: Kirigami.Units.smallSpacing

                                Label {
                                    text: modelData.label
                                    opacity: modelData.clickable && segmentMouse.containsMouse ? 0.8 : 0.5
                                    font.underline: modelData.clickable && segmentMouse.containsMouse
                                    Accessible.name: modelData.label
                                    Accessible.role: modelData.clickable ? Accessible.Link : Accessible.StaticText

                                    MouseArea {
                                        id: segmentMouse

                                        anchors.fill: parent
                                        hoverEnabled: modelData.clickable
                                        enabled: modelData.clickable
                                        cursorShape: modelData.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: window._navigateToBreadcrumbSegment(modelData.name)
                                    }

                                }

                                Label {
                                    visible: index < window._breadcrumbModel.length - 1
                                    text: "\u203A"
                                    opacity: 0.5
                                }

                            }

                        }

                    }

                    Item {
                        Layout.fillWidth: true
                    }

                }

                background: Rectangle {
                    color: "transparent"

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: Math.round(Kirigami.Units.devicePixelRatio)
                        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06)
                    }

                }

            }

            // ── Update banner (visible on all pages) ─────────────────────
            Pane {
                id: updateBanner

                Layout.fillWidth: true
                visible: settingsController.updateChecker.updateAvailable && settingsController.updateChecker.latestVersion !== settingsController.dismissedUpdateVersion
                padding: Kirigami.Units.smallSpacing
                topPadding: Kirigami.Units.smallSpacing
                bottomPadding: Kirigami.Units.smallSpacing

                RowLayout {
                    anchors.fill: parent
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: "update-none"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        color: Kirigami.Theme.positiveTextColor
                    }

                    Label {
                        text: i18n("PlasmaZones %1 is available", settingsController.updateChecker.latestVersion)
                        Layout.fillWidth: true
                        color: Kirigami.Theme.positiveTextColor
                    }

                    Button {
                        text: i18n("View Release")
                        flat: true
                        icon.name: "internet-web-browser"
                        visible: settingsController.updateChecker.releaseUrl.length > 0
                        Accessible.name: i18n("View release notes")
                        onClicked: Qt.openUrlExternally(settingsController.updateChecker.releaseUrl)
                    }

                    ToolButton {
                        icon.name: "dialog-close"
                        display: ToolButton.IconOnly
                        onClicked: settingsController.dismissUpdate()
                        Accessible.name: i18n("Dismiss update notification")
                        ToolTip.text: i18n("Dismiss")
                        ToolTip.visible: hovered
                    }

                }

                background: Rectangle {
                    color: Qt.rgba(Kirigami.Theme.positiveTextColor.r, Kirigami.Theme.positiveTextColor.g, Kirigami.Theme.positiveTextColor.b, 0.15)
                    border.width: Math.round(Kirigami.Units.devicePixelRatio)
                    border.color: Qt.rgba(Kirigami.Theme.positiveTextColor.r, Kirigami.Theme.positiveTextColor.g, Kirigami.Theme.positiveTextColor.b, 0.3)
                }

            }

            // ── What's New banner (visible when unseen changes exist) ──
            Pane {
                id: whatsNewBanner

                Layout.fillWidth: true
                visible: settingsController.hasUnseenWhatsNew
                padding: Kirigami.Units.smallSpacing
                topPadding: Kirigami.Units.smallSpacing
                bottomPadding: Kirigami.Units.smallSpacing

                RowLayout {
                    anchors.fill: parent
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: "documentinfo"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        color: Kirigami.Theme.linkColor
                    }

                    Label {
                        text: i18n("See what's new in PlasmaZones %1", Qt.application.version)
                        Layout.fillWidth: true
                        color: Kirigami.Theme.linkColor
                    }

                    Button {
                        text: i18n("What's New")
                        flat: true
                        icon.name: "go-next"
                        Accessible.name: i18n("View what's new")
                        onClicked: whatsNewDialog.open()
                    }

                    ToolButton {
                        icon.name: "dialog-close"
                        display: ToolButton.IconOnly
                        onClicked: settingsController.markWhatsNewSeen()
                        Accessible.name: i18n("Dismiss")
                        ToolTip.text: i18n("Dismiss")
                        ToolTip.visible: hovered
                    }

                }

                background: Rectangle {
                    color: Qt.rgba(Kirigami.Theme.linkColor.r, Kirigami.Theme.linkColor.g, Kirigami.Theme.linkColor.b, 0.15)
                    border.width: Math.round(Kirigami.Units.devicePixelRatio)
                    border.color: Qt.rgba(Kirigami.Theme.linkColor.r, Kirigami.Theme.linkColor.g, Kirigami.Theme.linkColor.b, 0.3)
                }

            }

            // Page content with crossfade transition
            Item {
                id: pageContainer

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                // Opaque background to prevent bleed-through
                Rectangle {
                    anchors.fill: parent
                    color: Kirigami.Theme.backgroundColor
                }

                Loader {
                    id: pageLoader

                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.largeSpacing
                    source: window._resolvePageSource(settingsController.activePage)
                    asynchronous: false
                    // Fade in on page change
                    onLoaded: {
                        fadeIn.restart();
                    }

                    PhosphorMotionAnimation {
                        id: fadeIn

                        target: pageLoader.item
                        properties: "opacity"
                        from: 0
                        to: 1
                        profile: "widget.fadeIn"
                        durationOverride: 180
                    }

                }

                // -- Toast notification -----------------------------------
                Rectangle {
                    id: toast

                    property string message: ""

                    function show(msg) {
                        message = msg;
                        toastShow.restart();
                        toastHide.restart();
                    }

                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Kirigami.Units.largeSpacing * 2
                    width: toastLabel.implicitWidth + Kirigami.Units.largeSpacing * 3
                    height: toastLabel.implicitHeight + Kirigami.Units.largeSpacing * 1.5
                    radius: height / 2
                    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.85)
                    opacity: 0
                    visible: opacity > 0
                    z: 100

                    Label {
                        id: toastLabel

                        anchors.centerIn: parent
                        text: toast.message
                        color: Kirigami.Theme.backgroundColor
                        font.weight: Font.Medium
                    }

                    PhosphorMotionAnimation {
                        id: toastShow

                        target: toast
                        properties: "opacity"
                        from: 0
                        to: 1
                        profile: "popup"
                        durationOverride: 200
                    }

                    SequentialAnimation {
                        id: toastHide

                        PauseAnimation {
                            duration: 2000
                        }

                        PhosphorMotionAnimation {
                            target: toast
                            properties: "opacity"
                            from: 1
                            to: 0
                            profile: "widget.fadeOut"
                        }

                    }

                }

            }

            // ── Layout context menu (lives outside Loader to avoid Qt6 SIGSEGV on Menu destruction) ──
            Menu {
                id: layoutContextMenu

                property var layout: null
                property int viewMode: 0
                /// Tracks the kind (`"snap"` / `"autotile"` / `"none"`) the
                /// aspect-ratio submenu was last reconciled to. showForLayout
                /// only mutates the menu when the current layout's kind
                /// differs from this state — `removeMenu` `deleteLater`s
                /// Qt 6's auto-generated MenuItem placeholder, and the
                /// inline submenu's reparenting back to its declared parent
                /// is unreliable enough that doing the dance on every show
                /// can lose the QML object after many cycles.
                property string _aspectRatioMenuKind: "none"
                readonly property bool isAutotile: layout && layout.isAutotile === true
                readonly property string layoutId: layout ? (layout.id || "") : ""
                /// Aspect ratio options: `[key, label, settingsIndex]`. Drives
                /// the `Instantiator` inside `aspectRatioSubMenu` so each row's
                /// ItemDelegate is created with a stable Qt-managed lifecycle.
                /// Imperative `Component.createObject(menu, ...)` parented to
                /// a popup that isn't yet in the graphics scene emits the
                /// `Created graphical object was not placed in the graphics
                /// scene` warning and the unplaced object can leak into Qt's
                /// menu state.
                readonly property var _aspectRatioOptions: [["any", window.aspectRatioLabels["any"], 0], ["standard", window.aspectRatioLabels["standard"], 1], ["ultrawide", window.aspectRatioLabels["ultrawide"], 2], ["super-ultrawide", window.aspectRatioLabels["super-ultrawide"], 3], ["portrait", window.aspectRatioLabels["portrait"], 4]]
                /// Driver for the dynamic "Edit on <screen>" Instantiator
                /// below. Empty array hides every dynamic row; switching to
                /// the `screens` list grows them. Single-screen setups never
                /// populate the model so the submenu collapses cleanly.
                readonly property var _screenItemsModel: {
                    var screens = settingsController.screens || [];
                    return screens.length > 1 ? screens : [];
                }

                // Signals for dialogs that live in LayoutsPage
                signal deleteRequested(var layout)
                signal exportRequested(string layoutId)

                function showForLayout(layout) {
                    layoutContextMenu.layout = layout;
                    layoutContextMenu.viewMode = (layout && layout.isAutotile === true) ? 1 : 0;
                    var wantKind = layoutContextMenu.isAutotile ? "autotile" : "snap";
                    // Only reconcile the submenu when the layout kind flips.
                    // Reconciling on every show churns Qt 6's MenuItem
                    // placeholder; after enough churn the inline submenu's
                    // reparenting back to its declared parent fails and
                    // the QML object is lost.
                    if (wantKind !== layoutContextMenu._aspectRatioMenuKind) {
                        if (wantKind === "snap") {
                            // Insert after the aspectRatioMarker separator so
                            // the submenu lands in its declared visual slot
                            // even though it's added imperatively. itemAt
                            // walks the menu's children, which the inline
                            // separator joined at parse time.
                            var markerIdx = -1;
                            for (var k = 0; k < layoutContextMenu.count; k++) {
                                if (layoutContextMenu.itemAt(k) === aspectRatioMarker) {
                                    markerIdx = k;
                                    break;
                                }
                            }
                            if (markerIdx >= 0)
                                layoutContextMenu.insertMenu(markerIdx + 1, aspectRatioSubMenu);
                            else
                                layoutContextMenu.addMenu(aspectRatioSubMenu);
                        } else {
                            layoutContextMenu.removeMenu(aspectRatioSubMenu);
                        }
                        layoutContextMenu._aspectRatioMenuKind = wantKind;
                    }
                    layoutContextMenu.popup();
                }

                // -- Edit --
                MenuItem {
                    text: i18n("Edit")
                    icon.name: "document-edit"
                    onTriggered: settingsController.editLayout(layoutContextMenu.layoutId)
                }

                // Dynamic "Edit on <screen>" items. Instantiator gives Qt
                // ownership of each row's lifecycle — rows are placed when
                // the model grows and torn down synchronously when it
                // shrinks, with no out-of-scene `createObject` parents and
                // no deferred `destroy()` racing the next popup. The
                // delegate is `ItemDelegate` rather than `MenuItem` to
                // bypass Qt 6's onItemTriggered → dismiss() cascade through
                // `finalizeExitTransition`; the click handler hides the
                // menu explicitly via `Qt.callLater` after the click body
                // finishes.
                Instantiator {
                    id: screenItemInstantiator

                    model: layoutContextMenu._screenItemsModel
                    onObjectAdded: function(index, object) {
                        // The Edit MenuItem occupies index 0; dynamic rows
                        // sit immediately after, ahead of screenSeparator
                        // and the rest of the static menu.
                        layoutContextMenu.insertItem(1 + index, object);
                    }
                    onObjectRemoved: function(index, object) {
                        layoutContextMenu.removeItem(object);
                    }

                    delegate: ItemDelegate {
                        required property var modelData
                        readonly property string _screenName: (modelData && modelData.name) ? modelData.name : ""

                        text: i18n("Edit on %1", (modelData && modelData.displayLabel) || (modelData && modelData.name) || "")
                        icon.name: (modelData && modelData.isPrimary) ? "starred-symbolic" : "monitor"
                        Accessible.name: text
                        onClicked: {
                            var screenName = _screenName;
                            var layoutId = layoutContextMenu.layoutId;
                            Qt.callLater(function() {
                                layoutContextMenu.visible = false;
                                if (screenName.length > 0)
                                    settingsController.editLayoutOnScreen(layoutId, screenName);

                            });
                        }
                    }

                }

                // Tracks the dynamic-row model directly so the separator
                // hides when no extra screens exist — including live
                // changes to `settingsController.screens` while the menu
                // is open.
                MenuSeparator {
                    id: screenSeparator

                    visible: layoutContextMenu._screenItemsModel.length > 0
                }

                // -- Open in Editor (external text editor) --
                MenuItem {
                    text: i18n("Open in Text Editor")
                    icon.name: "document-open"
                    Accessible.name: text
                    onTriggered: {
                        if (layoutContextMenu.isAutotile)
                            settingsController.openAlgorithm(settingsController.algorithmIdFromLayoutId(layoutContextMenu.layoutId));
                        else
                            settingsController.openLayoutFile(layoutContextMenu.layoutId);
                    }
                }

                MenuSeparator {
                }

                // -- State --
                MenuItem {
                    text: i18n("Set as Default")
                    icon.name: "favorite"
                    enabled: {
                        if (!layoutContextMenu.layout)
                            return false;

                        if (layoutContextMenu.viewMode === 1)
                            return layoutContextMenu.layoutId !== ("autotile:" + appSettings.defaultAutotileAlgorithm);

                        return layoutContextMenu.layoutId !== appSettings.defaultLayoutId;
                    }
                    onTriggered: {
                        if (layoutContextMenu.viewMode === 1)
                            appSettings.defaultAutotileAlgorithm = layoutContextMenu.layoutId.replace("autotile:", "");
                        else
                            appSettings.defaultLayoutId = layoutContextMenu.layoutId;
                    }
                }

                MenuItem {
                    text: layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector ? i18n("Show in Zone Selector") : i18n("Hide from Zone Selector")
                    icon.name: layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector ? "view-visible" : "view-hidden"
                    onTriggered: settingsController.setLayoutHidden(layoutContextMenu.layoutId, !(layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector))
                }

                MenuItem {
                    readonly property bool perLayoutAuto: layoutContextMenu.layout && layoutContextMenu.layout.autoAssign === true
                    readonly property bool globalAuto: appSettings.autoAssignAllLayouts === true

                    // When the global "Auto-assign for all layouts" toggle is on (#370),
                    // every layout effectively auto-assigns regardless of its per-layout flag,
                    // so the per-layout toggle is preserved but disabled here. The label
                    // points the user at the global setting that's overriding it.
                    text: globalAuto ? i18n("Auto-assign forced on (global setting)") : (perLayoutAuto ? i18n("Disable Auto-assign") : i18n("Enable Auto-assign"))
                    icon.name: (perLayoutAuto || globalAuto) ? "window-duplicate" : "window-new"
                    visible: !layoutContextMenu.isAutotile
                    enabled: !globalAuto
                    onTriggered: settingsController.setLayoutAutoAssign(layoutContextMenu.layoutId, !perLayoutAuto)
                }

                // -- Aspect Ratio insertion point (submenu managed imperatively in showForLayout) --
                MenuSeparator {
                    id: aspectRatioMarker

                    visible: !layoutContextMenu.isAutotile
                }

                // -- Manage (snapping layouts) --
                MenuSeparator {
                    visible: layoutContextMenu.viewMode === 0 && !layoutContextMenu.isAutotile
                }

                MenuItem {
                    text: i18n("Duplicate")
                    icon.name: "edit-copy"
                    visible: layoutContextMenu.viewMode === 0 && !layoutContextMenu.isAutotile
                    onTriggered: settingsController.duplicateLayout(layoutContextMenu.layoutId)
                }

                MenuItem {
                    text: i18n("Export")
                    icon.name: "document-export"
                    visible: layoutContextMenu.viewMode === 0 && !layoutContextMenu.isAutotile
                    onTriggered: layoutContextMenu.exportRequested(layoutContextMenu.layoutId)
                }

                MenuSeparator {
                    visible: layoutContextMenu.viewMode === 0 && layoutContextMenu.layout && !layoutContextMenu.layout.isSystem && !layoutContextMenu.isAutotile
                }

                MenuItem {
                    text: i18n("Delete")
                    icon.name: "edit-delete"
                    visible: layoutContextMenu.viewMode === 0 && layoutContextMenu.layout && !layoutContextMenu.layout.isSystem && !layoutContextMenu.isAutotile
                    onTriggered: layoutContextMenu.deleteRequested(layoutContextMenu.layout)
                }

                // -- Algorithms: Manage --
                MenuSeparator {
                    // Only show if at least one item below is visible (Duplicate/Export always
                    // are, so this fires for any autotile entry — but keeps the separator hidden
                    // when !isAutotile, avoiding a dangling line for snapping layouts).
                    visible: layoutContextMenu.isAutotile
                }

                MenuItem {
                    text: i18n("Duplicate")
                    icon.name: "edit-copy"
                    visible: layoutContextMenu.isAutotile
                    onTriggered: settingsController.duplicateAlgorithm(settingsController.algorithmIdFromLayoutId(layoutContextMenu.layoutId))
                }

                MenuItem {
                    text: i18n("Export")
                    icon.name: "document-export"
                    visible: layoutContextMenu.isAutotile
                    onTriggered: layoutContextMenu.exportRequested(layoutContextMenu.layoutId)
                }

                MenuSeparator {
                    visible: layoutContextMenu.isAutotile && layoutContextMenu.layout && !layoutContextMenu.layout.isSystem
                }

                MenuItem {
                    text: i18n("Delete")
                    icon.name: "edit-delete"
                    visible: layoutContextMenu.isAutotile && layoutContextMenu.layout && !layoutContextMenu.layout.isSystem
                    onTriggered: layoutContextMenu.deleteRequested(layoutContextMenu.layout)
                }

            }

            // Empty `enter` / `exit` Transitions are the
            // `finalizeExitTransition` hardening pattern (mirrors the
            // editor's metadata-preset menu): synchronous close avoids
            // the QQmlData::destroyed race Qt 6's animated Menu teardown
            // can otherwise hit.
            Menu {
                id: aspectRatioSubMenu

                title: i18n("Aspect Ratio")
                icon.name: "view-fullscreen"

                Instantiator {
                    id: aspectRatioItemInstantiator

                    model: layoutContextMenu._aspectRatioOptions
                    onObjectAdded: function(index, object) {
                        aspectRatioSubMenu.insertItem(index, object);
                    }
                    onObjectRemoved: function(index, object) {
                        aspectRatioSubMenu.removeItem(object);
                    }

                    delegate: ItemDelegate {
                        required property var modelData
                        readonly property string _arKey: (modelData && modelData[0]) ? modelData[0] : ""
                        readonly property int _arIndex: (modelData && modelData[2] !== undefined) ? modelData[2] : 0
                        // Bound off the layout's aspect-ratio class so the
                        // check mark tracks the current selection without
                        // any imperative refresh hook on the submenu —
                        // such a hook is what fails when the submenu's
                        // QML object goes null during teardown.
                        readonly property bool isSelected: {
                            var current = (layoutContextMenu.layout && layoutContextMenu.layout.aspectRatioClass) || "any";
                            return _arKey === current;
                        }

                        text: (modelData && modelData[1]) ? modelData[1] : ""
                        icon.name: isSelected ? "checkmark" : ""
                        Accessible.name: text
                        onClicked: {
                            var layoutId = layoutContextMenu.layoutId;
                            var idx = _arIndex;
                            Qt.callLater(function() {
                                aspectRatioSubMenu.visible = false;
                                layoutContextMenu.visible = false;
                                settingsController.setLayoutAspectRatio(layoutId, idx);
                            });
                        }
                    }

                }

                enter: Transition {
                }

                exit: Transition {
                }

            }

            // -- Footer action bar ----------------------------------------
            Rectangle {
                Layout.fillWidth: true
                height: Math.round(Kirigami.Units.devicePixelRatio)
                color: settingsController.needsSave ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)

                Behavior on color {
                    PhosphorMotionAnimation {
                        profile: "widget.tint"
                    }

                }

            }

            // -- Unsaved changes notification bar -------------------------
            Item {
                id: unsavedBar

                Layout.fillWidth: true
                implicitHeight: settingsController.needsSave ? unsavedBarContent.implicitHeight : 0
                clip: true

                Rectangle {
                    id: unsavedBarContent

                    width: parent.width
                    implicitHeight: unsavedBarRow.implicitHeight + Kirigami.Units.smallSpacing * 3
                    anchors.bottom: parent.bottom
                    color: Qt.rgba(Kirigami.Theme.neutralTextColor.r, Kirigami.Theme.neutralTextColor.g, Kirigami.Theme.neutralTextColor.b, 0.12)

                    // Top accent line
                    Rectangle {
                        anchors.top: parent.top
                        width: parent.width
                        height: Math.round(Kirigami.Units.devicePixelRatio)
                        color: Kirigami.Theme.neutralTextColor
                        opacity: 0.4
                    }

                    RowLayout {
                        id: unsavedBarRow

                        anchors.fill: parent
                        anchors.leftMargin: Kirigami.Units.largeSpacing
                        anchors.rightMargin: Kirigami.Units.largeSpacing
                        anchors.topMargin: Kirigami.Units.smallSpacing * 1.5
                        anchors.bottomMargin: Kirigami.Units.smallSpacing * 1.5
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            source: "dialog-information"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            color: Kirigami.Theme.neutralTextColor
                        }

                        Label {
                            text: i18n("Unsaved changes")
                            color: Kirigami.Theme.neutralTextColor
                            Layout.fillWidth: true
                        }

                        Button {
                            text: i18n("Discard")
                            icon.name: "edit-undo"
                            flat: true
                            Accessible.name: i18n("Discard changes")
                            onClicked: resetConfirmDialog.open()
                        }

                        Button {
                            text: i18n("Save")
                            icon.name: "document-save"
                            highlighted: true
                            Accessible.name: i18n("Save settings")
                            onClicked: {
                                settingsController.save();
                                toast.show(i18n("Settings saved"));
                            }
                        }

                    }

                }

                Behavior on implicitHeight {
                    PhosphorMotionAnimation {
                        // Direction is taken from `needsSave` (the same flag
                        // that drives `implicitHeight` for this bar) so the
                        // leg is decided when needsSave flips. Reading the
                        // animated `implicitHeight` would re-evaluate during
                        // the Behavior and converge to the wrong leg as the
                        // value approaches its target.
                        profile: settingsController.needsSave ? "widget.accordionExpand" : "widget.accordionCollapse"
                    }

                }

            }

        }

    }

    // ── Unsaved changes confirmation dialog ────────────────────────
    Kirigami.PromptDialog {
        id: unsavedChangesDialog

        title: i18n("Unsaved Changes")
        subtitle: i18n("You have unsaved changes. Do you want to apply them before closing?")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Apply")
                icon.name: "dialog-ok-apply"
                onTriggered: {
                    unsavedChangesDialog.close();
                    settingsController.save();
                    window._closeConfirmed = true;
                    window.close();
                }
            },
            Kirigami.Action {
                text: i18n("Discard")
                icon.name: "edit-delete"
                onTriggered: {
                    unsavedChangesDialog.close();
                    window._closeConfirmed = true;
                    window.close();
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: unsavedChangesDialog.close()
            }
        ]
    }

    // ── Reset confirmation dialog ───────────────────────────────────
    Kirigami.PromptDialog {
        id: resetConfirmDialog

        title: i18n("Discard Changes")
        subtitle: i18n("Are you sure you want to discard all unsaved changes?")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Discard")
                icon.name: "edit-undo"
                onTriggered: {
                    resetConfirmDialog.close();
                    settingsController.load();
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: resetConfirmDialog.close()
            }
        ]
    }

    // ── Defaults confirmation dialog ────────────────────────────────
    Kirigami.PromptDialog {
        id: defaultsConfirmDialog

        title: i18n("Restore Defaults")
        subtitle: i18n("Are you sure you want to reset all settings to their default values?")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Restore Defaults")
                icon.name: "document-revert"
                onTriggered: {
                    defaultsConfirmDialog.close();
                    settingsController.defaults();
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: defaultsConfirmDialog.close()
            }
        ]
    }

    // ── Keyboard shortcut overlay ──────────────────────────────────
    Rectangle {
        id: shortcutOverlay

        anchors.fill: parent
        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.6)
        visible: opacity > 0
        opacity: window._showShortcuts ? 1 : 0
        z: 200
        Keys.onEscapePressed: window._showShortcuts = false
        focus: window._showShortcuts

        MouseArea {
            anchors.fill: parent
            onClicked: window._showShortcuts = false
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width * 0.6, Kirigami.Units.gridUnit * 30)
            height: shortcutContent.implicitHeight + Kirigami.Units.largeSpacing * 3
            radius: Kirigami.Units.smallSpacing * 2
            color: Kirigami.Theme.backgroundColor
            border.width: Math.round(Kirigami.Units.devicePixelRatio)
            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)

            ColumnLayout {
                id: shortcutContent

                anchors.fill: parent
                anchors.margins: Kirigami.Units.largeSpacing * 1.5
                spacing: Kirigami.Units.largeSpacing

                Label {
                    text: i18n("Keyboard Shortcuts")
                    font.weight: Font.DemiBold
                    font.pixelSize: Kirigami.Units.gridUnit * 1.2
                    Layout.alignment: Qt.AlignHCenter
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                // Shortcut entries
                Repeater {
                    model: [{
                        "key": "Meta+Shift+P",
                        "action": i18n("Open PlasmaZones Settings")
                    }, {
                        "key": "Meta+Shift+E",
                        "action": i18n("Open Zone Editor")
                    }, {
                        "key": "Ctrl+PgUp",
                        "action": i18n("Previous page")
                    }, {
                        "key": "Ctrl+PgDown",
                        "action": i18n("Next page")
                    }, {
                        "key": "?",
                        "action": i18n("Toggle this overlay")
                    }]

                    delegate: RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: modelData.action
                            Layout.fillWidth: true
                            opacity: 0.7
                        }

                        Rectangle {
                            implicitWidth: keyLabel.implicitWidth + Kirigami.Units.largeSpacing
                            implicitHeight: keyLabel.implicitHeight + Kirigami.Units.smallSpacing
                            radius: Kirigami.Units.smallSpacing / 2
                            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)
                            border.width: Math.round(Kirigami.Units.devicePixelRatio)
                            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)

                            Label {
                                id: keyLabel

                                anchors.centerIn: parent
                                text: modelData.key
                                font: Kirigami.Theme.smallFont
                            }

                        }

                    }

                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                Label {
                    text: i18n("Press ? or Escape to close")
                    opacity: 0.4
                    Layout.alignment: Qt.AlignHCenter
                    font: Kirigami.Theme.smallFont
                }

            }

        }

        Behavior on opacity {
            PhosphorMotionAnimation {
                // Direction is taken from `_showShortcuts` (the same flag
                // driving `opacity` above) so the leg is decided when the
                // overlay is toggled.
                profile: window._showShortcuts ? "widget.fadeIn" : "widget.fadeOut"
                durationOverride: 200
            }

        }

    }

    // ── What's New dialog ──────────────────────────────────────────
    WhatsNewPage {
        id: whatsNewDialog
    }

    // Auto-show What's New dialog on first launch after update
    Timer {
        interval: 500
        running: settingsController.hasUnseenWhatsNew
        onTriggered: whatsNewDialog.open()
    }

}
