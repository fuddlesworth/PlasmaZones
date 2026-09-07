// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

/**
 * @brief One layer of the pointer chain.
 *
 * A collapsible card carrying the pack's name and description, an enable
 * toggle, reorder and remove actions, a live preview of this pack alone, and
 * its parameter editor.
 *
 * ## Addressed by index, not by pack id
 *
 * Unlike a decoration chain, a pointer chain may legitimately stack the same
 * pack twice (two trails at different widths and colours is a real look), so
 * the pack id is not a key. Every write this card makes goes to
 * `layerIndex`, and the page rebuilds the whole list after each one rather
 * than mutating a delegate in place, which is what keeps the indices and the
 * stored chain in step.
 *
 * ## The card owns no state
 *
 * Values come from `layerData` (the controller's merged pack-defaults-under-
 * user-overrides map) and every edit is written straight through the bridge.
 * The parameter editor's lock map is the one exception, and it is session-only
 * working state the shared editor owns itself.
 */
Item {
    id: root

    /// Position in the chain. Every bridge write targets it.
    required property int layerIndex
    /// The controller's row for this layer: effectId, name, enabled,
    /// parameters, missing.
    required property var layerData
    /// Total layers, so the last card knows to disable its "move down".
    required property int layerCount
    /// The installed-pack catalogue, so the card can resolve this pack's
    /// description and parameter schema without a second bridge call per
    /// delegate.
    required property var availableEffects
    /// Whether the preview should tick. The page hands down its own focus
    /// gate, so a backgrounded settings window stops every card's clock.
    property bool animating: true

    readonly property var bridge: settingsController.pointerPage

    readonly property string _effectId: layerData ? (layerData.effectId || "") : ""
    readonly property bool _missing: layerData ? layerData.missing === true : false
    readonly property var _values: (layerData && layerData.parameters) ? layerData.parameters : ({})

    readonly property var _effect: {
        for (var i = 0; i < root.availableEffects.length; i++) {
            if (root.availableEffects[i] && root.availableEffects[i].id === root._effectId)
                return root.availableEffects[i];
        }
        return null;
    }
    readonly property var _schema: (root._effect && root._effect.parameters) ? root._effect.parameters : []
    readonly property string _description: (root._effect && root._effect.description) ? root._effect.description : ""

    implicitHeight: card.implicitHeight

    SettingsCard {
        id: card

        anchors.fill: parent
        headerText: root.layerData ? (root.layerData.name || root._effectId) : ""
        // The layer's position, so two cards for the same pack are tellable
        // apart at a glance rather than only by their parameters.
        headerTrailingText: i18nc("@info position of a layer in the pointer chain", "Layer %1", root.layerIndex + 1)
        collapsible: true
        initiallyCollapsed: true
        showToggle: true
        toggleChecked: root.layerData ? root.layerData.enabled === true : false
        // The toggle REPORTS whether this layer paints; it is not a
        // precondition for editing it. Gating the body on it would lock the
        // parameters of a layer the user has parked, which is exactly when
        // they want to tune it before switching it back on.
        gateBodyOnToggle: false
        onToggleClicked: function (checked) {
            if (root.bridge)
                root.bridge.setLayerEnabled(root.layerIndex, checked);
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                type: Kirigami.MessageType.Warning
                visible: root._missing
                // The layer is kept rather than dropped, so reinstalling the
                // pack restores the look with its parameters intact.
                text: i18n("The pack “%1” is not installed. This layer will not paint until you install it again.", root._effectId)
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                visible: root._description.length > 0
                text: root._description
                wrapMode: Text.WordWrap
                color: Kirigami.Theme.disabledTextColor
            }

            // Reorder and remove. Reordering the chain changes what paints on
            // top of what, so it belongs with the layer rather than on a
            // separate list-management surface.
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.smallSpacing

                ToolButton {
                    icon.name: "arrow-up"
                    enabled: root.layerIndex > 0
                    text: i18nc("@action move a pointer layer earlier in the chain", "Move up")
                    display: AbstractButton.IconOnly
                    Accessible.name: text
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    onClicked: {
                        if (root.bridge)
                            root.bridge.moveLayer(root.layerIndex, root.layerIndex - 1);
                    }
                }

                ToolButton {
                    icon.name: "arrow-down"
                    enabled: root.layerIndex < root.layerCount - 1
                    text: i18nc("@action move a pointer layer later in the chain", "Move down")
                    display: AbstractButton.IconOnly
                    Accessible.name: text
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    onClicked: {
                        if (root.bridge)
                            root.bridge.moveLayer(root.layerIndex, root.layerIndex + 1);
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                ToolButton {
                    icon.name: "list-remove"
                    text: i18nc("@action remove a pointer layer", "Remove")
                    onClicked: {
                        if (root.bridge)
                            root.bridge.removeLayer(root.layerIndex);
                    }
                }
            }

            // This pack alone, over a stand-in desktop with a simulated
            // cursor. Only the layer the user has open renders, so a long
            // chain does not run one shader pass per card.
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                Layout.preferredHeight: Kirigami.Units.gridUnit * 12
                visible: !root._missing
                radius: Kirigami.Units.smallSpacing
                color: Kirigami.Theme.alternateBackgroundColor
                border.width: 1
                border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
                clip: true

                PointerPreviewCanvas {
                    id: preview

                    anchors.fill: parent
                    anchors.margins: 1
                    previewController: root.bridge ? root.bridge.previewController : null
                    packId: root._effectId
                    params: root._values
                    // Only while the card is open: a collapsed card's preview
                    // would compile a shader and run a 60 Hz clock for
                    // something nobody can see.
                    active: !card.collapsed && card.bodyLive && !root._missing
                    animating: root.animating
                }

                PZCommon.ShaderPreviewPlaceholder {
                    anchors.fill: parent
                    anchors.margins: 1
                    visible: !preview.previewable || !preview.showable || preview.hasError
                    text: preview.hasError ? i18nc("@info:placeholder shader preview", "This pack's shader did not compile.") : i18nc("@info:placeholder shader preview", "Preview unavailable")
                    backgroundColor: Kirigami.Theme.alternateBackgroundColor
                    radius: Kirigami.Units.smallSpacing
                }
            }

            PZCommon.ShaderParamsEditor {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                visible: root._schema.length > 0
                compact: true
                enableImage: true
                effectId: root._effectId
                parameters: root._schema
                currentValues: root._values
                onValueChanged: function (effectId, paramId, value) {
                    if (root.bridge)
                        root.bridge.setLayerParam(root.layerIndex, paramId, value);
                }
                // One batched write per roll, so a randomize is a single
                // undoable edit rather than one per parameter.
                onRandomizeRequested: function (rolled) {
                    if (root.bridge)
                        root.bridge.setLayerParams(root.layerIndex, rolled);
                }
                // Clears the stored overrides rather than writing the defaults
                // back, so the layer keeps tracking the pack across an update
                // the same way a freshly added one does.
                onResetRequested: {
                    if (root.bridge)
                        root.bridge.resetLayerParams(root.layerIndex);
                }
            }
        }
    }
}
