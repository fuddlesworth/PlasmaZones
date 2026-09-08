// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Appearance → Pointer → Chain.
 *
 * The master switch plus the pointer chain: an ordered list of pointer packs
 * that paint around the cursor, edited through the shared ChainEditor.
 *
 * ## Not a decoration surface
 *
 * Pointer is its own section under Appearance, a peer of Animations and
 * Decorations, and its own config domain: the two keys `Pointer.Enabled` and
 * `Pointer.Chain`, owned by this page alone. So a Reset here resets the
 * pointer and nothing else, where a decoration page's Reset acts on its
 * subtree of the shared profile tree.
 *
 * ## The same chain editor as every other shader chain
 *
 * The chain is keyed by pack id and hosted by ChainEditor, wired exactly as
 * DecorationSurfaceCard wires it minus the surface path (there is one pointer
 * chain, so there is no path to pass). A pack therefore appears at most once,
 * which is what lets drag-to-reorder, the per-layer switch and the parameter
 * editor address a layer by id.
 *
 * ## Reactive-latch, not bindings over invokables
 *
 * The chain comes from a Q_INVOKABLE, which QML records no dependency on, so
 * the page refreshes imperatively from `chainChanged` and `shaderEffectsChanged`
 * rather than binding a function call that would never re-evaluate. Same
 * pattern as DecorationSurfaceCard.refresh.
 */
SettingsFlickable {
    id: root

    readonly property var bridge: settingsController.pointerPage

    // ── Reactive model state ─────────────────────────────────────────────
    property var _effects: []
    property var _chain: []
    property var _params: ({})
    property var _disabledPacks: []

    function refreshChain() {
        if (!root.bridge)
            return;
        root._chain = root.bridge.chain();
        root._params = root.bridge.chainParams();
        root._disabledPacks = root.bridge.disabledPacks();
    }

    // The pack catalogue moves only on install / uninstall, so it is not
    // re-read on every chain write — refreshChain runs for each slider tick,
    // and availableShaderEffects materialises a map per installed pack.
    function refreshEffects() {
        if (!root.bridge)
            return;
        root._effects = root.bridge.availableShaderEffects();
    }

    contentHeight: content.implicitHeight
    clip: true

    Component.onCompleted: {
        root.refreshEffects();
        root.refreshChain();
    }

    Connections {
        target: root.bridge

        function onChainChanged() {
            root.refreshChain();
        }
        function onShaderEffectsChanged() {
            root.refreshEffects();
            root.refreshChain();
        }
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        SettingsCard {
            headerText: i18n("Pointer")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Decorate the pointer")
                    searchAnchor: "pointerEnabled"
                    description: i18n("Paint trails, glows and click ripples around the mouse cursor.")

                    SettingsSwitch {
                        checked: root.bridge ? root.bridge.enabled : false
                        accessibleName: i18n("Decorate the pointer")
                        onToggled: function (newValue) {
                            if (root.bridge)
                                root.bridge.enabled = newValue;
                        }
                    }
                }
            }
        }

        SettingsCard {
            headerText: i18n("Pointer chain")
            searchAnchor: "pointerChain"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // Pointer-specific and true: the pointer chain paints its
                // layers in list order, which no decoration surface card has an
                // equivalent note for.
                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    type: Kirigami.MessageType.Information
                    visible: true
                    text: i18n("Packs paint in list order, so a layer lower in the list paints over the ones above it.")
                }

                ChainEditor {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    availableShaders: root._effects
                    chain: root._chain
                    packParameters: root._params
                    disabledPacks: root._disabledPacks
                    // Live preview inside each expanded layer row: the same
                    // stage the pack browser shows, on this page's controller.
                    previewKind: "pointer"
                    previewController: root.bridge ? root.bridge.previewController : null
                    // The component's defaults name decoration packs and "this
                    // surface's chain", neither of which exists here.
                    emptyChainText: i18n("No pointer packs.")
                    emptyChainAddHintText: i18n("No pointer packs. Add one below.")
                    addRowTitle: i18n("Add pointer pack")
                    addRowDescription: i18n("Stack another pack onto the cursor")
                    noPacksInstalledText: i18n("No pointer packs are installed")
                    addComboAccessibleDescription: i18n("Add a pointer pack to the cursor's chain")
                    onChainChangeRequested: function (newChain) {
                        if (root.bridge)
                            root.bridge.setChain(newChain);
                    }
                    onLayerEnabledChangeRequested: function (packId, enabled) {
                        if (root.bridge)
                            root.bridge.setChainLayerEnabled(packId, enabled);
                    }
                    onParamChangeRequested: function (packId, paramId, value) {
                        if (root.bridge)
                            root.bridge.setChainParam(packId, paramId, value);
                    }
                    onParamsRandomizeRequested: function (packId, rolled) {
                        if (root.bridge)
                            root.bridge.setChainParams(packId, rolled);
                    }
                    onParamsResetRequested: function (packId, defaults) {
                        if (root.bridge)
                            root.bridge.setChainParams(packId, defaults);
                    }
                }
            }
        }
    }
}
