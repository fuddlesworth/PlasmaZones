// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Settings profiles: save the current configuration as a named profile,
 *        switch between profiles, and import/export them.
 *
 * A profile stores only what differs from its parent (another profile, or the
 * defaults at the root). Activating a profile STAGES its settings into the Save
 * footer — the change takes effect when the user saves, and reverts on discard.
 *
 * Talks to `settingsController.profilesPage.bridge` (a PlasmaZones::ProfileStore):
 *   QVariantList availableProfiles()   // rows: id, name, description,
 *                                      //   parentId, parentName, isRoot, depth,
 *                                      //   active, modified, signature
 *   QString createProfile(name, description, parentId)
 *   bool    renameProfile(id, newName, description)
 *   QString duplicateProfile(id)
 *   bool    setParent(id, parentId)
 *   bool    removeProfile(id)
 *   bool    activateProfile(id)
 *   bool    exportProfile(id, localPath)
 *   QString importProfile(pathOrUrl)
 *   bool    revertConfigChange(id, changeRow)
 *   bool    revertRuleChange(id, ruleId)
 *   void    openProfilesDirectory()
 *   signal  profilesChanged()
 *   signal  toastRequested(text)
 */
SettingsFlickable {
    id: root

    readonly property var bridge: settingsController.profilesPage ? settingsController.profilesPage.bridge : null

    // Q_INVOKABLE results are not reactive across the QML boundary — refresh
    // manually on profilesChanged.
    property var profilesList: bridge ? bridge.availableProfiles() : []

    contentHeight: content.implicitHeight
    clip: true

    // "Defaults" (empty id) first, then every existing profile — the parent
    // candidates for a new profile.
    readonly property var parentOptions: {
        const rows = [
            {
                "id": "",
                "name": i18n("Defaults")
            }
        ];
        for (let i = 0; i < profilesList.length; ++i)
            rows.push(profilesList[i]);
        return rows;
    }

    /// Ids of the profiles whose rows are expanded. Held HERE because _reload()
    /// replaces the row model wholesale, destroying every delegate along with
    /// its expansion state; rows seed from this map on creation and report
    /// toggles back into it. Mutated in place — the object's identity is the
    /// point, not change notification.
    readonly property var _expandedIds: ({})

    function _reload() {
        root.profilesList = root.bridge ? root.bridge.availableProfiles() : [];
    }

    /// Commit the inline save form, then clear it for the next one (mirrors the
    /// sets pages' save flow).
    function _saveCurrent() {
        if (!root.bridge)
            return;

        const parentId = parentCombo.currentValue !== undefined ? parentCombo.currentValue : "";
        if (root.bridge.createProfile(nameField.text.trim(), descField.text.trim(), parentId).length > 0) {
            nameField.text = "";
            descField.text = "";
            parentCombo.currentIndex = 0;
        }
    }

    // Toasts are NOT forwarded here: the bridge's toastRequested is wired at
    // window scope in Main.qml, because the sidebar switcher can raise one
    // while any page is loaded.
    Connections {
        function onProfilesChanged() {
            root._reload();
        }

        target: root.bridge
    }

    // A settings edit doesn't fire profilesChanged, but it can flip the active
    // profile's `modified` state — re-read the rows so the badge stays honest.
    // Debounced: settingsChanged fires per property change (a slider drag emits
    // a burst), and each reload walks every profile file on disk.
    Timer {
        id: settingsEditRefresh

        interval: 250
        onTriggered: root._reload()
    }

    Connections {
        function onSettingsChanged() {
            settingsEditRefresh.restart();
        }

        target: appSettings
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            visible: true
            text: i18n("A profile captures your current settings and rules. Only what differs from its parent profile (or the defaults) is stored. Activating a profile stages its settings. Save to apply, or discard to revert. Per-monitor and other hardware-specific settings are not included, so a profile stays portable between machines.")
        }

        // ── Save current settings as a new profile ──
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Save current settings")
            searchAnchor: "saveCurrent"
            collapsible: true
            initiallyCollapsed: root.profilesList.length > 0

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    text: i18n("Save everything as it is now as a new profile. Pick a parent to store only the differences from it.")
                    color: Kirigami.Theme.disabledTextColor
                    wrapMode: Text.WordWrap
                }

                // Stacked, not side-by-side — mirrors the sets pages: a 50/50
                // row makes the description column unreadably narrow at the
                // typical settings width. Save sits bottom-right so focus order
                // matches typeflow (name → description → parent → save).
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.smallSpacing

                    TextField {
                        id: nameField

                        Layout.fillWidth: true
                        placeholderText: i18n("Profile name…")
                        Accessible.name: i18n("Profile name")
                        onAccepted: if (saveButton.enabled)
                            saveButton.clicked()
                    }

                    TextField {
                        id: descField

                        Layout.fillWidth: true
                        placeholderText: i18n("Description (optional)…")
                        Accessible.name: i18n("Profile description")
                        onAccepted: if (saveButton.enabled)
                            saveButton.clicked()
                    }

                    // Profiles-only addition to the sets form: what the new
                    // profile inherits from, which decides what counts as a
                    // difference worth storing.
                    Label {
                        text: i18n("Inherit from")
                        color: Kirigami.Theme.disabledTextColor
                    }

                    ProfileComboBox {
                        id: parentCombo

                        Layout.fillWidth: true
                        model: root.parentOptions
                        // No `currentIndex: 0` binding: the ComboBox default is
                        // already 0, and _saveCurrent() resets the selection
                        // imperatively after a save — a binding here would just
                        // be severed by that write.
                        Accessible.name: i18n("Parent profile")
                    }

                    Button {
                        id: saveButton

                        Layout.alignment: Qt.AlignRight
                        text: i18n("Save")
                        icon.name: "document-save"
                        Accessible.name: i18n("Save the current settings as a profile")
                        // A colliding name is not a conflict here the way it is
                        // for sets: profiles are keyed by uuid and the store
                        // suffixes a duplicate display name, so there is nothing
                        // to overwrite and nothing to confirm.
                        enabled: nameField.text.trim().length > 0
                        onClicked: root._saveCurrent()
                    }
                }
            }
        }

        // ── Import a profile file ──
        ImportDropCard {
            id: importCard

            headerText: i18n("Import a profile")
            searchAnchor: "importProfile"
            description: i18n("Add a profile exported from PlasmaZones.")
            idleText: i18nc("@info drop-zone idle label", "Drop a profile file here to import it")
            hoverText: i18nc("@info drop-zone hover label", "Release to import the profile")
            importFn: url => {
                if (!root.bridge)
                    return false;
                return root.bridge.importProfile(url).length > 0;
            }
            openImportDialogFn: () => importDialog.open()
            openFolderFn: root.bridge ? () => root.bridge.openProfilesDirectory() : null
            importButtonAccessibleName: i18n("Import a profile from a file")
            openFolderAccessibleName: i18n("Open the profiles folder")
            successTextFn: name => i18nc("@info profile import success", "Imported profile “%1”.", name)
            failureTextFn: name => i18nc("@info profile import failure", "Could not import “%1”. The file must be a profile exported from PlasmaZones.", name)
        }

        // ── Saved profiles ──
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Profiles")
            searchAnchor: "profilesList"
            headerTrailingText: i18np("%n profile", "%n profiles", root.profilesList.length)

            contentItem: ColumnLayout {
                spacing: 0

                Label {
                    visible: root.profilesList.length === 0
                    text: i18n("No profiles saved yet.")
                    color: Kirigami.Theme.disabledTextColor
                    font.italic: true
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.largeSpacing
                }

                Repeater {
                    model: root.profilesList

                    delegate: ProfileRow {
                        Layout.fillWidth: true
                        bridge: root.bridge
                        Layout.leftMargin: Kirigami.Units.smallSpacing
                        Layout.rightMargin: Kirigami.Units.smallSpacing
                        // Seed from the page-held map: _reload() rebuilds the
                        // model wholesale, so a row mid-expansion (a per-row
                        // revert fires profilesChanged) is destroyed and
                        // recreated — without this it snapped shut. The click
                        // toggle severs the binding, which is fine: creation is
                        // the only moment the seed matters.
                        expanded: root._expandedIds[modelData.id] === true
                        onExpandedChanged: root._expandedIds[modelData.id] = expanded

                        onActivateRequested: root._activate(modelData)
                        onUpdateRequested: root._update(modelData)
                        onRenameRequested: root._rename(modelData)
                        onDuplicateRequested: root._duplicate(modelData)
                        onSetParentRequested: root._setParent(modelData)
                        onExportRequested: root._export(modelData)
                        onDeleteRequested: root._delete(modelData)
                    }
                }
            }
        }
    }

    // ── Row action handlers (wired from each ProfileRow's signals) ──
    function _activate(m) {
        if (root.bridge)
            root.bridge.activateProfile(m.id);
    }
    function _update(m) {
        if (root.bridge)
            root.bridge.updateProfileFromCurrent(m.id);
    }
    function _duplicate(m) {
        if (root.bridge)
            root.bridge.duplicateProfile(m.id);
    }
    function _rename(m) {
        profileDialog.mode = "rename";
        profileDialog.targetId = m.id;
        profileDialog.excludeId = "";
        profileDialog.nameText = m.name;
        profileDialog.descriptionText = m.description;
        profileDialog.open();
    }
    function _setParent(m) {
        profileDialog.mode = "parent";
        profileDialog.targetId = m.id;
        profileDialog.excludeId = m.id;
        profileDialog.parentId = m.parentId;
        profileDialog.open();
    }
    function _export(m) {
        exportDialog.profileId = m.id;
        exportDialog.currentFile = "file:" + m.name.replace(/[^a-zA-Z0-9 _-]/g, "_") + ".json";
        exportDialog.open();
    }
    function _delete(m) {
        deleteConfirm.profileId = m.id;
        deleteConfirm.profileName = m.name;
        deleteConfirm.open();
    }

    // ── Rename / set-parent form. Creating is inline on the card above. ──
    NewProfileDialog {
        id: profileDialog

        property string targetId: ""

        profiles: root.profilesList
        onProfileAccepted: (name, description, parentId) => {
            if (!root.bridge)
                return;

            if (profileDialog.mode === "rename")
                root.bridge.renameProfile(profileDialog.targetId, name, description);
            else if (profileDialog.mode === "parent")
                root.bridge.setParent(profileDialog.targetId, parentId);

            // Reset the per-open excludeId so a later rename sees the full list.
            profileDialog.excludeId = "";
        }
    }

    // ── Delete confirmation ──
    Kirigami.PromptDialog {
        id: deleteConfirm

        property string profileId: ""
        property string profileName: ""

        title: i18n("Delete profile?")
        subtitle: i18n("“%1” will be permanently deleted. Any profiles inheriting from it will be re-based on its parent, keeping their own settings.", deleteConfirm.profileName)
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    if (root.bridge)
                        root.bridge.removeProfile(deleteConfirm.profileId);
                    deleteConfirm.close();
                }
            }
        ]
    }

    // ── Export / import file pickers ──
    FileDialog {
        id: exportDialog

        property string profileId: ""

        title: i18n("Export Profile")
        nameFilters: [i18n("PlasmaZones Profile (*.json)"), i18n("All files (*)")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        onAccepted: {
            if (root.bridge)
                root.bridge.exportProfile(exportDialog.profileId, settingsController.urlToLocalFile(selectedFile));
        }
    }

    FileDialog {
        id: importDialog

        title: i18n("Import Profile")
        nameFilters: [i18n("PlasmaZones Profile (*.json)"), i18n("All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            if (!root.bridge)
                return;

            const path = settingsController.urlToLocalFile(selectedFile);
            importCard.showResult(root.bridge.importProfile(path).length > 0, selectedFile);
        }
    }
}
