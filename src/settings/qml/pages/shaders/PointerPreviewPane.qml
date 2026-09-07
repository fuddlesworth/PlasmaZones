// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

/**
 * @brief Live preview pane for a POINTER (cursor-decoration) pack.
 *
 * Fourth sibling of the zone/overlay pane (inline in
 * ShaderBrowserDetailDialog), DecorationPreviewPane and AnimationPreviewPane.
 * The stage itself is PointerPreviewCanvas, shared with the Pointer page's
 * layer cards so a pack cannot look like one thing in the catalogue and
 * another on the page that uses it. This file adds the frame, the compile
 * cover, the pause control and the notices.
 */
Item {
    id: root

    /// PointerPreviewController, handed down by the detail dialog. This pane
    /// never touches the page controller itself — the dialog owns that
    /// relationship.
    required property QtObject previewController
    /// The browsed pack's id.
    required property string packId
    /// Live (transient) friendly parameter map from the dialog's editor.
    property var liveParams: ({})
    /// Gated by the dialog: false while the pane is hidden. Gates the shader
    /// item's EXISTENCE; visibility only, never focus.
    property bool active: false
    /// Dropped-focus freeze: the clock stops, the preview holds its frame.
    property bool animating: true

    /// Invokable-call dependency tick — see DecorationPreviewPane._rev.
    readonly property int _rev: previewController ? previewController.previewRevision : 0

    readonly property var _info: {
        void root._rev;
        return (previewController && packId.length > 0) ? (previewController.packInfo(packId) || ({})) : ({});
    }
    readonly property bool _paintsAboveCursor: _info.layer === "above"
    readonly property bool _needsCursorSprite: _info.needsCursor === true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Kirigami.Units.smallSpacing
            // The same slot colour the other panes frame their previews with,
            // so a pack looks the same in all of them and the wallpaper
            // arriving is the only thing that changes.
            color: Kirigami.Theme.alternateBackgroundColor
            border.width: 1
            border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
            clip: true

            PointerPreviewCanvas {
                id: canvas

                anchors.fill: parent
                anchors.margins: 1
                previewController: root.previewController
                packId: root.packId
                params: root.liveParams
                // `active` tracks visibility only. Focus freezes the clock
                // through `animating` instead, the same split every other pane
                // makes: dropping the whole stage on focus loss tore the
                // shader down under a Plasma applet while the window was still
                // fully visible.
                active: root.active
                animating: root.animating && !pauseToggle.checked
                showCursor: cursorToggle.checked
            }

            // Covers the stage while the shader is still compiling, and stays
            // up when it failed. Opaque and slot-coloured — the item
            // underneath has to keep rendering to compile at all.
            PZCommon.ShaderPreviewPlaceholder {
                anchors.fill: parent
                anchors.margins: 1
                visible: !canvas.previewable || !canvas.showable || canvas.hasError
                text: canvas.hasError ? i18nc("@info:placeholder shader preview", "This pack's shader did not compile.") : i18nc("@info:placeholder shader preview", "Preview unavailable")
                backgroundColor: Kirigami.Theme.alternateBackgroundColor
                radius: Kirigami.Units.smallSpacing
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            CheckBox {
                id: pauseToggle

                text: i18nc("@option:check pointer preview", "Pause")
            }

            CheckBox {
                id: cursorToggle

                // Starts on, because a pointer pack is judged against the
                // cursor it decorates. Turning it off is the comparison: what
                // the pack alone paints, with nothing else in the frame.
                checked: true
                text: i18nc("@option:check pointer preview", "Show cursor")
            }

            Item {
                Layout.fillWidth: true
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root._paintsAboveCursor
            type: Kirigami.MessageType.Information
            text: i18nc("@info pointer preview note", "This pack paints over the cursor, so PlasmaZones hides the system cursor and draws it after the pack.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root._needsCursorSprite
            type: Kirigami.MessageType.Information
            text: i18nc("@info pointer preview limitation", "This pack samples the cursor image. The preview stands a plain arrow in for your cursor theme.")
        }
    }
}
