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
 * C++-resolved decoration props. When `decorationShaderSource` is empty (no pack
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

    /// C++-resolved surface pack, written by OverlayService::applyDecoration:
    ///   • decorationShaderSource  — file:// url of the pack's effect.frag (""
    ///                               = no decoration; component stays inert).
    ///   • decorationParamPreamble — generated `#define p_<id> …` preamble.
    ///   • decorationShaderParams  — translated `customParamsN_*` / `customColorN`
    ///                               slot map (SurfaceShaderRegistry output).
    property url decorationShaderSource
    property string decorationParamPreamble: ""
    property var decorationShaderParams: ({})

    /// Logical→device scale for the decorated surface. The OSD shell tracks the
    /// active output's devicePixelRatio; Screen.devicePixelRatio is the live
    /// value for the window this item lives in.
    readonly property real surfaceScale: Screen.devicePixelRatio

    /// Whether any decoration is active. Gates the capture + shader items so an
    /// undecorated card pays nothing and draws its native card.
    readonly property bool decorationActive: decorationShaderSource.toString() !== "" && shaderAnchorItem !== null

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
        width: root.shaderAnchorItem ? root.shaderAnchorItem.width : 0
        height: root.shaderAnchorItem ? root.shaderAnchorItem.height : 0
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

    // ── Surface shader pass ──────────────────────────────────────────────────
    // Sibling of the captured card (parented to this host, which is a sibling of
    // the slot's content Loader — never an ancestor of the anchor, so no feedback
    // loop). Positioned over the anchor's on-screen rect, mapped into this
    // host's coordinate space.
    SurfaceShaderItem {
        id: decoration

        // Anchor rect mapped into this host's coordinate space. The anchor lives
        // deep inside the loaded content; mapToItem walks the transform chain so
        // the decoration lands exactly over the card regardless of nesting.
        // Assumes the overlay host uses a fixed anchors.fill layout (it does):
        // mapToItem is not reactive to an ancestor transform change, so this
        // binding re-resolves only on decorationActive / shaderAnchorItem change,
        // not if a future host animated the anchor's ancestors mid-frame.
        readonly property point anchorOrigin: (root.decorationActive && root.shaderAnchorItem) ? root.shaderAnchorItem.mapToItem(root, 0, 0) : Qt.point(0, 0)

        // SurfaceAnimator anchor (compose — see _applyAnchorRouting). When
        // decoration is active this item IS the surface the animator captures and
        // animates; the raw card's shaderAnchor is demoted so the animator picks
        // this one. shaderContentRect mirrors the raw card's frame rect (this item
        // is sized to and positioned over the raw anchor 1:1, so the same local
        // coords apply) — the animator uses it for its card-space remap.
        property bool shaderAnchor: root.decorationActive
        property rect shaderContentRect: (root.shaderAnchorItem && root.shaderAnchorItem.shaderContentRect !== undefined) ? root.shaderAnchorItem.shaderContentRect : Qt.rect(0, 0, width, height)

        visible: root.decorationActive
        x: anchorOrigin.x
        y: anchorOrigin.y
        width: root.shaderAnchorItem ? root.shaderAnchorItem.width : 0
        height: root.shaderAnchorItem ? root.shaderAnchorItem.height : 0

        // The pack samples this snapshot as uTexture0 and REPLACES the card.
        sourceItem: cardSnapshot

        // Surface-state inputs (device px). The whole CAPTURE item is uTexture0;
        // the FRAME rect within it (shaderContentRect, anchor-local logical px)
        // scaled to device px is what the border rounds to — excluding the glow
        // ring PopupFrame's capture adds. surfaceFrameSize == surfaceSize only
        // if the capture had no padding; PopupFrame pads, so the real frame
        // rect is fed through.
        surfaceScale: root.surfaceScale
        // These overlays (OSD + transient popups) are always shown for the
        // active context — the focused colour params are the intended look. A
        // literal true is correct here.
        surfaceFocused: true
        surfaceSize: root.shaderAnchorItem ? Qt.size(root.shaderAnchorItem.width * root.surfaceScale, root.shaderAnchorItem.height * root.surfaceScale) : Qt.size(0, 0)
        surfaceFrameTopLeft: (root.shaderAnchorItem && root.shaderAnchorItem.shaderContentRect !== undefined) ? Qt.point(root.shaderAnchorItem.shaderContentRect.x * root.surfaceScale, root.shaderAnchorItem.shaderContentRect.y * root.surfaceScale) : Qt.point(0, 0)
        surfaceFrameSize: (root.shaderAnchorItem && root.shaderAnchorItem.shaderContentRect !== undefined) ? Qt.size(root.shaderAnchorItem.shaderContentRect.width * root.surfaceScale, root.shaderAnchorItem.shaderContentRect.height * root.surfaceScale) : Qt.size(0, 0)

        // Pack source + params. paramPreamble/shaderParams BEFORE shaderSource
        // is the load-trigger ordering the inherited ShaderEffect setters expect;
        // here they are bindings, so QML evaluates the value graph before the
        // first paint regardless of declaration order, but mirroring the
        // applyShaderInfoToWindow order keeps the intent explicit.
        paramPreamble: root.decorationParamPreamble
        shaderParams: root.decorationShaderParams
        shaderSource: root.decorationShaderSource
        // iTime is left at its default: the border pack is static (no iTime
        // reference survives the linker), so no per-frame driver is wired.
    }
}
