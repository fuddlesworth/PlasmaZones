// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

/**
 * @brief One overlay shader assignment card: the global default (path "")
 *        or a per-layout override (path = layout UUID).
 *
 * Mirrors DecorationSurfaceCard's shape over the flat OverlayShaderTree:
 * the baseline card (isBaseline) has no toggle and always edits the
 * global default; a layout card's toggle opens the editor and writes
 * NOTHING (the _editorLatch pattern) — an override is created only on the
 * first real edit. OFF clears the override so the layout inherits the
 * global default again, and the card shows the resolved shader read-only
 * with a "Using global default" banner.
 *
 * Reactive-latch pattern: imperative refresh from the controller on
 * `shaderProfileChanged` / `shaderEffectsChanged`, not function bindings
 * that re-query C++ every repaint.
 */
Item {
    id: root

    /// "" for the global default, a layout UUID (with braces) otherwise.
    required property string assignmentPath
    required property string cardLabel
    property bool isBaseline: false

    readonly property var bridge: settingsController.snappingShadersPage

    // Session-local "the user opened the editor" latch — see
    // DecorationSurfaceCard._editorLatch for the full rationale.
    property bool _editorLatch: false
    readonly property bool _editing: root.isBaseline || root._hasOverride || root._editorLatch

    // ── Reactive model state ─────────────────────────────────────────────
    property var _effects: []
    property bool _hasOverride: false
    property var _raw: ({})
    property var _resolved: ({})

    // The node the editor works on: the direct node when it exists, else
    // the resolved (inherited) node — a latched-open card with no override
    // starts from what the layout is actually drawing with.
    readonly property string _editShaderId: (root._hasOverride || root.isBaseline) ? (root._raw.shaderId || "") : (root._resolved.shaderId || "")
    readonly property var _editParams: (root._hasOverride || root.isBaseline) ? (root._raw.parameters || ({})) : (root._resolved.parameters || ({}))

    function _refreshEffects() {
        if (!root.bridge)
            return;
        root._effects = root.bridge.availableShaderEffects();
    }

    function refresh() {
        if (!root.bridge)
            return;
        var wasOverride = root._hasOverride;
        root._hasOverride = root.bridge.hasOverride(root.assignmentPath);
        // An EXTERNAL clear (page reset/discard, a D-Bus write) closes the
        // latched editor; our own OFF path clears the latch before writing.
        if (wasOverride && !root._hasOverride)
            root._editorLatch = false;
        root._raw = root.bridge.rawShaderProfile(root.assignmentPath);
        root._resolved = root.bridge.resolvedShaderProfile(root.assignmentPath);
    }

    function _shaderName(id) {
        if (!id || id.length === 0)
            return i18n("None");
        for (var i = 0; i < root._effects.length; i++) {
            if (root._effects[i] && root._effects[i].id === id)
                return root._effects[i].name;
        }
        return i18n("(missing: %1)", id);
    }

    // Write the whole node for this path. A latched card with no override
    // creates one here (the first real edit).
    function _writeNode(shaderId, params) {
        if (!root.bridge)
            return;
        root.bridge.setShaderOverride(root.assignmentPath, shaderId, params);
    }

    implicitHeight: card.implicitHeight
    Component.onCompleted: {
        root._refreshEffects();
        root.refresh();
    }

    Connections {
        target: root.bridge
        function onShaderProfileChanged(path) {
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
        showToggle: !root.isBaseline
        toggleChecked: root._editing
        gateBodyOnToggle: false
        onToggleClicked: function (checked) {
            if (checked) {
                // Open the editor, write nothing — an override is created
                // only when the user actually picks a shader or edits a
                // parameter.
                root._editorLatch = true;
            } else {
                root._editorLatch = false;
                if (root.bridge)
                    root.bridge.clearOverride(root.assignmentPath);
            }
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                type: Kirigami.MessageType.Information
                visible: !root.isBaseline && !root._editing
                text: i18n("Using global default")
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                visible: !root._editing
                text: i18n("Current: %1", root._shaderName(root._resolved.shaderId || ""))
                font.italic: true
                color: Kirigami.Theme.disabledTextColor
                wrapMode: Text.WordWrap
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                visible: root._editing
                spacing: Kirigami.Units.largeSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.largeSpacing

                    Label {
                        text: i18n("Shader:")
                    }

                    PZCommon.CategoryMenuButton {
                        Layout.fillWidth: true
                        items: root._effects
                        currentId: root._editShaderId
                        includeNoneEntry: true
                        placeholderText: i18n("Choose an overlay shader…")
                        Accessible.name: root.isBaseline ? i18n("Global default overlay shader") : i18n("Overlay shader for %1", root.cardLabel)
                        onSelected: function (id) {
                            // Switching packs resets the parameters to the new
                            // pack's defaults (an empty override map).
                            root._writeNode(id, ({}));
                        }
                    }
                }

                PZCommon.ShaderParamsEditor {
                    Layout.fillWidth: true
                    visible: root._editShaderId.length > 0
                    parameters: root.bridge ? root.bridge.shaderParameters(root._editShaderId) : []
                    currentValues: root._editParams
                    effectId: root._editShaderId
                    enableLocking: true
                    enableRandomize: true
                    enableImage: false
                    compact: true
                    onValueChanged: function (effectId, paramId, value) {
                        var next = ({});
                        var existing = root._editParams;
                        for (var k in existing)
                            next[k] = existing[k];
                        next[paramId] = value;
                        root._writeNode(root._editShaderId, next);
                    }
                    onRandomizeRequested: function (rolled) {
                        root._writeNode(root._editShaderId, rolled);
                    }
                    onResetRequested: function (defaults) {
                        root._writeNode(root._editShaderId, defaults);
                    }
                }
            }
        }
    }
}
