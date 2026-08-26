// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

/**
 * @brief The stand-in card run through the real decoration chain host.
 *
 * The rendering core shared by both decoration previews: the browser card's
 * inline thumbnail and the detail dialog's large pane. Neither reimplements
 * the composition, so a card and the dialog it opens can never disagree about
 * what a pack looks like.
 *
 * Everything here is the production path. The chain comes from
 * DecorationPreviewController (which composes stages through the same builder
 * the daemon uses) and is rendered by SurfaceDecoration, the same host the
 * daemon runs. The extended-FBO padding, the stage fold and the multipass set
 * therefore behave exactly as they do on screen.
 *
 * ## Cost
 *
 * A live instance carries a capture chain plus one shader item per stage, so
 * the browser instantiates these only for cards actually scrolled into view.
 * `active` is the switch: false tears the whole chain down rather than leaving
 * it idling.
 */
Item {
    id: root

    /// DecorationPreviewController, handed down by the host.
    required property var previewController
    /// The pack to render.
    required property string packId
    /// Friendly parameter map. Empty means the pack's declared defaults, which
    /// is what the browser card shows.
    property var params: ({})
    /// Drives uSurfaceFocused and the card's own styling together, so packs
    /// with an active / inactive split read consistently.
    property bool focusedLook: true
    /// Master gate. False composes no chain and instantiates no shader item.
    property bool active: false
    /// Live CAVA spectrum for audio-reactive packs; the host supplies it only
    /// where audio is actually running.
    property var audioSpectrum: []

    readonly property var _chain: (active && previewController && packId.length > 0) ? (previewController.previewChain(packId, params) || []) : []
    readonly property real _outerPad: (active && previewController && packId.length > 0) ? previewController.previewOuterPadding(packId, params) : 0

    // Flat neutral ground rather than a theme colour: the pack composites over
    // this, and a tinted backdrop would misrepresent the colours it produces.
    // Mid-grey so a dark border and a bright glow are both legible against it.
    Rectangle {
        anchors.fill: parent
        color: "#3a3a3a"
    }

    // The decorated subject. Sized to leave room for an outer effect: the host
    // extends its canvas bottom/right by _outerPad, while the card's own
    // PopupFrame capture ring supplies the top/left halo room (the extension is
    // trailing by design — placement comes from the item's own coordinates and
    // the FBO extension is offset inside it).
    PZCommon.DecorationPreviewCard {
        id: card

        anchors.centerIn: parent
        width: Math.max(Kirigami.Units.gridUnit * 4, Math.min(parent.width * 0.7, parent.width - root._outerPad - Kirigami.Units.gridUnit))
        height: Math.max(Kirigami.Units.gridUnit * 3, Math.min(parent.height * 0.7, parent.height - root._outerPad - Kirigami.Units.gridUnit))
        title: root.cardTitle
        focusedLook: root.focusedLook
    }

    /// Caption on the stand-in card. Exposed so the host supplies it already
    /// translated — this file stays free of user-facing copy.
    property string cardTitle: ""

    PZCommon.SurfaceDecoration {
        anchors.fill: parent
        contentItem: card
        decorationChain: root._chain
        decorationOuterPadding: root._outerPad
        audioSpectrum: root.audioSpectrum
    }
}
