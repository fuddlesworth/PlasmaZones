// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Dynamic workspaces — Named Workspaces leaf.
 *
 * Card order: the explainer, the add form (a collapsible SettingsCard,
 * initially collapsed once entries exist — the profiles-page shape), then the
 * reorderable declaration list (drag-to-reorder rows expanding into their
 * fields) with a placeholder for the empty state.
 *
 * The declarations are ONE whole-replace config composite
 * (Workspaces.Named/Entries), so the page keeps a staged deep copy and every
 * edit commits the whole array; the daemon reacts live (create / claim /
 * unpin / rebind the per-name shortcuts) with no restart. Order in the list
 * is UI order; each row's Position spin box edits the per-entry preferred
 * slice index (-1, shown as "Automatic", means before the trailing empty).
 */
SettingsFlickable {
    id: root

    // Staged copy of appSettings.workspacesNamedEntries.
    property var _entries: []

    readonly property var _screenOptions: {
        var options = [
            {
                "label": i18n("Any monitor"),
                "value": ""
            }
        ];
        var screens = settingsController.screens || [];
        for (var i = 0; i < screens.length; ++i)
            options.push({
                "label": screens[i].displayLabel || screens[i].name,
                "value": screens[i].name
            });
        return options;
    }

    function _normalizedStored() {
        var stored = appSettings.workspacesNamedEntries;
        var copy = [];
        for (var i = 0; i < stored.length; ++i)
            copy.push({
                "name": stored[i].name || "",
                "output": stored[i].output || "",
                "position": stored[i].position !== undefined ? stored[i].position : -1,
                "focusShortcut": stored[i].focusShortcut || "",
                "moveShortcut": stored[i].moveShortcut || ""
            });
        return copy;
    }

    function _loadEntries() {
        var copy = _normalizedStored();
        // Skip the reload when the store already matches the staged copy —
        // every commit from THIS page echoes back through the change signal,
        // and reassigning the array would rebuild all row delegates and
        // collapse whichever row the user is editing.
        if (JSON.stringify(copy) === JSON.stringify(_entries))
            return;
        _entries = copy;
    }

    function _commitEntries() {
        appSettings.workspacesNamedEntries = _entries;
    }

    function _names() {
        var names = [];
        for (var i = 0; i < _entries.length; ++i)
            names.push(_entries[i].name);
        return names;
    }

    Component.onCompleted: _loadEntries()

    Connections {
        function onWorkspacesNamedEntriesChanged() {
            root._loadEntries();
        }

        target: appSettings
    }

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Add named workspace")
            searchAnchor: "workspacesNamedAdd"
            collapsible: true
            initiallyCollapsed: root._entries.length > 0

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    text: i18n("A named workspace is created at login, keeps its place while empty, and can be pinned to a monitor. Shortcuts jump to it or send the active window there.")
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.largeSpacing

                    TextField {
                        id: addNameField

                        Layout.fillWidth: true
                        placeholderText: i18n("Workspace name")
                        Accessible.name: i18n("New workspace name")
                    }

                    WideComboBox {
                        id: addOutputCombo

                        textRole: "label"
                        valueRole: "value"
                        Accessible.name: i18n("Pinned monitor")
                        model: root._screenOptions
                        storedValue: ""
                    }
                }

                Button {
                    Layout.alignment: Qt.AlignRight
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    Layout.bottomMargin: Kirigami.Units.smallSpacing
                    text: i18n("Add")
                    icon.name: "list-add"
                    Accessible.name: i18n("Add named workspace")
                    enabled: {
                        var trimmed = addNameField.text.trim();
                        return trimmed.length > 0 && root._names().indexOf(trimmed) === -1;
                    }
                    onClicked: {
                        var arr = root._entries.slice();
                        arr.push({
                            "name": addNameField.text.trim(),
                            "output": addOutputCombo.currentValue || "",
                            "position": -1,
                            "focusShortcut": "",
                            "moveShortcut": ""
                        });
                        root._entries = arr;
                        root._commitEntries();
                        addNameField.clear();
                        addOutputCombo.currentIndex = 0;
                    }
                }
            }
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Named workspaces")
            headerTrailingText: root._entries.length > 0 ? String(root._entries.length) : ""
            searchAnchor: "workspacesNamedList"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Kirigami.PlaceholderMessage {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.gridUnit
                    visible: root._entries.length === 0
                    icon.name: "virtual-desktops"
                    text: i18n("No named workspaces")
                    explanation: i18n("Named workspaces you add appear here.")
                }

                ReorderableColumn {
                    id: entryList

                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.smallSpacing
                    Layout.rightMargin: Kirigami.Units.smallSpacing
                    Layout.bottomMargin: Kirigami.Units.smallSpacing
                    Layout.preferredHeight: totalHeight
                    visible: root._entries.length > 0
                    items: root._entries
                    anchorPrefix: "namedWorkspace:"
                    idOf: function (item) {
                        return item.name;
                    }
                    accessibleNameOf: function (item) {
                        return item.name || i18n("(unnamed)");
                    }
                    reorderableOf: function (item) {
                        return true;
                    }
                    onMoveRequested: function (fromIndex, toIndex) {
                        var arr = root._entries.slice();
                        if (fromIndex < 0 || fromIndex >= arr.length || toIndex < 0 || toIndex >= arr.length)
                            return;

                        arr.splice(toIndex, 0, arr.splice(fromIndex, 1)[0]);
                        root._entries = arr;
                        root._commitEntries();
                    }

                    rowDelegate: NamedWorkspaceRow {
                        entry: parent.rowModelData
                        entryIndex: parent.rowIndex
                        screenOptions: root._screenOptions
                        siblingNamesOf: function (index) {
                            var names = root._names();
                            names.splice(index, 1);
                            return names;
                        }
                        onFieldEdited: function (index, field, value) {
                            // In place, deliberately: reassigning _entries
                            // would rebuild every row delegate and collapse
                            // the row mid-edit. The row mirrors the header
                            // fields itself; structural ops (add / remove /
                            // reorder) still replace the array.
                            if (index < 0 || index >= root._entries.length)
                                return;

                            root._entries[index][field] = value;
                            root._commitEntries();
                        }
                        onRemoveRequested: function (index) {
                            var arr = root._entries.slice();
                            arr.splice(index, 1);
                            root._entries = arr;
                            root._commitEntries();
                        }
                    }
                }
            }
        }
    }
}
