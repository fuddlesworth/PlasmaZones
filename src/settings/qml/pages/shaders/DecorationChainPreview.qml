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
    required property QtObject previewController
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

    /// Whether EVERYTHING the finished preview is made of has arrived: the
    /// ground wallpaper decoded, a chain resolved, an anchor found, and every
    /// stage's shader compiled.
    ///
    /// All of it, not just the shaders. The ground is an asynchronous image
    /// load, so a gate that waited only on the chain lifted while the wallpaper
    /// was still decoding and the desktop appeared underneath a moment later —
    /// the same pop, one layer down. Anything added here that loads on its own
    /// clock belongs in this expression too.
    ///
    /// A host showing this preview to a user should COVER it until this is
    /// true, and cover it rather than hide it: a starved capture chain never
    /// reaches Ready, so hiding to wait for this waits forever.
    readonly property bool ready: decoration.chainReady && _groundReady

    /// A stage in the chain failed to compile.
    ///
    /// `ready` goes true either way — a failed stage has nothing left to wait
    /// for — so a host that lifts its cover on `ready` alone cannot tell a
    /// broken pack from a working one. Read this alongside it to say which.
    readonly property bool hasError: decoration.chainHasError

    /// The ground has settled: either a wallpaper decoded, or there is no
    /// wallpaper to wait for. An unresolvable one must not hold the preview
    /// back for ever.
    ///
    /// Error counts as settled, and that arm is load-bearing rather than
    /// defensive. wallpaperPath() only checks that the file EXISTS, so a path
    /// that resolves but will not decode — an unsupported format, a wallpaper
    /// package directory, a broken symlink, a file the user cannot read —
    /// reaches Image.Error and stays there. Waiting only for Ready would leave
    /// the host's cover over a preview that is drawing perfectly well
    /// underneath it, for ever. The ground is just a backdrop; not having one
    /// is a worse picture, not a broken one.
    readonly property bool _groundReady: root._wallpaper.length === 0 || groundImage.status === Image.Ready || groundImage.status === Image.Error

    /// The ground a previewed decoration is composited over.
    ///
    /// Deliberately NOTHING of its own: the wallpaper covers this whole item
    /// once it decodes, so the only times a ground shows are while that decode
    /// is in flight and when no wallpaper can be resolved at all. Painting a
    /// slab of its own for those left a flat mid-grey rectangle flashing in
    /// every card on the way to the real thing, and against a saturated colour
    /// scheme a neutral grey reads as its complement, so on a blue desktop it
    /// looked brown.
    ///
    /// Letting the host's own background show through instead means the slot
    /// looks exactly like a pack that ships a baked preview.png does before ITS
    /// image arrives, which is the behaviour to match, and it keeps a hardcoded
    /// colour out of a file that has a whole theme available to it.
    readonly property color groundColor: "transparent"
    /// Live CAVA spectrum for audio-reactive packs; the host supplies it only
    /// where audio is actually running.
    property var audioSpectrum: []

    /// Read by every binding below that calls into the controller.
    ///
    /// Those are Q_INVOKABLE CALLS, and QML records no dependency on a function
    /// call — only on the properties an expression touches. Without naming this
    /// one, a theme change or a pack install would leave an open preview
    /// showing its old composition until the dialog was reopened.
    ///
    /// Read via a bare `void root._rev;` statement, the same idiom
    /// SurfaceDecoration uses for the dependencies mapToItem does not register.
    /// Folding it into the guard as `&& _rev >= 0` also works, but reads like a
    /// condition and invites a later "simplification" that would silently make
    /// the whole mechanism inert.
    readonly property int _rev: previewController ? previewController.previewRevision : 0

    readonly property var _chain: {
        void root._rev;
        return (active && previewController && packId.length > 0) ? (previewController.previewChain(packId, params) || []) : [];
    }
    /// NOT gated on `active`, unlike the chain above, and that is the whole
    /// point of it being separate.
    ///
    /// `_cardSize` reserves this margin on all four sides, so a value that
    /// starts at 0 and only fills in once the preview goes active lays the card
    /// out at full size for a frame and then shrinks it. That is the size jump
    /// on opening the details, and it is worst on exactly the packs that need
    /// the margin most: phosphor-motes asks 56px a side, so its card visibly
    /// snapped inward.
    ///
    /// Nothing is saved by deferring it. This is one call returning a double —
    /// no chain composed, no capture started, no shader item instantiated — so
    /// an inactive preview can afford to know how big its card will be.
    ///
    /// Sanitised the same way SurfaceDecoration sanitises the number it is
    /// handed, because both halves of this preview reserve the margin from the
    /// same value and must agree about it. `_cardSize` subtracts it twice, so
    /// an unusable value propagates into a NaN width and the stand-in card
    /// vanishes, while the decoration host beside it silently clamps to 0 and
    /// carries on drawing.
    readonly property real _outerPad: {
        void root._rev;
        const requested = (previewController && packId.length > 0) ? previewController.previewOuterPadding(packId, params) : 0;
        return isFinite(requested) ? Math.max(0, requested) : 0;
    }

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
    ///
    /// Reads `_rev` like every other controller call here: wallpaperPath() is
    /// a Q_INVOKABLE with no notifier AND its answer is cached with a short
    /// TTL, so without the dependency a wallpaper change would leave an open
    /// preview standing on the old one until the card was rebuilt.
    readonly property string _wallpaper: {
        void root._rev;
        return (active && previewController) ? (previewController.wallpaperPath() || "") : "";
    }

    /// Whether the browsed pack samples the scene behind the window.
    readonly property bool _needsBackdrop: {
        void root._rev;
        return (previewController && packId.length > 0) ? (previewController.packInfo(packId) || ({})).needsBackdrop === true : false;
    }

    /// The wallpaper decoded for the backdrop sampler. Only fetched for a pack
    /// that samples it — decoding a wallpaper per card otherwise would be pure
    /// waste, since nothing else in the chain looks at it.
    ///
    /// Reads `_rev` for the same reason `_wallpaper` does. It would re-evaluate
    /// transitively through `_needsBackdrop` today, but that is an accident of
    /// which properties this expression happens to touch, not a dependency on
    /// the wallpaper changing.
    readonly property var _backdrop: {
        void root._rev;
        return (active && _needsBackdrop && previewController) ? previewController.wallpaperImage() : null;
    }

    // Ground the card sits on. The desktop wallpaper where we can resolve it:
    // a decoration is judged by how it looks ON a desktop, and flat grey
    // flatters everything equally — a border, a glow and a drop shadow all read
    // differently over a photograph, and the glass family is entirely about
    // what shows through.
    Rectangle {
        anchors.fill: parent
        color: root.groundColor

        Image {
            id: groundImage

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
        id: decoration

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
        // The ground Image above fills this same item with the very wallpaper
        // bound above, so the card refracts the part of it that is genuinely
        // behind it and the pane reads as one continuous surface. Without this
        // the card would sample the whole desktop shrunk into itself while the
        // ground behind it showed the wallpaper at a different scale.
        backdropSourceArea: Qt.rect(0, 0, width, height)
    }
}
