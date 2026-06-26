// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.plasmazones.settings
import "SearchAnchorHelpers.js" as SearchAnchors

/**
 * @brief Drag-reorderable, variable-height list of WindowRuleRow delegates.
 *
 * Hosts one rule-section's rows. Each row can independently expand its
 * WHEN/THEN preview, and rows can be reordered by dragging the grip
 * handle column or by Alt+Up / Alt+Down on a focused row. Priorities
 * within a section are entirely list-order driven via the controller's
 * `renormalizePriorities` — moving a row in the UI translates to a
 * `controller.moveRule(movedId, beforeId)` call on release, and the
 * model's `rowsMoved` signal is what `WindowRulesPage` listens for to
 * rebuild its bucketed `sectionModel`.
 *
 * Variable-height invariant: each delegate publishes its actual rendered
 * height (header row + expansion contribution) back to the container via
 * `setDelegateHeight(ruleId, h)`. The container's `cumulativeY`,
 * `totalHeight`, and `slotIndexAt` walk that map so the drag cascade
 * matches the layout even when rows differ in size — fixed-stride math
 * (the prior shape) wouldn't survive an expanded source row leaving a
 * gap larger than the displaced rows' shift, breaking the slot-in
 * preview.
 */
Item {
    id: root

    /// The section's rule entries — `[{ ruleId, name, enabled, ... }, ...]`
    /// as produced by `WindowRulesPage.sectionModel`. The container snapshots
    /// the array here so the Repeater delegate's `modelData` (which the
    /// delegate scope rebinds to the row, not the section) doesn't collide
    /// with the array reference.
    required property var rules
    /// The WindowRuleController — supplies match-field and operator tables
    /// to the row's expansion view and owns `moveRule` for drag commits.
    required property var controller
    /// Cached `controller.matchFields()` — passed through to each row's
    /// expansion preview so the Q_INVOKABLE isn't fired per-row.
    required property var matchFieldOptions
    /// Cached `controller.actionTypes()` — same caching rationale.
    required property var actionTypeOptions
    /// Composite app-settings surface (layouts + animations + screens +
    /// activities + window picker) threaded into MatchLeafEditor and
    /// ActionListView. May be null while the page is still wiring up.
    property var appSettings: null

    /// Emitted when the row's enable switch is toggled. Page-level
    /// handler routes to `controller.setRuleEnabled`.
    signal toggleRequested(string ruleId, bool enabled)
    /// Emitted when the row's pencil button is clicked. Page-level
    /// handler opens the RuleEditorSheet.
    signal editRequested(string ruleId)
    /// Emitted when the row's duplicate button is clicked.
    signal duplicateRequested(string ruleId)
    /// Emitted when the row's delete button is clicked.
    signal deleteRequested(string ruleId)

    // Default height for a freshly-instantiated delegate before its
    // WindowRuleRow has reported its implicitHeight back. Matches the
    // collapsed-row visual baseline so the first paint frame doesn't
    // show overlap before the publish lands.
    readonly property real headerRowHeight: Kirigami.Units.gridUnit * 4

    // Drag state — the container's own dragFromIndex / dropTargetIndex /
    // isDragging trio that the per-delegate `visualOffset` cascade
    // consumes. Reset to sentinel values on release.
    property int dragFromIndex: -1
    property int dropTargetIndex: -1
    property bool isDragging: false

    // Per-ruleId published height. Keyed by ruleId (not index) so
    // reorders don't invalidate the map. The whole object is
    // reassigned on each publish so QML bindings that read it
    // (heightOf / totalHeight / cumulativeY) re-evaluate — partial
    // mutation `delegateHeights[k] = v` wouldn't notify dependents.
    property var delegateHeights: ({})

    function setDelegateHeight(ruleId, h) {
        // Drop 0 / negative publishes. The delegate's `actualHeight`
        // binding can fire once at creation before the inner RowLayout's
        // children have computed their preferred heights — letting a
        // 0 land in the map would peg every dependant baseY /
        // totalHeight / drag.maximumY at 0, collapsing the layout
        // AND clamping `drag.maximumY = max(0, 0 - actualHeight) = 0`
        // so the drag can't move the row at all. The headerRowHeight
        // fallback in `heightOf` covers the unpublished case, so
        // dropping the 0 is strictly safer than recording it.
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
        // Same defence as setDelegateHeight's guard — never let a
        // stale 0 collapse the cumulative math. Should be
        // unreachable given the publish guard above, but cheap
        // insurance.
        return (h !== undefined && h > 0) ? h : headerRowHeight;
    }

    function cumulativeY(idx) {
        var y = 0;
        for (var i = 0; i < idx; ++i)
            y += heightOf(i);
        return y;
    }

    // Find the slot whose original (pre-cascade) range contains @p
    // centerY — used by the drag MouseArea to resolve dropTargetIndex
    // against variable heights. Walking the cumulative sum once per
    // pointer event is cheap for typical rule counts; the prior
    // fixed-stride formula `floor(centerY / rowHeight)` only worked
    // when every row was the same height.
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

    Layout.fillWidth: true
    Layout.preferredHeight: totalHeight
    clip: true

    Repeater {
        model: root.rules

        delegate: Item {
            id: delegateRoot

            required property var modelData
            required property int index

            readonly property real baseY: root.cumulativeY(index)
            // Cascade displacement = ± the dragged row's own height
            // (not a fixed stride) so displaced rows slide exactly
            // into the gap the dragged row leaves behind. With a
            // fixed stride a tall expanded source row would leave a
            // gap larger than the shift, breaking the slot-in
            // preview.
            readonly property real visualOffset: {
                if (!root.isDragging || index === root.dragFromIndex)
                    return 0;

                var from = root.dragFromIndex;
                var to = root.dropTargetIndex;
                if (from < 0 || to < 0)
                    return 0;

                if (from < to) {
                    if (index > from && index <= to)
                        return -root.draggedHeight;
                } else if (index >= to && index < from) {
                    return root.draggedHeight;
                }
                return 0;
            }

            // Publish the rendered height back to the container so
            // cumulativeY / totalHeight pick up expansion. `rowLayout`
            // is anchored without `anchors.fill` so its
            // implicitHeight follows its content (handle column max'd
            // against WindowRuleRow.implicitHeight), and the
            // delegate's own `height` tracks the layout — no circular
            // binding because the chain is one-directional from row
            // content up to delegate height.
            readonly property real actualHeight: rowLayout.implicitHeight
            onActualHeightChanged: root.setDelegateHeight(modelData.ruleId, actualHeight)

            // Register a per-rule deep-link anchor ("rule:<id>") with the host
            // page's reveal registry so a window-rule search result scrolls to
            // and pulses this exact row (expanding its section card if collapsed).
            Component.onCompleted: {
                root.setDelegateHeight(modelData.ruleId, actualHeight);
                Qt.callLater(function () {
                    var pg = SearchAnchors.pageFor(delegateRoot);
                    if (pg)
                        pg.registerSearchAnchor("rule:" + modelData.ruleId, delegateRoot, SearchAnchors.cardFor(delegateRoot));
                });
            }
            Component.onDestruction: {
                var pg = SearchAnchors.pageFor(delegateRoot);
                if (pg)
                    pg.unregisterSearchAnchor("rule:" + modelData.ruleId);
            }

            width: root.width
            height: actualHeight
            y: baseY + visualOffset
            z: dragArea.drag.active ? 100 : 0
            // No `focus: true` — Repeater rebuilds the delegate on
            // every model change, and a `focus: true` delegate would
            // yank focus from wherever the user was. activeFocusOnTab
            // gives the row a tab stop for keyboard reorder without
            // stealing focus on every rebuild.
            activeFocusOnTab: true
            Accessible.role: Accessible.ListItem
            Accessible.name: i18nc("Accessible row label for a window rule", "Rule %1 of %2: %3", delegateRoot.index + 1, root.rules.length, delegateRoot.modelData.name)

            // Keyboard reorder: Alt+Up / Alt+Down moves the focused
            // row in priority order without requiring the drag
            // MouseArea. Without this, the rule list was reorderable
            // by pointer only — screen-reader and keyboard-only users
            // had no way to change priority. Always-accept at the
            // clamp boundaries so Alt+Up at index 0 doesn't bubble
            // into menu mnemonics / focus traversal.
            Keys.onPressed: event => {
                if (!(event.modifiers & Qt.AltModifier))
                    return;
                var rulesSnapshot = root.rules;
                var from = delegateRoot.index;
                var to = from;
                if (event.key === Qt.Key_Up) {
                    event.accepted = true;
                    if (from <= 0)
                        return;
                    to = from - 1;
                } else if (event.key === Qt.Key_Down) {
                    event.accepted = true;
                    if (from >= rulesSnapshot.length - 1)
                        return;
                    to = from + 1;
                } else {
                    return;
                }
                var movedId = rulesSnapshot[from].ruleId;
                // Mirror the drag-release beforeId math.
                var beforeId = "";
                if (from < to) {
                    if (to + 1 < rulesSnapshot.length)
                        beforeId = rulesSnapshot[to + 1].ruleId;
                } else {
                    beforeId = rulesSnapshot[to].ruleId;
                }
                root.controller.moveRule(movedId, beforeId);
            }

            RowLayout {
                id: rowLayout

                // Top-anchored (not anchors.fill) so the delegate
                // Item's height can come from rowLayout.implicitHeight
                // without a circular binding through anchors.fill →
                // parent.height → rowLayout.implicitHeight.
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                spacing: 0

                // Drag-handle column. The drag MouseArea is scoped to
                // this strip so clicks on the row's toolbar buttons
                // (edit / duplicate / delete) still reach them.
                Item {
                    // Pin the handle column to the collapsed-row
                    // height — anchoring it via `Layout.fillHeight`
                    // would stretch the column down through the
                    // expansion body, centering the grip icon
                    // mid-expansion and giving the user a tall but
                    // visually unanchored drag strip. Pinning to
                    // headerRowHeight keeps the handle aligned with
                    // the header row whether the body is collapsed
                    // or expanded.
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium + Kirigami.Units.largeSpacing
                    Layout.preferredHeight: root.headerRowHeight

                    Kirigami.Icon {
                        anchors.centerIn: parent
                        width: Kirigami.Units.iconSizes.smallMedium
                        height: Kirigami.Units.iconSizes.smallMedium
                        source: "handle-sort"
                        opacity: dragArea.containsMouse || dragArea.drag.active ? 0.7 : 0.3
                    }

                    MouseArea {
                        id: dragArea

                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                        drag.target: delegateRoot
                        drag.axis: Drag.YAxis
                        drag.minimumY: 0
                        // Constrain the dragged row's top so its
                        // bottom edge can reach — but not exceed —
                        // the container's bottom. With variable
                        // heights this is `totalHeight - actualHeight`,
                        // not the old fixed-stride
                        // `(length-1) * rowHeight`.
                        drag.maximumY: Math.max(0, root.totalHeight - delegateRoot.actualHeight)
                        // Captured at onPressed so we move the rule
                        // the user actually grabbed even if the
                        // rules array mutates mid-drag (daemon-driven
                        // rulesChanged). Using the current-snapshot
                        // index at onReleased could pick up a
                        // different rule that landed at the same
                        // index.
                        property string draggedRuleId: ""
                        onPressed: {
                            root.dragFromIndex = delegateRoot.index;
                            root.dropTargetIndex = delegateRoot.index;
                            root.isDragging = true;
                            const snapshotRules = root.rules;
                            draggedRuleId = (delegateRoot.index >= 0 && delegateRoot.index < snapshotRules.length) ? snapshotRules[delegateRoot.index].ruleId : "";
                        }
                        onReleased: {
                            var rulesSnapshot = root.rules;
                            var from = root.dragFromIndex;
                            var to = root.dropTargetIndex;
                            var movedId = draggedRuleId;
                            root.isDragging = false;
                            root.dragFromIndex = -1;
                            root.dropTargetIndex = -1;
                            draggedRuleId = "";
                            // Snap delegate back to its layout
                            // position before the controller
                            // mutation reorders the underlying
                            // snapshot.
                            delegateRoot.y = Qt.binding(function () {
                                return delegateRoot.baseY + delegateRoot.visualOffset;
                            });
                            if (movedId !== "" && from >= 0 && to >= 0 && from !== to && to < rulesSnapshot.length) {
                                // controller.moveRule positions
                                // movedId immediately BEFORE
                                // beforeId. To move down (from <
                                // to), beforeId is the rule at
                                // (to + 1) so the moved rule lands
                                // after the target; if dropping at
                                // the end, pass empty.
                                var beforeId = "";
                                if (from < to) {
                                    if (to + 1 < rulesSnapshot.length)
                                        beforeId = rulesSnapshot[to + 1].ruleId;
                                } else {
                                    beforeId = rulesSnapshot[to].ruleId;
                                }
                                root.controller.moveRule(movedId, beforeId);
                            }
                        }
                        onPositionChanged: {
                            if (drag.active) {
                                // Pick the slot whose original
                                // (pre-cascade) vertical range
                                // contains the dragged row's
                                // centerY. slotIndexAt walks the
                                // variable-height cumulative sum,
                                // so an expanded row earlier in
                                // the list shifts the targets
                                // downward correctly.
                                var centerY = delegateRoot.y + delegateRoot.actualHeight / 2;
                                var targetIndex = root.slotIndexAt(centerY);
                                if (targetIndex !== root.dropTargetIndex)
                                    root.dropTargetIndex = targetIndex;
                            }
                        }
                    }
                }

                WindowRuleRow {
                    Layout.fillWidth: true
                    // No Layout.fillHeight — rowLayout's implicitHeight
                    // drives the delegate and the delegate sizes to
                    // fit, so fillHeight here would induce a
                    // self-referential cycle. WindowRuleRow's own
                    // ColumnLayout (header + collapsible body)
                    // supplies the implicit height the outer rowLayout
                    // inherits.
                    ruleId: delegateRoot.modelData.ruleId
                    ruleName: delegateRoot.modelData.name
                    ruleEnabled: delegateRoot.modelData.enabled
                    matchSummary: delegateRoot.modelData.matchSummary
                    actionSummary: delegateRoot.modelData.actionSummary
                    conditionCount: delegateRoot.modelData.conditionCount
                    actionCount: delegateRoot.modelData.actionCount
                    isComposite: delegateRoot.modelData.isComposite
                    validationIssueCount: delegateRoot.modelData.validationIssueCount
                    priority: delegateRoot.modelData.priority
                    managed: delegateRoot.modelData.managed === true
                    controller: root.controller
                    matchFieldOptions: root.matchFieldOptions
                    actionTypeOptions: root.actionTypeOptions
                    appSettings: root.appSettings
                    onToggleRequested: function (en) {
                        root.toggleRequested(delegateRoot.modelData.ruleId, en);
                    }
                    onEditRequested: {
                        root.editRequested(delegateRoot.modelData.ruleId);
                    }
                    onDuplicateRequested: {
                        root.duplicateRequested(delegateRoot.modelData.ruleId);
                    }
                    onDeleteRequested: {
                        root.deleteRequested(delegateRoot.modelData.ruleId);
                    }
                }
            }

            Behavior on y {
                enabled: !dragArea.drag.active

                PhosphorMotionAnimation {
                    profile: "widget.reorder"
                    durationOverride: Kirigami.Units.longDuration
                }
            }
        }
    }
}
