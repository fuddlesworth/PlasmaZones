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

    // A pack with an outer margin (glow, shadow, motes, fireflies) is drawn
    // through a stage deliberately LARGER than this item: the host inflates
    // both capture and stage by the padding so the effect has room. Everything
    // past our bounds is that overflow and must not escape into the
    // surrounding page.
    clip: true

    /// DecorationPreviewController, handed down by the host.
    required property var previewController
    /// The pack to render.
    required property string packId
    /// Friendly parameter map. Empty means the pack's declared defaults, which
    /// is what the browser card shows.
    property var params: ({})
    /// Drives uSurfaceFocused, and nothing else. The stand-in card holds still
    /// across it on purpose, so whatever moves when this is toggled is the
    /// pack's doing rather than the subject's.
    ///
    /// Defaults to UNFOCUSED, which is the state that actually shows what a
    /// pack does. focus-fade only washes the surface out while unfocused and is
    /// inert when focused; the border family's inactive colour is likewise
    /// invisible until then. It is also the state most windows on a desktop are
    /// in at any moment.
    property bool focused: false
    /// Master gate. False composes no chain and instantiates no shader item.
    property bool active: false
    /// Caption on the stand-in card. Exposed so the host supplies it already
    /// translated — this file stays free of user-facing copy.
    property string cardTitle: ""

    /// The ground a previewed decoration is composited over, and the fallback
    /// wherever the wallpaper cannot be resolved or does not cover.
    ///
    /// Deliberately a flat neutral rather than a theme colour: the pack
    /// composites over this, so a tinted ground would misrepresent the colours
    /// it produces. Mid-grey rather than the zone pane's black so a dark border
    /// and a bright glow are both legible. Exposed as a constant so the hosts
    /// that frame this preview paint the same ground instead of keeping their
    /// own copy of the literal.
    readonly property color groundColor: "#3a3a3a"
    /// Live CAVA spectrum for audio-reactive packs; the host supplies it only
    /// where audio is actually running.
    property var audioSpectrum: []

    readonly property var _chain: (active && previewController && packId.length > 0) ? (previewController.previewChain(packId, params) || []) : []
    readonly property real _outerPad: (active && previewController && packId.length > 0) ? previewController.previewOuterPadding(packId, params) : 0

    /// Window-ish aspect the stand-in card always keeps, so the same pack reads
    /// identically in a browser thumbnail and in the detail pane. Matches
    /// DecorationPreviewCard's implicit size.
    readonly property real _cardAspect: 22 / 14

    /// Card size: the largest _cardAspect rectangle that fits the space left
    /// once the outer margin is reserved on ALL FOUR sides (the padded canvas
    /// is centred on the card).
    ///
    /// Fitting an aspect rather than clamping width and height independently is
    /// what keeps the proportions stable: independent clamps let one axis hit
    /// its floor while the other stayed large, which squashed a thumbnail card
    /// to roughly 2.5:1 where the dialog showed the same pack at 1.8:1.
    ///
    /// The floor matters for a greedy pack: phosphor-motes asks for 56px of
    /// travel on every side of a card inside a ~138px-tall thumbnail, which
    /// simply cannot all be shown. The card stays legible and the halo is
    /// clipped, rather than the card collapsing to nothing to honour the halo.
    readonly property size _cardSize: {
        const minW = Kirigami.Units.gridUnit * 5;
        const minH = minW / _cardAspect;
        const availW = Math.max(minW, width - root._outerPad * 2);
        const availH = Math.max(minH, height - root._outerPad * 2);
        return (availW / availH > _cardAspect) ? Qt.size(availH * _cardAspect, availH) : Qt.size(availW, availW / _cardAspect);
    }

    /// The user's desktop wallpaper, resolved once per preview instance
    /// rather than per frame. Unlike `_backdrop` this is unconditional: it is
    /// the ground the stand-in card sits on, which every pack needs, not the
    /// texture only a needsBackdrop pack samples. Empty when it cannot be
    /// resolved (no provider, unreadable file).
    readonly property string _wallpaper: (active && previewController) ? (previewController.wallpaperPath() || "") : ""

    /// Whether the browsed pack samples the scene behind the window.
    readonly property bool _needsBackdrop: (previewController && packId.length > 0) ? (previewController.packInfo(packId) || ({})).needsBackdrop === true : false

    /// The wallpaper decoded for the backdrop sampler. Only fetched for a pack
    /// that samples it — decoding a wallpaper per card otherwise would be pure
    /// waste, since nothing else in the chain looks at it.
    readonly property var _backdrop: (active && _needsBackdrop && previewController) ? previewController.wallpaperImage() : null

    // Ground the card sits on. The desktop wallpaper where we can resolve it:
    // a decoration is judged by how it looks ON a desktop, and flat grey
    // flatters everything equally — a border, a glow and a drop shadow all read
    // differently over a photograph, and the glass family is entirely about
    // what shows through.
    Rectangle {
        anchors.fill: parent
        color: root.groundColor

        Image {
            anchors.fill: parent
            source: root._wallpaper.length > 0 ? "file://" + encodeURI(root._wallpaper).replace(/#/g, "%23").replace(/\?/g, "%3F") : ""
            // Crop rather than letterbox: a letterboxed wallpaper would leave
            // grey bands that read as part of the decoration.
            fillMode: Image.PreserveAspectCrop
            // Thumbnails are small and there are many on screen at once, so the
            // decode is capped near display size instead of loading a 4K frame
            // per card.
            sourceSize.width: Math.max(1, Math.round(width * 2))
            sourceSize.height: Math.max(1, Math.round(height * 2))
            asynchronous: true
            cache: true
            visible: status === Image.Ready
        }
    }

    // The decorated subject. Sized to leave room for an outer effect on every
    // side: the host centres its padded canvas on the card, so the halo needs
    // _outerPad clear above and below, left and right (see _cardSize).
    PZCommon.DecorationPreviewCard {
        id: card

        anchors.centerIn: parent
        width: root._cardSize.width
        height: root._cardSize.height
        title: root.cardTitle
    }

    PZCommon.SurfaceDecoration {
        anchors.fill: parent
        contentItem: card
        decorationChain: root._chain
        decorationOuterPadding: root._outerPad
        audioSpectrum: root.audioSpectrum
        // The only thing the focus toggle moves. The host used to pin this
        // true, so a focus-reactive pack could never show its inactive state
        // and the toggle appeared to do nothing but restyle the card.
        surfaceFocused: root.focused
        // Only decoded for a pack that actually samples it. Every other pack
        // ignores the backdrop, and handing one over regardless would upload a
        // wallpaper-sized texture per card for nothing.
        backdropTexture: root._needsBackdrop ? root._backdrop : null
        // The ground Image below fills this same item with the very wallpaper
        // bound above, so the card refracts the part of it that is genuinely
        // behind it and the pane reads as one continuous surface. Without this
        // the card would sample the whole desktop shrunk into itself while the
        // ground behind it showed the wallpaper at a different scale.
        backdropSourceArea: Qt.rect(0, 0, width, height)
    }
}
