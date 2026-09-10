// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami
import PlasmaZones

/**
 * @brief The pointer preview's stage: a desktop, a simulated cursor, and the
 * pack's screen-space pass over both.
 *
 * Hosted by PackPreview, the shared stage wrapper, and by nothing else
 * directly. Every pointer preview reaches this file through it: the browser's
 * detail pane (PointerPreviewPane) frames a PackPreview, and the Pointer
 * page's chain rows host one bare through PackEditorBody, so a pack composes
 * the same stage in the catalogue and in the row that uses it.
 *
 * ## The canvas contract
 *
 * On screen a pointer pack is one full-output pass, and its uniforms are in
 * output pixels with the origin at the top-left. The stage below IS that
 * output: the PointerShaderItem fills it, and every position the controller
 * pushes is in the stage's own coordinates. So a `reach` of 64 px covers the
 * same fraction of the preview as it does of a real screen only when the
 * preview is screen-sized, which it never is. That is deliberate and it is the
 * same trade the decoration preview makes: a pack's features are shown at
 * their declared size so they stay legible, rather than scaled down into
 * illegibility to represent a bigger surface.
 *
 * Which is exactly why the stage is pinned to PreviewCanvas.size rather than
 * filling this item. Declared px against a slot that changes size is a pack
 * that changes shape: a 64 px halo is a comfortable accent on a detail pane
 * and very nearly the whole of a 200 px chain row. Composing at one fixed
 * canvas and letting the HOST scale the finished composition makes the two the
 * same render at two magnifications. This item never applies that scale
 * itself; it renders at canvas size and the host decides how big to show it.
 *
 * The stage is layered for the same reason DecorationChainPreview sets
 * `layeredStages` on its decoration (SurfaceDecoration.layeredStages): the
 * shader item is a render node that computes its own viewport and overwrites
 * the scissor Qt set for a clip, so a scaled host would otherwise get a
 * full-size pass drawn at a scaled position, spilling over the rows beside it.
 * Layering makes the node render into an FBO sized to its own item, which the
 * scene graph then composites with the host's transform and clip like any
 * other texture.
 *
 * ## The simulated pointer
 *
 * A timer walks the cursor around a figure eight and presses the left button
 * once per lap. Every sample goes through PointerPreviewController::drivePointer
 * into the same PointerHistory ring the compositor feeds, so the trail, the
 * velocity and the click rings are produced by the shipped sampler rather than
 * by preview-only arithmetic.
 *
 * ## Cursor order
 *
 * A `below` pack paints under the cursor, an `above` pack over it, exactly as
 * the compositor orders them (an above pack has KWin's cursor hidden and the
 * sprite drawn after the chain). The stand-in arrow and the shader item swap z
 * on the pack's declared layer so the preview shows the real order.
 */
Item {
    id: root

    /// The composition is canvas-sized, so a host that does not size this item
    /// explicitly gets the canvas.
    implicitWidth: PreviewCanvas.size.width
    implicitHeight: PreviewCanvas.size.height

    /// PointerPreviewController, handed down by the host.
    required property QtObject previewController
    /// The pack being previewed.
    required property string packId
    /// Friendly parameter map to render with.
    property var params: ({})
    /// Gates the shader item's EXISTENCE. Visibility only, never focus.
    property bool active: false
    /// Dropped-focus freeze: the clock stops and the preview holds its frame.
    property bool animating: true
    /// Whether to draw the stand-in cursor at all. The pane exposes it so a
    /// trail can be judged on its own.
    property bool showCursor: true

    /// Invokable-call dependency tick — see DecorationPreviewPane._rev.
    readonly property int _rev: previewController ? previewController.previewRevision : 0

    readonly property var _info: {
        void root._rev;
        return (previewController && packId.length > 0) ? (previewController.packInfo(packId) || ({})) : ({});
    }
    readonly property bool previewable: _info.valid === true
    /// True when the pack replaces the cursor rather than painting under it.
    readonly property bool _paintsAboveCursor: _info.layer === "above"

    /// True once the shader item is configured and its program has compiled.
    /// The host covers the stage until then.
    readonly property bool showable: shaderLoader.item !== null && shaderLoader.item.configured && shaderLoader.item.shaderItem.status === PointerShaderItem.Ready
    readonly property bool hasError: shaderLoader.item !== null && shaderLoader.item.configured && shaderLoader.item.shaderItem.status === PointerShaderItem.Error

    // Percent-encode a local file path for a file:// URL — same idiom as
    // ShaderBrowserDetailDialog._encodeFilePath.
    readonly property string _wallpaperUrl: {
        void root._rev;
        var p = previewController ? (previewController.wallpaperPath() || "") : "";
        return p.length > 0 ? "file://" + encodeURI(p).replace(/#/g, "%23").replace(/\?/g, "%3F") : "";
    }

    // ── The simulated pointer ────────────────────────────────────────────
    /// Lap phase in turns. One lap is one figure eight plus one click.
    property real _phase: 0
    /// Seconds per lap. Slow enough to read the trail, quick enough that a
    /// click pack does not keep the viewer waiting.
    readonly property real _lapSeconds: 4.0
    /// Where in the lap the button goes down, and for how long. Placed at the
    /// crossing point of the eight, where the cursor is briefly slowest, so a
    /// click ring is not smeared by the motion around it.
    readonly property real _pressAt: 0.5
    readonly property real _pressTurns: 0.03
    readonly property bool _pressed: _phase >= _pressAt && _phase < _pressAt + _pressTurns

    /// Cursor position in STAGE coordinates, which is the space the shader
    /// renders in. A Lissajous eight inset far enough from the edges that a
    /// wide trail and a large click ring stay inside the stage, since the
    /// shader item is a render node and paints outside any QML clip.
    readonly property real _inset: Math.min(PreviewCanvas.size.width, PreviewCanvas.size.height) * 0.22
    readonly property real cursorX: PreviewCanvas.size.width / 2 + (PreviewCanvas.size.width / 2 - _inset) * Math.sin(2 * Math.PI * _phase)
    readonly property real cursorY: PreviewCanvas.size.height / 2 + (PreviewCanvas.size.height / 2 - _inset) * Math.sin(4 * Math.PI * _phase)

    // Re-upload translated parameters as the host's editor moves them.
    onParamsChanged: {
        if (previewController && shaderLoader.item && shaderLoader.item.configured)
            previewController.updatePreviewParams(shaderLoader.item.shaderItem, packId, params);
    }

    // A pack switch on a live item reconfigures it in place. Every host today
    // rebuilds the whole component on a switch, so this is not reached, but
    // the configure contract belongs to the item that configures rather than
    // to a promise about every host. A registry revision needs no arm here:
    // PackPreview bounces the whole component on it, as it does for the
    // decoration stage.
    onPackIdChanged: {
        if (!previewController || !shaderLoader.item)
            return;
        previewController.resetPointer(shaderLoader.item.shaderItem);
        shaderLoader.item.configured = previewController.configurePreviewItem(shaderLoader.item.shaderItem, packId, params);
    }

    // Everything visible composes HERE, at the fixed canvas size, and the
    // composition is what a host scales. See the canvas contract above.
    Item {
        id: canvasStage

        anchors.centerIn: parent
        width: PreviewCanvas.size.width
        height: PreviewCanvas.size.height

        // Mandatory, not an optimisation. The shader item below is a render
        // node that ignores the scissor Qt sets for a clip and derives its
        // viewport from its own size and mapped origin, so without a layer a
        // scaled host would get a full-size pass drawn at a scaled position,
        // painting over whatever sits beside the preview. Layering routes the
        // node through an FBO the scene graph composites normally.
        layer.enabled: true
        // A host's fit is reduce-only, so the layer is only ever minified;
        // mipmaps are what keep that minification from aliasing the trail.
        layer.mipmap: true
        layer.smooth: true

        // The desktop the cursor moves over. A trail or a halo reads completely
        // differently over a photograph than over flat grey, so the preview judges
        // a pack against the ground it will actually paint on.
        Image {
            anchors.fill: parent
            source: root._wallpaperUrl
            fillMode: Image.PreserveAspectCrop
            sourceSize.width: Math.max(1, Math.round(width))
            sourceSize.height: Math.max(1, Math.round(height))
            asynchronous: true
            visible: status === Image.Ready
        }

        // The stand-in cursor. A plain arrow rather than the user's cursor theme:
        // the theme's sprite is not reachable from the settings app, and a pack is
        // judged on what it paints around the pointer rather than on the pointer.
        Canvas {
            id: cursorArrow

            width: Kirigami.Units.gridUnit
            height: Kirigami.Units.gridUnit * 1.4
            // Hotspot at the tip, so the drawn arrow points at the position the
            // shader is told about rather than trailing behind it.
            x: Math.round(root.cursorX)
            y: Math.round(root.cursorY)
            z: root._paintsAboveCursor ? 0 : 2
            visible: root.showCursor
            antialiasing: true

            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                var w = width;
                var h = height;
                ctx.beginPath();
                ctx.moveTo(0, 0);
                ctx.lineTo(0, h);
                ctx.lineTo(w * 0.28, h * 0.74);
                ctx.lineTo(w * 0.48, h * 1.06);
                ctx.lineTo(w * 0.72, h * 0.94);
                ctx.lineTo(w * 0.52, h * 0.63);
                ctx.lineTo(w, h * 0.6);
                ctx.closePath();
                // Intentionally not theme colours: this stands in for the
                // cursor sprite, which is white-on-black whatever the theme.
                ctx.fillStyle = "white";
                ctx.fill();
                ctx.lineWidth = 1;
                ctx.strokeStyle = "black";
                ctx.stroke();
            }
        }

        // The pack itself, in a Loader so the shader item only exists while the
        // host shows a previewable pack and is torn down when it stops.
        Loader {
            id: shaderLoader

            anchors.fill: parent
            z: root._paintsAboveCursor ? 2 : 1
            active: root.active && root.previewable
            visible: active

            sourceComponent: Item {
                id: stage

                property alias shaderItem: shaderItem
                property bool configured: false

                PointerShaderItem {
                    id: shaderItem

                    anchors.fill: parent
                    // A pointer pack ticks continuously while the pointer is live,
                    // so iTime free-runs in seconds rather than sweeping a
                    // progress value. Frozen with the rest of the preview when the
                    // app is not frontmost.
                    playing: root.animating

                    Component.onCompleted: {
                        root.previewController.resetPointer(shaderItem);
                        stage.configured = root.previewController.configurePreviewItem(shaderItem, root.packId, root.params);
                    }
                }

                // The pointer clock. Drives the lap phase and hands every sample to
                // the controller, which feeds the shipped PointerHistory sampler.
                Timer {
                    id: pointerClock

                    property real lastMs: 0

                    interval: 16
                    repeat: true
                    running: stage.configured && root.animating
                    onRunningChanged: {
                        // A fresh lap on resume, so a preview that was frozen for a
                        // while does not report one enormous frame delta.
                        lastMs = 0;
                        if (running)
                            root.previewController.resetPointer(shaderItem);
                    }
                    onTriggered: {
                        var now = Date.now();
                        var dt = pointerClock.lastMs > 0 ? now - pointerClock.lastMs : pointerClock.interval;
                        pointerClock.lastMs = now;
                        root._phase = (root._phase + dt / 1000 / root._lapSeconds) % 1;
                        // The arrow's drawn size rides along so the frame's
                        // cursor rect is the rect the stand-in actually
                        // occupies, hotspot at the tip.
                        root.previewController.drivePointer(shaderItem, root.cursorX, root.cursorY, cursorArrow.width, cursorArrow.height, dt, root._pressed);
                    }
                }
            }
        }
    }
}
