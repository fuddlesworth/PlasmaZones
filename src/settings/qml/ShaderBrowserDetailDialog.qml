// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * @brief Detail view for a single shader effect (pack-agnostic).
 *
 * Triggered by clicking a card in a shader catalogue. Shows the same
 * data the card teases (name, badges, description, parameter count,
 * Used-in count) in full detail. Drives both the animation-shaders
 * browser and the snapping-overlay-shaders browser via a `bridge`
 * property exposing `shaderEffectUsages(id)`.
 *
 * Required:
 *   - `effect`: var — set by the host before calling `open()`.
 *   - `bridge`: QtObject — exposes `shaderEffectUsages(id)`.
 *
 * Optional:
 *   - `usagesRev`: int — host-owned tick that invalidates the
 *      `shaderEffectUsages(id)` Q_INVOKABLE result.
 *   - `usageHeaderTextFn`: function(count) → string — domain-tuned copy
 *      for the "Used in: …" section header. The host passes a closure
 *      that calls `i18ncp(..., count)` with the LIVE count so the
 *      correct plural form is selected per locale (Polish / Russian /
 *      Arabic etc. have plural forms beyond singular/plural). The
 *      default emits a generic "Used in N event(s)" — wrappers should
 *      override with their own context-specific copy.
 */
Kirigami.Dialog {
    id: root

    property var effect: null
    required property var bridge
    property int usagesRev: 0
    property var usageHeaderTextFn: function(count) {
        return i18ncp("@info shader usage section header", "Used in %n event", "Used in %n events", count);
    }
    readonly property string _description: effect && typeof effect.description === "string" ? effect.description : ""
    readonly property string _author: effect && effect.author && effect.author.length > 0 ? effect.author : ""
    readonly property string _version: effect && effect.version && effect.version.length > 0 ? effect.version : ""
    readonly property var _usages: {
        usagesRev; // reactive dep
        var id = effect ? effect.id : "";
        if (!id || id.length === 0 || !bridge)
            return [];

        return bridge.shaderEffectUsages(id);
    }
    readonly property bool _hasParameters: effect && effect.parameters && effect.parameters.length > 0

    function _fmt(v) {
        if (typeof v === "number")
            return Number.isInteger(v) ? String(v) : v.toFixed(2);

        if (typeof v === "boolean")
            return v ? i18nc("@info bool true", "Yes") : i18nc("@info bool false", "No");

        return String(v);
    }

    title: effect ? (effect.name || effect.id || "") : ""
    preferredWidth: Kirigami.Units.gridUnit * 36
    // Hard cap on dialog height. Kirigami.Dialog wraps the content in its
    // own internal QQC2.ScrollView; when implicitContentHeight exceeds this
    // cap the inner ScrollView clips and starts scrolling. See
    // /usr/lib/qt6/qml/org/kde/kirigami/dialogs/Dialog.qml lines 343–392.
    maximumHeight: Kirigami.Units.gridUnit * 32
    standardButtons: Kirigami.Dialog.Close
    padding: Kirigami.Units.largeSpacing

    // Plain Item wrapper so the dialog's `implicitWidth: max(implicitContentWidth, ...)`
    // sees a fixed value rather than the ColumnLayout's natural width
    // (a parameter-row Label was pushing it past 1300 px). Item doesn't
    // auto-compute implicit dimensions from children — it gives us a
    // hard handle on what the dialog reads. The ColumnLayout fills the
    // Item via anchors; its children then wrap/elide against the bounded
    // width instead of growing past it.
    Item {
        implicitWidth: root.preferredWidth - root.leftPadding - root.rightPadding
        implicitHeight: contentColumn.implicitHeight

        ColumnLayout {
            id: contentColumn

            anchors.left: parent.left
            anchors.right: parent.right
            spacing: Kirigami.Units.largeSpacing

            // ── 1. Preview ──────────────────────────────────────────────
            Rectangle {
                readonly property bool _hasPreview: root.effect && root.effect.previewPath && root.effect.previewPath.length > 0

                // Cap width so the preview can never dominate the dialog. Without
                // a cap, Layout.fillWidth lets the preview track the dialog's
                // outer width — which Kirigami.Dialog will grow past
                // preferredWidth if any other ColumnLayout child (e.g. a
                // parameters row's implicit width) demands more — and the
                // 16:9 preferredHeight then clips everything below it off-screen.
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 32
                Layout.preferredHeight: width * 9 / 16
                Layout.alignment: Qt.AlignHCenter
                visible: _hasPreview
                radius: Kirigami.Units.smallSpacing
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)
                border.width: Math.max(1, Math.round(Screen.devicePixelRatio))
                border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
                clip: true

                Image {
                    anchors.fill: parent
                    anchors.margins: Math.max(1, Math.round(Screen.devicePixelRatio))
                    source: parent._hasPreview ? "file://" + encodeURI(root.effect.previewPath) : ""
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
                        // Plural-form selection happens INSIDE the host's
                        // `i18ncp(..., count)` closure — pre-baking
                        // singular/plural strings here would break Polish /
                        // Russian / Arabic etc. (more than two plural forms)
                        // and would also lie about the count itself
                        // (`%n` would be replaced with whatever count the
                        // wrapper hard-coded, not the live `_usages.length`).
                        text: root.usageHeaderTextFn(root._usages.length)
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

                    delegate: Rectangle {
                        id: paramRow

                        required property var modelData
                        required property int index

                        Layout.fillWidth: true
                        implicitHeight: rowContent.implicitHeight + Kirigami.Units.smallSpacing
                        radius: Kirigami.Units.smallSpacing / 2
                        color: index % 2 === 0 ? "transparent" : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.04)

                        RowLayout {
                            id: rowContent

                            anchors.fill: parent
                            anchors.leftMargin: Kirigami.Units.smallSpacing
                            anchors.rightMargin: Kirigami.Units.smallSpacing
                            spacing: Kirigami.Units.smallSpacing

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

}
