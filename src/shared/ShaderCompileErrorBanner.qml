// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * Compile-error card for a live shader preview.
 *
 * Shown overlaid on the preview surface when the RHI shader compile fails —
 * an error icon, a "Shader failed to compile" heading, and the raw (scrollable,
 * monospaced) GLSL error log. Shared by the editor preview and the settings
 * shader browser so both surface the same in-app feedback.
 *
 * The host sets `errorLog` (typically `ZoneShaderItem.errorLog`) and positions
 * this with anchors; it self-hides when the log is empty.
 */
Control {
    id: root

    /// The compiler's error text. Empty hides the banner.
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
                text: i18nc("@info:status shader preview", "Shader failed to compile")
                color: Kirigami.Theme.negativeTextColor
                elide: Text.ElideRight
            }
        }

        // GLSL errors can span several lines — keep them scrollable rather than
        // letting the card grow unbounded over the preview.
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Label {
                width: root.availableWidth
                text: root.errorLog
                wrapMode: Text.Wrap
                font.family: "monospace"
                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                color: Kirigami.Theme.textColor
                Accessible.name: i18nc("@info:whatsthis", "Shader error details")
            }
        }
    }
}
