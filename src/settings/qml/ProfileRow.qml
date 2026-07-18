// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief One profile row in the Profiles page tree list.
 *
 * A single non-expanding row (the ShaderSetCard convention): depth indentation ·
 * icon · name + parent subtitle · status badge (Active / Modified) · a trailing
 * strip of visible action buttons (activate, update-when-modified, rename,
 * duplicate, set parent, export, delete). No overflow menu, no collapse.
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

    // Everything fits on one row — no expansion body.
    expandable: false

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
}
