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

    // Staged copy of appSettings.workspacesNamedEntries. Each element carries
    // one extra field beyond the five stored ones, `uid`: a synthetic key that
    // stays put across renames.
    //
    // The list is keyed by uid rather than by name because `name` is mutable
    // from inside the row. ReorderableColumn keys its published-height cache
    // and its deep-link search anchors by `idOf`, and a rename would move a row
    // to a key with no cached height (the row visually overlaps its neighbour
    // until it republishes) while leaving the old anchor entry pointing at a
    // live delegate under a key nothing looks up any more.
    property var _entries: []
    // Monotonic uid source. Only ever increments, so a uid is never reused
    // within a session even after a removal.
    property int _uidSeq: 0

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

    /// Build the staged shape from the store, with a fresh uid per row.
    ///
    /// `uidBase` is where this call's uids start. _loadEntries builds a
    /// candidate first and keeps it only when it differs from the staged copy,
    /// so most calls are echoes of our own commit whose result is thrown away.
    /// Passing the base in rather than incrementing the page counter lets the
    /// caller advance `_uidSeq` only for the copy it actually adopts, so the
    /// counter tracks rows issued instead of running away with every echo.
    function _normalizedStored(uidBase) {
        var stored = appSettings.workspacesNamedEntries;
        var copy = [];
        for (var i = 0; i < stored.length; ++i)
            copy.push({
                "uid": "ws" + (uidBase + i),
                "name": stored[i].name || "",
                "output": stored[i].output || "",
                "position": stored[i].position !== undefined ? stored[i].position : -1,
                "focusShortcut": stored[i].focusShortcut || "",
                "moveShortcut": stored[i].moveShortcut || ""
            });
        return copy;
    }

    /// The staged array projected back onto the five stored fields — the shape
    /// the config composite takes, and the shape the reload compare uses. The
    /// uid is ours alone and never reaches the store (the schema's
    /// canonicalNamedEntries would drop it anyway, which is exactly why the
    /// compare has to strip it too or every reload would look like a change).
    function _wireEntries(entries) {
        var out = [];
        for (var i = 0; i < entries.length; ++i)
            out.push({
                "name": entries[i].name,
                "output": entries[i].output,
                "position": entries[i].position,
                "focusShortcut": entries[i].focusShortcut,
                "moveShortcut": entries[i].moveShortcut
            });
        return out;
    }

    function _loadEntries() {
        // Skip the reload when the store already matches the staged copy —
        // every commit from THIS page echoes back through the change signal,
        // and reassigning the array would rebuild all row delegates (issuing
        // fresh uids), collapsing whichever row the user is editing.
        var copy = _normalizedStored(root._uidSeq);
        if (JSON.stringify(_wireEntries(copy)) === JSON.stringify(_wireEntries(_entries)))
            return;
        root._uidSeq += copy.length;
        _entries = copy;
        _namesTick++;
    }

    function _commitEntries() {
        appSettings.workspacesNamedEntries = _wireEntries(_entries);
    }

    // Renaming a workspace carries through to the quick-shortcut slot targets
    // that point at the old name.
    //
    // The cascade itself runs in the controller
    // (SettingsController::renameWorkspaceSlotTargets), not here. The slot
    // Target keys belong to the workspaces-shortcuts page's manifest, and a
    // manifest key has exactly one owner, so writing them straight from this
    // page left an edit only the OTHER page's Discard could revert. The
    // controller records which slots it rewrote and this page's own Discard
    // arm reverts them. The rationale for cascading at all, and for leaving
    // rules out of it, is written up at that definition.

    // Bumped whenever any entry's name changes. A rename is applied IN PLACE
    // (see onFieldEdited), so `_entries` itself does not change identity and
    // nothing reading it through a binding would re-evaluate — the Add
    // button's duplicate check being the one that matters.
    property int _namesTick: 0

    /// Every declared name, in list order. Names are compared exactly, and
    /// only trimmed: that is the daemon's own identity rule. The reconciler
    /// matches declarations by `decl.name` with QString equality
    /// (WorkspaceReconciler.cpp, applyNamedWorkspaces) and the config schema
    /// trims a name before storing it (canonicalNamedEntries in
    /// settingsschema_workspaces.cpp), so "Work" and "work" are two distinct
    /// workspaces and "Work " is the same one as "Work".
    readonly property var _names: {
        void root._namesTick;
        var names = [];
        for (var i = 0; i < root._entries.length; ++i)
            names.push(("" + (root._entries[i].name || "")).trim());
        return names;
    }

    /// Why `trimmed` cannot be the name of the entry at `index`, or empty when
    /// it can. `index` is -1 for the Add form, which has no row of its own to
    /// exclude from the duplicate check.
    ///
    /// One rule in one place for both the Add form and a row's rename, and the
    /// same rule the schema applies: canonicalNamedEntries drops an entry whose
    /// name is empty, longer than WorkspaceNameMaxLength, or carries a
    /// non-printable character, and a dropped entry looks to the user like an
    /// edit that was simply lost. The printable test is `\p{C}` because that is
    /// the set QChar::isPrint() rejects (control, format, surrogate,
    /// private-use, unassigned).
    function _nameRefusalFor(trimmed, index) {
        if (trimmed === "")
            return i18n("A workspace needs a name.");
        if (trimmed.length > root._nameMaxLength)
            return i18n("A workspace name can be at most %1 characters long.", root._nameMaxLength);
        if (/\p{C}/u.test(trimmed))
            return i18n("A workspace name cannot contain control characters.");
        var names = root._names;
        for (var i = 0; i < names.length; ++i) {
            if (i !== index && names[i] === trimmed)
                return i18n("Another workspace already uses that name.");
        }
        return "";
    }

    /// The schema's own bound, read off the controller rather than spelled
    /// here (ConfigDefaults::WorkspaceNameMaxLength).
    readonly property int _nameMaxLength: settingsController.workspaceNameMaxLength

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
            // Read off the STORE, not the staged copy: SettingsCard adopts
            // this one-shot in its own Component.onCompleted, which runs
            // before the page's, so the staged array is still empty then and
            // the form would always start expanded.
            initiallyCollapsed: appSettings.workspacesNamedEntries.length > 0

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
                        Accessible.description: addButton.enabled ? "" : root._nameRefusalFor(text.trim(), -1)
                        // Enter adds, so a keyboard-only user does not have to
                        // tab past the monitor combo to reach the button.
                        onAccepted: {
                            if (addButton.enabled)
                                addButton.clicked();
                        }
                    }

                    WideComboBox {
                        id: addOutputCombo

                        textRole: "label"
                        valueRole: "value"
                        Accessible.name: i18n("Pinned monitor")
                        model: root._screenOptions
                        // No storedValue: there is no stored value to follow,
                        // and WideComboBox re-runs its sync on every model
                        // change, so a constant "" would snap an in-progress
                        // pick back to "Any monitor" on a monitor hotplug.
                        // This is the component's documented "left undefined"
                        // mode; the site owns currentIndex, which the Add
                        // handler resets to 0.
                        currentIndex: 0
                    }
                }

                Button {
                    id: addButton

                    Layout.alignment: Qt.AlignRight
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    Layout.bottomMargin: Kirigami.Units.smallSpacing
                    text: i18n("Add")
                    icon.name: "list-add"
                    Accessible.name: i18n("Add named workspace")
                    // The schema's whole rule, not just non-empty and unique:
                    // an over-long or control-bearing name is dropped by
                    // canonicalNamedEntries on the round-trip, which would look
                    // like the Add had simply failed.
                    enabled: root._nameRefusalFor(addNameField.text.trim(), -1) === ""
                    onClicked: {
                        var arr = root._entries.slice();
                        arr.push({
                            "uid": "ws" + (root._uidSeq++),
                            "name": addNameField.text.trim(),
                            "output": addOutputCombo.currentValue || "",
                            "position": -1,
                            "focusShortcut": "",
                            "moveShortcut": ""
                        });
                        root._entries = arr;
                        root._namesTick++;
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
                    // The collapsed row is a two-line header (name over
                    // monitor); the default grip-column height (4 gridUnits)
                    // would pad it with dead space (the ChainEditor
                    // precedent, same value).
                    headerRowHeight: Kirigami.Units.gridUnit * 3
                    visible: root._entries.length > 0
                    items: root._entries
                    anchorPrefix: "namedWorkspace:"
                    // Keyed by the synthetic uid, never by the name: see the
                    // `_entries` comment for why a mutable key corrupts the
                    // height cache and strands the search anchor.
                    //
                    // Null-guarded like ReorderableColumn's own defaults:
                    // during a model reset the delegate's modelData detaches
                    // before its destruction handler runs, and both resolvers
                    // are called from bindings that re-evaluate in that window.
                    idOf: function (item) {
                        return item ? item.uid : "";
                    }
                    accessibleNameOf: function (item) {
                        return (item && item.name) ? item.name : i18n("(unnamed)");
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
                        // Read fresh at commit time rather than handed a plain
                        // array: sibling names change in place across renames,
                        // so a snapshot property would go stale.
                        nameRefusalFor: function (trimmed, index) {
                            return root._nameRefusalFor(trimmed, index);
                        }
                        onFieldEdited: function (index, field, value) {
                            // In place, deliberately: reassigning _entries
                            // would rebuild every row delegate and collapse
                            // the row mid-edit. The row mirrors the header
                            // fields itself; structural ops (add / remove /
                            // reorder) still replace the array.
                            if (index < 0 || index >= root._entries.length)
                                return;

                            var previousName = root._entries[index].name;
                            root._entries[index][field] = value;
                            if (field === "name") {
                                root._namesTick++;
                                settingsController.renameWorkspaceSlotTargets(previousName, value);
                            }
                            root._commitEntries();
                        }
                        onRemoveRequested: function (index) {
                            var arr = root._entries.slice();
                            arr.splice(index, 1);
                            root._entries = arr;
                            root._namesTick++;
                            root._commitEntries();
                        }
                    }
                }
            }
        }
    }
}
