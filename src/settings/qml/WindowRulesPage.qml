// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

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
    // Composite "appSettings" surface threaded into the rule editor so the
    // picker components (LayoutComboBox for snapping/tiling actions, and the
    // screen / activity match-leaf editors) all see the same object.
    // LayoutComboBox reads `layouts` + the `default*` ids from this; the leaf
    // pickers read `screens` and `activities`.
    readonly property QtObject
    _editorAppSettings: QtObject {
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
        var buckets = {
        };
        for (var s = 0; s < page.sectionDescriptors.length; ++s) buckets[page.sectionDescriptors[s].value] = []
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

        function onDataChanged() {
            page.modelRevision++;
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

    AddRuleSheet {
        id: addRuleSheet

        controller: page.controller
        onSubjectChosen: function(ruleJson) {
            ruleEditorSheet.openFor(ruleJson, false);
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
        onRuleSaved: function(ruleJson) {
            if (editing)
                page.controller.updateRuleFromJson(ruleJson);
            else
                page.controller.addRuleFromJson(ruleJson);
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
            onMonitorSelected: function(screenId) {
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
                onClicked: addRuleSheet.open()
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
                model: [{
                    "value": -1,
                    "label": i18n("All")
                }].concat(page.sectionDescriptors)

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
                // Section enum value 3 = WindowRuleModel::Section::Animation.
                // Drives both the "drag to set precedence" hint in the header
                // and the per-row drag handles below.
                readonly property bool _isAnimationSection: modelData.section === 3

                Layout.fillWidth: true
                headerText: modelData.label
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
                            onToggleRequested: function(en) {
                                page.controller.setRuleEnabled(ruleId, en);
                            }
                            onEditRequested: {
                                ruleEditorSheet.openFor(page.controller.ruleJson(ruleId), true);
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
                    // y-positioned by `index * rowHeight + visualOffset`; a
                    // MouseArea on the dedicated handle column sets
                    // `drag.target: delegateRoot`, which a ColumnLayout-based
                    // Repeater would silently snap back. visualOffset shifts
                    // the in-between rows during drag to preview the new
                    // ordering; release commits via controller.moveRule
                    // (translating the (from, to) index pair into the
                    // before-id reference the controller expects).
                    Item {
                        id: animationOrderContainer

                        readonly property real rowHeight: Kirigami.Units.gridUnit * 4
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

                        visible: _isAnimationSection
                        Layout.fillWidth: true
                        Layout.preferredHeight: _isAnimationSection ? rules.length * rowHeight : 0
                        clip: true

                        Repeater {
                            model: _isAnimationSection ? animationOrderContainer.rules : null

                            delegate: Item {
                                id: animDelegateRoot

                                required property var modelData
                                required property int index
                                readonly property real baseY: index * animationOrderContainer.rowHeight
                                readonly property real visualOffset: {
                                    if (!animationOrderContainer.isDragging || index === animationOrderContainer.dragFromIndex)
                                        return 0;

                                    var from = animationOrderContainer.dragFromIndex;
                                    var to = animationOrderContainer.dropTargetIndex;
                                    if (from < 0 || to < 0)
                                        return 0;

                                    if (from < to) {
                                        if (index > from && index <= to)
                                            return -animationOrderContainer.rowHeight;

                                    } else if (index >= to && index < from) {
                                        return animationOrderContainer.rowHeight;
                                    }
                                    return 0;
                                }

                                width: animationOrderContainer.width
                                height: animationOrderContainer.rowHeight
                                y: baseY + visualOffset
                                z: animDragArea.drag.active ? 100 : 0

                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 0

                                    // Drag-handle column. The drag MouseArea
                                    // is scoped to this strip so clicks on
                                    // the row's toolbar buttons (edit /
                                    // duplicate / delete) still reach them.
                                    Item {
                                        Layout.alignment: Qt.AlignVCenter
                                        Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium + Kirigami.Units.largeSpacing
                                        Layout.fillHeight: true

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
                                            drag.maximumY: Math.max(0, (animationOrderContainer.rules.length - 1) * animationOrderContainer.rowHeight)
                                            onPressed: {
                                                animationOrderContainer.dragFromIndex = animDelegateRoot.index;
                                                animationOrderContainer.dropTargetIndex = animDelegateRoot.index;
                                                animationOrderContainer.isDragging = true;
                                            }
                                            onReleased: {
                                                var rules = animationOrderContainer.rules;
                                                var from = animationOrderContainer.dragFromIndex;
                                                var to = animationOrderContainer.dropTargetIndex;
                                                animationOrderContainer.isDragging = false;
                                                animationOrderContainer.dragFromIndex = -1;
                                                animationOrderContainer.dropTargetIndex = -1;
                                                // Snap delegate back to its
                                                // layout position before the
                                                // controller mutation reorders
                                                // the underlying snapshot.
                                                animDelegateRoot.y = Qt.binding(function() {
                                                    return animDelegateRoot.baseY + animDelegateRoot.visualOffset;
                                                });
                                                if (from >= 0 && to >= 0 && from !== to && from < rules.length && to < rules.length) {
                                                    var movedId = rules[from].ruleId;
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
                                                    var centerY = animDelegateRoot.y + animationOrderContainer.rowHeight / 2;
                                                    var targetIndex = Math.max(0, Math.min(animationOrderContainer.rules.length - 1, Math.floor(centerY / animationOrderContainer.rowHeight)));
                                                    if (targetIndex !== animationOrderContainer.dropTargetIndex)
                                                        animationOrderContainer.dropTargetIndex = targetIndex;

                                                }
                                            }
                                        }

                                    }

                                    WindowRuleRow {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
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
                                        onToggleRequested: function(en) {
                                            page.controller.setRuleEnabled(animDelegateRoot.modelData.ruleId, en);
                                        }
                                        onEditRequested: {
                                            ruleEditorSheet.openFor(page.controller.ruleJson(animDelegateRoot.modelData.ruleId), true);
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
