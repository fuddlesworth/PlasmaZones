// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * Error card for a live shader preview.
 *
 * Shown overlaid on the preview surface when a shader fails to come up: an
 * error icon, a "Shader failed to load" heading, and the raw (scrollable,
 * monospaced) error log. The log is a glslang compile log on a bake failure,
 * but most of the reachable messages are LOAD failures (an unusable URL, a
 * missing zone.vert, a fragment file that will not open), so the heading
 * deliberately says load rather than compile. Its one production consumer is
 * the settings shader browser. The live daemon overlay shows the same errorLog
 * text through its own inline card in RenderNodeOverlayContent.qml rather than
 * through this component.
 *
 * The host sets `errorLog` (typically `ZoneShaderItem.errorLog`) and positions
 * this with anchors; it self-hides when the log is empty.
 */
Control {
    id: root

    /// The load or compile error text. Empty hides the banner.
    property string errorLog: ""

    visible: errorLog.length > 0
    padding: Kirigami.Units.largeSpacing

    background: Rectangle {
        color: Kirigami.Theme.backgroundColor
        opacity: 0.96
        radius: Kirigami.Units.smallSpacing
        border.color: Kirigami.Theme.negativeTextColor
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: "dialog-error"
                color: Kirigami.Theme.negativeTextColor
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
            }

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 5
                text: i18nc("@info:status shader preview", "Shader failed to load")
                color: Kirigami.Theme.negativeTextColor
                elide: Text.ElideRight
            }
        }

        // GLSL errors can span several lines — keep them scrollable rather than
        // letting the card grow unbounded over the preview.
        ScrollView {
            id: errorScroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            // The label wraps to availableWidth, but the ScrollView would
            // otherwise derive contentWidth from the label's unwrapped
            // implicitWidth, leaving a dead horizontal scroll range on long
            // single-line GLSL errors.
            contentWidth: availableWidth

            Label {
                // The ScrollView's own availableWidth, not the Control's —
                // a non-overlay vertical scrollbar narrows the viewport and
                // the full Control width would overflow past it.
                width: errorScroll.availableWidth
                text: root.errorLog
                wrapMode: Text.Wrap
                font.family: Kirigami.Theme.fixedWidthFont.family
                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                color: Kirigami.Theme.textColor
                // No Accessible.name override — the Label's own text (the
                // error log) is the default accessible name, and replacing it
                // would hide the actual error from assistive technology.
                Accessible.description: i18nc("@info:whatsthis", "Shader error details")
            }
        }
    }
}
