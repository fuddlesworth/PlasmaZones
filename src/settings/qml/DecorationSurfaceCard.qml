// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Per-surface decoration override card. Mirrors AnimationEventCard.
 *
 * One card per surface path. CATEGORY paths (window, popup) and the
 * standalone osd surface are alwaysEnabled ROOTS — there is no
 * global decoration default above them, so they have no override toggle and
 * always edit their own profile (a category root additionally shows the
 * "applies to all children" cascade banner). Concrete leaf paths under a
 * category (window.tiled / window.snapped / window.floating, popup.*) are
 * override cards: the master toggle engages a per-surface override in the
 * DecorationProfileTree; OFF clears it (reset to inherited — same as
 * AnimationEventCard, no separate reset button) and the card shows the
 * RESOLVED chain read-only with an "Inheriting from: …" breadcrumb.
 *
 * Reactive-latch pattern: imperative refresh from the controller on
 * `profilesChanged` / `shaderEffectsChanged`, NOT function bindings that
 * re-query C++ every repaint (mirrors AnimationEventCard.refreshFromTree).
 *
 * Required properties:
 *   - surfacePath: full path (e.g. "window.tiled" leaf, or "window" category)
 *   - cardLabel: i18n() display label from the page model (like
 *     AnimationEventCard.eventLabel) — labels are translated in QML, not C++.
 * Optional:
 *   - alwaysEnabled: bool — root surface (no override toggle, always editing).
 *   - isParentNode: bool — category node; shows the cascade banner.
 */
Item {
    id: root

    required property string surfacePath
    required property string cardLabel
    property bool alwaysEnabled: false
    property bool isParentNode: false

    readonly property var bridge: settingsController.decorationPage

    // True when this card edits its own DIRECT profile: an alwaysEnabled root
    // always does; a leaf only when its override is engaged.
    readonly property bool _editing: root.alwaysEnabled || root._hasOverride

    // ── Reactive model state ─────────────────────────────────────────────
    property var _effects: []
    property bool _hasOverride: false
    // Effective (resolved) values for the read-only / preview view.
    property var _resolved: ({})
    // Direct-override sparse map: which fields are engaged AT this path.
    property var _raw: ({})
    // Chain shown by the editor — the direct chain when overriding, else the
    // resolved chain (so the user previews "what they'd start from").
    property var _chain: []
    property var _params: ({})
    property string _parentChainText: ""
    // Parent-node only: count of descendant surfaces with their own override
    // that shadow this node, driving the "Clear shadowing children" warning.
    property int _shadowingChildrenCount: 0

    // Ancestor breadcrumb as raw dotted paths joined "child ← parent", matching
    // AnimationEventCard.parentChainText (labels are not re-derived in C++).
    function _computeParentChainText() {
        if (!root.bridge)
            return "";
        var chain = root.bridge.parentChain(root.surfacePath);
        return (chain.length > 1) ? chain.slice(1).join(" ← ") : "";
    }

    function refresh() {
        if (!root.bridge)
            return;
        root._effects = root.bridge.availableShaderEffects();
        root._hasOverride = root.bridge.hasOverride(root.surfacePath);
        root._resolved = root.bridge.resolvedProfile(root.surfacePath);
        root._raw = root.bridge.rawProfile(root.surfacePath);
        root._chain = root.bridge.chainAt(root.surfacePath);
        root._params = (root._raw && root._raw.parameters) ? root._raw.parameters : ({});
        root._parentChainText = root._computeParentChainText();
        root._shadowingChildrenCount = root.bridge.overrideDescendantCount(root.surfacePath);
    }

    // Engage a per-surface override (leaf toggle ON): seed the chain with the
    // currently resolved chain so the override starts visibly equal to what was
    // inherited, then the user diverges from there.
    function _engageOverride() {
        // Engaging is idempotent: if a direct override already exists, re-seeding
        // from the resolved chain would discard the user's diverged edits. Only
        // seed when there is no override yet (a stray re-fire of onToggleClicked
        // with checked === true must not clobber the current chain).
        if (root.bridge && !root._hasOverride)
            root.bridge.setChain(root.surfacePath, root.bridge.chainAt(root.surfacePath));
    }

    function _resolvedSummary() {
        var c = root._resolved && root._resolved.chain ? root._resolved.chain : [];
        var packs = c.length > 0 ? root._packNames(c).join(", ") : i18n("None");
        return i18n("Packs: %1", packs);
    }

    function _packNames(ids) {
        var out = [];
        for (var i = 0; i < ids.length; i++) {
            var found = ids[i];
            for (var j = 0; j < root._effects.length; j++) {
                if (root._effects[j] && root._effects[j].id === ids[i]) {
                    found = root._effects[j].name;
                    break;
                }
            }
            out.push(found);
        }
        return out;
    }

    implicitHeight: card.implicitHeight
    Layout.fillWidth: true
    Component.onCompleted: root.refresh()

    Connections {
        target: root.bridge
        function onProfilesChanged() {
            root.refresh();
        }
        function onShaderEffectsChanged() {
            root.refresh();
        }
    }

    SettingsCard {
        id: card

        anchors.fill: parent
        headerText: root.cardLabel
        // alwaysEnabled roots have nothing to inherit, so no override toggle —
        // mirrors AnimationEventCard's alwaysEnabled global root.
        showToggle: !root.alwaysEnabled
        toggleChecked: root._editing
        onToggleClicked: function (checked) {
            if (checked)
                root._engageOverride();
            else if (root.bridge)
                root.bridge.clearOverride(root.surfacePath);
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            // ── Inheritance summary ───────────────────────────────────────
            // Category roots show the cascade banner; leaf cards show the
            // inheritance breadcrumb when not overriding — same split as
            // AnimationEventCard.
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Information
                visible: root.isParentNode ? root._editing : !root._editing
                text: {
                    if (root.isParentNode)
                        return i18n("Settings here apply to all child surfaces unless individually overridden.");
                    return root._parentChainText.length > 0 ? i18n("Inheriting from: %1", root._parentChainText) : i18n("Using global defaults");
                }
            }

            // ── Shadowing-children warning (parent-node cards only) ───────
            // A descendant surface with its own override shadows this parent:
            // the DecorationProfileTree resolve stops at the descendant's own
            // profile, so this node's chain never reaches it — even though the
            // parent card visually shows its own settings. Surface it with
            // one-click remediation (mirrors AnimationEventCard).
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Warning
                visible: root.isParentNode && root._shadowingChildrenCount > 0
                text: i18np("%n descendant surface has its own override that shadows this parent.", "%n descendant surfaces have overrides that shadow this parent.", root._shadowingChildrenCount)
                actions: [
                    Kirigami.Action {
                        text: i18n("Clear shadowing children")
                        icon.name: "edit-clear-all"
                        onTriggered: {
                            if (root.bridge)
                                root.bridge.clearOverrideDescendants(root.surfacePath);
                        }
                    }
                ]
            }

            Label {
                Layout.fillWidth: true
                visible: !root._editing
                text: i18n("Current: %1", root._resolvedSummary())
                font.italic: true
                color: Kirigami.Theme.disabledTextColor
                wrapMode: Text.WordWrap
            }

            // ── Override editor ───────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                visible: root._editing
                spacing: Kirigami.Units.largeSpacing

                Label {
                    Layout.fillWidth: true
                    text: i18n("Decoration chain")
                    font.weight: Font.DemiBold
                }

                Label {
                    Layout.fillWidth: true
                    text: i18n("Each pack's settings (e.g. the Border pack's width, corner radius and colours) are shown beneath it. A surface shows a border only when the Border pack is in its chain.")
                    wrapMode: Text.WordWrap
                    opacity: 0.8
                }

                ChainEditor {
                    Layout.fillWidth: true
                    availableShaders: root._effects
                    chain: root._chain
                    packParameters: root._params
                    onChainChangeRequested: function (newChain) {
                        if (root.bridge)
                            root.bridge.setChain(root.surfacePath, newChain);
                    }
                    onParamChangeRequested: function (packId, paramId, value) {
                        if (root.bridge)
                            root.bridge.setChainParam(root.surfacePath, packId, paramId, value);
                    }
                    onParamsRandomizeRequested: function (packId, rolled) {
                        if (root.bridge)
                            root.bridge.setChainParams(root.surfacePath, packId, rolled);
                    }
                }
            }
        }
    }
}
