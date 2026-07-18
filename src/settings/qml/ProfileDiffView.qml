// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import "FontUtils.js" as FontUtils

/**
 * @brief Read-only display of one half of a profile's diff — the SETTINGS or
 *        RULES section of the profile preview.
 *
 * Mirrors `ActionListView` structurally (which in turn mirrors
 * `MatchExpressionView`) so the profile preview and the rule preview read as
 * one tree visualisation: each entry renders at depth-1 in a flat tree, with a
 * vertical guide and L-stub at column 0 plus an accent bullet at the row's
 * content edge. The row carries a bold subject label, then a sequence of
 * `LABEL value-pill` pairs — FROM / TO for a changed setting, CHANGE for a
 * rule — aligned in the same tabular columns the rule preview uses.
 *
 * A value that parses as a colour gets a swatch inside its pill, the way
 * ActionListView renders `color`-kind action params.
 */
ColumnLayout {
    id: root

    /// `[{ label, entries: [{ caption, value, emphasis }] }, ...]`. `emphasis`
    /// is an optional colour for the pill text (a rule's added / removed
    /// state); omit it for ordinary values.
    required property var rows

    /// Tree visualisation constants — kept in lockstep with ActionListView's
    /// and MatchExpressionView's equivalents so all three trees look like one.
    readonly property real _indentStep: Kirigami.Units.gridUnit * 1.5
    readonly property color _guideColor: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
    // 1 device-independent px — matches ActionListView's guide thickness.
    readonly property int _guideThickness: 1

    /// True when @p value is a hex colour a swatch can render.
    function isColorValue(value) {
        return typeof value === "string" && /^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(value);
    }

    // Spacing 0 — each delegate carries its own internal vertical padding, and
    // zero spacing lets the col-0 tree verticals join across rows without gaps.
    spacing: 0

    Repeater {
        model: root.rows || []

        delegate: Item {
            id: entryDelegate

            required property var modelData
            required property int index

            readonly property var entries: entryDelegate.modelData.entries || []
            // The bottom-most row's vertical stops at row-mid instead of running
            // to the row bottom, matching the "last child" terminator.
            readonly property bool isLastRow: entryDelegate.index === (root.rows ? root.rows.length - 1 : 0)

            Layout.fillWidth: true
            implicitHeight: contentRow.implicitHeight + Kirigami.Units.gridUnit

            // Tree connector — always the depth-1 case: one column vertical at
            // x = indentStep/2 with an L-stub to the bullet at content_left.
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
                    var x = Math.round(root._indentStep / 2) + 0.5;
                    var rowMid = Math.round(height / 2) + 0.5;
                    var contentLeft = root._indentStep + Kirigami.Units.smallSpacing;
                    // Vertical.
                    ctx.beginPath();
                    ctx.moveTo(x, 0);
                    ctx.lineTo(x, entryDelegate.isLastRow ? rowMid : height);
                    ctx.stroke();
                    // L-stub to the bullet.
                    ctx.beginPath();
                    ctx.moveTo(x, rowMid);
                    ctx.lineTo(contentLeft, rowMid);
                    ctx.stroke();
                }

                Connections {
                    function onIsLastRowChanged() {
                        treeCanvas.requestPaint();
                    }

                    target: entryDelegate
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
                anchors.leftMargin: root._indentStep + Kirigami.Units.smallSpacing
                anchors.right: parent.right
                anchors.rightMargin: Kirigami.Units.largeSpacing
                anchors.verticalCenter: parent.verticalCenter
                spacing: Kirigami.Units.largeSpacing

                // Bullet — accent, same as the rule preview's leaf bullets.
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: Math.round(Kirigami.Units.gridUnit / 2.5)
                    implicitHeight: implicitWidth
                    radius: width / 2
                    color: Kirigami.Theme.highlightColor
                }

                // Subject label — the same K=13gu base column the rule preview
                // uses, minus one indentStep, so this tree's subject column
                // lines up with the rule tree's field column.
                Label {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.minimumWidth: Math.max(Kirigami.Units.gridUnit * 4, Kirigami.Units.gridUnit * 13 - root._indentStep)
                    text: entryDelegate.modelData.label
                    elide: Text.ElideRight
                    font.bold: true
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
                            // First caption keeps the 8-gridUnit floor so its
                            // pill lands in the same column as the rule
                            // preview's value pills; later ones flow at their
                            // text width so a short caption doesn't pad out
                            // into dead space.
                            Layout.minimumWidth: pairRow.index === 0 ? Kirigami.Units.gridUnit * 8 : 0
                            text: pairRow.modelData.caption
                            // One binding: a font.<sub> sibling next to a whole-group `font:` is an
                            // illegal duplicate binding that fails the whole document. FontUtils
                            // passes only the size dimension the theme font actually carries.
                            font: FontUtils.withProps(Kirigami.Theme.smallFont, {
                                capitalization: Font.AllUppercase
                            })
                            opacity: 0.55
                        }

                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth: pillContent.implicitWidth + Kirigami.Units.largeSpacing * 2
                            implicitHeight: pillContent.implicitHeight + Kirigami.Units.smallSpacing
                            radius: Kirigami.Units.smallSpacing
                            Kirigami.Theme.colorSet: Kirigami.Theme.View
                            Kirigami.Theme.inherit: false
                            color: Kirigami.Theme.alternateBackgroundColor
                            border.width: 1
                            border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)

                            RowLayout {
                                id: pillContent

                                anchors.centerIn: parent
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
                                    text: pairRow.modelData.value
                                    color: pairRow.modelData.emphasis !== undefined ? pairRow.modelData.emphasis : Kirigami.Theme.textColor
                                    font.family: Kirigami.Theme.smallFont.family
                                }
                            }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                }
            }
        }
    }
}
