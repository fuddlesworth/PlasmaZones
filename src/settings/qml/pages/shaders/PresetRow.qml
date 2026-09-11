// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

/**
 * @brief Pick, save and maintain one pack's named parameter presets.
 *
 * The one preset surface, shared by every place a pack is tuned: the animation
 * event card, the decoration and pointer chain rows, the zone-overlay
 * assignment card, and the pack browser's detail dialog.
 *
 * ## What an assignment stores
 *
 * A preset id plus only the parameters the user changed afterwards. So the
 * effective values are the preset's, with those edits laid over the top, and
 * editing the preset moves every assignment bound to it. That is why this row
 * distinguishes three states rather than two:
 *
 *   • No preset. The assignment's values are the whole tuning.
 *   • A preset, matched. Every value comes from it.
 *   • A preset, modified. Some values were changed here afterwards; those stay
 *     put while the rest keep following the preset.
 *
 * The third state is the one that needs saying out loud, because otherwise a
 * user who tweaks a slider cannot tell whether they have quietly detached from
 * the preset (they have not) or whether their edit will be overwritten the next
 * time the preset changes (it will not).
 */
RowLayout {
    id: root

    /// The pack whose presets these are. Presets are only ever offered for
    /// their own pack: parameter ids mean nothing across packs.
    required property string packId
    /// The family's `ShaderPresetBridge`. Null disables the whole row, which
    /// is what a host with no preset support passes.
    property QtObject presetBridge: null
    /// The assignment's current preset id, or empty for none.
    property string presetId: ""
    /// The assignment's live parameter values — preset ⊕ deltas — used to save a
    /// new preset and to update the current one. These are the values the rows
    /// show, so saving from them captures the whole tuning.
    property var currentValues: ({})
    /// The assignment's OWN stored parameter map, if the host has one.
    ///
    /// Distinct from `currentValues`, which is the merged view. Supply it and the
    /// modified state becomes "does this assignment store any delta", which is
    /// the question the three-state model is actually about. Leave it undefined
    /// in a host that has no assignment behind it (the pack browser's preview)
    /// and the row falls back to comparing values.
    property var deltas: undefined

    /// Emitted when the user picks a different preset (or None). The host
    /// writes it to the assignment; this row does not persist anything itself.
    signal presetSelected(string presetId)
    /// Emitted when the user reverts to the preset's values, carrying the map
    /// the host should store as the assignment's parameters. Empty means "no
    /// deltas", which is what reverting to a preset means.
    signal revertRequested
    /// Emitted after a save or update changed what is on disk, so the host can
    /// re-read anything it derived from the preset.
    signal presetsChanged
    /// Emitted when the user deleted the selected preset. The host should drop the
    /// reference and leave the VALUES alone — deleting a preset says nothing about
    /// what the parameters should become. Distinct from `presetSelected("")`,
    /// which in a preview host means "show me the pack's defaults".
    signal presetDeleted(string presetId)

    // Imperative rather than bound, like the rest of this app's registry-backed
    // model state: the preset list lives on disk, so a function-call binding
    // would never re-evaluate when a preset is saved or a pack rescans. The
    // bridge's own change signal and the property hooks below are what refresh
    // it.
    property var _rows: []
    property var _presetParams: ({})

    readonly property bool _hasPreset: root.presetId.length > 0
    readonly property bool _presetMissing: root._hasPreset && _indexOfId(root.presetId) < 0
    readonly property bool _currentIsReadOnly: {
        const i = _indexOfId(root.presetId);
        return i >= 0 ? (root._rows[i].readOnly === true) : false;
    }

    /// True when this assignment carries edits on top of the selected preset.
    ///
    /// When the host supplies `deltas` — the assignment's OWN stored parameter
    /// map, as distinct from the merged values shown in the rows — the answer is
    /// simply whether that map has any keys. That is the honest reading, because
    /// presence in the delta map IS the override: the library keeps a delta whose
    /// value equals the preset's on purpose, so that the user's choice does not
    /// silently start following a later retune of the preset. A value comparison
    /// cannot see such a key at all, and it also misses a delta on a parameter
    /// the preset says nothing about.
    ///
    /// Falls back to the value comparison for a host that has no delta map to
    /// give (the pack browser's preview, where there is no assignment).
    readonly property bool _modified: {
        if (!root._hasPreset || root._presetMissing)
            return false;
        if (root.deltas !== undefined && root.deltas !== null)
            return Object.keys(root.deltas).length > 0;
        const preset = root._presetParams || {};
        const live = root.currentValues || {};
        for (const key in preset) {
            if (live[key] === undefined)
                continue;
            if (!_sameValue(live[key], preset[key]))
                return true;
        }
        return false;
    }

    function _indexOfId(id) {
        for (let i = 0; i < root._rows.length; ++i) {
            if (root._rows[i].id === id)
                return i;
        }
        return -1;
    }

    /// Numbers arrive as doubles from JSON and as ints from C++, so an exact
    /// compare would mark an untouched parameter modified. Colours and other
    /// strings compare as strings.
    function _sameValue(a, b) {
        if (typeof a === "number" && typeof b === "number")
            return Math.abs(a - b) < 1e-9;
        return String(a) === String(b);
    }

    function refresh() {
        root._rows = (root.presetBridge && root.packId.length > 0) ? root.presetBridge.presetsFor(root.packId) : [];
        root._presetParams = (root.presetBridge && root._hasPreset) ? root.presetBridge.presetParams(root.packId, root.presetId) : ({});
    }

    onPackIdChanged: root.refresh()
    onPresetBridgeChanged: root.refresh()
    onPresetIdChanged: root.refresh()
    Component.onCompleted: root.refresh()

    spacing: Kirigami.Units.smallSpacing
    visible: root.presetBridge !== null && root.packId.length > 0

    QQC2.Label {
        text: i18nc("@label:listbox", "Preset")
        Layout.alignment: Qt.AlignVCenter
    }

    // WideComboBox, not a bare ComboBox: the settings app pins its style to
    // org.kde.desktop, whose Menu-based popup binds its width to the combo and
    // ignores a popup.width override — so with the fixed width below, a longer
    // preset name was truncated IN THE LIST, where the user needs to read it to
    // choose. This component swaps the popup for one that fits its widest item,
    // and is the house pattern across ~20 other sites here. `storedValue` stays
    // undefined on purpose: this row manages `currentIndex` itself.
    WideComboBox {
        id: combo

        // A FIXED width, not fillWidth and not content-hugging. The row spans
        // the whole card so the preset reads as a heading for the parameters
        // under it, and a combo that filled that span stretched the full card
        // width, far wider than any preset name and wider than every control
        // below it. Hugging its content instead would make the combo resize
        // every time a longer name was picked. The popup is not bound by this
        // width (see above), so a long name stays readable when the list is open.
        Layout.preferredWidth: Kirigami.Units.gridUnit * 14
        Accessible.name: i18nc("@label:listbox", "Shader preset")
        // The modified state is shown beside the combo as its own label, which a
        // screen reader reaches only by moving on. Fold it in here too, so a user
        // who tabs to the combo is told the assignment diverges from the preset —
        // the one state this row exists to say out loud.
        Accessible.description: root._modified ? i18nc("@info:whatsthis", "Named parameter preset for this shader pack. This assignment has edits on top of it.") : i18nc("@info:whatsthis", "Named parameter preset for this shader pack.")

        // "None" is a real first entry rather than an empty row, because
        // clearing a preset is a thing the user does deliberately and it needs
        // somewhere to click.
        readonly property var _entries: {
            const out = [
                {
                    id: "",
                    name: i18nc("@item:inlistbox no shader preset", "None"),
                    readOnly: false
                }
            ];
            for (const row of root._rows)
                out.push(row);
            // A preset an assignment points at can vanish (deleted here, or
            // dropped by a pack update). Show it rather than silently
            // selecting None, or the user cannot tell why the look changed.
            if (root._presetMissing) {
                out.push({
                    id: root.presetId,
                    name: i18nc("@item:inlistbox preset that no longer exists", "Missing preset"),
                    readOnly: true
                });
            }
            return out;
        }

        model: _entries
        textRole: "name"
        valueRole: "id"

        currentIndex: {
            for (let i = 0; i < _entries.length; ++i) {
                if (_entries[i].id === root.presetId)
                    return i;
            }
            return 0;
        }

        onActivated: function (index) {
            // Bounds-guarded: `_entries` derives from `_rows`, which a file
            // watcher refreshes, so the model can shrink between the click and
            // this handler. Unguarded this threw on `.id` of undefined, and on a
            // mere reorder it would have activated a different preset than the
            // row the user clicked.
            if (index < 0 || index >= _entries.length)
                return;
            const picked = _entries[index].id;
            if (picked !== root.presetId)
                root.presetSelected(picked);
        }
    }

    // The modified marker. A label rather than an icon alone so a screen
    // reader announces the state, which is the whole point of showing it.
    QQC2.Label {
        visible: root._modified
        text: i18nc("@info shader preset has local edits", "Modified")
        color: Kirigami.Theme.neutralTextColor
        font: Kirigami.Theme.smallFont
        Layout.alignment: Qt.AlignVCenter
    }

    QQC2.ToolButton {
        visible: root._modified
        icon.name: "edit-undo"
        display: QQC2.AbstractButton.IconOnly
        text: i18nc("@action:button", "Revert to preset")
        Accessible.name: text
        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.text: text
        onClicked: root.revertRequested()
    }

    QQC2.ToolButton {
        // Only for a preset the user owns: a pack-declared one belongs to its
        // pack, and the next pack update would overwrite the edit anyway.
        visible: root._modified && !root._currentIsReadOnly && !root._presetMissing
        icon.name: "document-save"
        display: QQC2.AbstractButton.IconOnly
        text: i18nc("@action:button", "Update preset")
        Accessible.name: text
        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.text: i18nc("@info:tooltip", "Save these values into the preset, for everything using it")
        onClicked: {
            if (root.presetBridge.updatePreset(root.presetId, root.currentValues)) {
                root.revertRequested();
                root.presetsChanged();
            }
        }
    }

    QQC2.ToolButton {
        icon.name: "list-add"
        display: QQC2.AbstractButton.IconOnly
        text: i18nc("@action:button", "Save as new preset")
        Accessible.name: text
        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.text: text
        onClicked: nameDialog.openFor("")
    }

    QQC2.ToolButton {
        visible: root._hasPreset && !root._currentIsReadOnly && !root._presetMissing
        icon.name: "edit-rename"
        display: QQC2.AbstractButton.IconOnly
        text: i18nc("@action:button", "Rename preset")
        Accessible.name: text
        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.text: text
        onClicked: {
            const i = root._indexOfId(root.presetId);
            nameDialog.openFor(i >= 0 ? root._rows[i].name : "");
        }
    }

    QQC2.ToolButton {
        visible: root._hasPreset && !root._currentIsReadOnly && !root._presetMissing
        icon.name: "edit-delete"
        display: QQC2.AbstractButton.IconOnly
        text: i18nc("@action:button", "Delete preset")
        Accessible.name: text
        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.text: text
        onClicked: {
            const deletedId = root.presetId;
            if (root.presetBridge.deletePreset(deletedId)) {
                // Its own signal, NOT presetSelected(""). For the four assignment
                // hosts the two mean the same thing — drop the reference, keep the
                // values — but the pack browser's preview treats an empty
                // selection as "load the pack's defaults", so reusing it there
                // wiped the tuning the user was working on. Deleting a preset says
                // nothing about what the values should become.
                root.presetDeleted(deletedId);
                root.presetsChanged();
            }
        }
    }

    // Save-as and rename share one dialog: both ask for exactly a name, and
    // which one it is is decided by whether it opened with an existing one.
    Kirigami.PromptDialog {
        id: nameDialog

        property bool renaming: false

        title: renaming ? i18nc("@title:window", "Rename Preset") : i18nc("@title:window", "Save Preset")
        standardButtons: Kirigami.Dialog.NoButton

        function openFor(existingName) {
            renaming = existingName.length > 0 && root._hasPreset;
            nameField.text = existingName;
            open();
            nameField.forceActiveFocus();
            nameField.selectAll();
        }

        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button", "Save")
                enabled: root.presetBridge && root.presetBridge.canUsePresetName(nameField.text)
                onTriggered: nameDialog.commit()
            },
            Kirigami.Action {
                text: i18nc("@action:button", "Cancel")
                onTriggered: nameDialog.close()
            }
        ]

        function commit() {
            if (nameDialog.renaming) {
                if (root.presetBridge.renamePreset(root.presetId, nameField.text))
                    root.presetsChanged();
            } else {
                const id = root.presetBridge.savePreset(root.packId, nameField.text, root.currentValues);
                if (id.length > 0) {
                    // Select what was just saved and drop the deltas: the new
                    // preset IS these values, so nothing is layered on it yet.
                    root.presetSelected(id);
                    root.revertRequested();
                    root.presetsChanged();
                }
            }
            nameDialog.close();
        }

        QQC2.TextField {
            id: nameField

            Layout.fillWidth: true
            placeholderText: i18nc("@info:placeholder", "Preset name")
            onAccepted: {
                if (root.presetBridge && root.presetBridge.canUsePresetName(text))
                    nameDialog.commit();
            }
        }
    }

    // Absorbs the rest of the row's span, so the controls above stay grouped at
    // the left under the parameter labels instead of spreading across the card.
    Item {
        Layout.fillWidth: true
    }

    // The bridge relays registry changes, so a preset edited in another
    // process (or by the daemon's own copy) refreshes this row too.
    Connections {
        target: root.presetBridge
        function onPresetsChanged(packId) {
            if (packId === root.packId)
                root.refresh();
        }
        // Surfaced HERE rather than left to each host. The bridge emits this for
        // every refusal — an unwritable directory, a read-only pack preset, a
        // preset that vanished underneath, a failed delete — and only the pack
        // browser's dialog was listening, so in the four assignment hosts every
        // one of those failed silently: the save dialog simply closed and the
        // delete button did nothing visible. The failure is host-independent, so
        // the row owns it.
        function onPresetWriteFailed(reason) {
            failureMessage.text = reason;
            failureMessage.visible = true;
        }
    }

    // In the row rather than a toast: the row sits inside a scrollable card, and
    // a transient notification elsewhere on screen does not tell the user WHICH
    // preset refused. A Label rather than an icon so a screen reader announces
    // it, on the same reasoning as the modified marker above.
    QQC2.Label {
        id: failureMessage

        visible: false
        color: Kirigami.Theme.negativeTextColor
        font: Kirigami.Theme.smallFont
        elide: Text.ElideRight
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        QQC2.ToolTip.visible: hovered && truncated
        QQC2.ToolTip.text: text

        // Cleared as soon as the list changes, so a stale refusal does not sit
        // next to a preset the user has since fixed or switched away from.
        Connections {
            target: root
            function onPresetIdChanged() {
                failureMessage.visible = false;
            }
            function on_RowsChanged() {
                failureMessage.visible = false;
            }
        }

        // ToolTip.hovered needs a hover area on a plain Label.
        HoverHandler {
            id: failureHover
        }
        readonly property bool hovered: failureHover.hovered
    }
}
