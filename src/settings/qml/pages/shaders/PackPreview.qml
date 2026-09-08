// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

/**
 * @brief One pack's preview STAGE, rendered the same way wherever it appears.
 *
 * The stage and nothing else: no frame, no toggles, no captions. It dispatches
 * to the family's render core and hides the ways those cores differ, so a host
 * says which family it is in and nothing else.
 *
 * Every host goes through this, which is what makes a pack look the same in
 * all of them:
 *
 *   - the decoration and pointer chain rows, and the animation event card,
 *     host it bare — a compact row has no room for chrome;
 *   - the browser's detail panes (DecorationPreviewPane, PointerPreviewPane)
 *     host it INSIDE their frame and keep their own chrome around it: the
 *     Focused toggle, the Pause and Show cursor toggles, the stand-in and
 *     audio notices, and their own cover (see `showPlaceholder`).
 *
 * ## What it normalises
 *
 * **Composition size.** Every pane composes at PreviewCanvas.size and this
 * wrapper displays the finished composition scaled, REDUCE-ONLY, as one unit.
 * Scaling the result rather than the pane is the only thing that makes the
 * same pack look the same in a narrow chain row and in the browser's detail
 * dialog: a multipass pack's blur runs in a buffer sized from the item, the
 * capture margins are fixed pixel amounts, and a pointer pack's `reach` is
 * declared in output pixels. All of those follow item size rather than any
 * parameter, so only a fixed canvas holds them still. The reasoning is written
 * up in full on DecorationChainPreview._canvasSize.
 *
 * The scale is clamped at 1.0 because magnifying a composed texture only
 * softens it; a slot larger than the canvas keeps its leftover room empty.
 *
 * The decoration pane already applies that scale itself and is handed the
 * whole slot instead — see `_selfScaling`.
 *
 * **Layering.** The pointer and animation cores paint through render nodes,
 * which ignore ancestor clipping: a scale or a clip only reaches them once the
 * composition is a layer. So the stage below is layered, which is the same
 * reason DecorationChainPreview turns on `layeredStages`.
 *
 * **Readiness.** The three cores report progress differently, and `showable`
 * and `hasError` below are the one answer a host covers on.
 *
 * **How many run at once.** PackPreviewGate caps it, across every host.
 *
 * ## active vs animating
 *
 * `active` is EXISTENCE: false instantiates nothing at all, so a collapsed row
 * costs nothing. `animating` is a freeze lever that leaves the composition
 * standing. They stay separate on purpose. Folding "the window is not
 * frontmost" into `active` makes a preview vanish while it is still fully
 * visible under a Plasma applet, and replay its whole load on refocus — a bug
 * already fixed once, recorded at DecorationChainPreview.qml:59-64.
 */
Item {
    id: root

    /// Which family's render core to run: "decoration", "animation" or
    /// "pointer".
    required property string previewKind
    /// The family's preview controller, handed down by the host's bridge.
    required property QtObject previewController
    /// The pack to show.
    required property string packId
    /// Friendly parameter map to render with. Empty means the pack's declared
    /// defaults.
    property var params: ({})
    /// Master gate. False instantiates no pane and no shader item.
    property bool active: false
    /// Freeze without tearing down. Clocks stop and the preview holds its
    /// frame; the composition stays on screen.
    property bool animating: true

    // ── Core-specific passthroughs ───────────────────────────────────────
    // Three properties that belong to one core each and are inert for the
    // others. They are here rather than behind some generic property bag
    // because the panes that drive them are the ONLY hosts that do: the
    // browser's detail panes own the comparison affordances (a focus toggle,
    // a cursor toggle) that a compact chain row has no room for, and a bag
    // would trade a typed, documented property for an untyped map that no
    // reader can enumerate.

    /// Decoration only: drives uSurfaceFocused, and nothing else. Defaults to
    /// focused, which is the pack as its author intended it to be seen.
    property bool focused: true
    /// Decoration only: caption on the stand-in card. Taken from the host so
    /// its wording lives where the rest of that host's copy does.
    property string cardTitle: i18nc("@title sample window in a shader preview", "Sample Window")
    /// Pointer only: whether to draw the stand-in cursor at all. Turning it
    /// off is the comparison — what the pack alone paints.
    property bool showCursor: true

    /// Whether to cover the stage with the shared placeholder while it is
    /// arriving, or when it failed to compile.
    ///
    /// A host with its own placeholder sets this false and covers on
    /// `showable` / `hasError` itself. The browser's detail panes do exactly
    /// that: their cover spans the whole framed slot rather than the stage
    /// inside it, and two covers stacked would draw the same notice twice.
    property bool showPlaceholder: true

    /// Whether everything the finished preview is made of has arrived. A host
    /// showing this to a user should COVER it until this is true, and cover
    /// rather than hide: a capture chain that is not rendering never reaches
    /// readiness, so hiding to wait for it waits forever. The cover below does
    /// exactly that, so a plain host needs nothing of its own.
    readonly property bool showable: {
        if (!root._stageActive)
            return false;

        const item = paneLoader.item;
        if (!item)
            return false;

        // The decoration and pointer panes each publish their own settled
        // gate, which knows about pack switches and stage compilation.
        if (root.previewKind === "decoration" || root.previewKind === "pointer")
            return item.showable === true;

        // The animation pane publishes no readiness of its own. Its subject is
        // staged from Kirigami colours and a live capture rather than from an
        // asynchronous chain, so there is no compile for the pane to report;
        // what CAN be said is whether the registry considers the pack
        // previewable at all, which is the same test the pane gates its own
        // loader on.
        return root._packValid;
    }

    /// A stage failed to compile, so there is nothing worth showing.
    ///
    /// Only the decoration and pointer panes can answer this. The animation
    /// pane reports no compile status, so an animation pack that fails to
    /// build shows that pane's own empty subject rather than an error notice.
    /// Reported as false rather than guessed, because a host that dimmed every
    /// animation preview on a suspicion would be wrong far more often than it
    /// was right.
    readonly property bool hasError: {
        const item = paneLoader.item;
        if (!item)
            return false;

        if (root.previewKind === "decoration" || root.previewKind === "pointer")
            return item.hasError === true;

        return false;
    }

    /// The decoration core fits the canvas into its own bounds already, and
    /// its stages are layered for it. Scaling it a second time here would
    /// square the reduction, so it is handed the whole slot untouched.
    readonly property bool _selfScaling: previewKind === "decoration"

    /// Whether the loaded component is a whole PANE — a canvas-pinned frame
    /// plus caption and notice rows underneath — rather than a bare canvas.
    ///
    /// True for animation, and it is the one asymmetry in this file. The
    /// pointer and decoration families each split a canvas-shaped stage
    /// (PointerPreviewCanvas, DecorationChainPreview) out of the pane that
    /// frames it, and this wrapper loads the stage. The animation family has
    /// no such split: AnimationPreviewPane pins its field to the canvas but
    /// carries the per-class caption and the audio notice in the same item.
    ///
    /// So it cannot be given the canvas HEIGHT — that would leave its frame
    /// shorter than the canvas and the field would overhang the very frame
    /// meant to contain it. It gets the canvas width and the host's height
    /// instead, which is the shape it already expects, and only the width
    /// drives the scale.
    readonly property bool _paneShaped: previewKind === "animation"

    /// Room for a pane's caption and notice rows below its frame, on top of
    /// the canvas itself. AnimationPreviewPane is anchor-filled by every host
    /// and so reports no implicit height of its own; this is the allowance
    /// that keeps its frame at full canvas height once those rows are laid
    /// out. Theme-derived, so it follows the font the captions are drawn in.
    readonly property real _paneChromeReserve: Kirigami.Units.gridUnit * 4

    /// Whether a slot is even claimable: a kind, a pack, a controller, and a
    /// host that has switched us on.
    readonly property bool _wantsPreview: active && previewKind.length > 0 && packId.length > 0 && previewController !== null

    /// Granted by PackPreviewGate, which caps how many previews run at once.
    /// Written from there, never bound, so it must stay a plain property.
    property bool _capAllowed: false

    /// Invokable-call dependency tick — see DecorationPreviewPane._rev.
    readonly property int _rev: previewController ? previewController.previewRevision : 0

    readonly property bool _packValid: {
        void root._rev;
        if (!previewController || packId.length === 0)
            return false;

        const info = previewController.packInfo(packId) || ({});
        return info.valid === true;
    }

    /// One-frame teardown pulse. A registry revision while a preview is open
    /// (a pack installed, or its sources edited on disk) has to rebuild the
    /// whole pane rather than re-read metadata: every pane configures its
    /// shader item once, in Component.onCompleted, and a live item never
    /// rebakes an edited source on its own. Bouncing the Loader reconfigures
    /// from scratch, and the fresh load keys the bake cache on the edited
    /// files' new mtimes.
    ///
    /// Held here, above the pane, so all three kinds get it — the decoration
    /// pane carries no pulse of its own. Folded into the loader's `active`
    /// binding rather than written to it imperatively, which would sever the
    /// binding.
    property bool _refreshHold: false
    on_RevChanged: {
        if (!paneLoader.active)
            return;

        root._refreshHold = true;
        Qt.callLater(function () {
            root._refreshHold = false;
        });
    }

    readonly property bool _stageActive: _wantsPreview && _capAllowed && !_refreshHold

    on_WantsPreviewChanged: {
        if (root._wantsPreview)
            PackPreviewGate.request(root);
        else
            PackPreviewGate.release(root);
    }
    Component.onCompleted: {
        // Idempotent: the handler above may already have claimed the slot
        // during initialisation, and request() keeps our place in that case.
        if (root._wantsPreview)
            PackPreviewGate.request(root);
    }
    Component.onDestruction: PackPreviewGate.release(root)

    /// Uniform reduce-only fit of the canvas into whatever slot we occupy, so
    /// the composition is never distorted and never magnified.
    ///
    /// A pane-shaped child is fitted on WIDTH alone: its height is the host's
    /// already, so folding height in here would shrink it a second time and
    /// leave a band of empty slot under it.
    readonly property real _canvasScale: {
        const cw = PreviewCanvas.size.width;
        const ch = PreviewCanvas.size.height;
        if (!isFinite(root.width) || root.width <= 0 || cw <= 0 || ch <= 0)
            return 1.0;

        const byWidth = root.width / cw;
        if (root._paneShaped)
            return Math.min(1.0, byWidth);

        if (!isFinite(root.height) || root.height <= 0)
            return 1.0;

        return Math.min(1.0, byWidth, root.height / ch);
    }

    // Full canvas where there is room, and the height the width-driven
    // reduction actually needs where there is not, so a narrow host reserves
    // no empty band under the composition. Derived from `width` alone: reading
    // `height` here, which a layout sets FROM this, would be a loop.
    implicitWidth: PreviewCanvas.size.width
    implicitHeight: {
        const cw = PreviewCanvas.size.width;
        const ch = PreviewCanvas.size.height;
        const chrome = root._paneShaped ? root._paneChromeReserve : 0;
        if (!isFinite(root.width) || root.width <= 0 || cw <= 0)
            return ch + chrome;

        return Math.round((ch + chrome) * Math.min(1.0, root.width / cw));
    }

    // The composition, and the scale applied to it as one unit.
    Item {
        id: stage

        anchors.centerIn: parent
        width: root._selfScaling ? root.width : PreviewCanvas.size.width
        height: {
            if (root._selfScaling)
                return root.height;

            // A pane keeps the host's height, expressed BEFORE the scale so
            // that scaling it lands back on exactly the slot. Floored at the
            // canvas height so the pane's frame is never shorter than the
            // composition it contains, whatever a cramped host offers.
            if (root._paneShaped)
                return Math.max(PreviewCanvas.size.height, root.height / Math.max(root._canvasScale, 0.01));

            return PreviewCanvas.size.height;
        }
        scale: root._selfScaling ? 1.0 : root._canvasScale
        transformOrigin: Item.Center
        // A render node draws at full size, outside any QML clip, unless the
        // composition it belongs to is a layer. The decoration pane layers its
        // own stages, so layering it again here would only cost a texture.
        //
        // Only while the scale is actually reducing. At 1:1 there is nothing
        // for a layer to correct — the stage is exactly the canvas and the
        // panes keep their own painting inside it — and skipping it there
        // saves a full texture per preview and keeps any text in a pane-shaped
        // child crisp. It also keeps the animation pane's capture chain
        // (a ShaderEffectSource over its stand-in card) out of a nested layer
        // in the case that matters most, the browser's 1:1 detail slot.
        layer.enabled: !root._selfScaling && root._canvasScale < 1.0
        layer.mipmap: true

        Loader {
            id: paneLoader

            anchors.fill: parent
            active: root._stageActive
            visible: active
            sourceComponent: {
                switch (root.previewKind) {
                case "decoration":
                    return decorationComponent;
                case "pointer":
                    return pointerComponent;
                case "animation":
                    return animationComponent;
                }
                return null;
            }
        }
    }

    Component {
        id: decorationComponent

        DecorationChainPreview {
            previewController: root.previewController
            packId: root.packId
            params: root.params
            active: true
            animationsPaused: !root.animating
            focused: root.focused
            audioSpectrum: root.previewController ? (root.previewController.audioSpectrum || []) : []
            cardTitle: root.cardTitle
        }
    }

    Component {
        id: pointerComponent

        PointerPreviewCanvas {
            previewController: root.previewController
            packId: root.packId
            params: root.params
            active: true
            animating: root.animating
            showCursor: root.showCursor
        }
    }

    Component {
        id: animationComponent

        AnimationPreviewPane {
            previewController: root.previewController
            packId: root.packId
            liveParams: root.params
            active: true
            animating: root.animating
        }
    }

    // Covers the preview while it is still arriving, and stays up when a pack
    // failed to compile. Opaque, and the colour a preview slot is framed with,
    // so it reads as the empty slot rather than as a hole. Covering rather
    // than hiding is deliberate — see `showable`.
    PZCommon.ShaderPreviewPlaceholder {
        anchors.fill: parent
        visible: root.showPlaceholder && (!root.showable || root.hasError)
        backgroundColor: Kirigami.Theme.alternateBackgroundColor
        radius: Kirigami.Units.smallSpacing
        text: root.hasError ? i18nc("@info:placeholder shader preview", "This pack's shader did not compile.") : i18nc("@info:placeholder shader preview", "Preview unavailable")
    }
}
