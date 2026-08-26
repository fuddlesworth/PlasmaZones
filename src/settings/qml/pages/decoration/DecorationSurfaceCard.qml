// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Per-surface decoration override card. Mirrors AnimationEventCard.
 *
 * One card per surface path, every one an override card: the master toggle
 * opens the chain editor and writes NOTHING (the _editorLatch below — same
 * design as AnimationEventCard's timing latch); a per-surface override in
 * the DecorationProfileTree is created only when the user actually edits the
 * chain. OFF clears the override (reset to inherited — same as
 * AnimationEventCard, no separate reset button) and the card shows the
 * RESOLVED chain read-only with an "Inheriting from: …" breadcrumb.
 * CATEGORY paths (window, popup) and the standalone osd surface inherit
 * from the tree's BASELINE (empty by default), so their toggle doubles as
 * the category's decoration master switch: OFF renders the whole category
 * undecorated, matching the animations pages' top-level toggles. The
 * `shell` subtree is the exception: it is baseline-isolated
 * (DecorationSupportedPaths.h), inherits nothing, and stays undecorated
 * until a chain is engaged inside it. A category root additionally shows
 * the "applies to all children" cascade banner while editing. The
 * alwaysEnabled escape hatch (no toggle, always editing) remains for any
 * future surface that must never be disableable.
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

    // Routed through the C++ SSOT (decorationPathIsBaselineIsolated in
    // DecorationSupportedPaths.h) so a future isolated subtree cannot leave
    // this card claiming "Using global defaults": the shell subtree never
    // inherits the tree baseline.
    readonly property bool _baselineIsolated: root.bridge ? root.bridge.isBaselineIsolated(root.surfacePath) : false

    // Session-local "the user opened the chain editor" latch, mirroring
    // AnimationEventCard._editingTiming: flipping the toggle ON sets this and
    // writes NOTHING — an override is created only when the user actually
    // edits the chain. Without it the toggle had to commit a snapshot
    // override just to stay visually on, and an engaged (even empty) chain at
    // a category root suppresses the shipped seed decorations on its leaves.
    // Cleared by the toggle's OFF path, and by refresh() when an EXTERNAL
    // clear moves _hasOverride from true to false. Not persisted.
    property bool _editorLatch: false

    // True when this card edits its own DIRECT profile: an alwaysEnabled root
    // always does; a leaf when its override is engaged or the editor is
    // latched open for this session.
    readonly property bool _editing: root.alwaysEnabled || root._hasOverride || root._editorLatch

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
    property var _disabledPacks: []
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

    // The pack catalogue changes only on shaderEffectsChanged (install /
    // uninstall), so it is NOT re-read on every profile write — refresh()
    // runs on every built card for every tree edit, including each slider
    // drag tick, and availableShaderEffects() materialises a map per
    // installed pack.
    function _refreshEffects() {
        if (!root.bridge)
            return;
        root._effects = root.bridge.availableShaderEffects();
    }

    function refresh() {
        if (!root.bridge)
            return;
        var wasOverride = root._hasOverride;
        root._hasOverride = root.bridge.hasOverride(root.surfacePath);
        // EXTERNAL clear (a parent card's "Clear shadowing children", a page
        // reset/discard): a true→false transition closes the latched editor.
        // Our own OFF path already cleared the latch before the write, and our
        // own first edit moves the flag false→true, so a transition here is
        // never self-driven.
        if (wasOverride && !root._hasOverride)
            root._editorLatch = false;
        root._resolved = root.bridge.resolvedProfile(root.surfacePath);
        root._raw = root.bridge.rawProfile(root.surfacePath);
        root._chain = root.bridge.chainAt(root.surfacePath);
        // Direct override when engaged, else the RESOLVED map — the editor's
        // other two inputs (chainAt, disabledPacksAt) are resolved too, and a
        // latched-open card with no override must show the inherited values
        // the surface is actually drawing with, not the pack schema defaults
        // (the C++ writers engage from the same resolved map, filtered to the
        // effective chain, on first edit — display-equivalent, since the
        // editor only indexes per-pack entries for packs in the chain).
        root._params = (root._raw && root._raw.parameters) ? root._raw.parameters : ((root._resolved && root._resolved.parameters) ? root._resolved.parameters : ({}));
        root._disabledPacks = root.bridge.disabledPacksAt(root.surfacePath);
        root._parentChainText = root._computeParentChainText();
        root._shadowingChildrenCount = root.bridge.overrideDescendantCount(root.surfacePath);
    }

    function _resolvedSummary() {
        var c = root._resolved && root._resolved.chain ? root._resolved.chain : [];
        // Bare value: the sole caller wraps this in a "Current: %1" label, so a
        // "Packs: " prefix here would render the doubled "Current: Packs: ...".
        return c.length > 0 ? root._packNames(c).join(", ") : i18n("None");
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
    Component.onCompleted: {
        root._refreshEffects();
        root.refresh();
    }

    Connections {
        target: root.bridge
        function onProfilesChanged() {
            root.refresh();
        }
        function onShaderEffectsChanged() {
            root._refreshEffects();
            root.refresh();
        }
    }

    SettingsCard {
        id: card

        anchors.fill: parent
        headerText: root.cardLabel
        collapsible: true
        // alwaysEnabled roots have nothing to inherit, so no override toggle —
        // mirrors AnimationEventCard's alwaysEnabled global root.
        showToggle: !root.alwaysEnabled
        toggleChecked: root._editing
        // The toggle REPORTS override state; it is not a precondition for the
        // body. Gating the body on it would disable every row while OFF —
        // including the "Clear shadowing children" action on a parent card,
        // which is needed exactly when the parent has no override of its own.
        // Same rationale as AnimationEventCard.
        gateBodyOnToggle: false
        onToggleClicked: function (checked) {
            if (checked) {
                // Open the chain editor, write nothing. An override is created
                // only when the user edits the chain — committing a snapshot
                // here pinned an (often empty) chain override the user never
                // asked for, and an engaged chain at a category root
                // suppresses the shipped seed decorations on its leaves.
                root._editorLatch = true;
            } else {
                root._editorLatch = false;
                if (root.bridge)
                    root.bridge.clearOverride(root.surfacePath);
            }
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            // ── Inheritance summary ───────────────────────────────────────
            // Category roots show the cascade banner; leaf cards show the
            // inheritance breadcrumb when not overriding — same split as
            // AnimationEventCard.
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                type: Kirigami.MessageType.Information
                visible: root.isParentNode ? root._editing : !root._editing
                text: {
                    if (root.isParentNode)
                        return i18n("Settings here apply to all child surfaces unless individually overridden.");
                    // Baseline-isolated surfaces (the shell subtree) inherit
                    // nothing, so with an EMPTY resolved chain there is no
                    // decoration flowing in from anywhere and the inheritance
                    // breadcrumb below would be a false claim. Once a chain IS
                    // engaged at an ancestor (e.g. at "shell"), the resolved
                    // chain is non-empty and the breadcrumb is the truth again.
                    var resolvedChain = (root._resolved && root._resolved.chain) ? root._resolved.chain : [];
                    if (root._baselineIsolated && resolvedChain.length === 0)
                        return i18n("Not decorated. Add a decoration pack to style this surface.");
                    if (root._parentChainText.length > 0)
                        return i18n("Inheriting from: %1", root._parentChainText);
                    return i18n("Using global defaults");
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
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
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
                // Inset to match the rows / banners in this card instead of
                // hugging the left edge.
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                visible: !root._editing
                text: i18n("Current: %1", root._resolvedSummary())
                font.italic: true
                color: Kirigami.Theme.disabledTextColor
                wrapMode: Text.WordWrap
            }

            // ── Override editor ───────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                visible: root._editing
                spacing: Kirigami.Units.largeSpacing

                ChainEditor {
                    Layout.fillWidth: true
                    availableShaders: root._effects
                    chain: root._chain
                    packParameters: root._params
                    disabledPacks: root._disabledPacks
                    onChainChangeRequested: function (newChain) {
                        if (root.bridge)
                            root.bridge.setChain(root.surfacePath, newChain);
                    }
                    onLayerEnabledChangeRequested: function (packId, enabled) {
                        if (root.bridge)
                            root.bridge.setChainLayerEnabled(root.surfacePath, packId, enabled);
                    }
                    onParamChangeRequested: function (packId, paramId, value) {
                        if (root.bridge)
                            root.bridge.setChainParam(root.surfacePath, packId, paramId, value);
                    }
                    onParamsRandomizeRequested: function (packId, rolled) {
                        if (root.bridge)
                            root.bridge.setChainParams(root.surfacePath, packId, rolled);
                    }
                    onParamsResetRequested: function (packId, defaults) {
                        if (root.bridge)
                            root.bridge.setChainParams(root.surfacePath, packId, defaults);
                    }
                }
            }
        }
    }
}
