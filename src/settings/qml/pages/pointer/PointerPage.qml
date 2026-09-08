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
 * that paint around the cursor, each one a PointerLayerCard.
 *
 * ## Not a decoration surface
 *
 * This page sits beside the decoration surface pages in the navigation because
 * that is where a user looks for it, but it is its own config domain: the two
 * keys `Pointer.Enabled` and `Pointer.Chain`, owned by this page alone. So a
 * Reset here resets the pointer and nothing else, where a decoration page's
 * Reset acts on its subtree of the shared profile tree.
 *
 * ## Reactive-latch, not bindings over invokables
 *
 * The chain comes from a Q_INVOKABLE, which QML records no dependency on, so
 * the page refreshes imperatively from `chainChanged` and `shaderEffectsChanged`
 * rather than binding a function call that would never re-evaluate. Same
 * pattern as DecorationSurfaceCard.refreshFromTree.
 *
 * The whole list is rebuilt on every write rather than mutating one delegate,
 * because every card addresses its layer by INDEX and a reorder or a removal
 * moves every index after it.
 */
SettingsFlickable {
    id: root

    readonly property var bridge: settingsController.pointerPage

    // ── Reactive model state ─────────────────────────────────────────────
    property var chainLayers: []
    property var availableEffects: []

    /// True while the settings window is frontmost. Folded into every card's
    /// preview clock, so a backgrounded window does not keep a 60 Hz tick and
    /// a shader pass running for a page nobody is looking at.
    readonly property bool appActive: Qt.application.state === Qt.ApplicationActive

    function refreshChain() {
        if (!root.bridge)
            return;
        root.chainLayers = root.bridge.chain();
    }

    // The pack catalogue moves only on install / uninstall, so it is not
    // re-read on every chain write — refreshChain runs for each slider tick,
    // and availableShaderEffects materialises a map per installed pack.
    function refreshEffects() {
        if (!root.bridge)
            return;
        root.availableEffects = root.bridge.availableShaderEffects();
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

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    type: Kirigami.MessageType.Information
                    visible: true
                    text: i18n("Packs paint in list order, so a layer lower in the list paints over the ones above it.")
                }

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    visible: root.chainLayers.length === 0
                    text: root.availableEffects.length > 0 ? i18n("No packs on the pointer yet. Add one below.") : i18n("No pointer packs installed.")
                    font.italic: true
                    color: Kirigami.Theme.disabledTextColor
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: root.chainLayers

                    delegate: PointerLayerCard {
                        required property var modelData
                        required property int index

                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        layerIndex: index
                        layerData: modelData
                        layerCount: root.chainLayers.length
                        availableEffects: root.availableEffects
                        animating: root.appActive
                    }
                }

                // ── Add a pack ────────────────────────────────────────────
                // Every installed pack stays offered, including ones already in
                // the chain: stacking a pack twice at different settings is a
                // real look here, which is also why the layers are addressed by
                // index rather than by pack id.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.smallSpacing

                    ComboBox {
                        id: packPicker

                        Layout.fillWidth: true
                        model: root.availableEffects
                        textRole: "name"
                        valueRole: "id"
                        enabled: root.availableEffects.length > 0
                        Accessible.name: i18n("Pointer pack to add")
                    }

                    Button {
                        icon.name: "list-add"
                        text: i18nc("@action add a pack to the pointer chain", "Add pack")
                        enabled: packPicker.enabled && packPicker.currentValue !== undefined
                        onClicked: {
                            if (root.bridge)
                                root.bridge.addLayer(packPicker.currentValue);
                        }
                    }
                }
            }
        }
    }
}
