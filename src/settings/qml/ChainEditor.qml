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
    // Stable empty-map identity for param-less packs: a per-evaluation `({})`
    // literal would hand the inner ShaderParamsEditor a new object identity on
    // every host refresh and churn its currentValues rebind (same hoist as
    // ActionRow's _emptyShaderParams).
    readonly property var _emptyParams: ({})

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
    // The chain is a reorderable stack of expandable rows — the SAME UX model
    // as the rules priority list — so it reuses the shared ReorderableColumn:
    // drag the grip handle to reorder, expand a row to edit that pack's params.
    // The chain is an array of pack-id strings, so the item id IS the string.
    ReorderableColumn {
        id: packList

        Layout.fillWidth: true
        visible: root.chain && root.chain.length > 0
        items: root.chain || []
        // Collapsed pack row (bold name + one-line description + margins) is
        // shorter than a rule row, so pin the grip band to fit it.
        headerRowHeight: Kirigami.Units.gridUnit * 3
        idOf: function (item) {
            return item;
        }
        onMoveRequested: function (fromIndex, toIndex) {
            var next = (root.chain || []).slice();
            if (fromIndex < 0 || fromIndex >= next.length || toIndex < 0 || toIndex >= next.length)
                return;
            var moved = next.splice(fromIndex, 1)[0];
            next.splice(toIndex, 0, moved);
            root.chainChangeRequested(next);
        }

        // Each pack is an expandable row built from the SAME shell as a
        // RuleRow (ExpandableRowDelegate + ExpandChevron): identical hover
        // highlight, identical whole-row click-to-expand, identical accordion
        // motion. The header carries the pack name + one-line description and
        // a remove button; expanding reveals the pack's inline parameter
        // editor. Collapsed by default so a multi-pack chain reads as a tidy
        // list, exactly like the rules priority list.
        rowDelegate: ExpandableRowDelegate {
            id: packDelegate

            readonly property string packId: parent.rowModelData
            readonly property var _effect: root._effectFor(packDelegate.packId)
            readonly property var _schema: (packDelegate._effect && packDelegate._effect.parameters) ? packDelegate._effect.parameters : []
            readonly property var _values: (root.packParameters && root.packParameters[packDelegate.packId]) ? root.packParameters[packDelegate.packId] : root._emptyParams
            readonly property string _description: (packDelegate._effect && packDelegate._effect.description) ? packDelegate._effect.description : ""
            readonly property bool _hasParams: packDelegate._schema.length > 0

            expandable: packDelegate._hasParams
            expansionContent: packDelegate._hasParams ? paramsComponent : null

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Label {
                    Layout.fillWidth: true
                    text: root._displayName(packDelegate.packId)
                    font.bold: true
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    visible: packDelegate._description.length > 0
                    text: packDelegate._description
                    opacity: 0.7
                    elide: Text.ElideRight
                }
            }

            ToolButton {
                icon.name: "edit-delete-remove"
                display: ToolButton.IconOnly
                Accessible.name: i18n("Remove %1", root._displayName(packDelegate.packId))
                onClicked: {
                    var idx = (root.chain || []).indexOf(packDelegate.packId);
                    if (idx >= 0)
                        root.chainChangeRequested(root._withRemoved(idx));
                }
            }

            ExpandChevron {
                visible: packDelegate._hasParams
                expanded: packDelegate.expanded
            }

            // Lazily-loaded parameter editor, hosted by the shell's expansion
            // Loader. Reuses the shared ShaderParamsEditor — the same editor +
            // colour dialog + lock / randomize host the animation profile
            // editor and App-Rules action row use — so behaviour is identical
            // everywhere.
            Component {
                id: paramsComponent

                PZCommon.ShaderParamsEditor {
                    compact: true
                    enableGroups: true
                    enableLocking: true
                    enableRandomize: true
                    enableImage: false
                    parameters: packDelegate._schema
                    currentValues: packDelegate._values
                    effectId: packDelegate.packId
                    onValueChanged: function (effectId, paramId, value) {
                        root.paramChangeRequested(effectId, paramId, value);
                    }
                    onRandomizeRequested: function (rolled) {
                        root.paramsRandomizeRequested(packDelegate.packId, rolled);
                    }
                }
            }
        }
    }

    // Divider between the pack stack and the add action, so the "Add
    // decoration pack" row reads as a separate affordance rather than running
    // into the last pack's card.
    SettingsSeparator {
        Layout.topMargin: Kirigami.Units.smallSpacing
        visible: root.chain && root.chain.length > 0
    }

    // ── Add row ──────────────────────────────────────────────────────────
    // Compact right-aligned pack picker in a labelled SettingsRow, matching the
    // animation "Shader effect" selector (PZCommon.CategoryMenuButton) rather
    // than a full-width dropdown bar. It is an ACTION, not a persistent
    // selection: selecting a pack appends it to the chain, so currentId stays
    // empty and the button always shows its placeholder.
    SettingsRow {
        Layout.fillWidth: true
        title: i18n("Add decoration pack")
        description: root._addableEffects().length > 0 ? i18n("Stack another pack onto this surface's chain") : i18n("All installed packs are already in the chain")

        PZCommon.CategoryMenuButton {
            Layout.fillWidth: true
            enabled: root._addableEffects().length > 0
            items: root._addableEffects()
            currentId: ""
            includeNoneEntry: false
            placeholderText: i18nc("@action:button", "Add a pack…")
            onSelected: function (id) {
                if (id && id.length > 0)
                    root.chainChangeRequested(root._withAppended(id));
            }
        }
    }
}
