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
 * Renders the chain as an ordered list of expandable pack rows. A
 * collapsed row shows the enable switch, the pack's display name, a
 * ONE-LINE elided description teaser, and a remove (x) button. Expanding
 * a row reveals the FULL wrapped description (pack descriptions run to
 * several sentences, and the collapsed header cannot afford more than a
 * line) followed by a ShaderParamsEditor bound to that pack's parameter
 * schema + the surface's per-pack parameter overrides. Rows expand even
 * for param-less packs so the description is always reachable. An "Add"
 * combo at the bottom appends a pack not already in the chain.
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
 *   - paramsResetRequested(packId, defaults)  — a whole-pack reset to defaults
 *   - layerEnabledChangeRequested(packId, on) — per-pack enable toggle
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
    // Pack ids from `chain` whose per-layer toggle is OFF. The row stays in
    // the list (params editable, reorderable) but renders dimmed with its
    // switch off — the same read a disabled rule row gives in the rules list.
    property var disabledPacks: []
    // The rules-action embed hides the per-layer enable switches: a rule
    // chain is explicit (remove a pack instead of disabling it), and the
    // toggles would write to the DecorationProfileTree, not the rule.
    property bool showLayerToggles: true
    // The rules-action embed renders its own compact add-picker inline with
    // the THEN selector, so it hides this component's bottom add row (and
    // the empty-state hint adjusts: an empty rule chain is the "no
    // decoration" sentinel rather than an invitation to add below).
    property bool showAddRow: true
    // Stable empty-map identity for param-less packs: a per-evaluation `({})`
    // literal would hand the inner ShaderParamsEditor a new object identity on
    // every host refresh and churn its currentValues rebind (same hoist as
    // ActionRow's _emptyShaderParams).
    readonly property var _emptyParams: ({})

    signal chainChangeRequested(var newChain)
    signal paramChangeRequested(string packId, string paramId, var value)
    signal paramsRandomizeRequested(string packId, var rolled)
    signal paramsResetRequested(string packId, var defaults)
    signal layerEnabledChangeRequested(string packId, bool enabled)

    function _isLayerEnabled(packId) {
        var d = root.disabledPacks || [];
        return d.indexOf(packId) === -1;
    }

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
        text: root.showAddRow ? i18n("No decoration packs. Add one below.") : i18n("No decoration packs. Matched windows render undecorated.")
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
        accessibleNameOf: function (item) {
            return root._displayName(item);
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

            // Expandable whenever there is anything to reveal: the parameter
            // editor OR the full pack description. Pack descriptions run to
            // several sentences and the collapsed header can only afford one
            // elided line, so the expansion is where the full text lives —
            // param-less packs stay expandable for exactly that reason.
            expandable: packDelegate._hasParams || packDelegate._description.length > 0
            expansionContent: (packDelegate._hasParams || packDelegate._description.length > 0) ? expansionComponent : null

            readonly property bool _layerEnabled: root._isLayerEnabled(packDelegate.packId)

            // Enable/disable toggle — LEADS the row, before the pack name,
            // in the same position as the rule rows' switch so the two lists
            // read identically (the shared ExpandableRowDelegate shell lets
            // the switch consume its own clicks before row-expand).
            SettingsSwitch {
                visible: root.showLayerToggles
                Layout.alignment: Qt.AlignVCenter
                checked: packDelegate._layerEnabled
                accessibleName: packDelegate._layerEnabled ? i18n("Disable %1", root._displayName(packDelegate.packId)) : i18n("Enable %1", root._displayName(packDelegate.packId))
                onToggled: function (newValue) {
                    root.layerEnabledChangeRequested(packDelegate.packId, newValue);
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                // Disabled layers dim with the SAME per-label opacities as a
                // disabled rule row (name 1 -> 0.5, summary 0.7 -> 0.4).
                Label {
                    Layout.fillWidth: true
                    text: root._displayName(packDelegate.packId)
                    font.bold: true
                    opacity: packDelegate._layerEnabled ? 1 : 0.5
                    elide: Text.ElideRight
                }

                // One-line teaser while collapsed; hidden when expanded — the
                // expansion body shows the FULL wrapped description instead,
                // so the text never appears twice and never truncates where
                // the user cannot recover it.
                Label {
                    Layout.fillWidth: true
                    visible: packDelegate._description.length > 0 && !packDelegate.expanded
                    text: packDelegate._description
                    opacity: packDelegate._layerEnabled ? 0.7 : 0.4
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
                visible: packDelegate.expandable
                expanded: packDelegate.expanded
            }

            // Lazily-loaded expansion, hosted by the shell's Loader: the
            // FULL wrapped pack description (the header shows only a one-line
            // elided teaser while collapsed), then the parameter editor when
            // the pack declares parameters. The editor reuses the shared
            // ShaderParamsEditor — the same editor + colour dialog + lock /
            // randomize host the animation profile editor and App-Rules
            // action row use — so behaviour is identical everywhere.
            Component {
                id: expansionComponent

                ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        Layout.fillWidth: true
                        visible: packDelegate._description.length > 0
                        text: packDelegate._description
                        wrapMode: Text.WordWrap
                        opacity: packDelegate._layerEnabled ? 0.7 : 0.4
                    }

                    PZCommon.ShaderParamsEditor {
                        Layout.fillWidth: true
                        visible: packDelegate._hasParams
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
                        onResetRequested: function (defaults) {
                            root.paramsResetRequested(packDelegate.packId, defaults);
                        }
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
        visible: root.showAddRow && root.chain && root.chain.length > 0
    }

    // ── Add row ──────────────────────────────────────────────────────────
    // Compact right-aligned pack picker in a labelled SettingsRow, matching the
    // animation "Set shader" selector (PZCommon.CategoryMenuButton) rather
    // than a full-width dropdown bar. It is an ACTION, not a persistent
    // selection: selecting a pack appends it to the chain, so currentId stays
    // empty and the button always shows its placeholder.
    SettingsRow {
        id: addPackRow
        visible: root.showAddRow
        title: i18n("Add decoration pack")
        // Hoisted: the add-pack candidate list is scanned once per change of
        // chain/availableShaders instead of three times per re-evaluation
        // (description, enabled, items each rebuilt the filtered list).
        // Referenced by explicit id, not `parent`, because SettingsRow may
        // reparent the button into its own content layout.
        readonly property var _addable: root._addableEffects()
        description: _addable.length > 0 ? i18n("Stack another pack onto this surface's chain") : i18n("All installed packs are already in the chain")

        PZCommon.CategoryMenuButton {
            // SettingsRow lays its default children out in a plain Row
            // positioner, so Layout.* attached properties are inert here —
            // size explicitly or the button sits at implicit width. Clamped to
            // the same 45% of the row that SettingsRow caps its control slot
            // at: the Row neither clips nor shrinks its children, so a fixed
            // 16 grid units overhangs the right margin on a narrow window.
            // The cap is relative to SettingsRow's INNER layout, which is
            // inset by largeSpacing on each side — measuring the outer width
            // would still overhang by 0.45 * 2 * largeSpacing.
            width: Math.min(Kirigami.Units.gridUnit * 16, Math.max(0, (addPackRow.width - Kirigami.Units.largeSpacing * 2) * 0.45))
            enabled: addPackRow._addable.length > 0
            items: addPackRow._addable
            currentId: ""
            includeNoneEntry: false
            placeholderText: i18nc("@action:button", "Add a pack…")
            Accessible.description: i18n("Add a decoration pack to this surface's chain")
            onSelected: function (id) {
                if (id && id.length > 0)
                    root.chainChangeRequested(root._withAppended(id));
            }
        }
    }
}
