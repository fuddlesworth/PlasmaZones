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
 * is meaningful only as UI order today; the per-entry position field stays
 * -1 (before the trailing empty) from this editor.
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

    function _loadEntries() {
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
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    text: i18n("A named workspace is created at login, keeps its place while empty, and can be pinned to a monitor. Shortcuts jump to it or send the active window there.")
                }

                RowLayout {
                    Layout.fillWidth: true
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
                        siblingNames: {
                            var names = root._names();
                            names.splice(parent.rowIndex, 1);
                            return names;
                        }
                        onFieldEdited: function (index, field, value) {
                            var arr = root._entries.slice();
                            if (index < 0 || index >= arr.length)
                                return;

                            arr[index][field] = value;
                            root._entries = arr;
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
