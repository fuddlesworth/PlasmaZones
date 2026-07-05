// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.plasmazones.settings

/**
 * @brief The unified Rules page.
 *
 * One surface for every window-matching rule — monitors, applications,
 * activities, animations. Backed by a single `RuleModel` exposed by
 * `SettingsController.rulesPage`.
 *
 * The page is ONE flat, drag-reorderable priority list — highest precedence on
 * top. A drop re-stamps the single GLOBAL priority sequence
 * (`RuleController::renormalizePriorities`), so the user freely interleaves
 * rules from any section and position alone decides who wins. There are no
 * priority bands in evaluation (the daemon only reads the priority integer); the
 * old section "bands" survive only as a default-seeding hint for newly added
 * rules. Managed System rules pin to the bottom (INT_MIN) and are non-draggable.
 *
 * There is deliberately NO grouping/sorting on this page — precedence is only
 * meaningful in priority order, so a re-grouped/re-sorted view would fight the
 * drag. Browsing is served by narrowing instead: the search field, the section
 * filter button, and the read-only monitor overview strip (clicking a tile
 * filters the list). Category stays legible via a per-row section badge. (The
 * reusable GroupSortBar / GroupSortLogic / SegmentedViewSwitch components live on
 * for the Layouts page and any future use.)
 */
SettingsFlickable {
    id: page

    readonly property var controller: settingsController.rulesPage
    readonly property var ruleModel: controller.model
    /// True while any rule-authoring modal owned by this page is open
    /// — read by Main.qml's `_navShortcutsEnabled` guard so Ctrl+PgUp /
    /// Ctrl+PgDown can't drag the user off the page while they have an
    /// unsaved rule edit, picker selection, or force-save prompt open.
    /// `forceSaveConfirm` lives below mainCol and binds back through
    /// `_forceSaveConfirmOpen` to avoid a forward reference here. The
    /// `onAnyModalOpenChanged` handler republishes the value into the
    /// chrome-level `window._pageOwnedModalOpen` flag that Main.qml's
    /// nav-shortcut guard reads (the framework's PageHost Loader keeps
    /// the page item private, so a direct binding can't reach it).
    readonly property bool anyModalOpen: addRuleWizard.opened || windowPickerDialog.opened || ruleEditorSheet.opened || page._forceSaveConfirmOpen
    /// Internal bridge updated by `forceSaveConfirm.onVisibleChanged` so
    /// `anyModalOpen` can read it without forward-referencing an id that
    /// is declared further down in the file.
    property bool _forceSaveConfirmOpen: false

    onAnyModalOpenChanged: {
        // Defensive truthy-check: this page can be hosted by consumers
        // (KCM, future preview) that don't define `window` or the
        // `_pageOwnedModalOpen` cross-cut. The full settings app does;
        // standalone hosts ignore the publish.
        if (typeof window !== "undefined" && window && window._pageOwnedModalOpen !== undefined)
            window._pageOwnedModalOpen = anyModalOpen;
    }
    Component.onCompleted: {
        // Hand the controller the canonical curve-naming function so the
        // rule-list action summary renders "Curve: Standard (Cubic)" /
        // "Curve: Spring (12.00, 1.00)" like the editor's curve button,
        // instead of the raw wire string. The naming logic + i18n labels
        // live only in CurvePresets, so the C++ model resolves through this
        // JS resolver rather than duplicating the easing tables.
        page.controller.setCurveLabelResolver(function (curve) {
            return CurvePresets.curveLabel(curve);
        });
    }
    Component.onDestruction: {
        // Clear the flag on page swap — the destructor fires when the
        // PageHost Loader swaps source, and a stale `true` would
        // permanently disable Ctrl+PgUp/PgDown on the page we navigated
        // to.
        if (typeof window !== "undefined" && window && window._pageOwnedModalOpen !== undefined)
            window._pageOwnedModalOpen = false;
    }
    // Composite "appSettings" surface threaded into the rule editor so the
    // picker components (LayoutComboBox for snapping/tiling actions, and the
    // screen / activity match-leaf editors) all see the same object.
    // LayoutComboBox reads `layouts` + the `default*` ids from this; the leaf
    // pickers read `screens` and `activities`.
    readonly property QtObject _editorAppSettings: QtObject {
        readonly property var layouts: settingsController.layouts
        readonly property var screens: settingsController.screens
        readonly property var activities: settingsController.activities
        // Backs the RouteToDesktop action's virtual-desktop picker.
        readonly property int virtualDesktopCount: settingsController.virtualDesktopCount
        readonly property var virtualDesktopNames: settingsController.virtualDesktopNames
        // `AnimationsPageController` — exposes `eventSections()` and
        // `availableShaderEffects()` for the animationEvent / shaderEffect
        // picker editors in ActionRow.
        readonly property var animationsController: settingsController.animationsPage
        // `SnappingShadersPageController` — exposes `availableShaderEffects()`
        // (the overlay/snapping shader catalog) for the overlayShader picker
        // editor (OverrideOverlayShader) and its read-only name resolution.
        readonly property var snappingShadersPage: settingsController.snappingShadersPage
        // `DecorationPageController` — exposes `availableShaderEffects()` (the
        // surface-pack catalog) for the decorationChain editor in ActionRow
        // (OverrideDecorationChain) and its read-only name resolution.
        readonly property var decorationPage: settingsController.decorationPage
        // Reference to the page-level WindowPickerDialog — exposed via the
        // bridge so MatchLeafEditor can open the picker without having to
        // own its own instance. Hosting the picker inside the OverlaySheet
        // collapsed its content area (Kirigami.Dialog-in-OverlaySheet
        // doesn't get the full vertical envelope), so the page-level
        // instance is the supported pattern here.
        readonly property var windowPicker: windowPickerDialog
        readonly property string defaultLayoutId: appSettings.defaultLayoutId
        readonly property string defaultAutotileAlgorithm: appSettings.defaultAutotileAlgorithm
        readonly property bool autoAssignAllLayouts: appSettings.autoAssignAllLayouts === true
        // System highlight / inactive colours (alpha included), proxied from the
        // real Settings object so ActionRow's border-colour swatch can preview the
        // "accent" sentinel as the colour the border actually draws — highlight for
        // the focused (active) slot, inactive for the unfocused one. Without these
        // the composite would return undefined and the swatch would paint black.
        readonly property color highlightColor: appSettings.highlightColor
        readonly property color inactiveColor: appSettings.inactiveColor
    }
    // ── Filter state ──

    property string searchText: ""
    // Multi-select filter state: the set of filter KEYS the user has unchecked
    // (empty = show everything). Keys are mixed across three groups — section
    // values (ints), source ("src:system"/"src:user"), and status
    // ("status:active"/"status:disabled"). The filter button owns this; the page
    // reads it one-way.
    readonly property var excludedFilters: rulesFilterButton.excluded
    property string monitorFilter: ""
    // Ordered section descriptors — `[{ value: int, label }]` straight from
    // the controller. Drives the section filter menu; the C++ Section enum
    // order is never duplicated in QML.
    readonly property var sectionDescriptors: page.controller.sections()

    // Cached `matchFields()` / `actionTypes()` tables — threaded down to every
    // RuleRow's expansion view so the per-row Q_INVOKABLE doesn't fire on every
    // expand. The authoring catalogue is static, so this is a one-shot read.
    property var matchFieldOptions: page.controller.matchFields()
    property var actionTypeOptions: page.controller.actionTypes()
    // Bumped whenever the underlying model changes so filteredRules re-evaluates
    // without QML hardcoding the model's role layout.
    property int modelRevision: 0

    /// The flat, filtered, priority-ordered rule list — highest precedence first.
    /// This is the page's single view: precedence only has meaning in priority
    /// order, so there is no grouping/sorting UI — the list is always the
    /// drag-reorderable priority sequence. Search + the filter button (section /
    /// source / status) + the monitor strip narrow it without changing order.
    /// Managed System rules pin to the bottom for free (their priority is
    /// INT_MIN). Reactive on searchText / excludedFilters / monitorFilter and
    /// `modelRevision`.
    readonly property var filteredRules: {
        var rev = page.modelRevision;
        var snapshot = page.controller.rulesSnapshot();
        var search = page.searchText.toLowerCase();
        var excluded = page.excludedFilters;
        var out = [];
        for (var i = 0; i < snapshot.length; ++i) {
            var entry = snapshot[i];
            // Section filter — hide entries whose category the user unchecked.
            if (excluded.indexOf(entry.section) >= 0)
                continue;
            // Source filter — built-in (managed) vs user-created.
            if (excluded.indexOf(entry.managed === true ? "src:system" : "src:user") >= 0)
                continue;
            // Status filter — active (enabled) vs disabled.
            if (excluded.indexOf(entry.enabled === true ? "status:active" : "status:disabled") >= 0)
                continue;

            // Search filter — match name / match summary / action summary.
            if (search.length > 0) {
                var hay = (entry.name + " " + entry.matchSummary + " " + entry.actionSummary).toLowerCase();
                if (hay.indexOf(search) < 0)
                    continue;
            }
            // Monitor filter — keep only rules whose ScreenId predicate(s) name
            // the selected monitor (an exact id match, not a substring scan).
            if (page.monitorFilter.length > 0) {
                if (!entry.screenIds || entry.screenIds.indexOf(page.monitorFilter) < 0)
                    continue;
            }
            out.push(entry);
        }
        // Highest priority first. Qt's V4 sort is stable, so equal priorities keep
        // snapshot (model) order — the only ties are the managed System rules,
        // which all share INT_MIN and sort to the bottom. The global renormalize
        // keeps model row order in sync with priority order, so display order ==
        // model row order (the invariant the drag commit relies on).
        out.sort(function (a, b) {
            return (b.priority || 0) - (a.priority || 0);
        });
        return out;
    }
    // Bump `tilesRevision` whenever the upstream lists change. The tile
    // payload embeds layout-name + activity-name resolutions; a list-length
    // touch wouldn't fire when an existing layout is *renamed* (length
    // unchanged), leaving the tile caption stale. Listening to the change
    // signals directly invalidates on any structural OR content change.
    property int tilesRevision: 0
    // Roles the filteredRules list reads (filtering, the row summaries, and the
    // priority sort). filteredRules is built from `rulesSnapshot()` — a frozen
    // QVariantList — so a row only reflects a role change once the snapshot is
    // rebuilt. The onDataChanged handler below bumps modelRevision (→ rebuild)
    // when EITHER:
    //   (a) the dataChanged roles vector is empty — Qt's "any role" convention,
    //       which every updateRule() mutation uses, so enabled / name / match /
    //       action / in-place-priority edits already rebuild this way; OR
    //   (b) a changed role is in this list — which catches the model's only two
    //       TARGETED emitters: setPriorities() → {PriorityRole} and
    //       refreshLabels() → {NameRole, MatchSummaryRole, ActionSummaryRole}.
    // PriorityRole MUST be listed: a drag-reorder runs renormalizePriorities()
    // → setPriorities(), whose targeted dataChanged({PriorityRole}) the
    // empty-roles fallback never sees — omitting it would leave the reordered
    // rows in their stale pre-move order. The remaining entries (Section /
    // ScreenIds / ConditionCount / ActionCount / IsComposite /
    // ValidationIssueCount) are also read when building the rows but currently
    // change only through the roles-less updateRule path; they're kept so a
    // future targeted emitter carrying them rebuilds without a second edit here.
    readonly property var _summaryRoles: [RuleModel.SectionRole, RuleModel.MatchSummaryRole, RuleModel.ActionSummaryRole, RuleModel.ScreenIdsRole, RuleModel.ConditionCountRole, RuleModel.ActionCountRole, RuleModel.IsCompositeRole, RuleModel.ValidationIssueCountRole, RuleModel.PriorityRole]

    contentHeight: mainCol.implicitHeight
    clip: true

    // Re-derive filteredRules on every structural or per-rule change so a rule
    // that changes section gets its updated badge / filter classification.
    Connections {
        function onCountChanged() {
            page.modelRevision++;
        }

        function onRuleSectionChanged() {
            page.modelRevision++;
        }

        // moveRule fires through beginMoveRows / endMoveRows, which emits
        // rowsMoved (and never countChanged / dataChanged on a summary role).
        // Without this handler the flat priority list stays frozen on the
        // pre-move ordering: the dragged row's `y` re-binds to its OLD
        // cumulative position and the user sees a snap-back even though the
        // C++ model has accepted the move. The single drag container hosts
        // every rule, so this covers all reorders.
        function onRowsMoved() {
            page.modelRevision++;
        }

        function onDataChanged(topLeft, bottomRight, roles) {
            // A roles vector with overlap against the summary roles drives a
            // rebuild. An empty roles vector means "any role" (Qt convention
            // for QAbstractItemModel implementations that don't enumerate
            // specific roles) — fall through to a bump in that case to avoid
            // silently dropping legitimate updates.
            if (!roles || roles.length === 0) {
                page.modelRevision++;
                return;
            }
            for (var i = 0; i < roles.length; ++i) {
                if (page._summaryRoles.indexOf(roles[i]) >= 0) {
                    page.modelRevision++;
                    return;
                }
            }
        }

        target: page.ruleModel
    }

    Connections {
        function onLayoutsChanged() {
            page.tilesRevision++;
        }

        function onActivitiesChanged() {
            page.tilesRevision++;
        }

        function onScreensChanged() {
            page.tilesRevision++;
            // Drop a monitor filter whose screen was unplugged — otherwise the
            // rule list stays empty with no way to clear it (the MonitorOverview
            // tile that toggles the filter is gone with the screen). Mirrors
            // VirtualScreensPage's _selectedScreen re-validation.
            if (page.monitorFilter.length > 0) {
                var stillPresent = settingsController.screens.some(function (s) {
                    return s.name === page.monitorFilter || s.screenId === page.monitorFilter;
                });
                if (!stillPresent)
                    page.monitorFilter = "";
            }
        }

        target: settingsController
    }

    // Surface asyncCommit failures (force-save daemon errors, partial-
    // drop rejections) to the user. The controller's applyResult fires
    // for every asyncCommit invocation; we only want to toast on
    // failure — the success path is covered by the chrome's footer
    // state.
    Connections {
        function onApplyResult(ok, error) {
            if (!ok && typeof window !== "undefined" && window && window.showToast) {
                // Match the defensive shape used by `onAnyModalOpenChanged` /
                // `Component.onDestruction` above so every cross-host write
                // through this file uses the same `typeof window !== "undefined"`
                // pre-check. When this page is hosted outside Main.qml
                // (KCM / preview host), `window.showToast` is undefined and
                // an unguarded call would raise.
                window.showToast(error.length > 0 ? error : i18n("Failed to save rules."));
            }
        }

        target: page.controller
    }

    AddRuleWizard {
        id: addRuleWizard

        controller: page.controller
        appSettings: page._editorAppSettings
        onRuleSaved: function (ruleJson) {
            page.controller.addRuleFromJson(ruleJson);
        }
    }

    // Page-level picker instance — MatchLeafEditor opens this via the
    // appSettings bridge (see `_editorAppSettings.windowPicker`). One
    // instance shared across every leaf in every rule edit.
    WindowPickerDialog {
        id: windowPickerDialog

        controller: settingsController
    }

    RuleEditorSheet {
        id: ruleEditorSheet

        controller: page.controller
        appSettings: page._editorAppSettings
        onRuleSaved: function (ruleJson) {
            page.controller.updateRuleFromJson(ruleJson);
        }
    }

    ColumnLayout {
        id: mainCol

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Warning
            visible: !page.controller.daemonReachable
            text: i18n("The PlasmaZones daemon is not running. Rules cannot be loaded or saved until it starts.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Warning
            visible: page.controller.daemonChangedWhileDirty
            text: i18n("The rules changed on disk while you were editing. Saving now will overwrite those changes. Review your edits before saving, or discard them to reload.")
            // Escape hatch — the controller's normal asyncCommit(false)
            // refuses when daemonChangedWhileDirty is set so the user doesn't
            // silently overwrite. `asyncCommit(true)` (the QML-callable
            // force variant on RuleController) bypasses the guard
            // for the "I know, save anyway" path; mirrors the SettingsCard
            // confirm-prompt UX so the user has to acknowledge the
            // overwrite explicitly.
            actions: [
                Kirigami.Action {
                    icon.name: "document-save"
                    text: i18n("Save anyway")
                    onTriggered: forceSaveConfirm.open()
                },
                Kirigami.Action {
                    icon.name: "edit-undo"
                    text: i18n("Discard and reload")
                    onTriggered: page.controller.revert()
                }
            ]
        }

        Kirigami.PromptDialog {
            id: forceSaveConfirm

            // Bridge open-state to page-level `_forceSaveConfirmOpen` so
            // Main.qml's `_navShortcutsEnabled` can include this dialog
            // in its modal-open guard. Forward-reference avoidance: the
            // `anyModalOpen` binding is declared at the top of the file
            // and `forceSaveConfirm` is nested several scopes deep, so a
            // direct id reference up there is brittle.
            onOpened: page._forceSaveConfirmOpen = true
            onClosed: page._forceSaveConfirmOpen = false
            title: i18n("Overwrite daemon-side changes?")
            subtitle: i18n("Saving will replace the rule set that the daemon currently has on disk with your staged edits. Any rules that changed there while you were editing will be lost.")
            standardButtons: Kirigami.Dialog.NoButton
            customFooterActions: [
                Kirigami.Action {
                    icon.name: "dialog-cancel"
                    text: i18n("Cancel")
                    onTriggered: forceSaveConfirm.close()
                },
                Kirigami.Action {
                    icon.name: "document-save"
                    text: i18n("Overwrite")
                    onTriggered: {
                        // Use the async path (asyncCommit dispatches
                        // setAllRules via QDBusPendingCallWatcher) so a
                        // stuck daemon doesn't freeze the chrome.
                        // applyResult fires later with the outcome —
                        // surface a toast on failure via the one-shot
                        // wiring below.
                        forceSaveConfirm.close();
                        page.controller.asyncCommit(true);
                    }
                }
            ]
        }

        // ── Monitor overview strip ──
        // Bare, no SettingsCard wrapper: a direct child of the page column,
        // the same placement Monitor State and Virtual Screens use for their
        // own monitor pickers (no enclosing card or header strip).
        MonitorOverview {
            Layout.fillWidth: true
            screens: settingsController.screens
            // `tilesRevision` invalidates this binding whenever the upstream
            // lists (layouts/activities/screens) change — including content-
            // only changes like a layout rename that wouldn't move a length
            // counter. See the `Connections { target: settingsController }`
            // above for the bump points.
            tiles: {
                let _rev = page.tilesRevision;
                return page.controller.monitorOverview(settingsController.screens);
            }
            selectedScreenId: page.monitorFilter
            onMonitorSelected: function (screenId) {
                page.monitorFilter = screenId;
            }
        }

        // ── Search + filter + Add rule ──
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.SearchField {
                Layout.fillWidth: true
                placeholderText: i18n("Search rules…")
                Accessible.name: i18n("Search rules")
                onTextChanged: page.searchText = text
            }

            // Multi-select filter, modeled on the Layouts page filter button —
            // same group ORDER as the Layouts menu: source first (built-in before
            // user), then the categories, then the status/type pair. Three groups:
            // source (system vs user-created), category sections, status (active
            // vs disabled). Section labels/order come from the controller (C++).
            // System is dropped from the section group because a System rule is
            // exactly a managed rule (sectionFor maps managed → System), so the
            // Source group's "System" already covers it — listing it twice would
            // be redundant.
            FilterMenuButton {
                id: rulesFilterButton

                menuTitle: i18nc("@title:menu", "Filter Rules")
                groups: [[
                        {
                            "key": "src:system",
                            "label": i18nc("@option:check filter rules by source", "System")
                        },
                        {
                            "key": "src:user",
                            "label": i18nc("@option:check filter rules by source", "User-created")
                        }
                    ], page.sectionDescriptors.filter(function (s) {
                        return s.value !== RuleModel.System;
                    }).map(function (s) {
                        return {
                            "key": s.value,
                            "label": s.label
                        };
                    }), [
                        {
                            "key": "status:active",
                            "label": i18nc("@option:check filter rules by status", "Active")
                        },
                        {
                            "key": "status:disabled",
                            "label": i18nc("@option:check filter rules by status", "Disabled")
                        }
                    ]]
            }

            Button {
                text: i18n("Add rule")
                icon.name: "list-add"
                Accessible.name: i18n("Add a new rule")
                onClicked: addRuleWizard.open()
            }
        }

        // ── Empty state ──
        Kirigami.PlaceholderMessage {
            // When the daemon is unreachable the model is empty because it
            // could not be loaded — not because the user has no rules. Gate
            // the "No rules yet" copy on daemonReachable so the inline
            // warning above is the only thing shown in that case.
            readonly property bool _daemonDown: !page.controller.daemonReachable
            readonly property bool _trulyEmpty: page.ruleModel.count === 0 && !_daemonDown

            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.gridUnit * 2
            visible: page.filteredRules.length === 0 && !_daemonDown
            icon.name: "view-list-details"
            text: _trulyEmpty ? i18n("No rules yet") : i18n("No rules match the current filter")
            explanation: _trulyEmpty ? i18n("Add a rule to assign layouts to monitors, float application windows, or override animations.") : i18n("Try a different filter or search term.")
        }

        // ── The flat priority list ──
        // One drag-reorderable list, highest precedence on top — there is no
        // grouping, so a single card hosts every (filtered) rule. The header
        // carries the count and the reorder hint; the section badge on each row
        // keeps category legible without grouping.
        SettingsCard {
            Layout.fillWidth: true
            visible: page.filteredRules.length > 0
            headerText: i18n("All rules")
            headerTrailingText: {
                var base = i18np("%n rule", "%n rules", page.filteredRules.length);
                return i18nc("Suffix in the rule list header showing the count and a reorder hint", "%1 · drag to set precedence", base);
            }

            contentItem: ColumnLayout {
                spacing: 0

                // A drop re-stamps the global priority sequence via the
                // controller's `renormalizePriorities`; managed System rules pin
                // to the bottom and are non-draggable (handled per-row).
                RuleSectionList {
                    Layout.fillWidth: true
                    rules: page.filteredRules
                    reorderingEnabled: true
                    showSectionBadge: true
                    controller: page.controller
                    matchFieldOptions: page.matchFieldOptions
                    actionTypeOptions: page.actionTypeOptions
                    appSettings: page._editorAppSettings
                    onToggleRequested: function (ruleId, enabled) {
                        page.controller.setRuleEnabled(ruleId, enabled);
                    }
                    onEditRequested: function (ruleId) {
                        ruleEditorSheet.openFor(page.controller.ruleJson(ruleId));
                    }
                    onDuplicateRequested: function (ruleId) {
                        page.controller.duplicateRule(ruleId);
                    }
                    onDeleteRequested: function (ruleId) {
                        page.controller.removeRule(ruleId);
                    }
                }
            }
        }
    }
}
