// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "surfaceshaderitem.h"

#include "core/platform/logging.h"

#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorRendering/ShaderNodeRhi.h>

#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>
#include <PhosphorSurface/SurfaceUniformProfile.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <memory>
#include <optional>

namespace PlasmaZones {

// ============================================================================
// Construction / Destruction
// ============================================================================

QStringList SurfaceShaderItem::surfaceIncludePaths()
{
    // `#include <surface_uniforms.glsl>` in a pack's effect.frag resolves
    // through these dirs. Mirror ZoneShaderItem: locateAll() (not locate()) so
    // the system dir is included alongside ~/.local/share — the user dir holds
    // user packs but not the shared include. Surface packs install to
    // `plasmazones/surface` (singular; see the install() rule in the top-level
    // CMakeLists), the third pack category beside `plasmazones/overlays` and
    // `plasmazones/animations`. The daemon warm-bake (daemon.cpp) calls this
    // same function — see the header doc for why the two must not diverge.
    const QStringList allSurfaceDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/surface"), QStandardPaths::LocateDirectory);
    QStringList includePaths;
    for (const QString& dir : allSurfaceDirs) {
        const QString sharedDir = dir + QStringLiteral("/shared");
        if (QDir(sharedDir).exists()) {
            includePaths.append(sharedDir);
        }
        includePaths.append(dir);
    }
    return includePaths;
}

SurfaceShaderItem::SurfaceShaderItem(QQuickItem* parent)
    : PhosphorRendering::ShaderEffect(parent)
{
    setShaderIncludePaths(surfaceIncludePaths());

    // The surface shader node carries the item's opacity (pushed via
    // setSurfaceOpacity(opacity()) each updatePaintNode), but the base
    // ShaderEffect does not repaint on an opacity change. Schedule a paint when
    // the item's own opacity changes so a host
    // fading the decoration (the "host can fade the decoration" contract below)
    // actually re-uploads the new value instead of going stale.
    connect(this, &QQuickItem::opacityChanged, this, &QQuickItem::update);
    // No audioSpectrumChanged→update() connection is needed: the base
    // ShaderEffect's setAudioSpectrum / setAudioSpectrumVariant already call
    // update() on a value change, and update() schedules this item's
    // updatePaintNode whether or not the override is present.

    // Seed the inherited iMouse to the off-surface sentinel. The surface UBO
    // publishes iMouse (hover-reactive packs), and a host that wires no hover
    // source must read as "cursor off the surface", not as a phantom hover at
    // the base's (0, 0) default. A host that does wire a HoverHandler simply
    // overwrites this.
    setIMouse(QPointF(-1.0, -1.0));
}

SurfaceShaderItem::~SurfaceShaderItem()
{
    // Nothing to do HERE: updatePaintNode registers its node with the base
    // (registerRenderNode), so the base ~ShaderEffect severs the node's
    // back-pointer (invalidateItem) and the sceneGraphAboutToStop handler
    // releases its GPU resources — the same coverage the base gives its own
    // node. The scene graph owns and deletes the node itself.
}

// ============================================================================
// Render Node Factory
// ============================================================================

PhosphorRendering::ShaderNodeRhi* SurfaceShaderItem::createShaderNode()
{
    // The surface UBO profile is the ONLY thing that differs from the base/
    // overlay render path — a stock ShaderNodeRhi driven by the 672-byte
    // SurfaceUniformProfile rather than a SurfaceShaderItem-specific subclass.
    return new PhosphorRendering::ShaderNodeRhi(this,
                                                std::make_unique<PhosphorSurfaceShaders::SurfaceUniformProfile>());
}

// ============================================================================
// Surface-state setters
// ============================================================================
//
// These feed the surface-only fields of PhosphorShaders::UboFrameState that
// SurfaceUniformProfile::fill() reads. updatePaintNode() pushes them into the
// render node each frame via ShaderNodeRhi's surface-state setters, so a border
// or rounded-corner pack sees the real surface/frame geometry, scale and focus.
// Storing + emitting on change keeps the QML host's binding surface reactive.

void SurfaceShaderItem::setSurfaceScale(qreal scale)
{
    if (qFuzzyCompare(m_surfaceScale, scale)) {
        return;
    }
    m_surfaceScale = scale;
    Q_EMIT surfaceScaleChanged();
    update();
}

void SurfaceShaderItem::setSurfaceFocused(bool focused)
{
    if (m_surfaceFocused == focused) {
        return;
    }
    m_surfaceFocused = focused;
    Q_EMIT surfaceFocusedChanged();
    update();
}

void SurfaceShaderItem::setSurfaceSize(const QSizeF& size)
{
    if (m_surfaceSize == size) {
        return;
    }
    m_surfaceSize = size;
    Q_EMIT surfaceSizeChanged();
    update();
}

void SurfaceShaderItem::setSurfaceFrameTopLeft(const QPointF& topLeft)
{
    if (m_surfaceFrameTopLeft == topLeft) {
        return;
    }
    m_surfaceFrameTopLeft = topLeft;
    Q_EMIT surfaceFrameTopLeftChanged();
    update();
}

void SurfaceShaderItem::setSurfaceFrameSize(const QSizeF& size)
{
    if (m_surfaceFrameSize == size) {
        return;
    }
    m_surfaceFrameSize = size;
    Q_EMIT surfaceFrameSizeChanged();
    update();
}

void SurfaceShaderItem::setBackdropScreenRect(const QRectF& rect)
{
    if (m_backdropScreenRect == rect) {
        return;
    }
    m_backdropScreenRect = rect;
    Q_EMIT backdropScreenRectChanged();
    update();
}

void SurfaceShaderItem::setBackdropSurfaceRect(const QRectF& rect)
{
    if (m_backdropSurfaceRect == rect) {
        return;
    }
    m_backdropSurfaceRect = rect;
    Q_EMIT backdropSurfaceRectChanged();
    update();
}

void SurfaceShaderItem::syncBackdropRect(PhosphorRendering::ShaderNodeRhi* node) const
{
    // The whole texture, which is both the correct answer for a host with no
    // placement to offer and the behaviour every host had before this existed.
    constexpr float kWholeTexture[4] = {0.0f, 0.0f, 1.0f, 1.0f};

    const QImage wallpaper = wallpaperTexture();
    if (wallpaper.isNull() || !m_backdropScreenRect.isValid() || !m_backdropSurfaceRect.isValid()) {
        node->setBackdropRect(kWholeTexture[0], kWholeTexture[1], kWholeTexture[2], kWholeTexture[3]);
        return;
    }

    // The UNCLAMPED slice. The wallpaper is placed over the screen
    // aspect-correct and centred with the overflow cropped, exactly as
    // wallpaper.glsl::wallpaperUv does, and the surface's rect is mapped into
    // that placement. Doing the fit here rather than in the shader is what
    // keeps a 16:9 wallpaper from being stretched into a 4:3 surface.
    //
    // Unclamped matters: a padded stage is the anchor grown by outerPad on
    // every side, so a glow or shadow near the screen edge legitimately reaches
    // past it. The shader spreads uv across that whole padded quad, so a slice
    // clamped to the screen would be stretched to cover it and shift the
    // backdrop by the overhang. Out-of-range coordinates instead let the
    // sampler's edge clamp handle it, which is what a halo running off the
    // screen should look like.
    const std::optional<QRectF> slice = PhosphorShaders::ShaderRegistry::wallpaperSliceNormalized(
        wallpaper.size(), m_backdropScreenRect.toAlignedRect(), m_backdropSurfaceRect.toAlignedRect());
    if (!slice) {
        node->setBackdropRect(kWholeTexture[0], kWholeTexture[1], kWholeTexture[2], kWholeTexture[3]);
        return;
    }
    node->setBackdropRect(static_cast<float>(slice->x()), static_cast<float>(slice->y()),
                          static_cast<float>(slice->width()), static_cast<float>(slice->height()));
}

// ============================================================================
// Scene Graph Integration
// ============================================================================

QSGNode* SurfaceShaderItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data)
{
    Q_UNUSED(data)

    if (width() <= 0 || height() <= 0) {
        if (oldNode) {
            // Mirror the parent ShaderEffect's zero-size branch: sever the
            // node's back-pointer to this item via invalidateItem() before
            // deleting it, so any in-flight render-thread access fails safe
            // instead of walking a freed item. Deregister from the base so
            // its destructor doesn't invalidate a dangling pointer.
            static_cast<PhosphorRendering::ShaderNodeRhi*>(oldNode)->invalidateItem();
            registerRenderNode(nullptr);
            delete oldNode;
        }
        return nullptr;
    }

    // freshNode covers SG-deletion + first-call: a brand-new node has no shader
    // baked, so it must trigger a load even when nothing is dirty.
    bool freshNode = false;
    auto* node = static_cast<PhosphorRendering::ShaderNodeRhi*>(oldNode);
    if (!node) {
        // Scene graph deleted the previous node, or first call. Route node
        // creation through createShaderNode() so the surface UBO profile is
        // installed.
        node = createShaderNode();
        freshNode = true;
    }
    // REGISTER the node with the base, on every frame rather than only the
    // fresh-node path: this override replaces the base updatePaintNode that
    // normally does the tracking, and without it the base destructor /
    // sceneGraphAboutToStop teardown (invalidateItem + resource release)
    // silently no-ops for this item — a render-thread prepare() between item
    // destruction and node deletion would then dereference the freed item. The
    // scene graph can also swap in a node the base is not tracking, and a
    // reuse-path frame is the only chance to notice.
    registerRenderNode(node);

    // ── Sync base properties (time, params, colors, audio, multipass, depth) ──
    // syncBasePropertiesToNode pushes user textures (slots 0..3) and the
    // installed uniform extension (none here). Surface packs have no labels
    // texture, zone counts, or extension.
    syncBasePropertiesToNode(node);

    // ── Sync source texture provider (slot 0 / binding 7, uTexture0) ──
    // The base ShaderEffect binds this in ITS updatePaintNode, NOT in
    // syncBasePropertiesToNode — so a subclass that fully reimplements
    // updatePaintNode (like this one) must replicate it or sourceItem()
    // never reaches the node and uTexture0 stays unbound (surfaceTexel then
    // samples transparent black and the decoration shows no content). Pushed
    // every paint so a late setSourceItem picks up and a torn-down source
    // (QPointer auto-nulls) clears the binding. Mirrors shadereffect.cpp.
    if (QQuickItem* src = sourceItem(); src && src->isTextureProvider()) {
        node->setSourceTextureProvider(src->textureProvider());
    } else {
        node->setSourceTextureProvider(nullptr);
    }

    // ── Push surface-only state ──────────────────────────────────────
    // These land in the surface UBO's scene region (a SurfaceUniformProfile
    // reads them; a BaseUniformProfile ignores them). The geometry is what a
    // border/rounded-corner pack uses to place its edges. Opacity comes from
    // the item's own opacity so a host can fade the decoration.
    node->setSurfaceOpacity(static_cast<float>(opacity()));
    node->setSurfaceScale(static_cast<float>(m_surfaceScale));
    node->setSurfaceFocused(m_surfaceFocused);
    node->setSurfaceSize(static_cast<float>(m_surfaceSize.width()), static_cast<float>(m_surfaceSize.height()));
    node->setSurfaceFrameTopLeft(static_cast<float>(m_surfaceFrameTopLeft.x()),
                                 static_cast<float>(m_surfaceFrameTopLeft.y()));
    node->setSurfaceFrameSize(static_cast<float>(m_surfaceFrameSize.width()),
                              static_cast<float>(m_surfaceFrameSize.height()));
    // After the base sync above, which is what installs the wallpaper image
    // this reads. Resolved here rather than in the setters because it depends
    // on that image as much as on the two rects.
    syncBackdropRect(node);

    // NB: the audio spectrum (CAVA, binding 6 + the UBO's iAudioSpectrumSize) is
    // pushed by syncBasePropertiesToNode above — the daemon writes the inherited
    // audioSpectrum Q_PROPERTY via OverlayService, and a pack reads it through
    // surface_audio.glsl. The base ShaderEffect's own setAudioSpectrum /
    // setAudioSpectrumVariant call update() on a value change (see the
    // constructor note on why no extra connection is wired), which re-runs this
    // node sync on a new spectrum.

    // ── Sync shader source ───────────────────────────────────────────
    // Reload only on an actual dirty flag (runtime setShaderSource /
    // setShaderIncludePaths / reloadShader, or device-loss via
    // sceneGraphAboutToStop) or a freshly created node — mirrors
    // ShaderEffect::updatePaintNode. Do NOT reload on !isShaderReady(): a
    // permanent load/compile failure leaves the node un-ready forever, so
    // reloading on it re-runs the loader + glslang bake on EVERY frame.
    const bool wasDirty = consumeShaderDirty();
    const bool needLoad = wasDirty || freshNode;
    const bool shaderSourceValid = shaderSource().isValid() && !shaderSource().isEmpty();

    if (needLoad) {
        if (shaderSourceValid) {
            QString fragPath = shaderSource().toLocalFile();
            if (shaderSource().scheme() == QLatin1String("qrc")) {
                fragPath = QLatin1Char(':') + shaderSource().path();
            }

            // Resolve the vertex shader: an explicit per-item vertexShaderUrl
            // wins, then a per-pack `surface.vert` beside the fragment, then a
            // shared `surface.vert` from the include paths. Surface packs ship
            // no vertex shader today (the field defaults empty), so this falls
            // through to the include-path lookup — when the on-screen host
            // stage ships a shared fullscreen-quad surface.vert it resolves
            // here without a code change. This resolves the VERTEX stage, which
            // has no entry scaffold and relies on a shared surface.vert for its
            // main(); the FRAGMENT stage does get one (setEntryScaffold below,
            // so a pack may ship only `vec4 pSurface(vec2 uv)`).
            QString vertPath;
            if (vertexShaderUrl().isValid() && !vertexShaderUrl().isEmpty()) {
                vertPath = vertexShaderUrl().toLocalFile();
                // Mirror the fragment path: a qrc: URL has no local file, so
                // map it to the ':'-prefixed resource path instead of silently
                // dropping it and falling through to the surface.vert lookup.
                if (vertexShaderUrl().scheme() == QLatin1String("qrc")) {
                    vertPath = QLatin1Char(':') + vertexShaderUrl().path();
                }
            }
            if (vertPath.isEmpty() && !fragPath.isEmpty()) {
                const QString dir = QFileInfo(fragPath).absolutePath();
                const QString vertLocal = dir + QStringLiteral("/surface.vert");
                if (QFile::exists(vertLocal)) {
                    vertPath = vertLocal;
                } else {
                    for (const QString& incDir : shaderIncludePaths()) {
                        const QString candidate = incDir + QStringLiteral("/surface.vert");
                        if (QFile::exists(candidate)) {
                            vertPath = candidate;
                            break;
                        }
                    }
                }
            }

            node->setShaderIncludePaths(shaderIncludePaths());
            // Entry-point scaffold: a pack may define `vec4 pSurface(vec2 uv)`
            // and omit main(); loadFragmentShader assembles the generated main()
            // + prologue before include expansion, identical to the kwin-effect
            // path. A traditional main() pack is passed through unchanged.
            node->setEntryScaffold(PhosphorSurfaceShaders::SurfaceShaderRegistry::surfaceEntryPrologue(),
                                   PhosphorSurfaceShaders::SurfaceShaderRegistry::surfaceEntryCandidates());
            // Push the generated `#define p_<id> ...` preamble (set on this item
            // via the paramPreamble Q_PROPERTY by the host) so loadFragmentShader
            // splices it and keys the bake cache on it. Empty when the pack
            // declares no params — a no-op.
            node->setParamPreamble(paramPreamble());

            node->setVertexShaderSource(QString());
            node->setFragmentShaderSource(QString());

            bool loaded = true;
            if (!vertPath.isEmpty()) {
                if (!node->loadVertexShader(vertPath)) {
                    qCWarning(PlasmaZones::lcOverlay) << "SurfaceShaderItem: failed to load vertex shader:" << vertPath
                                                      << "error:" << node->shaderError();
                    loaded = false;
                }
            } else {
                qCWarning(PlasmaZones::lcOverlay) << "SurfaceShaderItem: no vertex shader found for" << fragPath
                                                  << "(expected surface.vert in the pack dir or a search path)";
                loaded = false;
            }

            if (loaded && !fragPath.isEmpty()) {
                if (!node->loadFragmentShader(fragPath)) {
                    loaded = false;
                }
            }

            if (loaded) {
                node->invalidateShader(); // Ensure node re-bakes
                setStatus(Status::Ready);
            } else {
                // Read the node's error BEFORE clearing — clearBakedShader
                // wipes it along with the resident bake.
                QString errorMsg = node->shaderError();
                if (errorMsg.isEmpty()) {
                    errorMsg = QStringLiteral("Shader loading failed - missing required files");
                }
                // Drop the partially-set sources and the resident bake
                // together (same pattern as ShaderEffect::updatePaintNode):
                // on the node-REUSE path a previously good bake keeps
                // isShaderReady() true, and the status block below would
                // promote the Error back to Ready in the same sync; the
                // armed rebake would then stamp an "empty source" error
                // over this real one on the next prepare().
                node->setVertexShaderSource(QString());
                node->setFragmentShaderSource(QString());
                node->clearBakedShader();
                setError(errorMsg);
            }
        } else {
            // Source empty — clear node. clearBakedShader (not
            // invalidateShader) drops the resident bake so the status block
            // below cannot promote Null back to Ready, and cancels the
            // rebake so a deliberate clear does not end at a manufactured
            // "empty source" Error.
            node->setVertexShaderSource(QString());
            node->setFragmentShaderSource(QString());
            node->clearBakedShader();
            setStatus(Status::Null);
        }
    }

    // ── Update status based on shader node state ─────────────────────
    if (node->isShaderReady() && status() != Status::Ready) {
        setStatus(Status::Ready);
    } else if (!node->shaderError().isEmpty() && status() != Status::Error) {
        setError(node->shaderError());
    }

    // Mark node as dirty to trigger re-render.
    node->markDirty(QSGNode::DirtyMaterial);

    return node;
}

} // namespace PlasmaZones
