// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * @brief The named preset a shader-override rule's `params` are deltas against.
 *
 * Its own file rather than one more Component inside ActionParamEditors.qml,
 * which is already past the size ceiling.
 *
 * Offered for whichever pack the row's OTHER param names, because a preset only
 * means anything against its own pack: the animation action names an animation
 * pack, the overlay action an overlay one.
 *
 * A plain combo rather than the full PresetRow the assignment pages use. A rule
 * row has no live preview to revert to and nowhere to save a new preset from,
 * so every maintenance action would be a dead control. Presets are created
 * where a pack is actually being tuned.
 */
ComboBox {
    /// The ActionRow this editor belongs to. Passed in rather than reached for
    /// through parent chains, which is what the in-file Component could rely on
    /// and a standalone type cannot. Typed, not `var`: it is an ActionRow, and the
    /// repo rule is typed properties wherever the type is known.
    required property ActionRow row
    /// The param descriptor this editor renders.
    required property var modelData

    readonly property var _param: modelData
    readonly property string _packId: row.action[row._shaderPresetPackKey] || ""
    readonly property var _bridge: {
        if (!row.appSettings)
            return null;
        return row._shaderActionType === "overrideOverlayShader" ? row.appSettings.overlayPresets : row.appSettings.animationPresets;
    }

    readonly property var _entries: {
        const out = [
            {
                id: "",
                name: i18nc("@item:inlistbox no shader preset", "None")
            }
        ];
        if (_bridge && _packId.length > 0) {
            const rows = _bridge.presetsFor(_packId);
            for (const r of rows)
                out.push(r);
        }
        // A rule can outlive the preset it names — deleted here, or dropped by a
        // pack update. Without a synthetic entry the combo fell back to index 0 and
        // read "None" while the action still stored the id, so a dangling reference
        // was indistinguishable from no reference at all. PresetRow solves it the
        // same way.
        const want = row.action[_param.key] || "";
        if (want.length > 0 && !_entries_has(out, want)) {
            out.push({
                id: want,
                name: i18nc("@item:inlistbox preset that no longer exists", "Missing preset")
            });
        }
        return out;
    }

    function _entries_has(list, id) {
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id === id)
                return true;
        }
        return false;
    }

    enabled: _packId.length > 0
    model: _entries
    textRole: "name"
    valueRole: "id"
    // A NAME as well as a description: without one the combo announces with no
    // name at all, and every name-worthy sibling in ActionRow sets it.
    Accessible.name: i18nc("@label:listbox", "Shader preset")
    Accessible.description: _param.label
    currentIndex: {
        const want = row.action[_param.key] || "";
        for (let i = 0; i < _entries.length; ++i) {
            if (_entries[i].id === want)
                return i;
        }
        return 0;
    }
    onActivated: function (index) {
        // Bounds-guarded: `_entries` derives from a live preset list, so the model
        // can shrink between the click and this handler.
        if (index < 0 || index >= _entries.length)
            return;
        row.actionEdited(row._withParam(_param.key, _entries[index].id));
    }
}
