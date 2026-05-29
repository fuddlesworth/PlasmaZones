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
    // expansion view so the per-row Q_INVOKABLE doesn't fire on every
    // expand. Same caching rationale as the RuleEditorBody cache.
    // Static-catalogue controllers never fire authoringCatalogueChanged
    // so this stays a one-shot read; a plugin-driven controller wanting
    // runtime catalogue refresh just needs to emit the signal.
    property var matchFieldOptions: page.controller.matchFields()
    // Cached `actionTypes()` — same caching rationale as matchFieldOptions;
    // threaded down to WindowRuleRow's expansion so each row doesn't re-invoke
    // the Q_INVOKABLE.
    property var actionTypeOptions: page.controller.actionTypes()

    Connections {
        function onAuthoringCatalogueChanged() {
            page.matchFieldOptions = page.controller.matchFields();
            page.actionTypeOptions = page.controller.actionTypes();
        }
        target: page.controller
        ignoreUnknownSignals: true
    }
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
    // Roles whose value feeds the sectionModel computation (bucketing,
    // filtering, summary cards). A dataChanged that only touches OTHER roles
    // (Name / Enabled / Priority — the row delegate's inline bindings) must
    // NOT trigger a full sectionModel rebuild: doing so re-walks every rule
    // on every per-row enable toggle. Mirrors the role enum in
    // src/settings/windowrulemodel.h — kept in sync by hand because QML can't
    // reference the C++ enum integers without yet another exposure surface.
    readonly property var _summaryRoles: [WindowRuleModel.SectionRole, WindowRuleModel.MatchSummaryRole, WindowRuleModel.ActionSummaryRole, WindowRuleModel.ScreenIdsRole, WindowRuleModel.ConditionCountRole, WindowRuleModel.ActionCountRole, WindowRuleModel.IsCompositeRole, WindowRuleModel.ValidationIssueCountRole]

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
            if (!ok && window && window.showToast) {
                // Match the defensive shape used throughout LayoutsPage:
                // when this page is hosted outside Main.qml (KCM / preview
                // host), `window.showToast` is undefined and an unguarded
                // call would raise.
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
            text: i18n("The PlasmaZones daemon is not running — window rules cannot be loaded or saved until it starts.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Warning
            visible: page.controller.daemonChangedWhileDirty
            text: i18n("The window rules changed on disk while you were editing — saving now will overwrite those changes. Review your edits before saving, or discard them to reload.")
            // Escape hatch — the controller's normal commit() refuses
            // when daemonChangedWhileDirty is set so the user doesn't
            // silently overwrite. forceCommit() bypasses the guard for
            // the "I know, save anyway" path; mirrors the SettingsCard
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
        // Bare, no SettingsCard wrapper — mirrors MonitorSelectorSection's
        // placement on the other pages (Monitor State, Tiling Algorithm,
        // Snapping, Virtual Screens) where the selector is a direct child of
        // the page column with no enclosing card or header strip.
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

        // ── Filter chips ──
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Label {
                text: i18n("Filter:")
                opacity: 0.7
            }

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

            Item {
                Layout.fillWidth: true
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
                // Drives both the "drag to set precedence" hint in the header
                // and the per-row drag handles below. Uses the C++ Section
                // enum exposed via QML_NAMED_ELEMENT(WindowRuleModel) so the
                // integer value isn't hardcoded in QML.
                readonly property bool _isAnimationSection: modelData.section === WindowRuleModel.Animation

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
                // For Animation, also tack on the drag hint since Animation
                // is the only section where the user-controlled ordering
                // (within the cascade band) actually matters and is
                // reorderable through the drag handle on each row.
                headerTrailingText: {
                    var count = modelData.rules ? modelData.rules.length : 0;
                    var base = i18np("%n rule", "%n rules", count);
                    return _isAnimationSection ? i18nc("Suffix in section header — count followed by reorder hint", "%1 · drag to set precedence", base) : base;
                }

                contentItem: ColumnLayout {
                    spacing: 0

                    // Non-Animation sections — straight Repeater of
                    // WindowRuleRow in the enclosing ColumnLayout. No drag
                    // semantics needed: priority within these sections is
                    // derived from cascade bands, not list order.
                    Repeater {
                        model: _isAnimationSection ? null : modelData.rules

                        delegate: WindowRuleRow {
                            required property var modelData

                            Layout.fillWidth: true
                            ruleId: modelData.ruleId
                            ruleName: modelData.name
                            ruleEnabled: modelData.enabled
                            matchSummary: modelData.matchSummary
                            actionSummary: modelData.actionSummary
                            conditionCount: modelData.conditionCount
                            actionCount: modelData.actionCount
                            isComposite: modelData.isComposite
                            validationIssueCount: modelData.validationIssueCount
                            section: modelData.section
                            priority: modelData.priority
                            controller: page.controller
                            matchFieldOptions: page.matchFieldOptions
                            actionTypeOptions: page.actionTypeOptions
                            appSettings: page._editorAppSettings
                            onToggleRequested: function (en) {
                                page.controller.setRuleEnabled(ruleId, en);
                            }
                            onEditRequested: {
                                ruleEditorSheet.openFor(page.controller.ruleJson(ruleId));
                            }
                            onDuplicateRequested: {
                                page.controller.duplicateRule(ruleId);
                            }
                            onDeleteRequested: {
                                page.controller.removeRule(ruleId);
                            }
                        }
                    }

                    // Animation section — manual-positioning drag container,
                    // mirrors OrderingPage's pattern. Each delegate Item is
                    // y-positioned by `cumulativeY(index) + visualOffset`; a
                    // MouseArea on the dedicated handle column sets
                    // `drag.target: delegateRoot`, which a ColumnLayout-based
                    // Repeater would silently snap back. visualOffset shifts
                    // the in-between rows during drag to preview the new
                    // ordering; release commits via controller.moveRule
                    // (translating the (from, to) index pair into the
                    // before-id reference the controller expects).
                    //
                    // Rows carry variable heights — the WindowRuleRow
                    // expansion body is opt-in per row, and the container
                    // keys per-ruleId heights through `delegateHeights` so
                    // baseY / totalHeight / drop-slot math all walk the
                    // actual heights rather than assuming a fixed stride.
                    Item {
                        id: animationOrderContainer

                        // Default height for a freshly-instantiated delegate
                        // before its WindowRuleRow has reported its
                        // implicitHeight back. Matches the collapsed-row
                        // height the previous fixed-stride layout assumed,
                        // so the first paint frame doesn't show overlap
                        // before the publish lands.
                        readonly property real headerRowHeight: Kirigami.Units.gridUnit * 4
                        // Snapshot the section's rules array onto the container
                        // so the inner Repeater delegate can reach it as
                        // `animationOrderContainer.rules` — within the inner
                        // delegate's scope, the unqualified `modelData`
                        // resolves to the **rule** (the delegate's own
                        // required property), so `modelData.rules.length`
                        // collapses to undefined and the drop-index math
                        // returns NaN. That silently disables the visual
                        // slot-in cascade (every other row's `visualOffset`
                        // reads `to=NaN` and stays at zero). Re-exposing the
                        // outer modelData here breaks the shadowing.
                        readonly property var rules: modelData ? modelData.rules : []
                        property int dragFromIndex: -1
                        property int dropTargetIndex: -1
                        property bool isDragging: false

                        // Per-ruleId published height. Keyed by ruleId (not
                        // index) so reorders don't invalidate the map. The
                        // whole object is reassigned on each publish so QML
                        // bindings that read it (heightOf / totalHeight /
                        // cumulativeY) re-evaluate.
                        property var delegateHeights: ({})

                        function setDelegateHeight(ruleId, h) {
                            // Drop 0 / negative publishes. The delegate's
                            // `actualHeight` binding fires once at creation
                            // before the inner RowLayout's children have
                            // computed their preferred heights — at that
                            // moment `animRow.implicitHeight` is 0. Letting
                            // that land in the map would peg every dependant
                            // baseY / totalHeight / drag.maximumY at 0,
                            // collapsing the layout AND clamping
                            // drag.maximumY = max(0, 0 - actualHeight) = 0
                            // so the drag can't move the row at all. The
                            // headerRowHeight fallback in `heightOf` covers
                            // the unpublished case, so dropping the 0 is
                            // strictly safer than recording it.
                            if (!ruleId || h <= 0 || delegateHeights[ruleId] === h)
                                return;
                            var copy = Object.assign({}, delegateHeights);
                            copy[ruleId] = h;
                            delegateHeights = copy;
                        }

                        function heightOf(idx) {
                            if (idx < 0 || idx >= rules.length)
                                return headerRowHeight;
                            var rule = rules[idx];
                            if (!rule)
                                return headerRowHeight;
                            var h = delegateHeights[rule.ruleId];
                            // Same defence as setDelegateHeight's guard —
                            // never let a stale 0 collapse the cumulative
                            // math. Should be unreachable given the publish
                            // guard above, but cheap insurance.
                            return (h !== undefined && h > 0) ? h : headerRowHeight;
                        }

                        function cumulativeY(idx) {
                            var y = 0;
                            for (var i = 0; i < idx; ++i)
                                y += heightOf(i);
                            return y;
                        }

                        // Find the slot whose original (pre-cascade) range
                        // contains @p centerY — used by the drag MouseArea
                        // to resolve dropTargetIndex against variable
                        // heights. Walking the cumulative sum once per
                        // pointer event is cheap for typical animation-rule
                        // counts; the prior fixed-stride formula
                        // `floor(centerY / rowHeight)` only worked when
                        // every row was the same height.
                        function slotIndexAt(centerY) {
                            var y = 0;
                            for (var i = 0; i < rules.length; ++i) {
                                var h = heightOf(i);
                                if (centerY < y + h)
                                    return i;
                                y += h;
                            }
                            return Math.max(0, rules.length - 1);
                        }

                        readonly property real totalHeight: {
                            var y = 0;
                            for (var i = 0; i < rules.length; ++i)
                                y += heightOf(i);
                            return y;
                        }

                        readonly property real draggedHeight: dragFromIndex >= 0 ? heightOf(dragFromIndex) : headerRowHeight

                        visible: _isAnimationSection
                        Layout.fillWidth: true
                        Layout.preferredHeight: _isAnimationSection ? totalHeight : 0
                        clip: true

                        Repeater {
                            model: _isAnimationSection ? animationOrderContainer.rules : null

                            delegate: Item {
                                id: animDelegateRoot

                                required property var modelData
                                required property int index
                                readonly property real baseY: animationOrderContainer.cumulativeY(index)
                                // Cascade displacement = ± the dragged
                                // row's own height (not a fixed stride) so
                                // displaced rows slide exactly into the
                                // gap the dragged row leaves behind. With
                                // a fixed stride a tall expanded source
                                // row would leave a gap larger than the
                                // shift, breaking the slot-in preview.
                                readonly property real visualOffset: {
                                    if (!animationOrderContainer.isDragging || index === animationOrderContainer.dragFromIndex)
                                        return 0;

                                    var from = animationOrderContainer.dragFromIndex;
                                    var to = animationOrderContainer.dropTargetIndex;
                                    if (from < 0 || to < 0)
                                        return 0;

                                    if (from < to) {
                                        if (index > from && index <= to)
                                            return -animationOrderContainer.draggedHeight;
                                    } else if (index >= to && index < from) {
                                        return animationOrderContainer.draggedHeight;
                                    }
                                    return 0;
                                }

                                // Publish the rendered height back to the
                                // container so cumulativeY / totalHeight
                                // pick up the expansion. animRow.height
                                // resolves to the inner RowLayout's
                                // implicitHeight (no anchors.fill on the
                                // row → its own ColumnLayout child drives
                                // the height), which grows as WindowRuleRow
                                // expands.
                                readonly property real actualHeight: animRow.implicitHeight
                                onActualHeightChanged: animationOrderContainer.setDelegateHeight(modelData.ruleId, actualHeight)
                                Component.onCompleted: animationOrderContainer.setDelegateHeight(modelData.ruleId, actualHeight)

                                width: animationOrderContainer.width
                                height: actualHeight
                                y: baseY + visualOffset
                                z: animDragArea.drag.active ? 100 : 0
                                // Make the delegate receive keyboard focus so
                                // the Keys.onPressed handler below can fire.
                                // Tab walks the list; Alt+Up/Down then
                                // reorders. WindowRuleRow's inner buttons
                                // still receive focus on their own — this
                                // only enables row-level focus for the
                                // reorder handler.
                                //
                                // No `focus: true` — Repeater rebuilds the
                                // delegate on every model change, and a
                                // `focus: true` delegate yanks focus from
                                // wherever the user was (an edit dialog
                                // button, a text field, etc.) on every
                                // rebuild. activeFocusOnTab gives the row
                                // its own tab stop without stealing focus.
                                activeFocusOnTab: true
                                Accessible.role: Accessible.ListItem
                                Accessible.name: i18nc("Accessible row label for an animation rule", "Animation rule %1 of %2: %3", animDelegateRoot.index + 1, animationOrderContainer.rules.length, animDelegateRoot.modelData.name)

                                // Keyboard reorder: Alt+Up / Alt+Down moves
                                // the focused row in priority order without
                                // requiring the drag MouseArea. Without
                                // this, the rule list was reorderable by
                                // pointer only — screen-reader and
                                // keyboard-only users had no way to change
                                // priority. Pairs with activeFocusOnTab on
                                // the row WindowRuleRow below so Tab walks
                                // the list focusably.
                                Keys.onPressed: event => {
                                    if (!(event.modifiers & Qt.AltModifier))
                                        return;
                                    var rules = animationOrderContainer.rules;
                                    var from = animDelegateRoot.index;
                                    var to = from;
                                    if (event.key === Qt.Key_Up) {
                                        // Always accept the keystroke at the
                                        // clamp boundaries — otherwise Alt+Up
                                        // bubbles up to ancestors (menu
                                        // mnemonics, focus traversal) and the
                                        // user gets surprising secondary
                                        // behaviour at the list endpoints.
                                        event.accepted = true;
                                        if (from <= 0)
                                            return;
                                        to = from - 1;
                                    } else if (event.key === Qt.Key_Down) {
                                        event.accepted = true;
                                        if (from >= rules.length - 1)
                                            return;
                                        to = from + 1;
                                    } else {
                                        return;
                                    }
                                    var movedId = rules[from].ruleId;
                                    // Mirror the drag-release beforeId math.
                                    var beforeId = "";
                                    if (from < to) {
                                        if (to + 1 < rules.length)
                                            beforeId = rules[to + 1].ruleId;
                                    } else {
                                        beforeId = rules[to].ruleId;
                                    }
                                    page.controller.moveRule(movedId, beforeId);
                                }

                                RowLayout {
                                    id: animRow

                                    // Top-anchored (not anchors.fill) so the
                                    // delegate Item's height can come from
                                    // animRow.implicitHeight without a
                                    // circular binding through anchors.fill
                                    // → parent.height → animRow.implicitHeight.
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    spacing: 0

                                    // Drag-handle column. The drag MouseArea
                                    // is scoped to this strip so clicks on
                                    // the row's toolbar buttons (edit /
                                    // duplicate / delete) still reach them.
                                    Item {
                                        // Pin the handle column to the
                                        // collapsed-row height — anchoring
                                        // it via `Layout.fillHeight` would
                                        // stretch the column down through
                                        // the expansion body, centering the
                                        // grip icon mid-expansion and
                                        // giving the user a tall but
                                        // visually unanchored drag strip.
                                        // Pinning to headerRowHeight keeps
                                        // the handle aligned with the
                                        // header row whether the body is
                                        // collapsed or expanded.
                                        Layout.alignment: Qt.AlignTop
                                        Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium + Kirigami.Units.largeSpacing
                                        Layout.preferredHeight: animationOrderContainer.headerRowHeight

                                        Kirigami.Icon {
                                            anchors.centerIn: parent
                                            width: Kirigami.Units.iconSizes.smallMedium
                                            height: Kirigami.Units.iconSizes.smallMedium
                                            source: "handle-sort"
                                            opacity: animDragArea.containsMouse || animDragArea.drag.active ? 0.7 : 0.3
                                        }

                                        MouseArea {
                                            id: animDragArea

                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                            drag.target: animDelegateRoot
                                            drag.axis: Drag.YAxis
                                            drag.minimumY: 0
                                            // Constrain the dragged row's top
                                            // so its bottom edge can reach
                                            // — but not exceed — the
                                            // container's bottom. With
                                            // variable heights this is
                                            // `totalHeight - actualHeight`,
                                            // not the old fixed-stride
                                            // `(length-1) * rowHeight`.
                                            drag.maximumY: Math.max(0, animationOrderContainer.totalHeight - animDelegateRoot.actualHeight)
                                            // Captured at onPressed so we
                                            // move the rule the user actually
                                            // grabbed even if the rules array
                                            // mutates mid-drag (daemon-driven
                                            // rulesChanged). Using the
                                            // current-snapshot index at
                                            // onReleased could pick up a
                                            // different rule that landed at
                                            // the same index.
                                            property string draggedRuleId: ""
                                            onPressed: {
                                                animationOrderContainer.dragFromIndex = animDelegateRoot.index;
                                                animationOrderContainer.dropTargetIndex = animDelegateRoot.index;
                                                animationOrderContainer.isDragging = true;
                                                const snapshotRules = animationOrderContainer.rules;
                                                draggedRuleId = (animDelegateRoot.index >= 0 && animDelegateRoot.index < snapshotRules.length) ? snapshotRules[animDelegateRoot.index].ruleId : "";
                                            }
                                            onReleased: {
                                                var rules = animationOrderContainer.rules;
                                                var from = animationOrderContainer.dragFromIndex;
                                                var to = animationOrderContainer.dropTargetIndex;
                                                var movedId = draggedRuleId;
                                                animationOrderContainer.isDragging = false;
                                                animationOrderContainer.dragFromIndex = -1;
                                                animationOrderContainer.dropTargetIndex = -1;
                                                draggedRuleId = "";
                                                // Snap delegate back to its
                                                // layout position before the
                                                // controller mutation reorders
                                                // the underlying snapshot.
                                                animDelegateRoot.y = Qt.binding(function () {
                                                    return animDelegateRoot.baseY + animDelegateRoot.visualOffset;
                                                });
                                                if (movedId !== "" && from >= 0 && to >= 0 && from !== to && to < rules.length) {
                                                    // controller.moveRule
                                                    // positions movedId
                                                    // immediately BEFORE
                                                    // beforeId. To move down
                                                    // (from < to), beforeId is
                                                    // the rule at (to + 1) so
                                                    // the moved rule lands
                                                    // after the target; if
                                                    // dropping at the end,
                                                    // pass empty.
                                                    var beforeId = "";
                                                    if (from < to) {
                                                        if (to + 1 < rules.length)
                                                            beforeId = rules[to + 1].ruleId;
                                                    } else {
                                                        beforeId = rules[to].ruleId;
                                                    }
                                                    page.controller.moveRule(movedId, beforeId);
                                                }
                                            }
                                            onPositionChanged: {
                                                if (drag.active) {
                                                    // Pick the slot whose
                                                    // original (pre-cascade)
                                                    // vertical range contains
                                                    // the dragged row's
                                                    // centerY. slotIndexAt
                                                    // walks the variable-
                                                    // height cumulative sum,
                                                    // so an expanded row
                                                    // earlier in the list
                                                    // shifts the targets
                                                    // downward correctly.
                                                    var centerY = animDelegateRoot.y + animDelegateRoot.actualHeight / 2;
                                                    var targetIndex = animationOrderContainer.slotIndexAt(centerY);
                                                    if (targetIndex !== animationOrderContainer.dropTargetIndex)
                                                        animationOrderContainer.dropTargetIndex = targetIndex;
                                                }
                                            }
                                        }
                                    }

                                    WindowRuleRow {
                                        Layout.fillWidth: true
                                        // No Layout.fillHeight — animRow's
                                        // implicitHeight drives the delegate
                                        // and the delegate sizes to fit, so
                                        // fillHeight here would induce a
                                        // self-referential cycle. The
                                        // WindowRuleRow's own ColumnLayout
                                        // (header + collapsible body)
                                        // supplies the implicit height the
                                        // outer animRow inherits.
                                        ruleId: animDelegateRoot.modelData.ruleId
                                        ruleName: animDelegateRoot.modelData.name
                                        ruleEnabled: animDelegateRoot.modelData.enabled
                                        matchSummary: animDelegateRoot.modelData.matchSummary
                                        actionSummary: animDelegateRoot.modelData.actionSummary
                                        conditionCount: animDelegateRoot.modelData.conditionCount
                                        actionCount: animDelegateRoot.modelData.actionCount
                                        isComposite: animDelegateRoot.modelData.isComposite
                                        validationIssueCount: animDelegateRoot.modelData.validationIssueCount
                                        section: animDelegateRoot.modelData.section
                                        priority: animDelegateRoot.modelData.priority
                                        controller: page.controller
                                        matchFieldOptions: page.matchFieldOptions
                                        actionTypeOptions: page.actionTypeOptions
                                        appSettings: page._editorAppSettings
                                        onToggleRequested: function (en) {
                                            page.controller.setRuleEnabled(animDelegateRoot.modelData.ruleId, en);
                                        }
                                        onEditRequested: {
                                            ruleEditorSheet.openFor(page.controller.ruleJson(animDelegateRoot.modelData.ruleId));
                                        }
                                        onDuplicateRequested: {
                                            page.controller.duplicateRule(animDelegateRoot.modelData.ruleId);
                                        }
                                        onDeleteRequested: {
                                            page.controller.removeRule(animDelegateRoot.modelData.ruleId);
                                        }
                                    }
                                }

                                Behavior on y {
                                    enabled: !animDragArea.drag.active

                                    PhosphorMotionAnimation {
                                        profile: "widget.reorder"
                                        durationOverride: Kirigami.Units.longDuration
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
