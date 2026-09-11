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
    /// The assignment's live parameter values, used to save a new preset and
    /// to work out whether it has been modified away from the current one.
    property var currentValues: ({})

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

    /// True when the live values diverge from the selected preset's. Compared
    /// key by key against the PRESET rather than against a remembered
    /// snapshot, so it stays right after the preset itself is edited.
    readonly property bool _modified: {
        if (!root._hasPreset || root._presetMissing)
            return false;
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

    QQC2.ComboBox {
        id: combo

        // A FIXED width, not fillWidth and not content-hugging. The row spans
        // the whole card so the preset reads as a heading for the parameters
        // under it, and a combo that filled that span stretched the full card
        // width, far wider than any preset name and wider than every control
        // below it. Hugging its content instead would make the combo resize
        // every time a longer name was picked, so the width is pinned and a
        // long name elides.
        Layout.preferredWidth: Kirigami.Units.gridUnit * 14
        Accessible.name: i18nc("@info:whatsthis", "Named parameter preset for this shader pack")

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
            if (root.presetBridge.deletePreset(root.presetId)) {
                // The assignment keeps its own values, so clearing the
                // reference changes nothing about how it looks.
                root.presetSelected("");
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
    }
}
