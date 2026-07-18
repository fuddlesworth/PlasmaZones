// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief One profile row in the Profiles page tree list.
 *
 * Collapsed: depth indentation · identicon · name + parent subtitle · status
 * badge (Active / Modified) · a trailing strip of visible action buttons
 * (activate, update-when-modified, rename, duplicate, set parent, export,
 * delete). Every action stays on the row — no overflow menu.
 *
 * Expanded (the rule row's pattern): a read-only diff of what this profile
 * overrides relative to its parent, split into SETTINGS and RULES sections
 * under the shared SectionHeaderPill capsules.
 */
ExpandableRowDelegate {
    id: row

    /// One row map from ProfileStore::availableProfiles().
    required property var modelData

    /// The ProfileStore, for the on-demand diff the expansion shows. Null
    /// disables expansion entirely (the shell keeps the row collapsed).
    property var bridge: null

    readonly property string profileId: modelData.id
    readonly property string profileName: modelData.name
    readonly property string profileDescription: modelData.description
    readonly property int depth: modelData.depth
    readonly property bool isRoot: modelData.isRoot
    readonly property string parentName: modelData.parentName
    readonly property bool isActive: modelData.active
    readonly property bool isModified: modelData.modified

    signal activateRequested
    signal updateRequested
    signal renameRequested
    signal duplicateRequested
    signal setParentRequested
    signal exportRequested
    signal deleteRequested

    // The header fits on one row; the body is the read-only diff below.
    expansionContent: row.bridge ? diffComponent : null

    // Depth indent: a leading guide so nesting reads at a glance.
    Item {
        Layout.preferredWidth: row.depth * Kirigami.Units.gridUnit * 1.5
        Layout.fillHeight: true
        visible: row.depth > 0

        Kirigami.Separator {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            opacity: 0.4
        }
    }

    // Identicon derived from the profile's resolved settings — two profiles
    // that cascade to the same values draw the same mark.
    ProfileSignature {
        signature: row.modelData.signature
        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: Kirigami.Units.iconSizes.medium
        Layout.preferredHeight: Kirigami.Units.iconSizes.medium

        HoverHandler {
            id: signatureHover
        }

        ToolTip.text: i18n("A visual fingerprint of everything this profile resolves to, including what it inherits.")
        ToolTip.visible: signatureHover.hovered
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        spacing: 0

        Label {
            Layout.fillWidth: true
            text: row.profileName
            elide: Text.ElideRight
            font.bold: row.isActive
        }

        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: {
                const base = row.isRoot ? i18n("Based on defaults") : i18n("Inherits from “%1”", row.parentName);
                return row.profileDescription.length > 0 ? (base + " · " + row.profileDescription) : base;
            }
            elide: Text.ElideRight
            color: Kirigami.Theme.disabledTextColor
            font: Kirigami.Theme.smallFont
        }
    }

    // Status badge — Active (clean) or Modified (edited away from the profile).
    RowLayout {
        visible: row.isActive
        Layout.alignment: Qt.AlignVCenter
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            source: row.isModified ? "documentinfo" : "dialog-ok-apply"
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
            color: row.isModified ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.positiveTextColor
        }

        Label {
            text: row.isModified ? i18n("Modified") : i18n("Active")
            color: row.isModified ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.positiveTextColor
            font: Kirigami.Theme.smallFont
        }
    }

    // ── Trailing action strip — all visible, RuleRow-style ──
    ToolButton {
        icon.name: "dialog-ok-apply"
        Layout.alignment: Qt.AlignVCenter
        // Already exactly on this profile → nothing to do. A modified active
        // profile stays enabled so the user can re-apply (revert their edits).
        enabled: !row.isActive || row.isModified
        ToolTip.text: row.isActive && row.isModified ? i18n("Re-apply this profile (discards changes since)") : i18n("Activate this profile")
        ToolTip.visible: hovered
        Accessible.name: i18n("Activate profile %1", row.profileName)
        onClicked: row.activateRequested()
    }

    ToolButton {
        icon.name: "document-save"
        Layout.alignment: Qt.AlignVCenter
        // Shown but disabled off the modified-active state, so the strip keeps
        // its column alignment across rows (the RuleRow managed-delete idiom).
        enabled: row.isActive && row.isModified
        ToolTip.text: i18n("Update this profile from the current settings")
        ToolTip.visible: hovered
        Accessible.name: i18n("Update profile %1 from current settings", row.profileName)
        onClicked: row.updateRequested()
    }

    ToolButton {
        icon.name: "edit-rename"
        Layout.alignment: Qt.AlignVCenter
        ToolTip.text: i18n("Rename")
        ToolTip.visible: hovered
        Accessible.name: i18n("Rename profile %1", row.profileName)
        onClicked: row.renameRequested()
    }

    ToolButton {
        icon.name: "edit-copy"
        Layout.alignment: Qt.AlignVCenter
        ToolTip.text: i18n("Duplicate")
        ToolTip.visible: hovered
        Accessible.name: i18n("Duplicate profile %1", row.profileName)
        onClicked: row.duplicateRequested()
    }

    ToolButton {
        icon.name: "document-import"
        Layout.alignment: Qt.AlignVCenter
        ToolTip.text: i18n("Set parent")
        ToolTip.visible: hovered
        Accessible.name: i18n("Set the parent of profile %1", row.profileName)
        onClicked: row.setParentRequested()
    }

    ToolButton {
        icon.name: "document-export"
        Layout.alignment: Qt.AlignVCenter
        ToolTip.text: i18n("Export")
        ToolTip.visible: hovered
        Accessible.name: i18n("Export profile %1", row.profileName)
        onClicked: row.exportRequested()
    }

    ToolButton {
        icon.name: "edit-delete"
        Layout.alignment: Qt.AlignVCenter
        ToolTip.text: i18n("Delete")
        ToolTip.visible: hovered
        Accessible.name: i18n("Delete profile %1", row.profileName)
        onClicked: row.deleteRequested()
    }

    // ── Expanded body: what this profile overrides, before → after ──
    Component {
        id: diffComponent

        ColumnLayout {
            id: diffColumn

            readonly property var configRows: row.bridge ? row.bridge.configChanges(row.profileId) : []
            readonly property var ruleRows: row.bridge ? row.bridge.ruleChanges(row.profileId) : []

            spacing: Kirigami.Units.smallSpacing

            /// Raw values arrive untranslated so the wording is decided here.
            function formatValue(value) {
                if (value === undefined || value === null)
                    return i18nc("a setting with no value", "unset");
                if (typeof value === "boolean")
                    return value ? i18nc("a boolean setting that is on", "On") : i18nc("a boolean setting that is off", "Off");
                if (typeof value === "string" && value.length === 0)
                    return i18nc("an empty text setting", "empty");
                return String(value);
            }

            Label {
                Layout.fillWidth: true
                visible: diffColumn.configRows.length === 0 && diffColumn.ruleRows.length === 0
                text: row.isRoot ? i18n("Nothing overridden — this profile matches the defaults.") : i18n("Nothing overridden — this profile matches “%1”.", row.parentName)
                color: Kirigami.Theme.disabledTextColor
                wrapMode: Text.WordWrap
            }

            SectionHeaderPill {
                Layout.alignment: Qt.AlignLeft
                visible: diffColumn.configRows.length > 0
                text: i18nc("@title diff section listing changed settings", "Settings")
            }

            Repeater {
                model: diffColumn.configRows

                delegate: RowLayout {
                    id: changeRow

                    required property var modelData

                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        Layout.fillWidth: true
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                        text: changeRow.modelData.group.length > 0 ? i18nc("settings breadcrumb followed by the key", "%1 › %2", changeRow.modelData.group, changeRow.modelData.key) : changeRow.modelData.key
                        elide: Text.ElideRight
                        font: Kirigami.Theme.smallFont
                    }

                    Label {
                        text: diffColumn.formatValue(changeRow.modelData.before)
                        elide: Text.ElideRight
                        Layout.maximumWidth: Kirigami.Units.gridUnit * 8
                        color: Kirigami.Theme.disabledTextColor
                        font: Kirigami.Theme.smallFont
                    }

                    Kirigami.Icon {
                        source: "arrow-right"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        opacity: 0.6
                    }

                    Label {
                        text: diffColumn.formatValue(changeRow.modelData.after)
                        elide: Text.ElideRight
                        Layout.maximumWidth: Kirigami.Units.gridUnit * 8
                        font.bold: true
                    }
                }
            }

            SectionHeaderPill {
                Layout.alignment: Qt.AlignLeft
                Layout.topMargin: Kirigami.Units.smallSpacing
                visible: diffColumn.ruleRows.length > 0
                text: i18nc("@title diff section listing changed rules", "Rules")
            }

            Repeater {
                model: diffColumn.ruleRows

                delegate: RowLayout {
                    id: ruleChangeRow

                    required property var modelData

                    readonly property string changeLabel: {
                        if (ruleChangeRow.modelData.change === "added")
                            return i18nc("a rule this profile adds", "Added");
                        if (ruleChangeRow.modelData.change === "removed")
                            return i18nc("a rule this profile drops", "Removed");
                        return i18nc("a rule this profile alters", "Changed");
                    }
                    readonly property color changeColor: {
                        if (ruleChangeRow.modelData.change === "added")
                            return Kirigami.Theme.positiveTextColor;
                        if (ruleChangeRow.modelData.change === "removed")
                            return Kirigami.Theme.negativeTextColor;
                        return Kirigami.Theme.neutralTextColor;
                    }

                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: ruleChangeRow.changeLabel
                        color: ruleChangeRow.changeColor
                        font: Kirigami.Theme.smallFont
                    }

                    Label {
                        Layout.fillWidth: true
                        text: ruleChangeRow.modelData.name
                        elide: Text.ElideRight
                        font: Kirigami.Theme.smallFont
                    }
                }
            }
        }
    }
}
