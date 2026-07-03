// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

/**
 * @brief Ordered editor for a chain of decoration shader packs.
 *
 * Renders the chain as an ordered list of pack rows. Each row shows the
 * pack's display name, reorder up/down arrows, and a remove (x) button.
 * Directly beneath each row, a ShaderParamsEditor is shown inline
 * (whenever the pack declares parameters) bound to that pack's parameter
 * schema + the surface's per-pack parameter overrides — mirroring the
 * always-visible shader editor in AnimationProfileEditor, so the per-pack
 * settings (e.g. the Border pack's width / radius / colours) are editable
 * in place rather than hidden behind an expander. An "Add" combo at the
 * bottom appends a pack not already in the chain.
 *
 * Pure props-and-signals — the component owns no persistence. The host
 * (DecorationSurfaceCard) feeds:
 *   - availableShaders: QVariantList of effect maps (id / name / parameters)
 *   - chain:            QStringList of pack ids in order
 *   - packParameters:   { packId -> { paramId -> value } } override map
 * and listens for:
 *   - chainChangeRequested(newChain)          — add / remove / reorder
 *   - paramChangeRequested(packId, id, value) — a per-pack parameter edit
 *   - paramsRandomizeRequested(packId, rolled) — a whole-pack randomize roll
 *
 * The host routes those signals into the DecorationPageController's
 * setChain / setChainParam mutators (with its own surface path), then
 * feeds the refreshed chain / params back down — this component never
 * touches C++ directly.
 */
ColumnLayout {
    id: root

    required property var availableShaders
    required property var chain
    property var packParameters: ({})

    signal chainChangeRequested(var newChain)
    signal paramChangeRequested(string packId, string paramId, var value)
    signal paramsRandomizeRequested(string packId, var rolled)

    function _effectFor(packId) {
        if (!root.availableShaders)
            return null;
        for (var i = 0; i < root.availableShaders.length; i++) {
            if (root.availableShaders[i] && root.availableShaders[i].id === packId)
                return root.availableShaders[i];
        }
        return null;
    }

    function _displayName(packId) {
        var e = root._effectFor(packId);
        return (e && e.name) ? e.name : packId;
    }

    // Packs not already in the chain, for the Add combo's model.
    function _addableEffects() {
        var out = [];
        if (!root.availableShaders)
            return out;
        var inChain = {};
        var c = root.chain || [];
        for (var i = 0; i < c.length; i++)
            inChain[c[i]] = true;
        for (var j = 0; j < root.availableShaders.length; j++) {
            var e = root.availableShaders[j];
            if (e && e.id && !inChain[e.id])
                out.push(e);
        }
        return out;
    }

    function _withAppended(packId) {
        var next = (root.chain || []).slice();
        next.push(packId);
        return next;
    }

    function _withRemoved(index) {
        var next = (root.chain || []).slice();
        if (index >= 0 && index < next.length)
            next.splice(index, 1);
        return next;
    }

    function _withMoved(index, delta) {
        var next = (root.chain || []).slice();
        var target = index + delta;
        if (index < 0 || index >= next.length || target < 0 || target >= next.length)
            return next;
        var tmp = next[index];
        next[index] = next[target];
        next[target] = tmp;
        return next;
    }

    spacing: Kirigami.Units.smallSpacing

    // ── Empty state ──────────────────────────────────────────────────────
    Label {
        Layout.fillWidth: true
        visible: !root.chain || root.chain.length === 0
        text: i18n("No decoration packs. Add one below.")
        wrapMode: Text.WordWrap
        opacity: 0.7
    }

    // ── Pack rows ────────────────────────────────────────────────────────
    Repeater {
        model: root.chain

        delegate: ColumnLayout {
            id: packDelegate

            required property string modelData
            required property int index

            readonly property var _effect: root._effectFor(packDelegate.modelData)
            readonly property var _schema: (packDelegate._effect && packDelegate._effect.parameters) ? packDelegate._effect.parameters : []
            readonly property var _values: (root.packParameters && root.packParameters[packDelegate.modelData]) ? root.packParameters[packDelegate.modelData] : ({})

            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    text: root._displayName(packDelegate.modelData)
                    elide: Text.ElideRight
                }

                ToolButton {
                    enabled: packDelegate.index > 0
                    icon.name: "arrow-up"
                    display: ToolButton.IconOnly
                    Accessible.name: i18n("Move %1 up", root._displayName(packDelegate.modelData))
                    onClicked: root.chainChangeRequested(root._withMoved(packDelegate.index, -1))
                }

                ToolButton {
                    enabled: packDelegate.index < (root.chain ? root.chain.length - 1 : 0)
                    icon.name: "arrow-down"
                    display: ToolButton.IconOnly
                    Accessible.name: i18n("Move %1 down", root._displayName(packDelegate.modelData))
                    onClicked: root.chainChangeRequested(root._withMoved(packDelegate.index, 1))
                }

                ToolButton {
                    icon.name: "edit-delete-remove"
                    display: ToolButton.IconOnly
                    Accessible.name: i18n("Remove %1", root._displayName(packDelegate.modelData))
                    onClicked: root.chainChangeRequested(root._withRemoved(packDelegate.index))
                }
            }

            // ── Per-pack parameters ──────────────────────────────────────
            // Shown inline whenever the pack declares parameters.
            // Reuses the shared ShaderParamsEditor — the same
            // editor + colour dialog + lock / randomize host the animation
            // profile editor and App-Rules action row use — so the Border
            // pack's colour swatches open the picker and lock / randomize
            // behave identically to the animation pages.
            PZCommon.ShaderParamsEditor {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                visible: packDelegate._schema.length > 0

                compact: true
                enableGroups: true
                enableLocking: true
                enableRandomize: true
                enableImage: false
                parameters: packDelegate._schema
                currentValues: packDelegate._values
                effectId: packDelegate.modelData
                onValueChanged: function (effectId, paramId, value) {
                    root.paramChangeRequested(effectId, paramId, value);
                }
                onRandomizeRequested: function (rolled) {
                    root.paramsRandomizeRequested(packDelegate.modelData, rolled);
                }
            }
        }
    }

    // ── Add row ──────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        ComboBox {
            id: addCombo

            Layout.fillWidth: true
            Accessible.name: i18n("Add decoration pack")
            model: root._addableEffects()
            textRole: "name"
            valueRole: "id"
            enabled: count > 0
            displayText: count > 0 ? i18n("Add a decoration pack…") : i18n("All installed packs are in the chain")
            // Keep the placeholder visible rather than latching a selection.
            currentIndex: -1
            onActivated: index => {
                var items = root._addableEffects();
                if (index >= 0 && index < items.length) {
                    root.chainChangeRequested(root._withAppended(items[index].id));
                    addCombo.currentIndex = -1;
                }
            }
        }
    }
}
