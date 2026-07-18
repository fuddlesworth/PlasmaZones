// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief One profile row in the Profiles page tree list.
 *
 * Collapsed header: depth indentation · icon · name + parent subtitle · status
 * badge (Active / Modified) · primary actions (Activate, Update-when-modified).
 * Expanded body: inheritance breadcrumb + management actions (rename, duplicate,
 * set parent, export, delete). Actions are visible buttons (the RuleRow /
 * ShaderSetCard convention), not an overflow menu.
 */
ExpandableRowDelegate {
    id: row

    /// One row map from ProfileStore::availableProfiles().
    required property var modelData

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

    expansionContent: expansionComponent

    // ── Collapsed header ──
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

    Kirigami.Icon {
        source: "bookmarks"
        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: Kirigami.Units.iconSizes.small
        Layout.preferredHeight: Kirigami.Units.iconSizes.small
        opacity: 0.7
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
            text: row.isRoot ? i18n("Based on defaults") : i18n("Inherits from “%1”", row.parentName)
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

    // Primary action: activate (or re-apply, discarding edits, when modified).
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

    // Update the active profile to capture the current (modified) settings.
    ToolButton {
        icon.name: "document-save"
        Layout.alignment: Qt.AlignVCenter
        visible: row.isActive && row.isModified
        ToolTip.text: i18n("Update this profile from the current settings")
        ToolTip.visible: hovered
        Accessible.name: i18n("Update profile %1 from current settings", row.profileName)
        onClicked: row.updateRequested()
    }

    ExpandChevron {
        expanded: row.expanded
    }

    // ── Expanded body: inheritance detail + management actions ──
    Component {
        id: expansionComponent

        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Label {
                Layout.fillWidth: true
                visible: row.profileDescription.length > 0
                text: row.profileDescription
                wrapMode: Text.WordWrap
                color: Kirigami.Theme.disabledTextColor
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Button {
                    flat: true
                    text: i18n("Rename")
                    icon.name: "edit-rename"
                    onClicked: row.renameRequested()
                }

                Button {
                    flat: true
                    text: i18n("Duplicate")
                    icon.name: "edit-copy"
                    onClicked: row.duplicateRequested()
                }

                Button {
                    flat: true
                    text: i18n("Set parent")
                    icon.name: "document-import"
                    onClicked: row.setParentRequested()
                }

                Button {
                    flat: true
                    text: i18n("Export")
                    icon.name: "document-export"
                    onClicked: row.exportRequested()
                }

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    flat: true
                    text: i18n("Delete")
                    icon.name: "edit-delete"
                    onClicked: row.deleteRequested()
                }
            }
        }
    }
}
