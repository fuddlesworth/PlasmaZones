// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Visual 3x3 position picker for zone selector placement
 *
 * Grid layout (cell indices):
 *   0=TopLeft,    1=Top,    2=TopRight
 *   3=Left,       4=Center, 5=Right
 *   6=BottomLeft, 7=Bottom, 8=BottomRight
 *
 * All 9 positions are valid, including Center (4).
 *
 * The caller owns `position`. A click only emits positionSelected, so bind
 * `position` to the state the handler writes and the selection follows.
 * Writing it from here would sever that binding on the first click, stranding
 * every later change the caller pushes back, and would show the raw click where
 * the backend may have clamped or refused it.
 */
Item {
    id: root

    // Selected cell index (0-8). Bind this; see the note above.
    property int position: 1
    // `enabled` is the built-in Item property (declaring our own shadowed the base
    // member — the "overrides a member of the base object" warning). The internal
    // `root.enabled` reads + the callers that set it work against the base property.

    // Position labels for tooltips
    readonly property var positionLabels: [i18n("Top-Left"), i18n("Top"), i18n("Top-Right"), i18n("Left"), i18n("Center"), i18n("Right"), i18n("Bottom-Left"), i18n("Bottom"), i18n("Bottom-Right")]

    signal positionSelected(int newPosition)

    implicitWidth: Kirigami.Units.gridUnit * 10
    implicitHeight: Kirigami.Units.gridUnit * 7

    // The miniature uses Kirigami.Units for all structural sizing/spacing/margins.
    // The small integer literals below are intentional fixed drawing dimensions
    // of this custom-painted preview: bar thicknesses, insets, corner radii, the
    // 1-2px borders, and the caps that stop a bar outgrowing its cell. They are
    // visual detail of the diagram itself rather than layout spacing, and are
    // deliberately not theme-scaled so the drawing keeps its proportions at
    // every gridUnit. Same rationale as ZoneSelectorSection's sample zones.
    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            id: screenFrame

            // Pin the View set so the frame's fill and border resolve against
            // the content-surface palette wherever the picker is hosted.
            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Kirigami.Theme.backgroundColor
            radius: Kirigami.Units.smallSpacing
            border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
            // Chrome border. border.width is in device-independent pixels;
            // the renderer scales it by the device pixel ratio itself, so a
            // plain integer gives a consistent hairline at every scale
            // factor (multiplying by devicePixelRatio here would
            // double-scale into a thicker, not crisper, border). This is
            // the repo-wide chrome-border convention. The drawing
            // dimensions below stay in logical units on purpose (see the
            // note above them).
            border.width: 1

            Rectangle {
                anchors.fill: parent
                anchors.margins: Kirigami.Units.smallSpacing
                color: Kirigami.Theme.alternateBackgroundColor
                radius: Kirigami.Units.smallSpacing / 2

                Grid {
                    id: positionGrid

                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    columns: 3
                    rows: 3
                    spacing: Kirigami.Units.smallSpacing

                    Repeater {
                        model: 9

                        Rectangle {
                            id: cell

                            required property int index
                            property bool isCenter: index === 4
                            property bool isSelected: index === root.position
                            property bool isHovered: cellMouse.containsMouse
                            // The cells are the control: one of nine, exclusive,
                            // named by the same labels the tooltips use. Without
                            // these the whole picker is a silent 3x3 of
                            // rectangles to a screen reader.
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: root.positionLabels[cell.index]
                            Accessible.checked: cell.isSelected
                            Accessible.focusable: true
                            // Keyboard path matching WizardTemplateCard: each
                            // cell is a Tab stop, Return/Enter/Space activates,
                            // and focus shows through the highlight border.
                            activeFocusOnTab: root.enabled
                            Keys.onReturnPressed: root.positionSelected(cell.index)
                            Keys.onEnterPressed: root.positionSelected(cell.index)
                            Keys.onSpacePressed: root.positionSelected(cell.index)
                            // Zone selector indicator bars, drawn per edge this
                            // cell sits on: a corner is on two, a side on one.
                            property bool isTopRow: cell.index <= 2
                            property bool isBottomRow: cell.index >= 6
                            property bool isLeftCol: cell.index % 3 === 0
                            property bool isRightCol: cell.index % 3 === 2

                            width: (positionGrid.width - positionGrid.spacing * 2) / 3
                            height: (positionGrid.height - positionGrid.spacing * 2) / 3
                            radius: 3
                            color: {
                                if (isSelected)
                                    return Kirigami.Theme.highlightColor;

                                if (isHovered && root.enabled)
                                    return Qt.alpha(Kirigami.Theme.hoverColor, 0.2);

                                return Kirigami.Theme.alternateBackgroundColor;
                            }
                            border.color: {
                                if (cell.activeFocus)
                                    return Kirigami.Theme.focusColor;

                                if (isSelected)
                                    return Kirigami.Theme.highlightColor;

                                if (isHovered && root.enabled)
                                    return Kirigami.Theme.hoverColor;

                                return Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast);
                            }
                            border.width: (cell.activeFocus || cell.isSelected) ? 2 : 1
                            opacity: root.enabled ? 1 : 0.5

                            // Horizontal bar (top or bottom edge)
                            Rectangle {
                                visible: cell.isSelected && !cell.isCenter && (cell.isTopRow || cell.isBottomRow)
                                color: Kirigami.Theme.highlightedTextColor
                                opacity: 0.95
                                radius: 2
                                width: Math.min(parent.width * 0.7, 24)
                                height: 4
                                x: {
                                    if (cell.isLeftCol)
                                        return 2;

                                    if (cell.isRightCol)
                                        return parent.width - width - 2;

                                    return (parent.width - width) / 2;
                                }
                                y: cell.isTopRow ? 2 : parent.height - height - 2
                            }

                            // Center indicator (small rectangle for center position)
                            Rectangle {
                                visible: cell.isSelected && cell.isCenter
                                color: Kirigami.Theme.highlightedTextColor
                                opacity: 0.95
                                radius: 2
                                width: Math.min(parent.width * 0.5, 16)
                                height: Math.min(parent.height * 0.4, 10)
                                anchors.centerIn: parent
                            }

                            // Vertical bar (left or right edge)
                            Rectangle {
                                visible: cell.isSelected && !cell.isCenter && (cell.isLeftCol || cell.isRightCol)
                                color: Kirigami.Theme.highlightedTextColor
                                opacity: 0.95
                                radius: 2
                                width: 4
                                height: Math.min(parent.height * 0.6, 16)
                                x: cell.isLeftCol ? 2 : parent.width - width - 2
                                y: {
                                    if (cell.isTopRow)
                                        return 2;

                                    if (cell.isBottomRow)
                                        return parent.height - height - 2;

                                    return (parent.height - height) / 2;
                                }
                            }

                            MouseArea {
                                id: cellMouse

                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: root.enabled
                                cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: {
                                    // Move active focus to the clicked cell so a
                                    // previously keyboard-focused cell doesn't keep
                                    // the focus ring and the key handlers.
                                    cell.forceActiveFocus();
                                    root.positionSelected(cell.index);
                                }
                                ToolTip.visible: containsMouse && root.enabled
                                ToolTip.delay: Kirigami.Units.toolTipDelay
                                ToolTip.text: root.positionLabels[cell.index]
                            }

                            Behavior on color {
                                PhosphorMotionAnimation {
                                    profile: "widget.hover"
                                    durationOverride: Kirigami.Units.shortDuration
                                }
                            }

                            Behavior on border.color {
                                PhosphorMotionAnimation {
                                    profile: "widget.hover"
                                    durationOverride: Kirigami.Units.shortDuration
                                }
                            }
                        }
                    }
                }
            }
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: root.positionLabels[root.position] || ""
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }
    }
}
