// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Compact shader card for a shader-browser grid.
 *
 * Renders one effect as a fixed-width card: preview thumbnail (or an
 * empty area when the pack didn't ship one), name + badges row, two-line
 * description, and a small footer chip showing parameter count and a
 * usage count. Clicking the card emits `showDetails(effect)`.
 *
 * Pack-agnostic: drives both the animation-shaders browser and the
 * snapping-overlay-shaders browser. The host passes a `bridge` property
 * exposing `shaderEffectUsages(id)` so the "Used in:" chip works for
 * either domain (per-event paths for animations, per-layout names for
 * snapping overlays).
 *
 * Required:
 *   - `effect`: var — effect map (id, name, description, parameters,
 *      previewPath, isUserEffect, ...).
 *   - `bridge`: QtObject — exposes `shaderEffectUsages(id)`.
 *
 * Optional:
 *   - `usagesRev`: int — host-owned tick that invalidates the
 *      `shaderEffectUsages(id)` Q_INVOKABLE result on registry /
 *      override mutations. Forwarded into the binding's dependency set.
 *   - `usageChipTextFn`: function(count) → string — domain-tuned copy
 *      for the small "N use(s)" chip in the card footer. Host passes a
 *      closure that calls `i18ncp(..., count)` with the LIVE count so
 *      the right plural form is picked per locale, and so the chip
 *      stays consistent with the dialog header's wording (animations
 *      use "event"; snapping uses "layout"). Default: a generic
 *      "%n use" / "%n uses".
 */
ItemDelegate {
    id: root

    required property var effect
    required property var bridge
    property int usagesRev: 0
    property var usageChipTextFn: function(count) {
        return i18ncp("@info shader usage count", "%n use", "%n uses", count);
    }

    signal showDetails(var effect)

    width: Kirigami.Units.gridUnit * 14
    implicitWidth: width
    implicitHeight: cardLayout.implicitHeight + topPadding + bottomPadding
    hoverEnabled: true
    Accessible.name: effect ? (effect.name || effect.id || "") : ""
    Accessible.description: effect && effect.description ? effect.description : i18nc("@info:tooltip generic shader card", "Shader effect details")
    onClicked: {
        if (effect)
            root.showDetails(effect);

    }

    background: Rectangle {
        radius: Kirigami.Units.smallSpacing
        color: root.hovered ? Kirigami.Theme.hoverColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.04)
        border.width: Math.max(1, Math.round(Kirigami.Units.devicePixelRatio))
        border.color: root.activeFocus ? Kirigami.Theme.focusColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.12)
    }

    contentItem: ColumnLayout {
        id: cardLayout

        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            readonly property bool _hasPreview: root.effect && root.effect.previewPath && root.effect.previewPath.length > 0

            Layout.fillWidth: true
            Layout.preferredHeight: width * 9 / 16
            visible: _hasPreview
            radius: Kirigami.Units.smallSpacing
            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)
            border.width: Math.max(1, Math.round(Kirigami.Units.devicePixelRatio))
            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.12)
            clip: true

            Image {
                anchors.fill: parent
                anchors.margins: Math.max(1, Math.round(Kirigami.Units.devicePixelRatio))
                // `encodeURI` percent-encodes spaces and unicode while
                // preserving path separators, which a raw `"file://" + path`
                // concat would silently break on (e.g. user-installed packs
                // under `~/My Shaders/`).
                source: parent._hasPreview ? "file://" + encodeURI(root.effect.previewPath) : ""
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: width * 2
                sourceSize.height: height * 2
                asynchronous: true
                cache: true
                visible: status === Image.Ready
            }

        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Label {
                Layout.fillWidth: true
                text: root.effect ? (root.effect.name || root.effect.id || "") : ""
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Label {
                visible: root.effect && root.effect.isUserEffect
                text: i18nc("@info shader source badge", "User")
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.positiveTextColor
            }

        }

        Label {
            readonly property string _description: root.effect && typeof root.effect.description === "string" ? root.effect.description : ""

            Layout.fillWidth: true
            Layout.preferredHeight: visible ? Kirigami.Units.gridUnit * 2 : 0
            visible: _description.length > 0
            text: _description
            color: Kirigami.Theme.disabledTextColor
            font: Kirigami.Theme.smallFont
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            verticalAlignment: Text.AlignTop
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Label {
                text: i18np("%n parameter", "%n parameters", (root.effect && root.effect.parameters) ? root.effect.parameters.length : 0)
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
            }

            Item {
                Layout.fillWidth: true
            }

            // Usage chip — visible only when this shader is assigned
            // somewhere. Tooltip carries the full list; the detail dialog
            // renders them inline.
            Label {
                readonly property var _usages: {
                    root.usagesRev; // reactive dep
                    var id = root.effect ? root.effect.id : "";
                    if (!id || id.length === 0 || !root.bridge)
                        return [];

                    return root.bridge.shaderEffectUsages(id);
                }

                visible: _usages.length > 0
                text: root.usageChipTextFn(_usages.length)
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
                ToolTip.visible: hovered.hovered && _usages.length > 0
                ToolTip.text: {
                    var names = [];
                    for (var i = 0; i < _usages.length; i++) names.push(_usages[i].label || _usages[i].path)
                    return names.join(", ");
                }
                ToolTip.delay: Kirigami.Units.toolTipDelay

                HoverHandler {
                    id: hovered
                }

            }

        }

    }

}
