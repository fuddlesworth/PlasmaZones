// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Compact shader card for the Animations → Shaders grid.
 *
 * Renders one effect as a fixed-width card: preview thumbnail (or a
 * theme-iconography placeholder when the pack didn't ship one), name
 * + badges row, two-line description, and a small footer chip showing
 * parameter count and an event-usage count. Clicking the card emits
 * `clicked(effect)` so the host can pop the detail dialog.
 *
 * Required:
 *   - `effect`: var — effect map from `availableShaderEffects()`.
 *
 * Optional:
 *   - `usagesRev`: int — host-owned tick that invalidates the
 *      `shaderEffectUsages(id)` Q_INVOKABLE result on registry /
 *      override mutations. Forwarded into the binding's dependency set.
 */
ItemDelegate {
    id: root

    required property var effect
    property int usagesRev: 0

    signal showDetails(var effect)

    // Card geometry — fixed `width` (not `implicitWidth`) so the Flow
    // host doesn't stretch cards based on enclosing-Layout fillWidth
    // propagation. Height is content-driven so cards without preview
    // images don't carry empty placeholder padding.
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

    // ItemDelegate's default contentItem is a Label; override with our
    // own column so we get full control over the card body.
    contentItem: ColumnLayout {
        id: cardLayout

        spacing: Kirigami.Units.smallSpacing

        // ── Preview thumbnail (only when the pack ships one) ──────
        // Skip the preview area entirely for packs without
        // `previewPath`. A placeholder iconography slot dominates the
        // card visually for no real information gain — text content
        // alone is enough when there's nothing to show.
        Rectangle {
            readonly property bool _hasPreview: root.effect && root.effect.previewPath && root.effect.previewPath.length > 0

            Layout.fillWidth: true
            Layout.preferredHeight: width * 9 / 16 // 16:9 aspect
            visible: _hasPreview
            radius: Kirigami.Units.smallSpacing
            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)
            border.width: Math.max(1, Math.round(Kirigami.Units.devicePixelRatio))
            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.12)
            clip: true

            Image {
                anchors.fill: parent
                anchors.margins: Math.max(1, Math.round(Kirigami.Units.devicePixelRatio))
                source: parent._hasPreview ? "file://" + root.effect.previewPath : ""
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: width * 2
                sourceSize.height: height * 2
                asynchronous: true
                cache: true
                visible: status === Image.Ready
            }

        }

        // ── Name row ──────────────────────────────────────────────
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

        // ── Description (max 2 lines) ─────────────────────────────
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

        // ── Footer chips: parameter count + Used-in count ─────────
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

            // "N events" chip — visible only when this shader is
            // assigned somewhere. Tooltip carries the full label list;
            // the detail dialog renders them inline. KI18n's `%n` is
            // the canonical placeholder for the count in plural strings;
            // `%1` here would render as a literal because the second
            // count argument doesn't substitute back into the singular/
            // plural template.
            Label {
                readonly property var _usages: {
                    root.usagesRev; // reactive dep
                    var id = root.effect ? root.effect.id : "";
                    if (!id || id.length === 0)
                        return [];

                    return settingsController.animationsPage.shaderEffectUsages(id);
                }

                visible: _usages.length > 0
                text: i18ncp("@info shader usage count", "%n event", "%n events", _usages.length)
                font: Kirigami.Theme.smallFont
                // Match the parameter-count's dim treatment — the chip
                // is a count, not an alert; positiveTextColor was too
                // attention-grabbing for a passive metadata item.
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
