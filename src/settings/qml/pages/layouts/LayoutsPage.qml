// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import "../../js/GroupSortLogic.js" as Core
import "../../js/LayoutFilterLogic.js" as Logic
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * @brief Layouts / tiling-algorithms listing page.
 *
 * Uses the standardized card-container pattern (see ShaderBrowserPage /
 * RulesPage): a SettingsFlickable root whose content is the view switch,
 * the import card, the search row, the filter/group/sort bar, and one
 * collapsible SettingsCard per group, each hosting a responsive Flow of
 * LayoutGridDelegate cards. Rooting on
 * SettingsFlickable + per-layout reveal anchors (LayoutGridDelegate registers
 * "layout:<id>") gives the page global-search reveal + deep-linking parity with
 * the other listing pages.
 */
SettingsFlickable {
    id: root

    // Capture the context property so child components can access it via root.settingsBridge
    readonly property var settingsBridge: appSettings
    // Capture the settingsController context property so it can be passed
    // explicitly to LayoutGridDelegate (whose own `settingsController` property
    // would otherwise shadow the context name inside the delegate's bindings).
    readonly property var controllerBridge: settingsController
    // Minimum card size. The grid fits as many columns as minCardWidth allows
    // and stretches the cards to fill the row; each card's height then scales
    // with its stretched width at the cardHeight:minCardWidth ratio, so these
    // two set the minimum size AND the fixed aspect of every card.
    readonly property real cardHeight: Kirigami.Units.gridUnit * 12
    readonly property real minCardWidth: Kirigami.Units.gridUnit * 14
    // View mode: 0 = Snapping Layouts, 1 = Auto Tile Algorithms
    property int viewMode: 0
    // Selected layout id (tracked across group cards).
    property string selectedLayoutId: ""
    // Grouped, filtered, sorted layouts — `[{ label, items: [...] }]` from
    // GroupSortLogic.finalizeGroups (Core). Rebuilt by rebuildModel().
    property var groupsModel: []

    // Capability group definitions for tiling view — hoisted to avoid
    // recreating on every rebuildModel() call. NOTE: items matching multiple
    // capabilities appear in multiple groups; the same card is highlighted in
    // both sections simultaneously.
    readonly property var tilingCapabilityGroups: [
        {
            "key": "masterCount",
            "label": i18n("Master Count"),
            "order": 0,
            "test": a => {
                return a.supportsMasterCount === true;
            }
        },
        {
            "key": "splitRatio",
            "label": i18n("Split Ratio"),
            "order": 1,
            "test": a => {
                return a.supportsSplitRatio === true;
            }
        },
        {
            "key": "overlapping",
            "label": i18n("Overlapping Zones"),
            "order": 2,
            "test": a => {
                return a.producesOverlappingZones === true;
            }
        },
        {
            "key": "persistent",
            "label": i18n("Persistent (Memory)"),
            "order": 3,
            "test": a => {
                return a.supportsMemory === true;
            }
        },
        {
            "key": "customParams",
            "label": i18n("Custom Parameters"),
            "order": 4,
            "test": a => {
                return a.supportsCustomParams === true;
            }
        },
        {
            "key": "reflowsOnResize",
            "label": i18n("Reflows"),
            "order": 5,
            "test": a => {
                return a.reflowsOnResize === true;
            }
        },
        {
            "key": "scriptState",
            "label": i18n("Script State"),
            "order": 6,
            "test": a => {
                return a.supportsScriptState === true;
            }
        },
        {
            "key": "singleWindow",
            "label": i18n("Single Window"),
            "order": 7,
            "test": a => {
                return a.supportsSingleWindow === true;
            }
        },
        {
            "key": "reflowsOnFocus",
            "label": i18n("Follows Focus"),
            "order": 8,
            "test": a => {
                return a.reflowsOnFocus === true;
            }
        }
    ]

    // Qt's FileDialog returns selectedFile as a percent-encoded QUrl —
    // a folder named "My Layouts" comes back as
    // file:///home/user/My%20Layouts. Delegate to the controller's
    // urlToLocalFile() (which calls QUrl::toLocalFile()) so percent-
    // decoding, embedded query/fragment, and non-trivial schemes are
    // all handled by Qt's canonical path.
    function filePathFromUrl(url) {
        return settingsController.urlToLocalFile(url);
    }

    function rebuildModel() {
        // Hold the current height across the delegate teardown the model swap
        // below causes, or the page snaps to the top whenever a layout is
        // edited while scrolled down.
        root.holdContentHeight();
        let allLayouts = settingsController.layouts;
        let filtered = [];
        for (let i = 0; i < allLayouts.length; i++) {
            let isAutotile = allLayouts[i].isAutotile === true;
            if (root.viewMode === 0 && !isAutotile)
                filtered.push(allLayouts[i]);
            else if (root.viewMode === 1 && isAutotile)
                filtered.push(allLayouts[i]);
        }
        let search = filterBar.filterText.toLowerCase();
        if (root.viewMode === 0)
            filtered = Logic.applySnappingFilters(filtered, search, filterBar);
        else
            filtered = Logic.applyTilingFilters(filtered, search, filterBar);
        let groups = root.buildGroups(filtered, filterBar.groupByIndex);
        // The priority order stores ids in each mode's own namespace: snapping
        // layouts by bare UUID (matches the cards), tiling algorithms by bare
        // algorithm id ("bsp"). The tiling cards are keyed "autotile:<id>", so
        // prefix the tiling order to the card namespace before matching, or the
        // Priority sort silently no-ops for the tiling view.
        let customOrder = root.viewMode === 0 ? settingsController.effectiveSnappingOrder() : settingsController.effectiveTilingOrder().map(id => "autotile:" + id);
        Logic.sortItems(groups, filterBar.sortByIndex, filterBar.sortAscending, customOrder);
        // "None" is the one grouping that should still show its (single) card
        // header — the user wants the title + count even with no grouping. Every
        // other grouping keeps the legacy "drop the header when it collapses to a
        // single group" behaviour (a lone "Built-in" card needs no redundant
        // header).
        let isNoneGroup = root.viewMode === 0 ? filterBar.groupByIndex === filterBar.groupSnappingNone : filterBar.groupByIndex === filterBar.groupTilingNone;
        root.groupsModel = Core.finalizeGroups(groups, isNoneGroup);
    }

    function selectDefaultLayout(mode) {
        let defaultId;
        if (mode === 1) {
            const algo = root.settingsBridge.defaultAutotileAlgorithm;
            // Empty algo would produce a truthy "autotile:" sentinel that
            // sails through the `if (defaultId)` guard below and selects a
            // nonsense layout id. Treat empty algo as "no default".
            defaultId = algo.length > 0 ? ("autotile:" + algo) : "";
        } else {
            defaultId = root.settingsBridge.defaultLayoutId;
        }
        if (defaultId) {
            root.selectedLayoutId = defaultId;
            return;
        }
        // Fall back to the first available layout id in the current view —
        // without this, a fresh install with no default set leaves no card
        // highlighted and the keyboard-driven edit shortcut (Return) silently
        // does nothing. Walk the rebuilt groups so the fallback honours the
        // current filter state. Mirrors the snap/autotile split above so the
        // autotile mode doesn't accidentally select a bare snap layout.
        const sections = root.groupsModel || [];
        for (let i = 0; i < sections.length; ++i) {
            const items = sections[i].items || [];
            for (let j = 0; j < items.length; ++j) {
                const item = items[j];
                const itemIsAutotile = item.isAutotile === true;
                if ((mode === 1) === itemIsAutotile && item.id) {
                    root.selectedLayoutId = String(item.id);
                    return;
                }
            }
        }
    }

    // Empty-string layoutId is treated as "no-op, keep current selection" so
    // the post-add Qt.callLater path can pass through unrejected when the
    // newly-added layout has no id yet.
    function selectLayoutById(layoutId) {
        if (layoutId)
            root.selectedLayoutId = layoutId;
    }

    // Grouping — kept in QML because group labels require i18n/i18np.
    function buildGroups(filtered, groupIdx) {
        if (root.viewMode === 1) {
            if (groupIdx === filterBar.groupCapability)
                return Logic.groupByCapability(filtered, root.tilingCapabilityGroups, i18n("Other"));
            else if (groupIdx === filterBar.groupTilingSource)
                return Core.groupByBoolKey(filtered, item => {
                    return Logic.isBuiltIn(item);
                }, "builtin", i18n("Built-in"), "user", i18n("User Scripts"));
            else if (groupIdx === filterBar.groupTilingVisibility)
                return Core.groupByBoolKey(filtered, item => {
                    return item.hiddenFromSelector !== true;
                }, "visible", i18n("Visible"), "hidden", i18n("Hidden"));
            return Core.ungrouped(filtered, i18n("All algorithms"));
        }
        // Snapping grouping
        if (groupIdx === filterBar.groupAspectRatio)
            return Logic.groupByAspectRatio(filtered);
        else if (groupIdx === filterBar.groupZoneCount)
            return Logic.groupByZoneCount(filtered, count => {
                return i18np("%n zone", "%n zones", count);
            }, i18n("Unknown"));
        else if (groupIdx === filterBar.groupAutoManual)
            return Core.groupByBoolKey(filtered, item => {
                return item.autoAssign === true;
            }, "auto", i18n("Auto"), "manual", i18n("Manual"));
        else if (groupIdx === filterBar.groupSource)
            return Core.groupByBoolKey(filtered, item => {
                return Logic.isBuiltIn(item);
            }, "builtin", i18n("Built-in"), "user", i18n("User Layouts"));
        else if (groupIdx === filterBar.groupVisibility)
            return Core.groupByBoolKey(filtered, item => {
                return item.hiddenFromSelector !== true;
            }, "visible", i18n("Visible"), "hidden", i18n("Hidden"));
        return Core.ungrouped(filtered, i18n("All layouts"));
    }

    // `naturalContentHeight`, not `contentHeight`: rebuildModel() swaps the
    // whole group model, which tears every card down for a few frames. See the
    // model-swap scroll guard in SettingsFlickable.
    naturalContentHeight: content.implicitHeight
    clip: true

    Component.onCompleted: {
        rebuildModel();
        selectDefaultLayout(root.viewMode);
    }

    // Reset to Snapping Layouts when autotiling is disabled.
    Connections {
        function onAutotileEnabledChanged() {
            if (!root.settingsBridge.autotileEnabled && root.viewMode !== 0) {
                root.viewMode = 0;
                root.selectedLayoutId = "";
                root.rebuildModel();
                root.selectDefaultLayout(0);
            }
        }

        target: root.settingsBridge
    }

    Connections {
        function onLayoutsChanged() {
            root.rebuildModel();
        }

        function onLayoutAdded(layoutId) {
            Qt.callLater(() => {
                root.selectLayoutById(layoutId);
            });
        }

        target: settingsController
    }

    ColumnLayout {
        id: content

        width: parent.width
        // Uniform large spacing between the toolbar, filter bar, and group cards
        // — matches the ShaderBrowserPage / RulesPage content rhythm so the
        // filter-row → first-section gap lines up with the other listing pages.
        spacing: Kirigami.Units.largeSpacing

        // ─── View switch (Snapping / Tiling) — only when autotiling is on ──
        // Centered monitor-switcher-style tiles (shared SegmentedViewSwitch).
        SegmentedViewSwitch {
            Layout.fillWidth: true
            visible: root.settingsBridge.autotileEnabled
            modes: [i18n("Snapping"), i18n("Tiling")]
            currentIndex: root.viewMode
            onIndexChanged: index => {
                root.viewMode = index;
                root.selectedLayoutId = "";
                // rebuildModel() runs via filterBar.onViewModeChanged → loadState → filterSettingsChanged.
                root.selectDefaultLayout(index);
            }
        }

        // ─── Import / Open Folder card (shader-style, drop-zone) ───
        LayoutManageCard {
            viewMode: root.viewMode
            onRequestImportLayout: importDialog.open()
            onRequestImportFromKZones: settingsController.importFromKZones()
            onRequestImportKZonesFile: kzonesFileDialog.open()
            onRequestOpenLayoutsFolder: settingsController.openLayoutsFolder()
            onRequestImportAlgorithm: algorithmImportDialog.open()
            onRequestOpenAlgorithmsFolder: settingsController.openAlgorithmsFolder()
        }

        // ─── Search row (search + filter + New) ────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.SearchField {
                id: searchField

                Layout.fillWidth: true
                placeholderText: root.viewMode === 0 ? i18n("Search layouts…") : i18n("Search algorithms…")
                onTextChanged: filterBar.setSearchText(text)

                Connections {
                    function onSearchCleared() {
                        if (searchField.text.length > 0)
                            searchField.text = "";
                    }

                    target: filterBar
                }
            }

            ToolButton {
                icon.name: "view-filter"
                // Active state is binding-driven, not a user toggle — checkable omitted.
                checked: filterBar.hasActiveFilters
                onClicked: filterBar.popupFilterMenu()
                Accessible.name: filterBar.hasActiveFilters ? i18nc("@action:button", "Filter (active)") : i18nc("@action:button", "Filter")
                ToolTip.visible: hovered
                ToolTip.delay: Kirigami.Units.toolTipDelay
                ToolTip.text: filterBar.hasActiveFilters ? i18nc("@info:tooltip", "Filters active. Click to change") : i18nc("@info:tooltip", "Filter")
            }

            Button {
                text: root.viewMode === 0 ? i18n("New Layout") : i18n("New Algorithm")
                icon.name: "list-add"
                onClicked: root.viewMode === 0 ? newLayoutDialog.open() : newAlgorithmDialog.open()
            }
        }

        // ─── Group / Sort row ────────────────────
        LayoutFilterBar {
            id: filterBar

            Layout.fillWidth: true
            viewMode: root.viewMode
            onFilterSettingsChanged: root.rebuildModel()
        }

        // ─── Empty state ───────────────────────────────────────────────────
        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.gridUnit * 2
            visible: root.groupsModel.length === 0
            icon.name: "view-grid-symbolic"
            text: {
                if (filterBar.hasActiveFilters)
                    return root.viewMode === 1 ? i18n("No matching algorithms") : i18n("No matching layouts");

                return root.viewMode === 1 ? i18n("No autotile algorithms available") : i18n("No layouts available");
            }
            explanation: {
                if (filterBar.hasActiveFilters) {
                    let hints = [];
                    if (root.viewMode === 0 && !filterBar.showBuiltInLayouts && !filterBar.showUserLayouts)
                        hints.push(i18n("Both Built-in and User layout sources are hidden"));

                    if (root.viewMode === 0 && !filterBar.showAutoLayouts && !filterBar.showManualLayouts)
                        hints.push(i18n("Both Auto and Manual layout types are hidden"));

                    if (root.viewMode === 1 && !filterBar.showBuiltInAlgorithms && !filterBar.showUserAlgorithms)
                        hints.push(i18n("Both Built-in and User algorithm sources are hidden"));

                    return hints.length > 0 ? hints.join("\n") : i18n("Try adjusting your filters or search terms");
                }
                // The tiling view is only reachable while autotiling is on (the
                // switch is gated on it, and turning it off forces viewMode back
                // to 0), so an empty list here means the same thing it means for
                // snapping: nothing has been loaded yet.
                return root.viewMode === 1 ? i18n("Start the PlasmaZones daemon or add a tiling algorithm") : i18n("Start the PlasmaZones daemon or create a new layout");
            }

            helpfulAction: Kirigami.Action {
                visible: filterBar.hasActiveFilters
                text: i18n("Reset Filters")
                icon.name: "edit-reset"
                onTriggered: filterBar.resetFilters()
            }
        }

        // ─── Grouped layout cards ──────────────────────────────────────────
        // One collapsible SettingsCard per group, each hosting a responsive
        // Flow of layout cards — the same grouped-catalogue treatment as the
        // shader listing page. Groups with no label (ungrouped) render a
        // single header-less card.
        Repeater {
            model: root.groupsModel

            delegate: SettingsCard {
                id: groupCard

                required property var modelData

                Layout.fillWidth: true
                headerText: modelData.label
                headerTrailingText: root.viewMode === 1 ? i18np("%n algorithm", "%n algorithms", modelData.items.length) : i18np("%n layout", "%n layouts", modelData.items.length)
                // Only labelled groups get a collapse affordance — a header-less
                // ungrouped card has nothing to click to toggle.
                collapsible: modelData.label.length > 0

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Flow {
                        id: cardFlow

                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing

                        // Responsive columns: fit as many cards as the minimum
                        // card width allows, then stretch each to fill the row
                        // evenly so there's no dead gap on the right edge.
                        // The width must be floored: a fractional card width
                        // accumulates float error in Flow's x positions, and a
                        // sub-pixel overshoot wraps the last card of every row,
                        // leaving a full empty slot on the right.
                        readonly property int _columns: Math.max(1, Math.floor((width + spacing) / (root.minCardWidth + spacing)))
                        readonly property real _cardWidth: Math.floor((width - spacing * (_columns - 1)) / _columns)

                        Repeater {
                            model: groupCard.modelData.items

                            LayoutGridDelegate {
                                appSettings: root.settingsBridge
                                settingsController: root.controllerBridge
                                cellWidth: cardFlow._cardWidth
                                // Height tracks the stretched width so cards keep
                                // the same proportions at every window size and
                                // the fit-sized preview grows with them, instead
                                // of a fixed height capping the preview no matter
                                // how wide the card gets.
                                cellHeight: cardFlow._cardWidth * (root.cardHeight / root.minCardWidth)
                                viewMode: root.viewMode
                                isSelected: String(modelData.id) === root.selectedLayoutId
                                // When LayoutsPage is hosted outside Main.qml
                                // (KCM / future preview) `window.layoutContextMenu`
                                // is undefined; suppress the right-button mask so a
                                // missing menu doesn't pretend to exist.
                                contextMenuEnabled: !!(typeof window !== "undefined" && window && window.layoutContextMenu)
                                onSelected: idx => {
                                    root.selectedLayoutId = String(modelData.id);
                                }
                                onActivated: layoutId => {
                                    settingsController.editLayout(layoutId);
                                }
                                onDeleteRequested: layout => {
                                    deleteConfirmDialog.layoutToDelete = layout;
                                    deleteConfirmDialog.open();
                                }
                                onContextMenuRequested: layout => {
                                    if (typeof window !== "undefined" && window && window.layoutContextMenu)
                                        window.layoutContextMenu.showForLayout(layout);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Import file dialog
    FileDialog {
        id: importDialog

        title: i18n("Import Layout")
        nameFilters: [i18n("JSON files (*.json)"), i18n("All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            settingsController.importLayout(root.filePathFromUrl(selectedFile));
        }
    }

    // Export file dialog
    FileDialog {
        id: exportDialog

        property string layoutId: ""

        title: i18n("Export Layout")
        nameFilters: [i18n("JSON files (*.json)")]
        defaultSuffix: "json"
        fileMode: FileDialog.SaveFile
        onAccepted: {
            settingsController.exportLayout(exportDialog.layoutId, root.filePathFromUrl(selectedFile));
        }
    }

    // Algorithm import dialog
    FileDialog {
        id: algorithmImportDialog

        title: i18n("Import Tiling Algorithm")
        nameFilters: [i18n("Luau files (*.luau)"), i18n("All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            // Success only. A failure already toasts through
            // algorithmOperationFailed below, with the reason this could not
            // know, and reporting it here too gave the user two messages.
            if (settingsController.importAlgorithm(root.filePathFromUrl(selectedFile)) && typeof window !== "undefined" && window && window.showToast)
                window.showToast(i18n("Algorithm imported"));
        }
    }

    // KZones file import dialog
    FileDialog {
        id: kzonesFileDialog

        title: i18n("Import KZones Layout File")
        nameFilters: [i18n("JSON files (*.json)"), i18n("All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            settingsController.importFromKZonesFile(root.filePathFromUrl(selectedFile));
        }
    }

    // KZones import result notification — uses Main.qml's toast. The signal
    // carries (count, message); the toast text already includes the count from
    // the C++ side, so we only show `message`. `count` mirrors the C++ signal
    // parameter name for grep-friendliness even though it is unused here.
    Connections {
        function onKzonesImportFinished(count, message) {
            if (typeof window !== "undefined" && window && window.showToast)
                window.showToast(message);
        }

        target: settingsController
    }

    // Connect context menu signals from Main.qml to local dialogs.
    Connections {
        function onDeleteRequested(layout) {
            deleteConfirmDialog.layoutToDelete = layout;
            deleteConfirmDialog.open();
        }

        function onExportRequested(layoutId) {
            if (layoutId.startsWith("autotile:")) {
                algorithmExportDialog.algorithmId = settingsController.algorithmIdFromLayoutId(layoutId);
                algorithmExportDialog.open();
            } else {
                exportDialog.layoutId = layoutId;
                exportDialog.open();
            }
        }

        // When LayoutsPage is hosted outside Main.qml (KCM / preview host),
        // `window.layoutContextMenu` is undefined; an unguarded target would
        // emit a runtime warning on every signal fire. Disable entirely then.
        enabled: typeof window !== "undefined" && window && window.layoutContextMenu
        target: typeof window !== "undefined" && window && window.layoutContextMenu ? window.layoutContextMenu : null
    }

    // New Layout wizard dialog
    NewLayoutDialog {
        id: newLayoutDialog

        controller: settingsController
    }

    // New Algorithm wizard dialog
    NewAlgorithmDialog {
        id: newAlgorithmDialog

        controller: settingsController
    }

    // Algorithm created/failed signals from C++ (fires after AlgorithmRegistry picks up the new file)
    Connections {
        function onAlgorithmCreated(algorithmId) {
            // Always rebuild so the new algorithm is available; only switch view
            // and auto-select if the user is already looking at the tiling view
            // (avoids jarring view switch when duplicating from a different context)
            root.rebuildModel();
            if (root.viewMode === 1)
                root.selectedLayoutId = "autotile:" + algorithmId;
        }

        function onAlgorithmOperationFailed(reason) {
            // Only show toast when the wizard dialog is closed — if the dialog
            // is open, it shows the error inline via its own Connections block.
            // The routing decision is deferred: create can emit this
            // synchronously for a created-but-degraded result and still return
            // true, and the wizard only close()s after that return. Checking
            // `opened` at emit time would see the dialog still open and swallow
            // the toast even though the dialog is about to close and take its
            // inline error with it. A plain validation failure keeps the dialog
            // open, so the deferred check still routes it inline.
            Qt.callLater(function () {
                if (!newAlgorithmDialog.opened && typeof window !== "undefined" && window && window.showToast)
                    window.showToast(reason);
            });
        }

        function onLayoutOperationFailed(reason) {
            // Deferred for the same emit-before-close ordering as
            // onAlgorithmOperationFailed above.
            Qt.callLater(function () {
                if (!newLayoutDialog.opened && typeof window !== "undefined" && window && window.showToast)
                    window.showToast(reason);
            });
        }

        target: settingsController
    }

    // Algorithm export file dialog
    FileDialog {
        id: algorithmExportDialog

        property string algorithmId: ""

        title: i18n("Export Algorithm")
        nameFilters: [i18n("Luau files (*.luau)")]
        defaultSuffix: "luau"
        fileMode: FileDialog.SaveFile
        onAccepted: {
            settingsController.exportAlgorithm(algorithmExportDialog.algorithmId, root.filePathFromUrl(selectedFile));
        }
    }

    // Delete confirmation dialog (handles both layouts and algorithms)
    Kirigami.PromptDialog {
        id: deleteConfirmDialog

        property var layoutToDelete: null
        readonly property bool isAlgorithm: layoutToDelete && layoutToDelete.isAutotile === true

        title: isAlgorithm ? i18n("Delete Algorithm") : i18n("Delete Layout")
        subtitle: layoutToDelete ? i18n("Are you sure you want to delete \"%1\"?", layoutToDelete.displayName || "") : ""
        standardButtons: Kirigami.Dialog.NoButton
        onRejected: layoutToDelete = null
        onClosed: layoutToDelete = null
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    if (deleteConfirmDialog.layoutToDelete) {
                        if (deleteConfirmDialog.isAlgorithm) {
                            let algoId = settingsController.algorithmIdFromLayoutId(deleteConfirmDialog.layoutToDelete.id);
                            settingsController.deleteAlgorithm(algoId);
                        } else {
                            settingsController.deleteLayout(deleteConfirmDialog.layoutToDelete.id);
                        }
                        deleteConfirmDialog.layoutToDelete = null;
                    }
                    deleteConfirmDialog.close();
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: deleteConfirmDialog.close()
            }
        ]
    }
}
