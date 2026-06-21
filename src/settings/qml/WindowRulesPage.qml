// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.plasmazones.settings

/**
 * @brief The unified Window Rules page.
 *
 * One surface for every window-matching rule — monitors, applications,
 * activities, animations. Backed by a single `WindowRuleModel` exposed by
 * `SettingsController.windowRulesPage`; section grouping, search and chip
 * filtering are all derived here in QML over that one model — there are no
 * per-section proxy models.
 *
 * The monitor overview strip is read-only; clicking a tile filters the list.
 */
SettingsFlickable {
    id: page

    readonly property var controller: settingsController.windowRulesPage
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
        // `AnimationsPageController` — exposes `eventSections()` and
        // `availableShaderEffects()` for the animationEvent / shaderEffect
        // picker editors in ActionRow.
        readonly property var animationsController: settingsController.animationsPage
        // `SnappingShadersPageController` — exposes `availableShaderEffects()`
        // (the overlay/snapping shader catalog) for the overlayShader picker
        // editor (OverrideOverlayShader) and its read-only name resolution.
        readonly property var snappingShadersPage: settingsController.snappingShadersPage
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
    }
    // ── Filter state ──

    property string searchText: ""
    // Chip filter: -1 = All, else a WindowRuleModel.Section enum value as int.
    property int chipFilter: -1
    property string monitorFilter: ""
    // Ordered section descriptors — `[{ value: int, label }]` straight from
    // the controller. The C++ Section enum order is never duplicated in QML.
    readonly property var sectionDescriptors: page.controller.sections()
    // Cached `matchFields()` table — threaded down to every WindowRuleRow's
    // expansion view so the per-row Q_INVOKABLE doesn't fire on every expand.
    // The authoring catalogue is static, so this is a one-shot read.
    property var matchFieldOptions: page.controller.matchFields()
    // Cached `actionTypes()` — same caching rationale as matchFieldOptions;
    // threaded down to WindowRuleRow's expansion so each row doesn't re-invoke
    // the Q_INVOKABLE.
    property var actionTypeOptions: page.controller.actionTypes()
    // Bumped whenever the underlying model changes so sectionModel re-evaluates
    // without QML hardcoding the model's role layout.
    property int modelRevision: 0
    /// Build a `[ { section, label, rules: [...] } ]` array over the flat
    /// model, applying search / chip / monitor filters. Reactive: depends on
    /// searchText / chipFilter / monitorFilter and `modelRevision`.
    readonly property var sectionModel: {
        // Touch the revision so the binding re-evaluates on any model change.
        var rev = page.modelRevision;
        var snapshot = page.controller.rulesSnapshot();
        var buckets = {};
        for (var s = 0; s < page.sectionDescriptors.length; ++s)
            buckets[page.sectionDescriptors[s].value] = [];
        var search = page.searchText.toLowerCase();
        for (var i = 0; i < snapshot.length; ++i) {
            var entry = snapshot[i];
            // Chip filter.
            if (page.chipFilter >= 0 && entry.section !== page.chipFilter)
                continue;

            // Search filter — match name / match summary / action summary.
            if (search.length > 0) {
                var hay = (entry.name + " " + entry.matchSummary + " " + entry.actionSummary).toLowerCase();
                if (hay.indexOf(search) < 0)
                    continue;
            }
            // Monitor filter — keep only rules whose ScreenId predicate(s)
            // name the selected monitor (an exact id match, not a substring
            // scan of the localized summary).
            if (page.monitorFilter.length > 0) {
                if (!entry.screenIds || entry.screenIds.indexOf(page.monitorFilter) < 0)
                    continue;
            }
            if (buckets[entry.section] !== undefined)
                buckets[entry.section].push(entry);
        }
        var out = [];
        for (var so = 0; so < page.sectionDescriptors.length; ++so) {
            var sec = page.sectionDescriptors[so];
            if (buckets[sec.value].length === 0)
                continue;

            out.push({
                "section": sec.value,
                "label": sec.label,
                "rules": buckets[sec.value]
            });
        }
        return out;
    }
    // Bump `tilesRevision` whenever the upstream lists change. The tile
    // payload embeds layout-name + activity-name resolutions; a list-length
    // touch wouldn't fire when an existing layout is *renamed* (length
    // unchanged), leaving the tile caption stale. Listening to the change
    // signals directly invalidates on any structural OR content change.
    property int tilesRevision: 0
    // Roles the sectionModel reads (bucketing, filtering, summary cards, and
    // the per-row badges including Priority). The sectionModel is built from
    // `rulesSnapshot()` — a frozen QVariantList — so a row only reflects a role
    // change once the snapshot is rebuilt. The onDataChanged handler below
    // bumps modelRevision (→ rebuild) when EITHER:
    //   (a) the dataChanged roles vector is empty — Qt's "any role" convention,
    //       which every updateRule() mutation uses, so enabled / name / match /
    //       action / in-place-priority edits already rebuild this way; OR
    //   (b) a changed role is in this list — which catches the model's only two
    //       TARGETED emitters: setPriorities() → {PriorityRole} and
    //       refreshLabels() → {NameRole, MatchSummaryRole, ActionSummaryRole}.
    // PriorityRole MUST be listed: a drag-reorder runs renormalizePriorities()
    // → setPriorities(), whose targeted dataChanged({PriorityRole}) the
    // empty-roles fallback never sees — omitting it left the reordered rows'
    // Priority badges showing their stale pre-move numbers. The remaining
    // entries (Section / ScreenIds / ConditionCount / ActionCount / IsComposite
    // / ValidationIssueCount) are also read by the sectionModel but currently
    // change only through the roles-less updateRule path; they're kept so a
    // future targeted emitter carrying them rebuilds without a second edit here.
    readonly property var _summaryRoles: [WindowRuleModel.SectionRole, WindowRuleModel.MatchSummaryRole, WindowRuleModel.ActionSummaryRole, WindowRuleModel.ScreenIdsRole, WindowRuleModel.ConditionCountRole, WindowRuleModel.ActionCountRole, WindowRuleModel.IsCompositeRole, WindowRuleModel.ValidationIssueCountRole, WindowRuleModel.PriorityRole]

    contentHeight: mainCol.implicitHeight
    clip: true

    // Re-derive the section model on every structural or per-rule change so a
    // rule that changes section is re-bucketed.
    Connections {
        function onCountChanged() {
            page.modelRevision++;
        }

        function onRuleSectionChanged() {
            page.modelRevision++;
        }

        // moveRule fires through beginMoveRows / endMoveRows, which emits
        // rowsMoved (and never countChanged / dataChanged on a summary role).
        // Without this handler the section buckets stay frozen on the
        // pre-move ordering: the dragged row's `y` re-binds to its OLD
        // cumulative position and the user sees a snap-back even though the
        // C++ model has accepted the move. The Animation section's drag
        // container is the only place this surfaces today, but the bump is
        // model-wide so future reorderable sections inherit the fix.
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
                window.showToast(error.length > 0 ? error : i18n("Failed to save window rules."));
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
            text: i18n("The PlasmaZones daemon is not running. Window rules cannot be loaded or saved until it starts.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Warning
            visible: page.controller.daemonChangedWhileDirty
            text: i18n("The window rules changed on disk while you were editing. Saving now will overwrite those changes. Review your edits before saving, or discard them to reload.")
            // Escape hatch — the controller's normal asyncCommit(false)
            // refuses when daemonChangedWhileDirty is set so the user doesn't
            // silently overwrite. `asyncCommit(true)` (the QML-callable
            // force variant on WindowRuleController) bypasses the guard
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

        // ── Search + Add rule ──
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.SearchField {
                Layout.fillWidth: true
                placeholderText: i18n("Search rules…")
                Accessible.name: i18n("Search window rules")
                onTextChanged: page.searchText = text
            }

            Button {
                text: i18n("Add rule")
                icon.name: "list-add"
                Accessible.name: i18n("Add a new window rule")
                onClicked: addRuleWizard.open()
            }
        }

        // ── Filter chips (collapsible) ──
        // Disclosure toggle + single-select section chips. Collapsed by default;
        // the dot shows when a section filter is active while collapsed.
        FilterDisclosureHeader {
            id: rulesFilterHeader

            hasActiveFilters: page.chipFilter !== -1
        }

        Flow {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            visible: rulesFilterHeader.expanded

            Repeater {
                // "All" chip prepended to the controller's section list — the
                // section labels and order come from C++, not hardcoded here.
                model: [
                    {
                        "value": -1,
                        "label": i18n("All")
                    }
                ].concat(page.sectionDescriptors)

                delegate: Button {
                    required property var modelData

                    text: modelData.label
                    checkable: true
                    checked: page.chipFilter === modelData.value
                    Accessible.name: i18n("Filter rules: %1", modelData.label)
                    onClicked: page.chipFilter = modelData.value
                }
            }
        }

        // ── Empty state ──
        Kirigami.PlaceholderMessage {
            // When the daemon is unreachable the model is empty because it
            // could not be loaded — not because the user has no rules. Gate
            // the "No window rules yet" copy on daemonReachable so the inline
            // warning above is the only thing shown in that case.
            readonly property bool _daemonDown: !page.controller.daemonReachable
            readonly property bool _trulyEmpty: page.ruleModel.count === 0 && !_daemonDown

            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.gridUnit * 2
            visible: page.sectionModel.length === 0 && !_daemonDown
            icon.name: "view-list-details"
            text: _trulyEmpty ? i18n("No window rules yet") : i18n("No rules match the current filter")
            explanation: _trulyEmpty ? i18n("Add a rule to assign layouts to monitors, float application windows, or override animations.") : i18n("Try a different filter or search term.")
        }

        // ── Grouped rule list ──
        // Each section renders as a standard `SettingsCard` with `headerText`
        // — same visual treatment as the rest of the app (Animations Presets,
        // Virtual Screens, General). The card carries the section label as
        // its header and the rule rows as its body.
        Repeater {
            model: page.sectionModel

            delegate: SettingsCard {
                required property var modelData

                Layout.fillWidth: true
                headerText: modelData.label
                // Collapsible sections — clicking the header chevron toggles
                // visibility of the section's rule list. State lives on the
                // delegate so each section collapses independently; resets on
                // page reload (acceptable — recovering the list is one click
                // away and the page's rule count makes empty sections honest).
                collapsible: true
                // Right-aligned rule count in the section header — matches
                // the mockup's "MONITOR & LAYOUT  4 rules" pattern and gives
                // the user a quick scan of how many rules are bucketed where.
                // The "drag to set precedence" hint applies to every section:
                // priority within a section is uniformly list-order driven
                // (`renormalizePriorities` stamps band-base + per-band offset
                // for ALL sections), so the drag affordance and its hint are
                // section-agnostic.
                headerTrailingText: {
                    var count = modelData.rules ? modelData.rules.length : 0;
                    var base = i18np("%n rule", "%n rules", count);
                    return i18nc("Suffix in a section header showing the count and a reorder hint", "%1 · drag to set precedence", base);
                }

                contentItem: ColumnLayout {
                    spacing: 0

                    // One drag-reorderable, variable-height list for every
                    // section. Priorities within each cascade band are
                    // computed from list order by the C++ controller's
                    // `renormalizePriorities` (called on every moveRule),
                    // so the same drag UX applies uniformly across Monitor,
                    // Application, Activity, Animation, and Advanced.
                    WindowRuleSectionList {
                        Layout.fillWidth: true
                        rules: modelData.rules
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
}
