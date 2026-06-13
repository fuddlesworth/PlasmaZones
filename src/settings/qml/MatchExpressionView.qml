// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.settings

/**
 * @brief Read-only match-expression preview backed by
 *        `QtQuick.Controls.TreeView` over a `MatchExpressionTreeModel`.
 *
 * Composite groups (`all` / `any` / `none`) render as theme-aware pills;
 * leaves render as `[bullet] Field OP [value-pill]`. The tree is
 * fully-expanded on every load — this is a peek view, not interactive
 * editing — so the user always sees the full match without per-node
 * expand/collapse interaction. Editing happens through the row's pencil
 * button, which opens `RuleEditorSheet`.
 *
 * Wire → user-label resolution stays on the QML side, keyed off the
 * controller's `matchFields()` / `operatorsForField()` tables already
 * cached on `WindowRulesPage`. The C++ model deliberately exposes only
 * the raw wire fields so the label-resolution logic lives in exactly
 * one place per axis (this file for the read-only view; the editor
 * components for authoring).
 */
ColumnLayout {
    id: root

    /// The WindowRuleController — supplies the wire→label tables for
    /// fields and operators. Threaded down from `WindowRulesPage`.
    required property var controller
    /// Cached `controller.matchFields()` table. Same caching rationale as
    /// `RuleEditorBody` — the Q_INVOKABLE allocates a fresh list per call.
    required property var matchFieldOptions
    /// The match-expression sub-tree to render — `{ field, op, value }` or
    /// `{ all | any | none: [...] }`. Re-assigning this rebuilds the
    /// backing model and re-expands the tree.
    required property var matchJson
    /// Composite app-settings surface (screens + activities), threaded down
    /// from `WindowRulesPage`. Used by `_valueLabel` to resolve screen-id /
    /// activity-uuid leaves to the same friendly labels the leaf editor
    /// shows. Optional — when null, screen/activity leaves fall back to the
    /// raw wire value (still better than nothing, mirrors the editor's own
    /// dangling-pin fallback).
    property var appSettings: null

    /// Wire→label resolver for a field. Returns the wire string unchanged
    /// when the field is unknown (e.g. a forward-compat rule referencing a
    /// field this build doesn't ship) — better than a blank pill.
    function _fieldLabel(wire) {
        for (var i = 0; i < root.matchFieldOptions.length; ++i) {
            if (root.matchFieldOptions[i].wire === wire)
                return root.matchFieldOptions[i].label;
        }
        return wire;
    }

    /// Wire→Field-enum-int helper for `_opLabel`.
    function _fieldEnum(wire) {
        for (var i = 0; i < root.matchFieldOptions.length; ++i) {
            if (root.matchFieldOptions[i].wire === wire)
                return root.matchFieldOptions[i].value;
        }
        return -1;
    }

    /// Wire→label resolver for an operator, scoped to the leaf's field.
    /// Same wire-string fallback as `_fieldLabel`.
    function _opLabel(fieldWire, opWire) {
        var fieldValue = root._fieldEnum(fieldWire);
        if (fieldValue < 0)
            return opWire;

        var ops = root.controller.operatorsForField(fieldValue);
        for (var i = 0; i < ops.length; ++i) {
            if (ops[i].wire === opWire)
                return ops[i].label;
        }
        return opWire;
    }

    /// Wire→valueKind helper for `_valueLabel`. Returns the controller-side
    /// kind string ("string" / "number" / "bool" / "screen" / "activity") or
    /// "string" for unknown fields — the safest default since `String(value)`
    /// is what a plain-string render would do anyway.
    function _valueKind(wire) {
        for (var i = 0; i < root.matchFieldOptions.length; ++i) {
            if (root.matchFieldOptions[i].wire === wire)
                return root.matchFieldOptions[i].valueKind || "string";
        }
        return "string";
    }

    /// Localize a leaf's value for display, mirroring `MatchLeafEditor`'s
    /// per-kind editors so the read-only preview agrees with the editor:
    ///   - bool → "True" / "False" (i18n'd)
    ///   - screen → `appSettings.screens.displayLabel` for the matching name
    ///   - activity → `appSettings.activities.name` for the matching id
    ///   - everything else (string, number) → `String(value)`
    /// Falls back to the raw wire value when a lookup misses (e.g. the
    /// rule references an unplugged monitor or removed activity), matching
    /// the editor's dangling-pin fallback.
    function _valueLabel(value, fieldWire) {
        if (value === undefined || value === null)
            return "";

        var kind = root._valueKind(fieldWire);
        if (kind === "bool")
            return value === true || value === "true" ? i18n("True") : i18n("False");

        if (kind === "screen" && root.appSettings) {
            var screens = root.appSettings.screens;
            if (screens) {
                for (var i = 0; i < screens.length; ++i) {
                    if (screens[i].name === value) {
                        // displayLabel carries make/model/resolution + connector;
                        // mark the primary monitor to match the editor's picker.
                        var label = screens[i].displayLabel || String(value);
                        if (screens[i].isPrimary)
                            label += " · " + i18n("Primary");
                        return label;
                    }
                }
            }
        }
        if (kind === "activity" && root.appSettings) {
            var activities = root.appSettings.activities;
            if (activities) {
                for (var j = 0; j < activities.length; ++j) {
                    if (activities[j].id === value)
                        return activities[j].name || String(value);
                }
            }
        }
        if (kind === "windowType") {
            // The field entry's `options` carry the {value, label} pairs the
            // editor surfaces; reuse them so the read-only summary shows
            // "Dialog" instead of the bare int "2".
            for (var k = 0; k < root.matchFieldOptions.length; ++k) {
                var entry = root.matchFieldOptions[k];
                if (entry.wire !== fieldWire)
                    continue;
                var opts = entry.options || [];
                for (var o = 0; o < opts.length; ++o) {
                    if (opts[o].value === value)
                        return opts[o].label || String(value);
                }
                break;
            }
        }
        return String(value);
    }

    spacing: 0

    TreeView {
        // ── Tree connector lines ──
        // Repeater-of-Rectangles repeatedly failed to actually paint
        // anything visible on real systems — most likely a
        // delegate-recycle binding race where `delegate.height`
        // hasn't resolved by the time the Repeater instantiates the
        // child rectangles. A single Canvas per row sidesteps the
        // whole problem: it owns its own paint pass, lives in the
        // delegate's coordinate system via anchors.fill, and just
        // strokes 2D lines using the current `depth` /
        // `ancestorIsLastChild` snapshot.

        id: tree

        Layout.fillWidth: true
        // Size to fit the (always-expanded) content — TreeView's contentHeight
        // updates after every model reset, so the bound preferredHeight grows
        // / shrinks with the rule. interactive: false disables internal
        // scrolling — the row delegate is the scroll context, not the tree.
        Layout.preferredHeight: tree.contentHeight
        interactive: false
        clip: false
        boundsBehavior: Flickable.StopAtBounds
        Component.onCompleted: {
            tree.expandRecursively(0, -1);
        }

        // Auto-expand every node on every model reset. `expandRecursively`
        // walks from the given row down to the requested depth (-1 = all),
        // so calling it once after a rebuild fully-expands the new tree.
        // The TreeView API requires a row index (not a QModelIndex) — the
        // model exposes a single top-level row 0, so seeding from 0 is
        // correct.
        Connections {
            function onMatchJsonChanged() {
                Qt.callLater(function () {
                    tree.expandRecursively(0, -1);
                });
            }

            target: treeModel
        }

        model: MatchExpressionTreeModel {
            id: treeModel

            matchJson: root.matchJson
        }

        // Plain Item delegate — ItemDelegate / TreeViewDelegate's
        // built-in background/padding slots interfered with the
        // tree-connector geometry we paint here. A plain Item gives
        // full layout control: the connector Canvas and the content
        // row are both direct children positioned at explicit x/y, so
        // the geometry is exactly what we see in the file.
        delegate: Item {
            id: delegate

            required property string kind
            required property string fieldWire
            required property string opWire
            required property var value
            required property bool isLastChild
            /// Depth role from the C++ model. `TreeView.depth` (attached)
            /// doesn't reliably resolve on plain `Item` delegates here,
            /// so the model exposes depth directly — see DepthRole
            /// rationale in `matchexpressiontreemodel.h`.
            required property int depth
            /// Per-ancestor last-child flags along this row's path
            /// (root-first, length == depth). Drives the per-column
            /// vertical-continues decision so non-immediate-parent
            /// columns blank out below a "last-child" ancestor instead
            /// of trailing a dangling line.
            required property var ancestorIsLastChild
            /// True iff this row has its own children in the model.
            /// Composite rows with children draw an extra half-vertical
            /// at the children's column position from row-midpoint to
            /// row-bottom, so the parent's L-stub visually connects to
            /// the first child's vertical (which begins at the next
            /// row's top). Renamed to avoid clashing with QML's own
            /// "hasChildren" semantics — the C++ role is
            /// `hasChildrenRow`.
            required property bool hasChildrenRow
            /// Effective layout depth: the WHEN section header hosted by
            /// WindowRuleRow is treated as the depth-0 parent, so the model's
            /// root composite renders one level in —
            /// indented under the header with a connector, mirroring how the
            /// THEN action list sits under its pill (ActionListView already
            /// renders actions at this depth-1). Every model depth shifts one
            /// column right; the operator / value-pill columns stay put because
            /// the leaf field-width compensation below keys off this same
            /// effective depth, so the +indentStep in the content margin cancels
            /// against the -indentStep in the field width.
            readonly property int _effectiveDepth: delegate.depth + 1
            /// Ancestor last-child flags for the effective tree. The virtual
            /// header-pill root has exactly one child — the model root — so it
            /// is always that child's last sibling; prepend `true`. Keeps the
            /// connector's column-0 vertical terminating at the root row instead
            /// of trailing a dangling line down the left of every deeper row.
            readonly property var _effectiveAncestors: [true].concat(delegate.ancestorIsLastChild || [])
            readonly property real _indentStep: Kirigami.Units.gridUnit * 1.5
            /// Tree connector color. Foreground textColor at 0.75 alpha:
            /// 0.4 and 0.55 both still vanished into the dark expansion
            /// surface in user testing. The mockup shows clearly legible
            /// connector lines, so erring on the bright side here is
            /// closer to the intended affordance than another low-alpha
            /// attempt — they should read as tree lines, not whispers.
            readonly property color _guideColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.75)
            /// One-physical-pixel hairline (`Math.max(1, ...)` so even when
            /// devicePixelRatio rounds to zero we still draw something).
            /// Matches the rest of the chrome's hairline conventions —
            /// borders, separators, hover strokes. The earlier
            /// `Math.max(2, Math.round(... * 1.5))` shape was tuned against
            /// the broken `Kirigami.Units.devicePixelRatio` (which evaluated
            /// to NaN and fell through to the floor of 2); once the
            /// underlying ratio started returning the real value the
            /// `* 1.5` made the connectors noticeably thicker than the
            /// original visual baseline.
            readonly property int _guideThickness: Math.max(1, Math.round(Screen.devicePixelRatio))

            // Size delegate to the tree's available width so the
            // rightmost spacer pushes content into a clean column.
            implicitWidth: tree.width
            // Row height: content + generous vertical breathing room.
            // The mockup's clear vertical rhythm needs ~one gridUnit of
            // padding on top of the content height; using `gridUnit`
            // (not largeSpacing*2) keeps the spacing proportional to the
            // font size across themes.
            implicitHeight: contentRow.implicitHeight + Kirigami.Units.gridUnit

            // For each ancestor column `a` (0 ≤ a < depth):
            //   - a == depth-1 (immediate parent): draw L-stub at the
            //     row midpoint; the vertical runs full height when the
            //     current row has more siblings below, and stops at the
            //     midpoint when the row is its parent's last child.
            //   - a < depth-1 (non-immediate ancestor): no L-stub. The
            //     vertical runs full height iff the ancestor at depth
            //     `a+1` on this row's path is NOT the last child of its
            //     parent (else the sub-tree carrying that column has
            //     ended above this row).
            Canvas {
                id: treeCanvas

                anchors.fill: parent
                // Canvas paint is opt-in: any time the values driving the
                // path geometry change (depth / ancestor flags / size /
                // theme) we need to nudge a repaint.
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    ctx.strokeStyle = delegate._guideColor;
                    ctx.lineWidth = delegate._guideThickness;
                    ctx.lineCap = "butt";
                    var indentStep = delegate._indentStep;
                    var smallSpacing = Kirigami.Units.smallSpacing;
                    var depth = delegate._effectiveDepth;
                    var anc = delegate._effectiveAncestors;
                    var rowMid = Math.round(height / 2) + 0.5;
                    // Content's left edge — kept in sync with contentRow's
                    // anchors.leftMargin so the L-stub ends exactly where
                    // the bullet / pill begins instead of crossing through
                    // it.
                    var contentLeft = depth * indentStep + smallSpacing;
                    for (var a = 0; a < depth; ++a) {
                        var x = Math.round(a * indentStep + indentStep / 2) + 0.5;
                        var isImmediateParent = (a === depth - 1);
                        // `anc[c]` answers: is the ancestor at depth c+1 on
                        // this row's path the last child of the ancestor at
                        // depth c? Earlier code used `anc[a+1]` here, which
                        // is the *next* ancestor's flag — the wrong axis.
                        // For non-immediate columns the visibility check
                        // needs anc[a] (the on-path ancestor that owns this
                        // column's sub-tree); for the immediate-parent
                        // column the row's own last-child flag (which IS
                        // anc[depth-1] == anc[a] when a == depth-1) drives
                        // the "stop at midpoint" terminator.
                        var currentIsLast = anc[a] === true;
                        // Vertical.
                        if (isImmediateParent || !currentIsLast) {
                            ctx.beginPath();
                            ctx.moveTo(x, 0);
                            ctx.lineTo(x, (isImmediateParent && currentIsLast) ? rowMid : height);
                            ctx.stroke();
                        }
                        // L-stub — stops at content's left edge. The old
                        // `indentStep`-wide stub overshot through the
                        // leaf-row bullet ("line going through the dot").
                        if (isImmediateParent) {
                            ctx.beginPath();
                            ctx.moveTo(x, rowMid);
                            ctx.lineTo(contentLeft, rowMid);
                            ctx.stroke();
                        }
                    }
                    // Children-connector vertical. For composite rows
                    // with children, drop a line from just below the
                    // parent's pill down to row-bottom, at the
                    // *children's* immediate-parent column position
                    // (x = depth * indentStep + indentStep / 2).
                    // Starting at pill-bottom (rather than row-mid)
                    // matters: the children-column x sits inside the
                    // pill's horizontal extent, so starting at row-mid
                    // would trace the line straight through the pill
                    // body — visible (and ugly) wherever the pill fill
                    // is translucent.
                    if (delegate.hasChildrenRow) {
                        var childX = Math.round(depth * indentStep + indentStep / 2) + 0.5;
                        var pillBottom = Math.round(rowMid + contentRow.height / 2) + 0.5;
                        ctx.beginPath();
                        ctx.moveTo(childX, pillBottom);
                        ctx.lineTo(childX, height);
                        ctx.stroke();
                    }
                }

                Connections {
                    function onDepthChanged() {
                        treeCanvas.requestPaint();
                    }

                    function onAncestorIsLastChildChanged() {
                        treeCanvas.requestPaint();
                    }

                    function onIsLastChildChanged() {
                        treeCanvas.requestPaint();
                    }

                    function onHasChildrenRowChanged() {
                        treeCanvas.requestPaint();
                    }

                    target: delegate
                }

                // contentRow.height drives where the children-connector
                // starts (just below the pill). Repaint when it shifts
                // so the line doesn't briefly cross the pill body.
                Connections {
                    function onHeightChanged() {
                        treeCanvas.requestPaint();
                    }

                    target: contentRow
                }
            }

            // ── Row content ──
            // Positioned manually based on depth — no ItemDelegate
            // leftPadding here, so the connector Canvas above renders
            // in clean indent columns without competing with built-in
            // padding semantics.
            RowLayout {
                id: contentRow

                anchors.left: parent.left
                anchors.leftMargin: delegate._effectiveDepth * delegate._indentStep + Kirigami.Units.smallSpacing
                anchors.right: parent.right
                anchors.rightMargin: Kirigami.Units.largeSpacing
                anchors.verticalCenter: parent.verticalCenter
                spacing: Kirigami.Units.smallSpacing

                // ── Composite group pill ──
                Loader {
                    active: delegate.kind !== "leaf"
                    visible: active
                    Layout.alignment: Qt.AlignVCenter
                    sourceComponent: groupPill
                }

                // ── Leaf row ──
                Loader {
                    active: delegate.kind === "leaf"
                    visible: active
                    Layout.alignment: Qt.AlignVCenter
                    sourceComponent: leafRow
                }

                Item {
                    Layout.fillWidth: true
                }
            }

            Component {
                id: groupPill

                Rectangle {
                    // One semantic tint per group kind. The three hues
                    // are picked from non-overlapping color families so
                    // each pill stands clear of the dark-navy expansion
                    // surface — the previous highlight-blue ANY pill
                    // blended into the surface hue regardless of alpha.
                    //   ALL  → positiveText (green)  — every child must match
                    //   ANY  → neutralText  (amber)  — at least one matches
                    //   NONE → negativeText (red)    — no child may match
                    readonly property color _tint: delegate.kind === "all" ? Kirigami.Theme.positiveTextColor : delegate.kind === "none" ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.neutralTextColor

                    implicitWidth: groupLabel.implicitWidth + Kirigami.Units.largeSpacing * 2
                    implicitHeight: groupLabel.implicitHeight + Kirigami.Units.smallSpacing * 2
                    // Capsule shape — mockup pills are fully rounded.
                    radius: implicitHeight / 2
                    // Tinted fill at 0.4 alpha + matching solid border.
                    // 0.25 was too faint against the dark expansion
                    // surface to read as a distinct pill background;
                    // 0.4 gives each badge a clearly visible fill while
                    // staying soft enough that the white label keeps
                    // its high-contrast read.
                    color: Qt.rgba(_tint.r, _tint.g, _tint.b, 0.4)
                    border.width: Math.max(1, Math.round(Screen.devicePixelRatio))
                    border.color: Qt.rgba(_tint.r, _tint.g, _tint.b, 0.9)

                    Label {
                        id: groupLabel

                        anchors.centerIn: parent
                        // First word uppercase ("ALL of" / "ANY of" /
                        // "NONE of") per mockup — literal cased strings
                        // since font.capitalization can't selectively
                        // upper-case only the first word.
                        text: delegate.kind === "all" ? i18nc("Match-tree group where every child must match", "ALL of") : delegate.kind === "any" ? i18nc("Match-tree group where at least one child must match", "ANY of") : i18nc("Match-tree group where no child may match", "NONE of")
                        font.bold: true
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        // Foreground textColor guarantees high contrast
                        // against the tinted pill fill on every theme.
                        color: Kirigami.Theme.textColor
                    }
                }
            }

            Component {
                id: leafRow

                RowLayout {
                    spacing: Kirigami.Units.largeSpacing

                    // Leaf bullet — accent blue (highlightColor) to
                    // match the mockup's distinct blue dot. The bullet
                    // is a small marker so the limited contrast vs the
                    // dark surface still reads cleanly (unlike the
                    // long tree-line strokes, which need foreground
                    // textColor to stand out).
                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        implicitWidth: Math.round(Kirigami.Units.gridUnit / 2.5)
                        implicitHeight: implicitWidth
                        radius: width / 2
                        color: Kirigami.Theme.highlightColor
                    }

                    // Field-name column — depth-compensated minimum
                    // width so the operator label always lands at the
                    // same *absolute* x across rows, regardless of
                    // tree depth. The contentRow's leftMargin grows by
                    // one `indentStep` per (effective) depth level, so
                    // the field column shrinks by the same amount to
                    // keep operator_x = leftMargin + bullet + field_w
                    // constant. Keys off `_effectiveDepth` to match the
                    // margin above — the +indentStep there cancels the
                    // -indentStep here, so indenting the whole tree under
                    // the header pill leaves the operator / value columns
                    // exactly where they were (still aligned with THEN).
                    // The Math.max floor (4 gridUnits) keeps deeply-nested
                    // rows readable when the formula would otherwise hand
                    // back a sub-field-width value.
                    Label {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.minimumWidth: Math.max(Kirigami.Units.gridUnit * 4, Kirigami.Units.gridUnit * 13 - delegate._effectiveDepth * delegate._indentStep)
                        text: root._fieldLabel(delegate.fieldWire)
                        font.bold: true
                    }

                    // Operator-label column — fixed minimum (depth no
                    // longer matters: the field column above has
                    // already absorbed the indent difference, so this
                    // column starts at a constant x for all rows).
                    Label {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.minimumWidth: Kirigami.Units.gridUnit * 8
                        text: root._opLabel(delegate.fieldWire, delegate.opWire)
                        font.capitalization: Font.AllUppercase
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        opacity: 0.55
                    }

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        implicitWidth: valueLabel.implicitWidth + Kirigami.Units.largeSpacing * 2
                        implicitHeight: valueLabel.implicitHeight + Kirigami.Units.smallSpacing
                        radius: Kirigami.Units.smallSpacing
                        color: Kirigami.Theme.alternateBackgroundColor
                        border.width: Math.round(Screen.devicePixelRatio)
                        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)

                        Label {
                            id: valueLabel

                            anchors.centerIn: parent
                            text: root._valueLabel(delegate.value, delegate.fieldWire)
                            font.family: Kirigami.Theme.smallFont.family
                        }
                    }
                }
            }
        }
    }
}
