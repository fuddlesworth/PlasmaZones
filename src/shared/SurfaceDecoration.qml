// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Bound: the stage Repeater's delegate closes over `root` and `stageRepeater`,
// and every reference is already explicitly qualified, so this only pins the
// scoping the file already relies on.
pragma ComponentBehavior: Bound

// PlasmaZones 1.0 is registered IMPERATIVELY by each host process
// (qmlRegisterType<SurfaceShaderItem>), not by a QML module, so a host that
// instantiates this type must do that registration first or every stage
// fails with "SurfaceShaderItem is not a type". The daemon and the settings
// app both do; the editor registers only ZoneShaderItem and the KCM neither,
// so either would need it before using this. See src/settings/main.cpp.
import PlasmaZones 1.0
import QtQuick
import QtQuick.Window

/**
 * Surface-shader decoration host (Stage d).
 *
 * Renders a daemon overlay card (OSD or a transient popup — snap-assist,
 * zone-selector, layout-picker) through a SURFACE shader pack (rounded corners +
 * border today) resolved by C++ from `DecorationProfileTree.resolve(<path>)`.
 * The pack SAMPLES the card content as `uTexture0` and REPLACES it with a
 * clipped-and-bordered version, so the decoration must (a) capture the live card
 * into a texture, (b) suppress the card's own square-cornered direct draw, and
 * (c) re-render that texture through the shader over the same on-screen rect.
 *
 * ## Capture target — the `shaderAnchor`
 *
 * Most card bodies wrap their visible frame in a `PopupFrame`, whose
 * `captureItem` is tagged BOTH `objectName: "shaderAnchor"` AND
 * `property bool shaderAnchor: true` (the same item `SurfaceAnimator` captures
 * for show/hide transitions). It is larger than the visible frame by a glow ring
 * and publishes `shaderContentRect` — the visible frame's rect inside the
 * (glow-padded) capture item, in anchor-local logical px. We capture the WHOLE
 * anchor as `uTexture0` and feed `shaderContentRect` as the frame geometry, so
 * the surface contract rounds to the visible frame corners (not the glow-padded
 * bounds). This is the faithful mapping for PopupFrame's padded capture;
 * surface_uniforms.glsl's uSurfaceFrameTopLeft/uSurfaceFrameSize exist precisely
 * for this.
 *
 * Some content (snap-assist) has no PopupFrame: its CONTENT ROOT itself carries
 * only `property bool shaderAnchor: true` (no objectName, no shaderContentRect).
 * The anchor finder below matches EITHER a truthy `shaderAnchor` property OR
 * objectName === "shaderAnchor" (mirroring SurfaceAnimator's
 * findShaderAnchorRecursive), and checks the content root itself — not just its
 * descendants — so snap-assist's root-as-anchor resolves. The
 * `shaderContentRect !== undefined` guard below then falls back to full-anchor
 * geometry when no PopupFrame publishes that rect.
 *
 * ## Hide-source idiom — mirrors SurfaceAnimator verbatim
 *
 * `SurfaceAnimator::attachShaderToAnchor` (libs/phosphor-animation) snapshots
 * the anchor via a `QQuickShaderEffectSource` with `hideSource: true`, parked
 * far off-screen and `live: true`, then feeds THAT source to the shader's
 * `setSourceItem`. `hideSource: true` is what suppresses the anchor's own direct
 * draw so its square corners don't show under the rounded output; the off-screen
 * park keeps Qt's FBO render alive (visible:false / opacity:0 would cull it)
 * while the source's own composite node draws where the user can't see it. The
 * shader effect is a SIBLING of the anchor (never a parent/descendant — a
 * feedback loop) reading the snapshot, not the live layer. This component
 * reproduces that exact chain in QML.
 *
 * ## Lifecycle
 *
 * The host slot (PassiveOverlayShell.osdSlot / snapAssistSlot / layoutPickerSlot
 * / zoneSelectorSlot) passes the loaded content root as `contentItem` and the
 * C++-resolved decoration props. When `decorationChain` is empty (no pack
 * resolves for this surface path) the component is inert: the capture/shader
 * items don't activate and the card draws normally with its native
 * square-cornered chrome.
 */
Item {
    id: root

    /// Loaded content root (the slot Loader's item). Its nested PopupFrame
    /// (OSD / selector / picker) — or the root itself (snap-assist) — exposes
    /// the `shaderAnchor` capture item we decorate.
    property Item contentItem: null

    /// C++-resolved surface pack CHAIN, written by OverlayService::
    /// applyDecoration:
    ///   • decorationChain — ordered stage list, one entry per resolved pack:
    ///     { source (file:// url of effect.frag), vertexSource (url or ""),
    ///       preamble (generated `#define p_<id> …`), params (translated
    ///       `customParamsN_*` / `customColorN` slot map), animated (bool,
    ///       gates that stage's per-frame iTime tick) }. Empty list = no
    ///     decoration; the component stays inert. Stages fold left-to-right:
    ///     stage 0 samples the card snapshot, each later stage samples the
    ///     previous stage's output — the QML analogue of the compositor's
    ///     composite ping-pong, so a border + glow chain composes here too.
    ///   • decorationOuterPadding  — the chain's LARGEST declared paddingParam
    ///                               value (logical px, e.g. glow's glowSize).
    ///                               The capture + shader items grow by twice
    ///                               this margin, CENTRED on the anchor: the
    ///                               content is inset by the margin on all
    ///                               four sides, giving an OUTER effect the
    ///                               same room in every direction it
    ///                               emanates. The daemon analogue of the
    ///                               compositor's padded capture canvas.
    ///                               Centring is why the stage draws at
    ///                               negative coordinates relative to the
    ///                               anchor — see the placement set at the
    ///                               capture below. 0 for margin-less chains
    ///                               keeps the classic 1:1 geometry.
    property var decorationChain: []
    // Live CAVA audio spectrum, forwarded to every stage's SurfaceShaderItem so
    // an audio-reactive pack (one that includes surface_audio.glsl) reacts.
    // Empty when the audio visualizer is off. The daemon writes it via
    // OverlayService while a decoration host is displaying and audio is enabled.
    property var audioSpectrum: []

    /// Stand-in for the scene behind this surface, as a QImage — in practice
    /// the desktop wallpaper. A pack declaring `needsBackdrop` (the glass /
    /// blur family) samples it through backdropTexel(); every other pack
    /// ignores it entirely.
    ///
    /// A daemon surface has no live scene behind it the way a compositor-side
    /// window does, so this is an approximation: it shows the wallpaper, not
    /// whatever windows happen to sit under the card. That is the difference
    /// between a frosted OSD looking like frosted glass and looking like a
    /// flat tint, and it is the same image the overlay category's wallpaper
    /// sampler uses. Null (the default) leaves uHasBackdrop at 0 and every
    /// pack on its documented fallback appearance.
    property var backdropTexture: null

    /// Drives `uSurfaceFocused` on every stage. A pack that distinguishes an
    /// active from an inactive appearance keys on it — the border family mixes
    /// its two colours on it, and focus-fade washes the whole surface out when
    /// it is false.
    ///
    /// True by default, which is what the daemon's overlay surfaces want: an
    /// OSD or a transient popup is only ever shown for the active context, so
    /// the focused look is the intended one and no daemon host sets this.
    ///
    /// It exists for hosts that need to show BOTH states — the settings
    /// preview, which offers a focus toggle. It was previously a literal
    /// `true` on the stage, which meant such a host could restyle its own
    /// stand-in card but never actually reach the shader, so a focus-reactive
    /// pack looked inert no matter what the host did.
    property bool surfaceFocused: true
    property real decorationOuterPadding: 0

    /// Sanitised device-independent margin. The capture's sourceRect and every
    /// geometry binding below read THIS, so a garbage negative value can't
    /// mirror the capture.
    // isFinite as well as the floor: Math.max(0, NaN) is NaN, and this feeds
    // the capture rect, the stage offsets and surfaceFrameTopLeft, so one
    // non-finite value would take the entire placement set with it.
    readonly property real outerPad: isFinite(decorationOuterPadding) ? Math.max(0, decorationOuterPadding) : 0

    /// Logical→device scale for the decorated surface. The OSD shell tracks the
    /// active output's devicePixelRatio; Screen.devicePixelRatio is the live
    /// value for the window this item lives in.
    readonly property real surfaceScale: Screen.devicePixelRatio

    /// The area the bound `backdropTexture` covers, in this item's coordinates.
    ///
    /// Only meaningful for a host that binds ONE image across many surfaces —
    /// a daemon or preview host handing every surface the same desktop
    /// wallpaper. Each surface has to be told which part of that image lies
    /// behind it, or all of them sample the whole desktop squeezed into their
    /// own box. Left empty (the default) the backdrop is sampled whole, which
    /// is what the compositor wants: its capture already covers this window's
    /// canvas rather than the entire screen.
    ///
    /// A host whose root item IS the full screen can simply bind
    /// `Qt.rect(0, 0, width, height)`.
    property rect backdropSourceArea: Qt.rect(0, 0, 0, 0)

    /// Whether any decoration is active. Gates the capture + shader items so an
    /// undecorated card pays nothing and draws its native card.
    readonly property bool decorationActive: (decorationChain ? decorationChain.length : 0) > 0 && shaderAnchorItem !== null

    /// The shaderAnchor capture item inside (or equal to) the loaded content.
    /// Re-resolved whenever the content swaps (Loader re-instantiation on each
    /// show produces a fresh anchor — matches the per-show shaderAnchor the
    /// dismiss path forces via the mode="" / loaded=false unload).
    property Item shaderAnchorItem: null

    function _resolveAnchor() {
        // Un-demote the anchor we are leaving before dropping our reference: if
        // it was demoted (shaderAnchor=false while decorationActive), it would
        // otherwise be stranded with the property cleared. Masked today because
        // the Loader re-instantiates content per show, but correct regardless of
        // whether the anchor item is destroyed or merely swapped.
        // Guard the property write: _findShaderAnchor may match by objectName
        // alone (mirroring SurfaceAnimator), and assigning a nonexistent
        // property on such an anchor would throw in QML JS.
        if (shaderAnchorItem && shaderAnchorItem.shaderAnchor !== undefined)
            shaderAnchorItem.shaderAnchor = true;
        shaderAnchorItem = contentItem ? _findShaderAnchor(contentItem) : null;
        _applyAnchorRouting();
    }

    // Compose with SurfaceAnimator instead of competing with it. SurfaceAnimator
    // and this decoration both want to capture (hideSource) and re-render the
    // surface; if both target the SAME raw card the static decoration smothers
    // the show/hide transition. So when decoration is ACTIVE we route the
    // animator to capture the DECORATION's output (the `decoration` item below,
    // which carries shaderAnchor) and DEMOTE the raw card's shaderAnchor PROPERTY
    // so the animator (which matches that property — see
    // findShaderAnchorRecursive) skips it. We still capture the raw card here via
    // our own stored reference. Net chain: raw card → decoration → animator, so
    // the transition runs OVER the decorated surface (the daemon analogue of the
    // compositor's uSurfaceLayer compose). When INACTIVE we restore the raw
    // card's property so the animator animates the bare card exactly as before.
    function _applyAnchorRouting() {
        // Same objectName-matched-anchor guard as _resolveAnchor: only demote /
        // restore via the property when the anchor actually declares it.
        if (root.shaderAnchorItem && root.shaderAnchorItem.shaderAnchor !== undefined)
            root.shaderAnchorItem.shaderAnchor = !root.decorationActive;
    }

    onDecorationActiveChanged: root._applyAnchorRouting()

    // Depth-first search for the shaderAnchor. Mirrors SurfaceAnimator's
    // findShaderAnchorRecursive (libs/phosphor-animation): matches EITHER a
    // truthy `shaderAnchor` property OR objectName === "shaderAnchor", and
    // checks the node ITSELF before its descendants — snap-assist's anchor IS
    // the content root passed in as contentItem (only a `shaderAnchor: true`
    // property, no objectName, no nested PopupFrame). QML has no built-in
    // recursive findChild for visual items.
    function _findShaderAnchor(node) {
        if (!node)
            return null;
        if (node.shaderAnchor === true || node.objectName === "shaderAnchor")
            return node;
        var kids = node.children;
        for (var i = 0; i < kids.length; i++) {
            var found = _findShaderAnchor(kids[i]);
            if (found)
                return found;
        }
        return null;
    }

    anchors.fill: parent
    // Re-resolve the anchor whenever the loaded content changes identity (mode
    // swap / Loader re-instantiation). onCompleted of the content is not
    // observable here, so bind on contentItem and let the bindings below settle
    // once the anchor's geometry is non-zero.
    onContentItemChanged: _resolveAnchor()
    Component.onCompleted: _resolveAnchor()

    // ── Snapshot of the card (hide-source) ───────────────────────────────────
    // QQuickShaderEffectSource registered as the QML element ShaderEffectSource.
    // hideSource:true suppresses the anchor's own direct draw; the off-screen
    // park keeps the FBO render alive without a second visible composite. live:
    // true re-captures each frame so a card whose content animates (e.g. badge
    // toast) stays current under the decoration.
    ShaderEffectSource {
        id: cardSnapshot

        // Park far off-screen — see the SurfaceAnimator rationale above. A
        // zero/negative SIZE would skip updatePaintNode and starve the shader's
        // uTexture0, so size tracks the anchor and only the POSITION is hidden.
        readonly property real offscreenCoord: -1000000

        sourceItem: root.decorationActive ? root.shaderAnchorItem : null
        live: true
        hideSource: root.decorationActive
        // Padded capture: when the pack declares an outer margin, capture a
        // sourceRect inflated past the anchor's bounds — the out-of-bounds
        // band renders TRANSPARENT, which is exactly the room an outer effect
        // (glow, shadow, motes) lights up.
        //
        // The rect is CENTRED on the anchor: origin (-outerPad, -outerPad),
        // size anchor + 2·outerPad. An outer effect emanates from the frame in
        // every direction, so it needs the same room on every side — a
        // trailing bottom/right band leaves a halo clipped along the top and
        // left edges (phosphor-motes asks for 56px of travel and would have
        // had ~23px of it above the card).
        //
        // Symmetry is what forces the stage below to negative coordinates. The
        // three bindings that make up the placement — this origin, the stage's
        // x/y, and surfaceFrameTopLeft — are a SET: the capture insets the
        // content by outerPad, the stage shifts back by outerPad to land it
        // over the anchor again, and the frame rect gains the same outerPad so
        // the pack still rounds to the visible frame rather than to the padded
        // canvas. Change one and the decorated surface draws off-position; an
        // earlier attempt at symmetry moved only some of them and mis-drew the
        // whole surface. The all-zero rect is the documented "whole item"
        // default for the margin-less case.
        sourceRect: root.outerPad > 0 ? Qt.rect(-root.outerPad, -root.outerPad, (root.shaderAnchorItem ? root.shaderAnchorItem.width : 0) + root.outerPad * 2, (root.shaderAnchorItem ? root.shaderAnchorItem.height : 0) + root.outerPad * 2) : Qt.rect(0, 0, 0, 0)
        width: (root.shaderAnchorItem ? root.shaderAnchorItem.width : 0) + root.outerPad * 2
        height: (root.shaderAnchorItem ? root.shaderAnchorItem.height : 0) + root.outerPad * 2
        x: offscreenCoord
        y: offscreenCoord
        // MUST stay visible: SurfaceAnimator's rationale (surfaceanimator.cpp
        // ~640) is that visible:false (and opacity:0) suppress updatePaintNode
        // and therefore the FBO render — starving the shader's uTexture0. The
        // off-screen park above is what hides it; Qt keeps processing it there.
        // When no pack resolves, sourceItem is null + hideSource false, so this
        // captures nothing and the card draws itself normally.
        visible: true
    }

    // ── Surface shader chain ─────────────────────────────────────────────────
    // One stage per chain pack, folded left-to-right: stage 0 samples the card
    // snapshot, stage k samples stage k-1's output through an interposed
    // ShaderEffectSource (a SurfaceShaderItem is a render-node item, not a
    // texture provider, so every hop needs the explicit capture — the QML
    // analogue of the compositor's composite ping-pong). Only the LAST stage
    // draws on screen: each earlier stage's direct draw is suppressed by the
    // next stage's hideSource capture. The last stage also carries the
    // SurfaceAnimator anchor tags, so show/hide transitions animate the FULLY
    // composited output.
    Repeater {
        id: stageRepeater

        model: root.decorationActive ? root.decorationChain.length : 0

        // Detag RELEASED delegates immediately. The Repeater releases old
        // delegates with deleteLater, so on a dismiss → fast re-show they
        // are still in the item tree (and their bindings still LIVE) when
        // SurfaceAnimator's beginShow walks it in the same event-loop turn:
        // the dying last stage re-evaluates `decorationActive && isLast`
        // against the NEW chain and tags itself again, and the animator can
        // anchor the corpse — it then animates a frozen capture invisibly
        // while the real new stage draws the full decorated surface
        // statically until teardown ("surface pops in when the animation
        // stops", intermittent). The imperative writes below both clear the
        // tags and BREAK those bindings, so a released delegate can never
        // re-tag itself while it waits for deletion.
        onItemRemoved: function (index, item) {
            if (item && item.detagAnchors)
                item.detagAnchors();
        }

        delegate: Item {
            id: stage

            required property int index
            readonly property var stageData: root.decorationChain[stage.index] || ({})
            readonly property bool isLast: stage.index === (root.decorationChain ? root.decorationChain.length : 0) - 1
            // Output tap for the NEXT stage's sourceItem lookup.
            readonly property Item outputTap: tap

            // Called by the Repeater's onItemRemoved when this delegate is
            // released: imperative assignment clears the animator tags AND
            // severs their bindings, so the deleteLater-pending item cannot
            // re-tag itself against the successor chain.
            function detagAnchors() {
                stageItem.shaderAnchor = false;
                stageItem.shaderAnchorOverride = false;
            }

            anchors.fill: parent

            SurfaceShaderItem {
                id: stageItem

                // Forward the host's live audio spectrum so an audio-reactive
                // pack (surface_audio.glsl) sees it. Inherited from ShaderEffect.
                audioSpectrum: root.audioSpectrum

                // Backdrop for a needsBackdrop pack. Both properties are
                // inherited from ShaderEffect and reach binding 11, the same
                // sampler the overlay category fills with the wallpaper —
                // useWallpaper is what makes the node bind the real texture
                // instead of its dummy, and the node raises uHasBackdrop off
                // exactly that. Every stage in the chain gets the same
                // backdrop, mirroring how each sees the same canvas.
                wallpaperTexture: root.backdropTexture ? root.backdropTexture : undefined
                useWallpaper: root.backdropTexture !== null && root.backdropTexture !== undefined

                // Which slice of that shared backdrop lies behind THIS stage.
                // Both rects are in the host's coordinate space, so the item
                // can cover-fit the image over the area and cut this stage's
                // rect out of it. Passing an empty source area (the default)
                // leaves the item sampling the backdrop whole.
                backdropScreenRect: root.backdropSourceArea
                backdropSurfaceRect: Qt.rect(stageItem.x, stageItem.y, stageItem.width, stageItem.height)

                // Anchor rect mapped into the host's coordinate space (the
                // delegate fills the host, so its coordinates coincide). The
                // anchor lives deep inside the loaded content; mapToItem walks
                // the transform chain so the decoration lands exactly over the
                // card regardless of nesting. mapToItem registers no QML
                // dependencies, so the anchor/host sizes are read explicitly
                // first: a centerIn-driven move of the anchor is always
                // accompanied by a size change (of the anchor or the host),
                // and touching those values makes a resize-driven recenter
                // re-resolve the mapped origin. The anchor's own x/y are read
                // too so a pure move of the anchor itself re-resolves; ancestor
                // pure-moves (position changes higher in the mapped chain)
                // still require content re-instantiation, which the slots
                // guarantee per show.
                readonly property point anchorOrigin: {
                    if (!root.decorationActive || !root.shaderAnchorItem)
                        return Qt.point(0, 0);
                    void (root.shaderAnchorItem.x + root.shaderAnchorItem.y + root.shaderAnchorItem.width + root.shaderAnchorItem.height + root.width + root.height);
                    return root.shaderAnchorItem.mapToItem(root, 0, 0);
                }

                // SurfaceAnimator anchor (compose — see _applyAnchorRouting).
                // Only the LAST stage carries the tags: it IS the fully
                // composited surface the animator captures and animates.
                // shaderAnchorOverride makes the preference structural: the
                // animator's findShaderAnchorRecursive picks an override over
                // ANY plain shaderAnchor, so the decorated output wins even if
                // a demote / promote write lands late relative to a beginShow
                // resolution — the ordering class behind "the border draws
                // statically at final size while the card animates in".
                //
                // Card rect for the animator's card-space remap: with an outer
                // margin the WHOLE padded canvas is published as the card. The
                // margin band carries drawn decoration (the glow halo), and a
                // sub-canvas card rect would leave that band OUTSIDE the
                // animation shader's card space, where transition shaders
                // resolve to a static 1:1 passthrough — the halo then sits at
                // full final size while the card animates (the glow flavour of
                // the detachment bug). Publishing the full canvas makes the
                // transition sweep the halo together with the card. Margin-less
                // chains keep mirroring the raw card's frame rect, the geometry
                // the border-detachment fix shipped with.
                property bool shaderAnchor: root.decorationActive && stage.isLast
                property bool shaderAnchorOverride: root.decorationActive && stage.isLast
                property rect shaderContentRect: root.outerPad > 0 ? Qt.rect(0, 0, width, height) : ((root.shaderAnchorItem && root.shaderAnchorItem.shaderContentRect !== undefined) ? root.shaderAnchorItem.shaderContentRect : Qt.rect(0, 0, width, height))

                // Every stage stays VISIBLE while active: an explicitly
                // invisible item generates no scene-graph nodes, so the next
                // stage's capture would render EMPTY and the downstream pack
                // would composite against a transparent base (glow's
                // outer-only gate then lights the WHOLE canvas — the cyan
                // slab bug). Intermediate stages are hidden from the direct
                // draw by the next stage's hideSource capture instead; only
                // the last stage actually reaches the screen.
                visible: root.decorationActive
                // Shifted back by the SAME outerPad the capture inset above, so
                // the anchor content lands exactly over the anchor again while
                // the padded band surrounds it evenly. The two offsets are one
                // pair: the capture's -outerPad origin puts the content
                // outerPad into the texture, and this -outerPad puts that
                // texture back on position. Drop either and the decorated
                // surface draws outerPad off from the card it decorates.
                x: anchorOrigin.x - root.outerPad
                y: anchorOrigin.y - root.outerPad
                width: (root.shaderAnchorItem ? root.shaderAnchorItem.width : 0) + root.outerPad * 2
                height: (root.shaderAnchorItem ? root.shaderAnchorItem.height : 0) + root.outerPad * 2

                // Stage 0 samples the card snapshot; stage k samples stage
                // k-1's output tap. itemAt is NOT notifiable, so the binding
                // reads stageRepeater.count first — count changes as the
                // Repeater populates, forcing a re-evaluation once the
                // previous delegate exists (creation is in index order, so
                // by full population every hop resolves).
                sourceItem: {
                    if (stage.index === 0)
                        return cardSnapshot;
                    var populated = stageRepeater.count;
                    var prev = populated > stage.index ? stageRepeater.itemAt(stage.index - 1) : null;
                    return prev ? prev.outputTap : null;
                }

                // Surface-state inputs (device px). The whole padded canvas is
                // uTexture0; the FRAME rect within it (shaderContentRect in
                // anchor-local logical px, plus the capture's outerPad inset)
                // scaled to device px is what the border rounds to — so the
                // pack outlines the visible card while a halo lands in the
                // transparent band that now surrounds it on all four sides
                // (uSurfaceSize exceeds uSurfaceFrameSize by the 2 × outerPad
                // extension, like the compositor's padded composite canvas).
                // Identical for every stage, mirroring the compositor's fold
                // where each pack sees the same canvas.
                surfaceScale: root.surfaceScale
                surfaceFocused: root.surfaceFocused
                surfaceSize: root.shaderAnchorItem ? Qt.size((root.shaderAnchorItem.width + root.outerPad * 2) * root.surfaceScale, (root.shaderAnchorItem.height + root.outerPad * 2) * root.surfaceScale) : Qt.size(0, 0)
                // Inset by the SAME outerPad the capture applied: with a centred
                // capture the anchor content sits outerPad into the canvas, so
                // the frame's anchor-local rect has to be shifted by it too or
                // the pack rounds its corners to a rectangle outerPad up-left
                // of the visible card. Third member of the placement set (with
                // the capture origin and the stage's x/y). The (0,0) fallback
                // is for root-as-anchor content that publishes no
                // shaderContentRect, where the frame IS the whole anchor.
                surfaceFrameTopLeft: (root.shaderAnchorItem && root.shaderAnchorItem.shaderContentRect !== undefined) ? Qt.point((root.shaderAnchorItem.shaderContentRect.x + root.outerPad) * root.surfaceScale, (root.shaderAnchorItem.shaderContentRect.y + root.outerPad) * root.surfaceScale) : Qt.point(root.outerPad * root.surfaceScale, root.outerPad * root.surfaceScale)
                // No published shaderContentRect (root-as-anchor content like
                // snap-assist): the frame IS the whole anchor, per this
                // component's documented fallback. A (0, 0) fallback here
                // would trip every pack's degenerate-frame guard
                // (uSurfaceFrameSize < 1 → passthrough) and render nothing on
                // those surfaces.
                surfaceFrameSize: (root.shaderAnchorItem && root.shaderAnchorItem.shaderContentRect !== undefined) ? Qt.size(root.shaderAnchorItem.shaderContentRect.width * root.surfaceScale, root.shaderAnchorItem.shaderContentRect.height * root.surfaceScale) : (root.shaderAnchorItem ? Qt.size(root.shaderAnchorItem.width * root.surfaceScale, root.shaderAnchorItem.height * root.surfaceScale) : Qt.size(0, 0))

                // Pack source + params, per stage. paramPreamble/shaderParams
                // BEFORE shaderSource is the load-trigger ordering the
                // inherited ShaderEffect setters expect; here they are
                // bindings, so QML evaluates the value graph before the first
                // paint regardless of declaration order, but mirroring the
                // applyShaderInfoToWindow order keeps the intent explicit.
                paramPreamble: stage.stageData.preamble !== undefined ? stage.stageData.preamble : ""
                shaderParams: stage.stageData.params !== undefined ? stage.stageData.params : ({})
                vertexShaderUrl: stage.stageData.vertexSource !== undefined ? stage.stageData.vertexSource : ""
                shaderSource: stage.stageData.source !== undefined ? stage.stageData.source : ""

                // Multipass buffer passes, forwarded from the composer's stage
                // map. Inherited wholesale from ShaderEffect — a surface pack's
                // buffer passes need no surface-specific handling, only these
                // bindings. `multipass` is false for every single-pass pack, so
                // the empty-list / default arms below keep those stages on the
                // classic single-pass path.
                bufferShaderPaths: stage.stageData.multipass === true && stage.stageData.bufferShaderPaths !== undefined ? Array.from(stage.stageData.bufferShaderPaths) : []
                bufferFeedback: stage.stageData.bufferFeedback === true
                bufferScale: stage.stageData.bufferScale !== undefined ? stage.stageData.bufferScale : 1
                bufferWrap: stage.stageData.bufferWrap !== undefined && stage.stageData.bufferWrap !== "" ? stage.stageData.bufferWrap : "clamp"
                bufferWraps: stage.stageData.bufferWraps !== undefined ? Array.from(stage.stageData.bufferWraps) : []
                bufferFilter: stage.stageData.bufferFilter !== undefined && stage.stageData.bufferFilter !== "" ? stage.stageData.bufferFilter : "linear"
                bufferFilters: stage.stageData.bufferFilters !== undefined ? Array.from(stage.stageData.bufferFilters) : []
                useDepthBuffer: stage.stageData.useDepthBuffer === true

                // Multipass REQUIRES a private layer FBO: the render node
                // drives its own buffer passes, and without an isolated target
                // the scene graph's batch renderer desynchronizes its internal
                // pass tracking (the rationale ZoneShaderRenderer.qml documents
                // for the overlay path — same render node, same constraint).
                // Gated on the stage actually being multipass so a plain border
                // or glow stage pays no extra canvas-sized FBO. The intermediate
                // taps below still capture a layered stage: layer.enabled
                // changes where the item renders, not whether it renders, so
                // the hide-source fold is unaffected.
                layer.enabled: stage.stageData.multipass === true && root.decorationActive
                // Qt's DEFAULT mirroring (MirrorVertically) — deliberately NOT
                // the NoMirroring that ZoneShaderRenderer.qml uses.
                //
                // ShaderNodeRhi skips the OpenGL NDC flip whenever it renders
                // into a texture (`yUpInNDC = isYUpInNDC() && !renderingIntoTexture()`),
                // because an inter-stage tap's consumer samples that texture
                // with Qt's top-origin UV convention. Layering a stage makes it
                // render into a texture too — but this one is COMPOSITED by the
                // scene graph rather than sampled by a shader, so the flip the
                // node skipped has to be re-applied here or the stage draws
                // upside down. NoMirroring here is what flipped every multipass
                // pack (the whole glass/blur family) while single-pass packs
                // stayed upright.
                layer.textureMirroring: ShaderEffectSource.MirrorVertically
                // iTime driver: only a stage whose pack declares "animated"
                // subscribes to the per-frame tick — static packs (the border)
                // leave iTime at its default and pay nothing. Gated on
                // decorationActive so a cleared decoration stops ticking.
                playing: stage.stageData.animated === true && root.decorationActive
            }

            // The next stage's uTexture0: captures this stage's output. Same
            // hide-source idiom as cardSnapshot — parked off-screen with the
            // FBO render alive (visible:false would starve the consumer).
            // Inert on the last stage (no consumer; the stage draws directly).
            ShaderEffectSource {
                id: tap

                readonly property real offscreenCoord: -1000000

                sourceItem: stage.isLast ? null : stageItem
                live: true
                hideSource: !stage.isLast
                width: stageItem.width
                height: stageItem.height
                x: offscreenCoord
                y: offscreenCoord
                visible: true
            }
        }
    }
}
