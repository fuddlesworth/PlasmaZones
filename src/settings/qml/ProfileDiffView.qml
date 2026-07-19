// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import "FontUtils.js" as FontUtils
import "ProfileDiffTree.js" as DiffTree

/**
 * @brief Read-only display of one half of a profile's diff — the SETTINGS or
 *        RULES section of the profile preview.
 *
 * Rows arrive as a PATH (`segments`) rather than a finished label, and this
 * view nests them: everything sharing a prefix hangs off one parent node, so a
 * setting reads as its own name under "Animations › Shader profile tree ›
 * Overrides" instead of every row restating (and eliding) the whole breadcrumb.
 *
 * The connector geometry is ported from `MatchExpressionView`, which renders
 * the rule preview's tree, so the two read as one visualisation: one Canvas per
 * row strokes the ancestor verticals, and the immediate-parent column also
 * strokes an L-stub into the row's content edge.
 *
 * A leaf carries a sequence of `LABEL value-pill` pairs — FROM / TO for a
 * changed setting, CHANGE for a rule. A value that parses as a colour gets a
 * swatch inside its pill, the way ActionListView renders `color`-kind params.
 */
ColumnLayout {
    id: root

    /// `[{ segments: [...], entries: [{ caption, value, emphasis, detail }] }]`.
    /// `segments` is the path to the leaf; a row carrying a plain `label`
    /// instead (the rules section, whose entries have no hierarchy) is treated
    /// as a single-segment path. `emphasis` is an optional colour for the pill
    /// text (a rule's added / removed state); `detail` is optional long-form
    /// text shown on the pill's tooltip.
    required property var rows

    /// When true, every leaf row grows a trailing Revert button that emits
    /// revertRequested with the row's original `source` map — the host decides
    /// what reverting means (the profile store drops that override).
    property bool revertable: false

    signal revertRequested(var source)

    /// Fixed COLUMN widths — the pills inside them still hug their content, the
    /// way the rule preview's value pills do.
    ///
    /// That preview holds its columns with a minimum width, which works because
    /// an action-type label is short. A settings leaf name can still outrun a
    /// floor, so a minimum would let each row set its own edges and the
    /// captions would stagger. Pinning the CELL rather than the pill keeps every
    /// column square without padding "0" out into a box the width of a blob.
    /// Wide enough that a folded path fits outright in the common case. The
    /// rows have room to spare: the caption and value columns beyond this are
    /// fixed, so widening the subject costs empty right margin, not layout.
    readonly property real subjectColumnWidth: Kirigami.Units.gridUnit * 26
    readonly property real captionColumnWidth: Kirigami.Units.gridUnit * 4
    readonly property real valueColumnWidth: Kirigami.Units.gridUnit * 10

    /// Tree visualisation constants — kept in lockstep with MatchExpressionView's
    /// and ActionListView's equivalents so all three trees look like one.
    readonly property real _indentStep: Kirigami.Units.gridUnit * 1.5
    readonly property color _guideColor: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
    // 1 device-independent px — matches MatchExpressionView's guide thickness.
    readonly property int _guideThickness: 1

    /// Flattened tree: one entry per rendered row, depth-first, each carrying
    /// the last-child flags of every node on its path (`ancestors`) so the
    /// Canvas knows which ancestor columns are still open below it.
    readonly property var _nodes: DiffTree.buildRows(root.rows)

    /// True when @p value is a hex colour a swatch can render.
    function isColorValue(value) {
        return typeof value === "string" && /^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(value);
    }

    // Spacing 0 — each delegate carries its own internal vertical padding, and
    // zero spacing lets the tree verticals join across rows without gaps.
    spacing: 0

    Repeater {
        model: root._nodes

        delegate: Item {
            id: entryDelegate

            required property var modelData

            readonly property var entries: entryDelegate.modelData.entries || []
            readonly property int depth: entryDelegate.modelData.depth
            readonly property var ancestors: entryDelegate.modelData.ancestors
            readonly property bool hasChildren: entryDelegate.modelData.hasChildren
            /// Where this row's own content starts, in lockstep with
            /// contentRow's leftMargin so the L-stub ends exactly at the bullet.
            readonly property real contentLeft: entryDelegate.depth * root._indentStep + Kirigami.Units.smallSpacing

            Layout.fillWidth: true
            implicitHeight: contentRow.implicitHeight + Kirigami.Units.gridUnit

            // For each ancestor column `a` (0 ≤ a < depth):
            //   - a == depth-1 (immediate parent): draw the L-stub at the row
            //     midpoint; the vertical runs full height when this row has
            //     siblings below, and stops at the midpoint when it is the last
            //     child.
            //   - a < depth-1 (outer ancestor): no stub. The vertical runs full
            //     height only while that ancestor's sub-tree is still open,
            //     which `ancestors[a]` answers.
            Canvas {
                id: treeCanvas

                anchors.fill: parent
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    ctx.strokeStyle = root._guideColor;
                    ctx.lineWidth = root._guideThickness;
                    ctx.lineCap = "butt";
                    var indentStep = root._indentStep;
                    var depth = entryDelegate.depth;
                    var anc = entryDelegate.ancestors;
                    var rowMid = Math.round(height / 2) + 0.5;
                    for (var a = 0; a < depth; ++a) {
                        var x = Math.round(a * indentStep + indentStep / 2) + 0.5;
                        var isImmediateParent = (a === depth - 1);
                        var currentIsLast = anc[a] === true;
                        if (isImmediateParent || !currentIsLast) {
                            ctx.beginPath();
                            ctx.moveTo(x, 0);
                            ctx.lineTo(x, (isImmediateParent && currentIsLast) ? rowMid : height);
                            ctx.stroke();
                        }
                        if (isImmediateParent) {
                            ctx.beginPath();
                            ctx.moveTo(x, rowMid);
                            ctx.lineTo(entryDelegate.contentLeft, rowMid);
                            ctx.stroke();
                        }
                    }
                    // Children connector: drop from just below this row's own
                    // content down to the row bottom, at the CHILDREN's column.
                    // Starting below the content rather than at row-mid keeps
                    // the line from tracing through this row's own label.
                    if (entryDelegate.hasChildren) {
                        var childX = Math.round(depth * indentStep + indentStep / 2) + 0.5;
                        var contentBottom = Math.round(rowMid + contentRow.height / 2) + 0.5;
                        ctx.beginPath();
                        ctx.moveTo(childX, contentBottom);
                        ctx.lineTo(childX, height);
                        ctx.stroke();
                    }
                }

                Connections {
                    function onDepthChanged() {
                        treeCanvas.requestPaint();
                    }

                    function onAncestorsChanged() {
                        treeCanvas.requestPaint();
                    }

                    function onHasChildrenChanged() {
                        treeCanvas.requestPaint();
                    }

                    target: entryDelegate
                }

                // onPaint reads contentRow.height for the children-connector
                // start point, so a late relayout (wrapping label) must
                // repaint too or the connector top sits at the stale height.
                Connections {
                    function onHeightChanged() {
                        treeCanvas.requestPaint();
                    }

                    target: contentRow
                }

                // onPaint samples the theme-derived guide colour. Every
                // PlatformTheme colour shares the one `colorsChanged` notify
                // signal, so this one handler repaints on any palette change.
                Connections {
                    function onColorsChanged() {
                        treeCanvas.requestPaint();
                    }

                    target: Kirigami.Theme
                }
            }

            RowLayout {
                id: contentRow

                anchors.left: parent.left
                anchors.leftMargin: entryDelegate.contentLeft
                anchors.right: parent.right
                anchors.rightMargin: Kirigami.Units.largeSpacing
                anchors.verticalCenter: parent.verticalCenter
                spacing: Kirigami.Units.largeSpacing

                // Bullet — accent on a leaf, muted on a grouping node, so the
                // rows carrying an actual value stand out from the scaffolding.
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: Math.round(Kirigami.Units.gridUnit / 2.5)
                    implicitHeight: implicitWidth
                    radius: width / 2
                    color: entryDelegate.entries.length > 0 ? Kirigami.Theme.highlightColor : root._guideColor
                }

                // Subject label.
                //
                // A LEAF is held to the subject column, shrunk by its own
                // indent, so the captions and pills of every row line up
                // regardless of depth. A GROUPING row has no captions or pills
                // to line up with, so holding it to that column only truncated
                // it for nothing — it takes the whole row instead.
                //
                // Leaves elide in the MIDDLE. A folded path keeps its meaning at
                // both ends ("Animations › … › Effect id"); eliding the tail
                // throws away the half that says which setting this is.
                Label {
                    readonly property bool isLeaf: entryDelegate.entries.length > 0

                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillWidth: !isLeaf
                    Layout.preferredWidth: isLeaf ? Math.max(Kirigami.Units.gridUnit * 6, root.subjectColumnWidth - entryDelegate.depth * root._indentStep) : -1
                    Layout.maximumWidth: isLeaf ? Layout.preferredWidth : Number.POSITIVE_INFINITY
                    text: entryDelegate.modelData.label
                    elide: isLeaf ? Text.ElideMiddle : Text.ElideRight
                    font.bold: true
                    opacity: isLeaf ? 1 : 0.7
                }

                Repeater {
                    model: entryDelegate.entries

                    delegate: RowLayout {
                        id: pairRow

                        required property var modelData
                        required property int index

                        spacing: Kirigami.Units.largeSpacing

                        Label {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: root.captionColumnWidth
                            Layout.maximumWidth: root.captionColumnWidth
                            elide: Text.ElideRight
                            text: pairRow.modelData.caption
                            // One binding: a font.<sub> sibling next to a whole-group `font:` is an
                            // illegal duplicate binding that fails the whole document. FontUtils
                            // passes only the size dimension the theme font actually carries.
                            font: FontUtils.withProps(Kirigami.Theme.smallFont, {
                                capitalization: Font.AllUppercase
                            })
                            opacity: 0.55
                        }

                        // Fixed-width cell; the pill within it is content-sized
                        // and only capped at the column, so a short value stays
                        // a short pill while a long one elides at the edge.
                        Item {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: root.valueColumnWidth
                            implicitHeight: valuePill.implicitHeight

                            Rectangle {
                                id: valuePill

                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(pillContent.implicitWidth + Kirigami.Units.largeSpacing * 2, parent.width)
                                implicitHeight: pillContent.implicitHeight + Kirigami.Units.smallSpacing
                                radius: Kirigami.Units.smallSpacing
                                Kirigami.Theme.colorSet: Kirigami.Theme.View
                                Kirigami.Theme.inherit: false
                                color: Kirigami.Theme.alternateBackgroundColor
                                border.width: 1
                                border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)

                                // Long-form payload on hover — the pill elides at the
                                // column edge, the tooltip carries the whole value.
                                HoverHandler {
                                    id: pillHover

                                    enabled: (pairRow.modelData.detail || "") !== ""
                                }
                                ToolTip.visible: pillHover.hovered
                                ToolTip.text: pairRow.modelData.detail || ""

                                RowLayout {
                                    id: pillContent

                                    anchors.fill: parent
                                    anchors.leftMargin: Kirigami.Units.smallSpacing
                                    anchors.rightMargin: Kirigami.Units.smallSpacing
                                    spacing: Kirigami.Units.smallSpacing

                                    // Swatch for a colour-valued setting, matching
                                    // how the rule preview renders colour params.
                                    Rectangle {
                                        visible: root.isColorValue(pairRow.modelData.value)
                                        Layout.alignment: Qt.AlignVCenter
                                        implicitWidth: pillLabel.implicitHeight
                                        implicitHeight: pillLabel.implicitHeight
                                        radius: Math.round(Kirigami.Units.smallSpacing / 2)
                                        color: root.isColorValue(pairRow.modelData.value) ? pairRow.modelData.value : "transparent"
                                        border.width: 1
                                        border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
                                    }

                                    Label {
                                        id: pillLabel

                                        Layout.alignment: Qt.AlignVCenter
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        text: pairRow.modelData.value
                                        color: pairRow.modelData.emphasis !== undefined ? pairRow.modelData.emphasis : Kirigami.Theme.textColor
                                        font.family: Kirigami.Theme.smallFont.family
                                    }
                                }
                            }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                // Per-leaf revert: drop this one override so the value falls
                // back to the parent's. Only on rows that carry an actual
                // change (grouping rows have nothing to revert).
                ToolButton {
                    visible: root.revertable && entryDelegate.entries.length > 0 && entryDelegate.modelData.source !== null && entryDelegate.modelData.source !== undefined
                    Layout.alignment: Qt.AlignVCenter
                    icon.name: "edit-undo"
                    ToolTip.text: i18n("Revert this change")
                    ToolTip.visible: hovered
                    Accessible.name: i18n("Revert this change")
                    onClicked: root.revertRequested(entryDelegate.modelData.source)
                }
            }
        }
    }
}
