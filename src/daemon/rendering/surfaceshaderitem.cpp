// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "surfaceshaderitem.h"

#include "../../core/logging.h"

#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorRendering/ShaderNodeRhi.h>

#include <PhosphorSurface/SurfaceShaderRegistry.h>
#include <PhosphorSurface/SurfaceUniformProfile.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <memory>

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
    // CMakeLists), the third pack category beside `plasmazones/shaders` and
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
    // overlay render path — a stock ShaderNodeRhi driven by the 576-byte
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
        // installed, and REGISTER the node with the base: this override
        // replaces the base updatePaintNode that normally does the tracking,
        // and without it the base destructor / sceneGraphAboutToStop teardown
        // (invalidateItem + resource release) silently no-ops for this item —
        // a render-thread prepare() between item destruction and node deletion
        // would then dereference the freed item.
        registerRenderNode(nullptr);
        node = createShaderNode();
        registerRenderNode(node);
        freshNode = true;
    }

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

    // NB: the audio spectrum (CAVA, binding 6 + the UBO's iAudioSpectrumSize) is
    // pushed by syncBasePropertiesToNode above — the daemon writes the inherited
    // audioSpectrum Q_PROPERTY via OverlayService, and a pack reads it through
    // surface_audio.glsl. The audioSpectrumChanged -> update() connection in the
    // constructor is what re-runs this node sync on a new spectrum.

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
                QString errorMsg = node->shaderError();
                if (errorMsg.isEmpty()) {
                    errorMsg = QStringLiteral("Shader loading failed - missing required files");
                }
                setError(errorMsg);
            }
        } else {
            // Source empty — clear node.
            node->setVertexShaderSource(QString());
            node->setFragmentShaderSource(QString());
            node->invalidateShader();
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
