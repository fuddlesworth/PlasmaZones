// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Small form dialog for renaming and reparenting a profile.
 *
 * Reused across two flows via `mode`:
 *   "rename"  — name + description (no parent picker) → accepted(name, desc, "")
 *   "parent"  — parent picker only → accepted("", "", parentId)
 *
 * Creating is NOT here: the Profiles page takes a new profile through an inline
 * form on its "Save current settings" card, the way the sets pages do.
 *
 * The host wires `profiles` (the availableProfiles() rows) so the parent combo
 * can list the inheritance candidates, and `excludeId` to drop the profile
 * being edited from that list (the store still guards deeper cycles).
 */
Kirigami.Dialog {
    id: root

    property string mode: "rename"
    property var profiles: []
    property string excludeId: ""
    property alias nameText: nameField.text
    property alias descriptionText: descField.text
    property string parentId: ""

    signal profileAccepted(string name, string description, string parentId)

    readonly property bool _showName: mode === "rename"
    readonly property bool _showParent: mode === "parent"

    // "Defaults" (empty id) first, then every profile except the excluded one.
    readonly property var _parentModel: {
        const rows = [
            {
                "id": "",
                "name": i18n("Defaults")
            }
        ];
        for (let i = 0; i < profiles.length; ++i) {
            if (profiles[i].id !== root.excludeId)
                rows.push(profiles[i]);
        }
        return rows;
    }

    title: mode === "parent" ? i18n("Set Parent Profile") : i18n("Rename Profile")
    standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
    // Ok is meaningless for create/rename without a name.
    readonly property bool _acceptable: !root._showName || nameField.text.trim().length > 0

    onOpened: {
        // Installed on open, not at construction, matching the sets pages'
        // edit dialog: Ok stays disabled while the name is empty so a blank
        // rename cannot silently close as a no-op (Enter is gated the same
        // way in the fields' onAccepted).
        standardButton(Kirigami.Dialog.Ok).enabled = Qt.binding(function () {
            return root._acceptable;
        });
        if (root._showName)
            nameField.forceActiveFocus();
        else
            parentCombo.forceActiveFocus();
    }

    onAccepted: {
        if (!root._acceptable)
            return;

        const chosenParent = root._showParent ? (parentCombo.currentValue !== undefined ? parentCombo.currentValue : "") : "";
        root.profileAccepted(nameField.text.trim(), descField.text.trim(), chosenParent);
    }

    ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        TextField {
            id: nameField

            visible: root._showName
            Layout.fillWidth: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 18
            placeholderText: i18n("Profile name…")
            Accessible.name: i18n("Profile name")
            onAccepted: if (root._acceptable)
                root.accept()
        }

        TextField {
            id: descField

            visible: root._showName
            Layout.fillWidth: true
            placeholderText: i18n("Description (optional)…")
            Accessible.name: i18n("Profile description")
            onAccepted: if (root._acceptable)
                root.accept()
        }

        Label {
            visible: root._showParent
            text: i18n("Inherit from")
            color: Kirigami.Theme.disabledTextColor
        }

        ProfileComboBox {
            id: parentCombo

            visible: root._showParent
            Layout.fillWidth: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 18
            model: root._parentModel
            Accessible.name: i18n("Parent profile")
            currentIndex: {
                for (let i = 0; i < root._parentModel.length; ++i) {
                    if (root._parentModel[i].id === root.parentId)
                        return i;
                }
                return 0;
            }
        }

        Label {
            visible: root._showParent
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            text: i18n("Only settings that differ from the parent are stored in this profile.")
            color: Kirigami.Theme.disabledTextColor
            font: Kirigami.Theme.smallFont
            wrapMode: Text.WordWrap
        }
    }
}
