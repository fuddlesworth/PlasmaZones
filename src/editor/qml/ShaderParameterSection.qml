// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * Collapsible section for shader parameters.
 *
 * Displays a clickable header with expand/collapse chevron and a parameter count badge.
 * Content is shown/hidden based on the expanded state.
 *
 * For accordion behavior, bind `expanded` to a shared index and handle `toggled` signal:
 *   ShaderParameterSection {
 *       title: "Group Name"
 *       groupParams: myParameterArray
 *       expanded: sharedExpandedIndex === myIndex
 *       onToggled: sharedExpandedIndex = (expanded ? -1 : myIndex)
 *   }
 */
ColumnLayout {
    id: root

    required property string title
    property var groupParams: [] // Parameter array for this group (explicit, not closure-based)
    property int paramCount: groupParams ? groupParams.length : 0
    property bool expanded: true
    property alias contentComponent: contentLoader.sourceComponent
    // Dialog reference for lock state access
    property var dialogRoot: null

    // Signal emitted when user clicks header (for accordion behavior)
    signal toggled()

    Layout.fillWidth: true
    spacing: 0

    // Section header - clickable to expand/collapse
    QQC.ItemDelegate {
        id: headerDelegate

        Layout.fillWidth: true
        Layout.preferredHeight: Kirigami.Units.gridUnit * 2.5
        Accessible.name: root.title
        Accessible.role: Accessible.Button
        Accessible.description: i18nc("@info:tooltip", "%1 parameters. Click to %2.", root.paramCount, root.expanded ? i18nc("@action", "collapse") : i18nc("@action", "expand"))
        onClicked: root.toggled()

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: "arrow-right"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
                color: Kirigami.Theme.textColor
                // Smooth rotation animation: 0° = collapsed, 90° = expanded
                rotation: root.expanded ? 90 : 0

                Behavior on rotation {
                    NumberAnimation {
                        duration: Kirigami.Units.longDuration
                        easing.type: Easing.OutCubic
                    }

                }

            }

            QQC.Label {
                text: root.title
                font.weight: Font.Medium
                Layout.fillWidth: true
            }

            // Group lock toggle
            QQC.ToolButton {
                readonly property var _lockedRef: root.dialogRoot ? root.dialogRoot.lockedParams : null
                readonly property bool allLocked: {
                    void (_lockedRef);
                    if (!root.dialogRoot || !root.groupParams || root.groupParams.length === 0)
                        return false;

                    for (var i = 0; i < root.groupParams.length; i++) {
                        var p = root.groupParams[i];
                        if (p && p.id !== undefined && !root.dialogRoot.isParamLocked(p.id))
                            return false;

                    }
                    return true;
                }

                icon.name: allLocked ? "object-locked" : "object-unlocked"
                icon.width: Kirigami.Units.iconSizes.small
                icon.height: Kirigami.Units.iconSizes.small
                opacity: allLocked ? 1 : 0.4
                display: QQC.ToolButton.IconOnly
                QQC.ToolTip.text: allLocked ? i18nc("@info:tooltip", "Unlock all in %1", root.title) : i18nc("@info:tooltip", "Lock all in %1", root.title)
                QQC.ToolTip.visible: hovered
                QQC.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: {
                    if (!root.dialogRoot || !root.groupParams)
                        return ;

                    var lock = !allLocked;
                    // Batch update: copy once, modify, assign once (avoids N intermediate copies)
                    var copy = {
                    };
                    var current = root.dialogRoot.lockedParams;
                    for (var key in current) copy[key] = current[key]
                    for (var i = 0; i < root.groupParams.length; i++) {
                        var p = root.groupParams[i];
                        if (p && p.id !== undefined) {
                            if (lock)
                                copy[p.id] = true;
                            else
                                delete copy[p.id];
                        }
                    }
                    root.dialogRoot.lockedParams = copy;
                }
            }

            // Parameter count badge
            Rectangle {
                implicitWidth: countLabel.implicitWidth + Kirigami.Units.smallSpacing * 2
                implicitHeight: Kirigami.Units.gridUnit
                radius: height / 2
                color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.2)

                QQC.Label {
                    id: countLabel

                    anchors.centerIn: parent
                    text: root.paramCount
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.textColor
                }

            }

        }

        background: Rectangle {
            color: headerDelegate.hovered ? Kirigami.Theme.hoverColor : "transparent"
            radius: Kirigami.Units.smallSpacing

            // Subtle bottom border when expanded
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
                visible: root.expanded
                opacity: 0.5
            }

        }

    }

    // Content area with smooth height animation
    Item {
        id: contentContainer

        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing
        // Clip content during animation to hide overflow
        clip: true
        // Animate height between 0 and content's actual height
        implicitHeight: root.expanded ? contentLoader.implicitHeight : 0

        Loader {
            id: contentLoader

            width: parent.width
            // Keep active during collapse animation so content is visible while shrinking
            active: root.expanded || contentContainer.implicitHeight > 0
            // Ensure Kirigami theme context propagates to loaded content
            Kirigami.Theme.inherit: true
            // Fade content along with height animation
            opacity: root.expanded ? 1 : 0

            Behavior on opacity {
                NumberAnimation {
                    duration: Kirigami.Units.longDuration
                    easing.type: Easing.OutCubic
                }

            }

        }

        Behavior on implicitHeight {
            NumberAnimation {
                duration: Kirigami.Units.longDuration
                easing.type: Easing.OutCubic
            }

        }

    }

}
