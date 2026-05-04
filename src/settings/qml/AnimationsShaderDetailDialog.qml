// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * @brief Detail view for a single shader effect.
 *
 * Triggered by clicking a card in the shader catalogue. Shows the same
 * data the card teases (name, badges, description, parameter count,
 * Used-in count) in full detail — large preview, complete description,
 * full Used-in list, and the entire parameter list using the same
 * compact one-line-per-param format the catalogue uses.
 *
 * ## Layout
 *
 * The dialog is structured as a vertical column of *sections*:
 *
 *   1. Optional preview thumbnail (full-width, 16:9).
 *   2. Header row — category pill, "User" badge, parameter count chip.
 *   3. Description (full text, no truncation).
 *   4. Author / version line (italic, dimmed).
 *   5. "Used in: …" list (only when the shader has direct overrides).
 *   6. Parameters section — `Kirigami.Heading` + `Kirigami.Separator`
 *      then one row per parameter.
 *
 * Each section is followed by a `Kirigami.Separator` so the visual
 * groups read as distinct, not as a wall of stacked labels. Sections
 * with no content (no description, no Used-in, no parameters) are
 * hidden along with their trailing separator so the dialog tightens
 * around what's actually shown.
 *
 * Required:
 *   - `effect`: var — set by the host before calling `open()`. The
 *      dialog reads from this directly; no separate "load" step.
 *
 * Optional:
 *   - `usagesRev`: int — host-owned tick that invalidates the
 *      `shaderEffectUsages(id)` Q_INVOKABLE result. Without it, the
 *      Used-in list goes stale after any override mutation.
 */
Kirigami.Dialog {
    // No `parent: root.Window.window.contentItem` reparenting —
    // `Window.window` only works on Items, not Popups. Kirigami.Dialog
    // (a Popup) handles its own modal parenting against the application
    // window automatically.

    id: root

    property var effect: null
    property int usagesRev: 0
    readonly property string _description: effect && typeof effect.description === "string" ? effect.description : ""
    readonly property string _author: effect && effect.author && effect.author.length > 0 ? effect.author : ""
    readonly property string _version: effect && effect.version && effect.version.length > 0 ? effect.version : ""
    readonly property var _usages: {
        usagesRev; // reactive dep
        var id = effect ? effect.id : "";
        if (!id || id.length === 0)
            return [];

        return settingsController.animationsPage.shaderEffectUsages(id);
    }
    readonly property bool _hasParameters: effect && effect.parameters && effect.parameters.length > 0

    // Format helper shared between the param-row text segments.
    function _fmt(v) {
        if (typeof v === "number")
            return Number.isInteger(v) ? String(v) : v.toFixed(2);

        if (typeof v === "boolean")
            return v ? i18nc("@info bool true", "Yes") : i18nc("@info bool false", "No");

        return String(v);
    }

    title: effect ? (effect.name || effect.id || "") : ""
    // Both `preferredWidth` AND `preferredHeight` are required —
    // `Kirigami.Dialog` collapses its content area to zero when either
    // is unset. Bind preferredHeight to the actual content's implicit
    // height plus a chrome estimate (title bar + padding + footer)
    // so the dialog tightens around what's actually shown — capped at
    // 85% of the host item's height so it never overflows.
    preferredWidth: Kirigami.Units.gridUnit * 36
    preferredHeight: {
        var content = contentColumn.implicitHeight + Kirigami.Units.gridUnit * 6;
        var hostMax = parent ? parent.height * 0.85 : Kirigami.Units.gridUnit * 32;
        return Math.min(content, hostMax);
    }
    standardButtons: Kirigami.Dialog.Close
    padding: Kirigami.Units.largeSpacing

    ColumnLayout {
        id: contentColumn

        Layout.fillWidth: true
        spacing: Kirigami.Units.largeSpacing

        // ── 1. Preview ──────────────────────────────────────────────
        Rectangle {
            readonly property bool _hasPreview: root.effect && root.effect.previewPath && root.effect.previewPath.length > 0

            Layout.fillWidth: true
            Layout.preferredHeight: width * 9 / 16
            visible: _hasPreview
            radius: Kirigami.Units.smallSpacing
            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)
            border.width: Math.max(1, Math.round(Kirigami.Units.devicePixelRatio))
            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
            clip: true

            Image {
                anchors.fill: parent
                anchors.margins: Math.max(1, Math.round(Kirigami.Units.devicePixelRatio))
                source: parent._hasPreview ? "file://" + root.effect.previewPath : ""
                fillMode: Image.PreserveAspectFit
                sourceSize.width: width * 2
                sourceSize.height: height * 2
                asynchronous: true
                cache: true
                visible: status === Image.Ready
            }

        }

        // ── 2. Header row: category pill + badges + parameter count ─
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            // Category pill (matches the card aesthetic). Explicit
            // Layout.preferredWidth/Height ensures the RowLayout
            // gives it space — a Rectangle's `implicitWidth` alone
            // sometimes loses the layout negotiation when siblings
            // declare `Layout.fillWidth`.
            Rectangle {
                visible: root.effect && root.effect.category && root.effect.category.length > 0
                radius: height / 2
                color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.18)
                Layout.preferredWidth: categoryLabel.implicitWidth + Kirigami.Units.largeSpacing
                Layout.preferredHeight: categoryLabel.implicitHeight + Kirigami.Units.smallSpacing
                Layout.alignment: Qt.AlignVCenter

                Label {
                    id: categoryLabel

                    anchors.centerIn: parent
                    text: root.effect ? (root.effect.category || "") : ""
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.highlightColor
                }

            }

            Label {
                visible: root.effect && root.effect.isUserEffect
                text: i18nc("@info shader source badge", "User")
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.positiveTextColor
            }

            Item {
                Layout.fillWidth: true
            }

            Label {
                visible: root._hasParameters
                text: i18np("%n parameter", "%n parameters", root.effect ? (root.effect.parameters || []).length : 0)
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
            }

        }

        // ── 3. Description + author/version ────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            visible: root._description.length > 0 || root._author.length > 0 || root._version.length > 0
            spacing: Kirigami.Units.smallSpacing

            Label {
                visible: root._description.length > 0
                Layout.fillWidth: true
                text: root._description
                wrapMode: Text.WordWrap
            }

            Label {
                visible: root._author.length > 0 || root._version.length > 0
                Layout.fillWidth: true
                text: {
                    var parts = [];
                    if (root._author.length > 0)
                        parts.push(i18nc("@info shader author", "by %1", root._author));

                    if (root._version.length > 0)
                        parts.push(i18nc("@info shader version", "v%1", root._version));

                    return parts.join(" · ");
                }
                color: Kirigami.Theme.disabledTextColor
                font.italic: true
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                elide: Text.ElideRight
            }

        }

        // ── 4. Used-in section ─────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            visible: root._usages.length > 0
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: "checkmark"
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                    color: Kirigami.Theme.positiveTextColor
                }

                Label {
                    text: i18ncp("@info shader usage section header", "Used in %n event", "Used in %n events", root._usages.length)
                    font.weight: Font.DemiBold
                }

            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                text: {
                    var labels = [];
                    for (var i = 0; i < root._usages.length; i++) labels.push(root._usages[i].label || root._usages[i].path)
                    return labels.join(", ");
                }
                color: Kirigami.Theme.disabledTextColor
                font: Kirigami.Theme.smallFont
                wrapMode: Text.WordWrap
            }

        }

        // ── 5. Parameters section ──────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            visible: root._hasParameters
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            Kirigami.Heading {
                text: i18nc("@title:group shader parameters section", "Parameters")
                level: 4
            }

            Repeater {
                model: root.effect && root.effect.parameters ? root.effect.parameters : []

                // Each row is a Rectangle (the alternating-tint
                // container) directly under the ColumnLayout, with a
                // RowLayout anchored inside it. Putting the Rectangle
                // *as the row* — not as a sibling of the RowLayout —
                // avoids the "anchors on a layout-managed item" warning
                // QML flags when an anchored child sits inside a
                // RowLayout / ColumnLayout.
                delegate: Rectangle {
                    id: paramRow

                    required property var modelData
                    required property int index

                    Layout.fillWidth: true
                    implicitHeight: rowContent.implicitHeight + Kirigami.Units.smallSpacing
                    radius: Kirigami.Units.smallSpacing / 2
                    // Subtle alternating-row tint — improves
                    // scannability on long parameter lists (e.g.
                    // hexagon's 13 params) without the heavy striping
                    // a ListView would impose.
                    color: index % 2 === 0 ? "transparent" : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.04)

                    RowLayout {
                        id: rowContent

                        anchors.fill: parent
                        anchors.leftMargin: Kirigami.Units.smallSpacing
                        anchors.rightMargin: Kirigami.Units.smallSpacing
                        spacing: Kirigami.Units.smallSpacing

                        // Fixed-width name column so metadata aligns
                        // across rows — much more scannable than a
                        // self-sizing label per row.
                        Label {
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                            Layout.alignment: Qt.AlignVCenter
                            text: paramRow.modelData ? (paramRow.modelData.name || paramRow.modelData.id || "") : ""
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            text: {
                                var p = paramRow.modelData;
                                if (!p)
                                    return "";

                                var parts = [];
                                if (p.type && p.type.length > 0)
                                    parts.push(p.type);

                                if (p.min !== undefined && p.max !== undefined)
                                    parts.push(i18nc("@info parameter range", "[%1 .. %2]", root._fmt(p.min), root._fmt(p.max)));

                                if (p.default !== undefined)
                                    parts.push(i18nc("@info parameter default value", "default %1", root._fmt(p.default)));

                                return parts.join(" · ");
                            }
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                            elide: Text.ElideRight
                        }

                    }

                }

            }

        }

    }

}
