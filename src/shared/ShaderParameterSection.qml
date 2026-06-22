// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Collapsible accordion section for a group of shader parameters.
 *
 * Header shows the group title, a parameter-count badge, and an optional
 * group-level lock toggle. Bind `expanded` to a shared index and react to
 * `toggled()` for accordion behavior:
 *
 *   ShaderParameterSection {
 *       title: groupName
 *       groupParams: paramsForGroup
 *       expanded: sharedIndex === myIndex
 *       onToggled: sharedIndex = (expanded ? -1 : myIndex)
 *   }
 *
 * Lock-all state is computed from `lockedParams` (a `{ paramId: bool }`
 * map). The host owns the map; we emit `groupLockToggled(lock)` so the
 * host can write a batched update (one map mutation, not one per param).
 *
 * Required:
 *   - `title`: string
 *
 * Optional:
 *   - `groupParams`: var — parameter array for this group (default `[]`)
 *   - `expanded`: bool
 *   - `lockedParams`: var — `{ paramId: bool }`
 *   - `enableLocking`: bool — show the group-lock toggle
 *   - `contentComponent`: Component — body content (rows)
 *
 * Must be placed inside a `ColumnLayout` / `RowLayout` / Layout-aware
 * parent — the root is itself a `ColumnLayout`.
 */
ColumnLayout {
    id: root

    required property string title
    property var groupParams: []
    readonly property int paramCount: groupParams ? groupParams.length : 0
    property bool expanded: true
    property alias contentComponent: contentLoader.sourceComponent
    property var lockedParams: ({})
    property bool enableLocking: true

    signal toggled
    /// Emitted when the group-lock button is clicked. `lock` is the new
    /// state (true = lock all, false = unlock all). Host applies it as a
    /// single batched mutation across all `groupParams`.
    signal groupLockToggled(bool lock)

    Layout.fillWidth: true
    spacing: 0

    QQC.ItemDelegate {
        id: headerDelegate

        Layout.fillWidth: true
        Layout.preferredHeight: Kirigami.Units.gridUnit * 2.5
        Accessible.name: root.title
        Accessible.role: Accessible.Button
        // Use complete sentences in each branch — nesting `i18nc` for verb
        // substitution leaves translators with detached "collapse"/"expand"
        // tokens that can't be re-ordered, gendered, or pluralised. `i18ncp`
        // handles singular/plural for the count.
        Accessible.description: root.expanded ? i18ncp("@info:tooltip expanded section", "%1 parameter. Click to collapse.", "%1 parameters. Click to collapse.", root.paramCount) : i18ncp("@info:tooltip collapsed section", "%1 parameter. Click to expand.", "%1 parameters. Click to expand.", root.paramCount)
        onClicked: root.toggled()

        contentItem: Item {
            implicitWidth: paramHeaderRow.implicitWidth + Kirigami.Units.largeSpacing * 2
            implicitHeight: paramHeaderRow.implicitHeight

            // Inset via a margin inside contentItem (not the Control's padding,
            // which the org.kde.desktop ItemDelegate style overrides) so the
            // header content doesn't hug the edge.
            RowLayout {
                id: paramHeaderRow

                anchors.fill: parent
                anchors.leftMargin: Kirigami.Units.largeSpacing
                anchors.rightMargin: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: "arrow-right"
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                    color: Kirigami.Theme.textColor
                    rotation: root.expanded ? 90 : 0

                    Behavior on rotation {
                        PhosphorMotionAnimation {
                            // Direction-bound: profile reads root.expanded after the
                            // flip — true is the expand direction, false is collapse.
                            profile: root.expanded ? "widget.accordionExpand" : "widget.accordionCollapse"
                            durationOverride: Kirigami.Units.longDuration
                        }
                    }
                }

                QQC.Label {
                    text: root.title
                    font.weight: Font.Medium
                    Layout.fillWidth: true
                }

                QQC.ToolButton {
                    // Reads `root.lockedParams[p.id]` inside the loop register
                    // the parent map as a binding dependency directly — no
                    // separate proxy property is needed for reactivity.
                    readonly property bool allLocked: {
                        if (!root.groupParams || root.groupParams.length === 0 || !root.lockedParams)
                            return false;

                        for (var i = 0; i < root.groupParams.length; i++) {
                            var p = root.groupParams[i];
                            if (p && p.id !== undefined && root.lockedParams[p.id] !== true)
                                return false;
                        }
                        return true;
                    }

                    // Hide the lock button on empty groups too — there's
                    // nothing to lock there, and rendering the unlocked-icon
                    // button on an empty section is just visual noise.
                    visible: root.enableLocking && root.paramCount > 0
                    icon.name: allLocked ? "object-locked" : "object-unlocked"
                    icon.width: Kirigami.Units.iconSizes.small
                    icon.height: Kirigami.Units.iconSizes.small
                    opacity: allLocked ? 1 : 0.4
                    display: QQC.ToolButton.IconOnly
                    QQC.ToolTip.text: allLocked ? i18nc("@info:tooltip", "Unlock all in %1", root.title) : i18nc("@info:tooltip", "Lock all in %1", root.title)
                    QQC.ToolTip.visible: hovered
                    QQC.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: root.groupLockToggled(!allLocked)
                }

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
        }

        background: Rectangle {
            color: headerDelegate.hovered ? Kirigami.Theme.hoverColor : "transparent"
            radius: Kirigami.Units.smallSpacing

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                // Hairline divider: 1 device-independent px. Qt scales DIPs
                // to physical pixels itself; multiplying by devicePixelRatio
                // here would double-scale into a thicker, not crisper, line.
                height: 1
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
                visible: root.expanded
                opacity: 0.5
            }
        }
    }

    Item {
        id: contentContainer

        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing
        clip: true
        implicitHeight: root.expanded ? contentLoader.implicitHeight : 0

        Loader {
            id: contentLoader

            width: parent.width
            // Keep loaded during collapse animation so content stays visible
            // while shrinking. Track `height` (the animated value) rather than
            // `implicitHeight` (the steady-state target) so the loader doesn't
            // briefly deactivate when an in-flight animation crosses zero.
            active: root.expanded || contentContainer.height > 0
            Kirigami.Theme.inherit: true
            opacity: root.expanded ? 1 : 0

            Behavior on opacity {
                PhosphorMotionAnimation {
                    profile: root.expanded ? "widget.accordionExpand" : "widget.accordionCollapse"
                    durationOverride: Kirigami.Units.longDuration
                }
            }
        }

        Behavior on implicitHeight {
            PhosphorMotionAnimation {
                profile: root.expanded ? "widget.accordionExpand" : "widget.accordionCollapse"
                durationOverride: Kirigami.Units.longDuration
            }
        }
    }
}
