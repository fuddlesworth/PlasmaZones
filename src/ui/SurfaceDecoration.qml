// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

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
    ///                               this margin as a TRAILING bottom/right
    ///                               band (content stays at the canvas
    ///                               top-left, so the stage draws at the
    ///                               anchor's QML coordinates) — transparent
    ///                               room for an OUTER effect, the daemon
    ///                               analogue of the compositor's padded
    ///                               capture canvas. Top/left halo room is
    ///                               the anchor's own glow ring. 0 for
    ///                               margin-less chains keeps the classic
    ///                               1:1 geometry.
    property var decorationChain: []
    // Live CAVA audio spectrum, forwarded to every stage's SurfaceShaderItem so
    // an audio-reactive pack (one that includes surface_audio.glsl) reacts.
    // Empty when the audio visualizer is off. The daemon writes it via
    // OverlayService while a decoration host is displaying and audio is enabled.
    property var audioSpectrum: []
    property real decorationOuterPadding: 0

    /// Sanitised device-independent margin. The capture's sourceRect and every
    /// geometry binding below read THIS, so a garbage negative value can't
    /// mirror the capture.
    readonly property real outerPad: Math.max(0, decorationOuterPadding)

    /// Logical→device scale for the decorated surface. The OSD shell tracks the
    /// active output's devicePixelRatio; Screen.devicePixelRatio is the live
    /// value for the window this item lives in.
    readonly property real surfaceScale: Screen.devicePixelRatio

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
        // band renders TRANSPARENT, which is exactly the room an outer
        // effect (glow) lights up. The rect starts at the anchor's OWN
        // origin (0, 0) so the anchor content sits at the texture TOP-LEFT
        // and the extension is a trailing bottom/right band: the stage item
        // can then be drawn at the anchor's QML position with no negative
        // offset. (A symmetric -outerPad origin here forced the stage to
        // -outerPad coordinates, and that extended-FBO-based placement is
        // what mis-drew the whole surface — coordinates must come from the
        // QML item, the FBO extension is offset inside it.) The all-zero
        // rect is the documented "whole item" default for the margin-less
        // case.
        sourceRect: root.outerPad > 0 ? Qt.rect(0, 0, (root.shaderAnchorItem ? root.shaderAnchorItem.width : 0) + root.outerPad * 2, (root.shaderAnchorItem ? root.shaderAnchorItem.height : 0) + root.outerPad * 2) : Qt.rect(0, 0, 0, 0)
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
                // Drawn at the anchor's QML coordinates — NOT shifted by the
                // FBO extension. The capture puts the anchor content at the
                // texture top-left (sourceRect origin 0,0 above), so the
                // padded band trails bottom/right past the item's natural
                // rect and the visible frame stays exactly where the QML
                // placed it. Positioning the stage at anchorOrigin - outerPad
                // (the extended FBO's coordinate frame) is what drew the whole
                // decorated surface in the wrong place.
                x: anchorOrigin.x
                y: anchorOrigin.y
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
                // uTexture0; the FRAME rect within it (shaderContentRect,
                // anchor-local logical px — the content sits at the canvas
                // top-left, so no outer-margin inset applies) scaled to
                // device px is what the border rounds to — so the pack
                // outlines the visible card while a halo lands in the
                // transparent trailing band (uSurfaceSize exceeds
                // uSurfaceFrameSize by the 2 × outerPad extension, like the
                // compositor's padded composite canvas). Identical for every
                // stage, mirroring the compositor's fold where each pack
                // sees the same canvas.
                surfaceScale: root.surfaceScale
                // These overlays (OSD + transient popups) are always shown for
                // the active context — the focused colour params are the
                // intended look. A literal true is correct here.
                surfaceFocused: true
                surfaceSize: root.shaderAnchorItem ? Qt.size((root.shaderAnchorItem.width + root.outerPad * 2) * root.surfaceScale, (root.shaderAnchorItem.height + root.outerPad * 2) * root.surfaceScale) : Qt.size(0, 0)
                // No outer-margin inset: the capture places the anchor content
                // at the canvas TOP-LEFT (trailing extension), so the frame
                // sits at its anchor-local rect directly. Adding outerPad here
                // belonged to the old symmetric capture and would push the
                // border/shadow off the visible card by the pad.
                surfaceFrameTopLeft: (root.shaderAnchorItem && root.shaderAnchorItem.shaderContentRect !== undefined) ? Qt.point(root.shaderAnchorItem.shaderContentRect.x * root.surfaceScale, root.shaderAnchorItem.shaderContentRect.y * root.surfaceScale) : Qt.point(0, 0)
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
