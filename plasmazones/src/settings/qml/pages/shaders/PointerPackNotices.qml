// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The two notices a pointer pack's preview carries, read from the
 * pack's own metadata.
 *
 * One component for every host that shows a pointer preview, so the browser's
 * detail pane and the Pointer page's chain rows say the same thing about the
 * same pack: which order it paints in against the cursor, and whether the
 * preview's plain arrow stands in for a cursor image the pack samples. Both
 * facts come from PointerPreviewController.packInfo, the same map the canvas
 * orders its stand-in cursor by.
 *
 * Collapses to nothing when neither notice applies, so a host can place it
 * unconditionally under its preview.
 */
ColumnLayout {
    id: root

    /// PointerPreviewController, handed down by the host. Null renders no
    /// notice at all.
    property QtObject previewController: null
    /// The pack the notices describe.
    property string packId: ""

    /// Invokable-call dependency tick — see DecorationPreviewPane._rev.
    readonly property int _rev: previewController ? previewController.previewRevision : 0

    readonly property var _info: {
        void root._rev;
        return (previewController && packId.length > 0) ? (previewController.packInfo(packId) || ({})) : ({});
    }
    readonly property bool _paintsAboveCursor: _info.layer === "above"
    readonly property bool _needsCursorSprite: _info.needsCursor === true

    spacing: Kirigami.Units.smallSpacing
    visible: _paintsAboveCursor || _needsCursorSprite

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
