// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Shader parameter editor bundled with its colour-picker dialog
 *        and lock / randomize working state.
 *
 * Wraps ShaderParameterEditor and hosts the QtQuick.Dialogs.ColorDialog
 * that every consumer previously hand-wired (the animation profile editor,
 * the App-Rules action row, and the decoration chain editor). Owns the
 * session-only lock map internally and rolls randomized values via the
 * inner editor's helper, so a host only ever has to persist values:
 * connect to @c valueChanged (single edit / colour pick) and
 * @c randomizeRequested (the rolled map).
 *
 * @c effectId is forwarded with every value write and captured into the
 * colour dialog at open time, so a registry refresh mid-pick cannot
 * retarget the write at a different effect's parameter map. For a
 * decoration chain it is the pack id; for an animation leg / rule action
 * it is the shader effect id.
 *
 * Pure props-and-signals — it owns no persistence. The lock map is
 * working-state only (never persisted); consumers that reset locks on a
 * shader change assign @c lockedParams directly.
 */
ColumnLayout {
    id: root

    required property var parameters
    required property var currentValues
    property string effectId: ""
    /// Session-only lock map ({ paramId: true }). Owned here; the inner
    /// editor self-updates it on toggle. Consumers may assign it (e.g. to
    /// clear locks when the selected effect changes).
    property var lockedParams: ({})
    property bool enableLocking: true
    property bool enableRandomize: true
    property bool enableImage: false
    property bool enableGroups: true
    property bool compact: true
    property int expandedGroupIndex: 0

    /// Fired for a single edit AND for a colour pick (paramId resolved).
    /// @p effectId echoes the effect/pack captured at the time of the edit.
    signal valueChanged(string effectId, string paramId, var value)
    /// Fired after a randomize roll with the full rolled value map. Locked
    /// and image params are preserved by the roll; the host persists it.
    signal randomizeRequested(var rolled)
    /// Re-exposed lock signals (working-state only) for hosts that mirror
    /// the lock map elsewhere. Most consumers ignore these.
    signal lockToggled(string paramId, bool locked)
    signal lockAllRequested(bool locked)
    /// Image params delegate picking to the host (platform FileDialog
    /// ownership stays at page level). No-op for packs without image params.
    signal requestImagePicker(string paramId)

    spacing: Kirigami.Units.smallSpacing

    ShaderParameterEditor {
        id: editor

        Layout.fillWidth: true
        parameters: root.parameters
        currentValues: root.currentValues
        lockedParams: root.lockedParams
        enableLocking: root.enableLocking
        enableRandomize: root.enableRandomize
        enableImage: root.enableImage
        enableGroups: root.enableGroups
        compact: root.compact
        expandedGroupIndex: root.expandedGroupIndex
        onValueChanged: function (paramId, value) {
            root.valueChanged(root.effectId, paramId, value);
        }
        onLockToggled: function (paramId, locked) {
            // Session-only working state — self-update so the inner editor
            // re-renders the lock glyph; no host persistence required.
            root.lockedParams = editor.lockedAfterToggle(paramId, locked);
            root.lockToggled(paramId, locked);
        }
        onLockAllRequested: function (locked) {
            root.lockedParams = editor.lockedAfterAllToggle(locked);
            root.lockAllRequested(locked);
        }
        onRandomizeRequested: {
            // computeRandomized honours the lock map and preserves image
            // params; emit the rolled map for the host to persist.
            root.randomizeRequested(editor.computeRandomized());
        }
        onRequestColorPicker: function (paramId, paramName, current) {
            // Capture effectId NOW so a registry refresh between open and
            // accept cannot retarget the write at another effect's params.
            colorDialog.effectId = root.effectId;
            colorDialog.paramId = paramId;
            colorDialog.paramName = paramName;
            colorDialog.selectedColor = current;
            colorDialog.open();
        }
        onRequestImagePicker: function (paramId) {
            root.requestImagePicker(paramId);
        }
    }

    // QtQuick.Dialogs.ColorDialog wraps the OS-native picker — runs in its
    // own platform window, no `parent` assignment needed (and none accepted).
    ColorDialog {
        id: colorDialog

        options: ColorDialog.ShowAlphaChannel

        property string effectId: ""
        property string paramId: ""
        property string paramName: ""

        title: paramName.length > 0 ? i18nc("@title:window", "Choose %1", paramName) : i18nc("@title:window", "Pick color")
        onAccepted: {
            if (paramId === "" || effectId === "")
                return;

            root.valueChanged(effectId, paramId, selectedColor.toString());
        }
    }
}
