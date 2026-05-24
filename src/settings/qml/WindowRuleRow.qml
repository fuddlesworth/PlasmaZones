// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief One rule row in the grouped WindowRulesPage list.
 *
 * Layout mirrors the SVG mockup: enabled dot · match summary · `→` · action
 * summary · edit / delete. Composite rules show condition / action-count
 * badges. The enabled dot is a toggle; edit / delete are buttons.
 */
ItemDelegate {
    id: row

    /// Per-rule fields from WindowRuleModel's roles.
    required property string ruleId
    required property string ruleName
    /// The rule's own enabled flag — distinct from `ItemDelegate.enabled`,
    /// which gates the delegate's interactivity.
    required property bool ruleEnabled
    required property string matchSummary
    required property string actionSummary
    required property int conditionCount
    required property int actionCount
    required property bool isComposite

    signal editRequested()
    signal deleteRequested()
    // Parameter named `ruleEnabled` for symmetry with the `ruleEnabled`
    // property — using the bare name `enabled` would shadow the row's own
    // `enabled` in any handler that relies on implicit-argument scope.
    signal toggleRequested(bool ruleEnabled)

    // Instantiated inside a Repeater/ColumnLayout — `Layout.fillWidth: true`
    // (set by the delegate's parent) drives the width; there is no enclosing
    // ListView, so no `ListView.view` branch.
    hoverEnabled: true

    contentItem: RowLayout {
        spacing: Kirigami.Units.largeSpacing

        // Enabled dot — toggles the rule.
        AbstractButton {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: Kirigami.Units.iconSizes.small
            implicitHeight: Kirigami.Units.iconSizes.small
            // Expose as a CheckBox to screen readers so the enable/disable
            // state is announced alongside the action label.
            Accessible.role: Accessible.CheckBox
            Accessible.checked: row.ruleEnabled
            Accessible.name: row.ruleEnabled ? i18n("Disable rule %1", row.ruleName) : i18n("Enable rule %1", row.ruleName)
            onClicked: row.toggleRequested(!row.ruleEnabled)

            contentItem: Rectangle {
                radius: width / 2
                color: row.ruleEnabled ? Kirigami.Theme.highlightColor : "transparent"
                border.width: row.ruleEnabled ? 0 : 2
                border.color: Kirigami.Theme.disabledTextColor
            }

        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Label {
                Layout.fillWidth: true
                text: row.ruleName.length > 0 ? row.ruleName : row.matchSummary
                font.bold: true
                opacity: row.ruleEnabled ? 1 : 0.5
                elide: Text.ElideRight
            }

            Label {
                Layout.fillWidth: true
                text: row.matchSummary
                opacity: row.ruleEnabled ? 0.7 : 0.4
                elide: Text.ElideRight
                visible: row.ruleName.length > 0
            }

        }

        // Condition-count badge for composite rules.
        Rectangle {
            visible: row.isComposite
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: condLabel.implicitWidth + Kirigami.Units.largeSpacing
            implicitHeight: condLabel.implicitHeight + Kirigami.Units.smallSpacing
            radius: Kirigami.Units.smallSpacing
            color: Kirigami.Theme.alternateBackgroundColor

            Label {
                id: condLabel

                anchors.centerIn: parent
                text: i18np("%n condition", "%n conditions", row.conditionCount)
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.7
            }

        }

        // Action-count badge — shown for rules carrying more than one action.
        Rectangle {
            visible: row.actionCount > 1
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: actionLabel.implicitWidth + Kirigami.Units.largeSpacing
            implicitHeight: actionLabel.implicitHeight + Kirigami.Units.smallSpacing
            radius: Kirigami.Units.smallSpacing
            color: Kirigami.Theme.alternateBackgroundColor

            Label {
                id: actionLabel

                anchors.centerIn: parent
                text: i18np("%n action", "%n actions", row.actionCount)
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.7
            }

        }

        Kirigami.Icon {
            source: "arrow-right"
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
            Layout.alignment: Qt.AlignVCenter
            opacity: 0.6
        }

        // Action summary — wraps to two lines before eliding so multi-action
        // labels like "Engine: Snapping · Snapping: Portrait 2-Row" don't get
        // cut at "Portrait..." (a constant-width single-line elide makes the
        // user open the editor just to see which layout the rule picks).
        Label {
            Layout.alignment: Qt.AlignVCenter
            Layout.maximumWidth: Kirigami.Units.gridUnit * 22
            Layout.preferredWidth: Kirigami.Units.gridUnit * 18
            elide: Text.ElideRight
            font.bold: true
            maximumLineCount: 2
            opacity: row.ruleEnabled ? 1 : 0.5
            text: row.actionSummary
            wrapMode: Text.WordWrap
        }

        ToolButton {
            icon.name: "document-edit"
            Layout.alignment: Qt.AlignVCenter
            ToolTip.text: i18n("Edit rule")
            ToolTip.visible: hovered
            Accessible.name: i18n("Edit rule %1", row.ruleName)
            onClicked: row.editRequested()
        }

        ToolButton {
            icon.name: "edit-delete"
            Layout.alignment: Qt.AlignVCenter
            ToolTip.text: i18n("Delete rule")
            ToolTip.visible: hovered
            Accessible.name: i18n("Delete rule %1", row.ruleName)
            onClicked: row.deleteRequested()
        }

    }

}
