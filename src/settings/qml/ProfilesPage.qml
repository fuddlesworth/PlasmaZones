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
 *                                      //   parentId, parentName, isRoot, active
 *   QString createProfile(name, description, parentId)
 *   bool    renameProfile(id, newName, description)
 *   QString duplicateProfile(id)
 *   bool    setParent(id, parentId)
 *   bool    removeProfile(id)
 *   bool    activateProfile(id)
 *   bool    exportProfile(id, localPath)
 *   QString importProfile(pathOrUrl)
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

    function _reload() {
        root.profilesList = root.bridge ? root.bridge.availableProfiles() : [];
    }

    Connections {
        function onProfilesChanged() {
            root._reload();
        }

        function onToastRequested(text) {
            if (typeof window !== "undefined" && window && window.showToast)
                window.showToast(text);
        }

        target: root.bridge
    }

    // A settings edit doesn't fire profilesChanged, but it can flip the active
    // profile's `modified` state — re-read the rows so the badge stays honest.
    Connections {
        function onSettingsChanged() {
            root._reload();
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
            text: i18n("A profile captures your current settings and rules. Only what differs from its parent profile (or the defaults) is stored. Activating a profile stages its settings — save to apply, or discard to revert. Per-monitor and other hardware-specific settings are not included, so a profile stays portable between machines.")
        }

        // ── Save current settings as a new profile ──
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Save current settings")
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

                Button {
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.alignment: Qt.AlignLeft
                    text: i18n("New profile…")
                    icon.name: "list-add"
                    Accessible.name: i18n("Create a new profile from the current settings")
                    onClicked: {
                        profileDialog.mode = "create";
                        profileDialog.targetId = "";
                        profileDialog.nameText = "";
                        profileDialog.descriptionText = "";
                        profileDialog.parentId = "";
                        profileDialog.open();
                    }
                }
            }
        }

        // ── Import a profile file ──
        ImportDropCard {
            id: importCard

            headerText: i18n("Import a profile")
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
                        Layout.leftMargin: Kirigami.Units.smallSpacing
                        Layout.rightMargin: Kirigami.Units.smallSpacing

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

    // ── Create / rename / set-parent form ──
    NewProfileDialog {
        id: profileDialog

        property string targetId: ""

        profiles: root.profilesList
        onProfileAccepted: (name, description, parentId) => {
            if (!root.bridge)
                return;

            if (profileDialog.mode === "create")
                root.bridge.createProfile(name, description, parentId);
            else if (profileDialog.mode === "rename")
                root.bridge.renameProfile(profileDialog.targetId, name, description);
            else if (profileDialog.mode === "parent")
                root.bridge.setParent(profileDialog.targetId, parentId);

            // Reset the per-open excludeId so a later create/rename sees the full list.
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
