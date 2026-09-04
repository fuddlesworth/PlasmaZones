// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorRendering/ShaderNodeRhi.h>

#include "internal.h"

#include <PhosphorShaders/CustomParamsKey.h>
#include <PhosphorShaders/IUniformExtension.h>

#include <QImageReader>
#include <QMutexLocker>
#include <QPainter>
#include <QPointer>
#include <QQuickWindow>
#include <QRunnable>
#include <QThread>
#if QT_VERSION < QT_VERSION_CHECK(6, 11, 0)
#include <QScreen>
#endif
#include <QSvgRenderer>

#include <cmath>

namespace PhosphorRendering {

// Keep the public ShaderEffect.h mirror constant in lock-step with the
// authoritative ShaderNodeRhi.h value. Compile-time check — a future drift
// would otherwise silently mis-size m_userTexture* member arrays.
static_assert(kMaxUserTextureSlots == kMaxUserTextures,
              "ShaderEffect::kMaxUserTextureSlots must equal ShaderNodeRhi::kMaxUserTextures");

// ============================================================================
// Default identity vertex shader
// ============================================================================
//
// Pushed to the node whenever a fragment-only consumer (e.g. SurfaceAnimator
// transition shaders, generic ShaderEffect QML usage) sets a fragmentShader
// without supplying its own vertex stage. The pipeline requires both stages
// to compile — without this fallback the bake bails with "Vertex or fragment
// shader source is empty" and m_shaderReady stays false forever, which the
// render path surfaces as a silent "render(): bail — shaderReady: false".
//
// Geometry-aware effects (slide/popin/morph/etc.) that need to translate or
// scale the quad must override this by calling node->setVertexShaderSource()
// or node->loadVertexShader() through a subclass — ZoneShaderItem is the
// in-tree example of that pattern.
//
// The QuadVertices buffer (see internal.h) emits positions in clip space
// (-1..1) so a pass-through is sufficient. We bind the UBO at binding 0 as a
// single-field view (`mat4 qt_Matrix` at offset 0), layout-compatible with the
// full BaseUniforms block the fragment stage declares (qt_Matrix is its first
// member). qt_Matrix carries the per-backend NDC Y-orientation correction
// (identity on Y-down-NDC backends like Vulkan, a Y-flip on Y-up-NDC backends
// like OpenGL) — without applying it the fixed-NDC fullscreen quad presents
// upside down on OpenGL when rendered direct-to-window (the daemon animation
// path). ShaderNodeRhi sets this value (see shadernoderhiuniforms.cpp).
//
// Stored as `static const QString` (not QLatin1String) so the conversion
// to QString happens once at static-init. Previously a per-paint
// `QString(QLatin1String(...))` allocated a fresh QString every frame
// just to feed `setVertexShaderSource`, which short-circuits via equality.
static const QString kDefaultVertexShaderSource = QStringLiteral(R"(#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;
layout(location = 0) out vec2 vTexCoord;

layout(std140, binding = 0) uniform DefaultVertexUniforms {
    mat4 qt_Matrix;
};

void main() {
    vTexCoord = texCoord;
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
}
)");

// ============================================================================
// Construction / Destruction
// ============================================================================

QImage ShaderEffect::loadUserTextureFile(const QString& path, int svgMaxDim)
{
    if (path.isEmpty()) {
        return {};
    }
    const bool isSvg = path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)
        || path.endsWith(QLatin1String(".svgz"), Qt::CaseInsensitive);
    if (!isSvg) {
        QImageReader reader(path);
        const QSize rasterSize = reader.size();
        // Same byte budget the SVG branch enforces below, applied during
        // DECODE: setScaledSize means an oversized file never materialises at
        // full resolution, unlike a post-hoc QImage::scaled() which would need
        // the full allocation first. A reader that cannot report size() up
        // front returns an invalid QSize; decode unscaled in that case rather
        // than guess. Deliberately NO setAutoTransform: QImage(path) never
        // applied EXIF orientation, and turning it on here would silently
        // rotate existing pack textures that carry an orientation tag.
        if (rasterSize.isValid() && !rasterSize.isEmpty()) {
            const qint64 rasterBytes = static_cast<qint64>(rasterSize.width()) * rasterSize.height() * 4;
            if (rasterBytes > kMaxSvgPixelBytes) {
                const double scale = std::sqrt(static_cast<double>(kMaxSvgPixelBytes) / rasterBytes);
                const QSize scaledSize(qMax(1, static_cast<int>(rasterSize.width() * scale)),
                                       qMax(1, static_cast<int>(rasterSize.height() * scale)));
                qCWarning(lcShaderNode) << "ShaderEffect::loadUserTextureFile: raster size" << rasterSize
                                        << "exceeds byte budget" << kMaxSvgPixelBytes << "B, downscaling to"
                                        << scaledSize << "for path" << path;
                reader.setScaledSize(scaledSize);
            }
        }
        return reader.read().convertToFormat(QImage::Format_RGBA8888);
    }
    QSvgRenderer renderer(path);
    if (!renderer.isValid()) {
        return {};
    }
    QSize size = renderer.defaultSize();
    // Cap the requested per-axis size at the library ceiling regardless of
    // the per-slot setting — defends against subclasses or future setters
    // that bypass the setShaderParams parse-time clamp.
    const int maxDim = qBound(64, svgMaxDim, kMaxSvgDimension);
    if (!size.isEmpty()) {
        size.scale(maxDim, maxDim, Qt::KeepAspectRatio);
    } else {
        size = QSize(maxDim, maxDim);
    }
    // Byte-budget guard: even after the per-axis ceiling, a near-square
    // doc can request ~maxDim² × 4 bytes (16 MiB at 2048²). Downscale
    // proportionally so the pixel area stays within kMaxSvgPixelBytes / 4
    // pixels and warn.
    const qint64 pixelBytes = static_cast<qint64>(size.width()) * size.height() * 4;
    if (pixelBytes > kMaxSvgPixelBytes) {
        const double scale = std::sqrt(static_cast<double>(kMaxSvgPixelBytes) / pixelBytes);
        const QSize scaledSize(qMax(1, static_cast<int>(size.width() * scale)),
                               qMax(1, static_cast<int>(size.height() * scale)));
        qCWarning(lcShaderNode) << "ShaderEffect::loadUserTextureFile: SVG rasterise size" << size
                                << "exceeds byte budget" << kMaxSvgPixelBytes << "B, downscaling to" << scaledSize
                                << "for path" << path;
        size = scaledSize;
    }
    // Rasterise into ARGB32_Premultiplied — QPainter's source-over
    // compositing is defined for premultiplied targets; rendering an SVG
    // with semi-transparent strokes/fills onto a non-premul RGBA8888
    // produces subtly wrong alpha at partial-cover paths. Convert to
    // RGBA8888 afterwards for the RHI upload path (which expects
    // QRhiTexture::RGBA8 layout).
    QImage rasterised(size, QImage::Format_ARGB32_Premultiplied);
    rasterised.fill(Qt::transparent);
    QPainter painter(&rasterised);
    renderer.render(&painter);
    painter.end();
    return rasterised.convertToFormat(QImage::Format_RGBA8888);
}

ShaderEffect::ShaderEffect(QQuickItem* parent)
    : QQuickItem(parent)
{
    // No lock needed: the token is not shared with any render job until
    // releaseIdleGraphicsResources hands it to scheduleRenderJob, which
    // synchronises the publication.
    m_selfToken->effect = this;
    setFlag(ItemHasContents, true);

    m_userTextureSvgSizes.fill(kDefaultUserTextureSvgSize);

    // When the scene graph is invalidated (e.g. window hide on Vulkan destroys
    // the QRhi), release GPU resources from the render node and mark shader
    // dirty so it reinitializes on the next show().
    //
    // sceneGraphAboutToStop fires on the render thread BEFORE teardown, while
    // the node and QRhi are still valid. Use DirectConnection because the
    // signal is emitted on the render thread.
    //
    // m_connectedWindow is a QPointer so a window destroyed out from under us
    // (reparent-during-teardown) leaves a null pointer instead of dangling.
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow* win) {
        // Idempotency guard: same-window re-emit (Qt has been observed to
        // emit windowChanged with the unchanged QQuickWindow* in certain
        // QQuickItem reparenting flows). Without this early-out we
        // disconnect-then-reconnect the same `sceneGraphAboutToStop`
        // signal, which is benign but burns ~2 lookups per emit.
        if (m_connectedWindow.data() == win) {
            return;
        }
        if (m_connectedWindow) {
            disconnect(m_connectedWindow.data(), &QQuickWindow::sceneGraphAboutToStop, this, nullptr);
        }
        m_connectedWindow = win;

        // Deliberately does NOT clear the tracked node. An earlier version did,
        // on the theory that the old window's scene graph had already taken the
        // node for deletion by the time windowChanged fires — but "taken for
        // deletion" is not "deleted", and the gap between the two is exactly
        // where a render-thread prepare() still runs. Clearing here dropped the
        // only handle to a node that was still alive, so a detach-then-destroy
        // (reparent an item out of its window, then delete it) left the node's
        // item back-pointer dangling and SIGSEGV'd inside QQuickItem::window().
        //
        // Nothing has to be cleared now: the node retracts itself from the
        // liveness block when the scene graph actually deletes it, so tracking
        // it across a window change is safe and the destructor keeps a live
        // handle for as long as one exists. See ShaderNodeLiveness.

        if (win) {
            connect(
                win, &QQuickWindow::sceneGraphAboutToStop, this,
                [this]() {
                    withTrackedNode([](ShaderNodeRhi* node) {
                        node->releaseResources();
                    });
                    // No node deregistration here either: the scene-graph
                    // invalidation that follows this signal deletes the nodes,
                    // and each one clears itself out of its liveness block as
                    // it goes. Raising the dirty flag is still ours to do — the
                    // node object itself is about to be destroyed, so the next
                    // updatePaintNode has to re-bake from source.
                    m_shaderDirty.store(true);
                },
                Qt::DirectConnection);
        }
    });
}

ShaderEffect::~ShaderEffect()
{
    // FIRST statement, before any member teardown: invalidate the liveness
    // token so a queued ReleaseIdleResourcesJob that runs from here on
    // observes null and never touches this object. Taking the token's mutex
    // also BLOCKS this destructor until a job that is mid-run finishes — the
    // job holds the lock across its node access — which closes the
    // destruction race the previous atomic-only token merely narrowed (see
    // m_selfToken's doc).
    {
        const std::lock_guard<std::mutex> tokenLock(m_selfToken->mutex);
        m_selfToken->effect = nullptr;
    }

    // Disconnect the DirectConnection sceneGraphAboutToStop callback FIRST.
    // That lambda executes on the render thread and touches this object.
    //
    // Be exact about what this buys, because an earlier version of this comment
    // claimed the opposite of ShaderEffect.h's and both cannot be right:
    // QObject::disconnect is thread-safe but does NOT block a direct-connection
    // slot that is already running (doActivate releases the sender's lock
    // across the call). It guarantees no FUTURE invocation, nothing more. What
    // keeps teardown safe today is that sceneGraphAboutToStop is emitted only
    // while the render thread is tearing the scene graph down, which does not
    // overlap an item destruction in practice. Narrow, not closed.
    if (m_connectedWindow) {
        disconnect(m_connectedWindow.data(), &QQuickWindow::sceneGraphAboutToStop, this, nullptr);
        m_connectedWindow.clear();
    }

    // Invalidate the render node's back-pointer before our members are torn
    // down. The scene graph render thread may still call prepare()/render() on
    // the node between now and the node's deletion; without this, the item
    // pointer inside the node would dangle.
    //
    // Unconditional, and it has to stay that way. This guard used to also
    // require window() to be non-null, as a stand-in liveness check for the
    // node — but a null window only proves the ITEM is detached, not that the
    // scene graph has deleted the node, and the two are not the same event.
    // An item reparented out of its window and then destroyed took the skip
    // path with a live node still holding a back-pointer to it, which the next
    // prepare() dereferenced after the item was freed. withTrackedNode() asks
    // the node's own liveness block instead of inferring from a proxy, and
    // holds that block's lock across the call so the node cannot be deleted
    // between the check and the invalidation.
    withTrackedNode([](ShaderNodeRhi* node) {
        node->invalidateItem();
    });
    {
        const std::lock_guard<std::mutex> guard(m_renderNodeMutex);
        m_renderNodeLink.reset();
    }
}

// ============================================================================
// Render Node Tracking
// ============================================================================

void ShaderEffect::registerRenderNode(ShaderNodeRhi* node)
{
    // Compare bare pointers before touching the shared_ptr: subclasses
    // re-register the same node on every frame (the documented contract, so a
    // window change can't leave the tracking disarmed), and the steady-state
    // path should cost one uncontended lock, not a pair of refcount atomics.
    const ShaderNodeLiveness* incoming = node ? node->liveness().get() : nullptr;
    const std::lock_guard<std::mutex> guard(m_renderNodeMutex);
    if (m_renderNodeLink.get() == incoming) {
        return;
    }
    m_renderNodeLink = node ? node->liveness() : nullptr;
}

std::shared_ptr<ShaderNodeLiveness> ShaderEffect::trackedNodeLink() const
{
    const std::lock_guard<std::mutex> guard(m_renderNodeMutex);
    return m_renderNodeLink;
}

void ShaderEffect::withTrackedNode(const std::function<void(ShaderNodeRhi*)>& fn) const
{
    // Copy the shared_ptr out under our own lock first, then release it before
    // taking the block's, so the two mutexes are never held at the same time.
    // Holding m_renderNodeMutex across the whole call would block every
    // registerRenderNode (one per frame, on the render thread) for as long as
    // fn runs — and fn can be releaseResources(), a full RHI teardown. Dropping
    // it early costs nothing: the copied share keeps the block alive even if
    // the node dies and something deregisters it while fn runs.
    const std::shared_ptr<ShaderNodeLiveness> link = trackedNodeLink();
    if (!link) {
        return;
    }
    // The block's mutex is what makes this safe rather than merely likely: a
    // node destructor that has begun has already run retractLiveness() (every
    // destructor in the hierarchy does, as its first statement) or is blocked
    // here, so link->node is live for the whole call.
    const std::lock_guard<std::mutex> livenessLock(link->mutex);
    if (link->node) {
        fn(link->node);
    }
}

// ============================================================================
// Uniform Extension
// ============================================================================

void ShaderEffect::setUniformExtension(std::shared_ptr<PhosphorShaders::IUniformExtension> extension)
{
    m_uniformExtension = std::move(extension);
    update();
}

std::shared_ptr<PhosphorShaders::IUniformExtension> ShaderEffect::uniformExtension() const
{
    return m_uniformExtension;
}

// ============================================================================
// Shader Loading
// ============================================================================

void ShaderEffect::reloadShader()
{
    if (!m_shaderSource.isValid() || m_shaderSource.isEmpty()) {
        setStatus(Status::Null);
        return;
    }
    setStatus(Status::Loading);
    m_shaderDirty = true;
    update();
}

void ShaderEffect::releaseIdleGraphicsResources()
{
    QQuickWindow* win = window();
    if (!win) {
        return;
    }
    // The node is owned and only ever touched by the scene-graph thread, and
    // an idle (typically invisible) item never reaches updatePaintNode — so
    // the release must travel as a render job, which the render loop runs on
    // that thread at the next opportunity even when no frame is pending
    // (QQuickWindow::NoStage).
    //
    // Lifetime, spelled out because it is the whole safety argument:
    //   • The job re-reads the tracked node when it RUNS, through
    //     withTrackedNode(), which holds the node's liveness lock across the
    //     release call. A node the scene graph has already deleted reads back
    //     as null, and one it is deleting concurrently cannot complete its
    //     destructor until the call returns — so the job never touches a dead
    //     node regardless of which thread retired it.
    //   • The self-token guards effect deletion. A QPointer is not documented
    //     thread-safe against concurrent destruction, and a queued NoStage job
    //     can sit for an unbounded interval (destroyPassiveShell on screen
    //     removal is a real path that deletes overlay content on the GUI
    //     thread with a quiesce job pending). The job captures the shared
    //     token by value and holds its mutex across the node access; the
    //     destructor takes the same mutex and nulls the pointer as its FIRST
    //     statement, so it blocks until a mid-run job finishes. That CLOSES
    //     the destruction race (an earlier atomic-only token merely narrowed
    //     it to the destructor body).
    //   • Recovery is node-side: releaseRhiResources() retains the shader
    //     sources and re-arms the node's own dirty flags, so the next painted
    //     frame re-bakes from cached source with zero file I/O. The item-side
    //     m_shaderDirty is deliberately NOT raised here — that would force
    //     updatePaintNode's needLoad branch, a synchronous QFile read +
    //     include expansion in the sync phase on the first frame of the next
    //     drag. (The sceneGraphAboutToStop hook DOES raise it, because there
    //     the node object itself is about to be destroyed.)
    class ReleaseIdleResourcesJob : public QRunnable
    {
    public:
        explicit ReleaseIdleResourcesJob(std::shared_ptr<SelfToken> token)
            : m_token(std::move(token))
        {
        }
        void run() override
        {
            // Hold the token's mutex across the node access: a destructor
            // that begins while this job runs blocks on the same mutex, so
            // the effect (and its node tracking) cannot be freed out from
            // under the release call.
            const std::lock_guard<std::mutex> tokenLock(m_token->mutex);
            ShaderEffect* effect = m_token->effect;
            if (!effect) {
                return;
            }
            // Field-verifiable: whether the render loop dispatches a NoStage
            // job for an idle (mapped-but-undamaged, or unexposed) window is
            // a Qt-version-dependent behaviour the release depends on. This
            // line is the check — if it never appears after the idle grace
            // fires, the reclaim is a no-op in that configuration.
            qCDebug(lcShaderNode) << "ReleaseIdleResourcesJob: releasing idle shader GPU resources";
            effect->withTrackedNode([](ShaderNodeRhi* node) {
                node->releaseResources();
            });
        }

    private:
        std::shared_ptr<SelfToken> m_token;
    };
    win->scheduleRenderJob(new ReleaseIdleResourcesJob(m_selfToken), QQuickWindow::NoStage);
    // Force one render-loop pass. Two duties ride on this frame:
    //   • The QML overlay host latches its park state (dropping the item's
    //     QQuickItemLayer FBO) immediately before calling in here, and on an
    //     idle window nothing else schedules the sync pass that applies it.
    //     This is the frame that flushes the layer drop.
    //   • Belt for the NoStage job above: whether the render loop dispatches
    //     a queued job for a mapped-but-undamaged window without a frame
    //     behind it is Qt-version-dependent (the qCDebug line in the job is
    //     the field check); a real frame removes the doubt.
    // update() requests a frame through the normal damage path. While the
    // surface is mapped (keepMappedOnHide — which is effects-gated, so with
    // visual effects disabled the hidden surface is unmapped and the frame
    // waits for the next show) the compositor grants it, the render thread
    // runs once, and both the layer drop and the queued job apply. One extra
    // composited frame of an invisible overlay, once per idle grace.
    win->update();
}

// ============================================================================
// Status Management
// ============================================================================

ShaderNodeRhi* ShaderEffect::createShaderNode()
{
    return new ShaderNodeRhi(this);
}

void ShaderEffect::setError(const QString& error)
{
    bool changed = false;
    {
        QMutexLocker lock(&m_errorLogMutex);
        changed = (m_errorLog != error);
        if (changed) {
            m_errorLog = error;
        }
    }
    if (changed) {
        notifyOnGuiThread(&ShaderEffect::errorLogChanged);
    }
    setStatus(Status::Error);
}

void ShaderEffect::setStatus(Status newStatus)
{
    // Single atomic swap, not load-compare-store. Status is written from the
    // render thread and read from the GUI thread, so a split read and write
    // lets two concurrent transitions both pass the compare and emit
    // statusChanged() twice, or interleave into a transition nobody reports.
    const Status previous = m_status.exchange(newStatus, std::memory_order_acq_rel);
    if (previous == newStatus) {
        return;
    }
    notifyOnGuiThread(&ShaderEffect::statusChanged);
}

void ShaderEffect::notifyOnGuiThread(void (ShaderEffect::*signal)())
{
    // setStatus/setError run in updatePaintNode, i.e. in the sync phase on
    // the RENDER thread under the threaded loop. A direct Q_EMIT there hands
    // any DirectConnection consumer (and any queued side effect a handler
    // creates, which acquires render-thread affinity) the wrong thread — the
    // same hazard the afterAnimating choice elsewhere in this file exists to
    // avoid. QML's own connection path marshals, but marshal here so the
    // signal's thread contract holds for every consumer. During sync the GUI
    // thread is blocked, so `this` cannot be destroyed before the queued
    // notify is posted.
    if (QThread::currentThread() == thread()) {
        Q_EMIT(this->*signal)();
    } else {
        QMetaObject::invokeMethod(this, signal, Qt::QueuedConnection);
    }
}

// ============================================================================
// Shared Property Sync (used by updatePaintNode and subclass overrides)
// ============================================================================

qreal ShaderEffect::effectiveResolutionScale() const
{
    const bool needsPhysical = !m_uniformExtension || m_uniformExtension->requiresPhysicalResolution();
    // No screen() term: effectiveDevicePixelRatio() has its own null-screen
    // fallback, and gating on a transiently screen-less window (output
    // hotplug) would silently answer 1.0 on a fractional-scale display —
    // iResolution in logical units against a physical gl_FragCoord, the
    // exact edge-stripe bug documented at the physical/logical split below.
    if (needsPhysical && window()) {
        return window()->effectiveDevicePixelRatio();
    }
    return 1.0;
}

void ShaderEffect::syncBasePropertiesToNode(ShaderNodeRhi* node)
{
    // ── Shadertoy uniforms ───────────────────────────────────────────
    // iTime is passed as double; the node splits into wrapped-lo + wrap-offset
    // before GPU float32 cast.
    node->setTime(m_iTime);
    node->setTimeDelta(static_cast<float>(m_iTimeDelta));
    node->setFrame(m_iFrame);
    node->setIsReversed(m_isReversed);
    // iResolution unit semantics — controlled by the installed uniform
    // extension's `requiresPhysicalResolution()`:
    //
    //   * physical (default, overlay path): DPR-scaled so the value
    //     matches `gl_FragCoord` — which is the viewport coordinate of
    //     the rasterised fragment. QtQuick's QRhi viewport is set in
    //     physical pixels (item.size * DPR), so `gl_FragCoord` ranges
    //     0..viewport.size in physical pixels too. Zone-overlay
    //     fragment shaders that compute SDF masks via
    //     `gl_FragCoord / iResolution` need both operands in physical
    //     pixels; setting iResolution in logical pixels at DPR > 1
    //     caused the rounded-rect mask to cover only the logical-sized
    //     region of the physical-sized surface (transparent edge
    //     stripe).
    //
    //   * logical (animation path): the AnimationUniformExtension
    //     reports `requiresPhysicalResolution() == false`. Animation
    //     shaders use the vertex-stage `vTexCoord` (auto-interpolated
    //     0..1 over the rasterised quad regardless of DPR) and pair
    //     `iResolution` with logical-pixel companions (`iAnchorSize`,
    //     `iAnchorPosInFbo`, `iSurfaceScreenPos`) for UV / clip-space
    //     ratios. Scaling iResolution by DPR there would mismatch
    //     units across the UBO and shrink rendered output by 1/DPR
    //     (fly-in) or shift the anchor UV remap (broken-glass, morph).
    //
    // `m_iResolution` itself stays in logical units (Q_PROPERTY
    // semantics — QML callers expect the same units they bound it
    // from). Only the GPU-bound value is conditionally multiplied.
    const qreal dpr = effectiveResolutionScale();
    node->setResolution(static_cast<float>(m_iResolution.width() * dpr),
                        static_cast<float>(m_iResolution.height() * dpr));
    // Scale the mouse by the SAME dpr as the resolution so iMouse.xy (pixels)
    // and iMouse.zw (normalised by the node's width/height) stay in the
    // device-pixel space of iResolution / fragCoord. Without this, on a scaled
    // display a mouse-position shader lands at 1/dpr of the cursor (up-left).
    // The Q_PROPERTY itself stays logical — QML callers bind logical units,
    // exactly as they do for iResolution; only the GPU-bound value is scaled.
    // The off-region sentinel (-1,-1) becomes (-dpr,-dpr): still negative, so
    // any `iMouse.x < 0` region check still reads it as "cursor outside".
    node->setMousePosition(QPointF(m_iMouse.x() * dpr, m_iMouse.y() * dpr));

    // ── Custom parameters (indexed API) ──────────────────────────────
    for (int i = 0; i < kMaxCustomParams; ++i)
        node->setCustomParams(i, m_customParams[i]);

    // ── Custom colors (indexed API) ──────────────────────────────────
    for (int i = 0; i < kMaxCustomColors; ++i)
        node->setCustomColor(i, m_customColors[i]);

    // ── Audio spectrum ───────────────────────────────────────────────
    node->setAudioSpectrum(m_audioSpectrum);

    // ── Depth buffer, grid mesh and wallpaper ────────────────────────
    node->setUseDepthBuffer(m_useDepthBuffer);
    node->setGridSubdivisions(m_gridSubdivisions);
    node->setUseWallpaper(m_useWallpaper);
    {
        QMutexLocker lock(&m_wallpaperTextureMutex);
        node->setWallpaperTexture(m_wallpaperTexture);
    }

    // ── User textures (uTexture0..3 / SRB bindings 7..10) ────────────
    // Pushed here rather than in updatePaintNode so subclasses that
    // override updatePaintNode and delegate to syncBasePropertiesToNode
    // inherit texture sync without owning their own user-texture state
    // (see ZoneShaderItem). The arrays are populated by setShaderParams
    // and (for slot 0 on the SurfaceAnimator path) by
    // setSourceTextureProvider rebinding the surface FBO.
    for (int i = 0; i < kMaxUserTextures; ++i) {
        node->setUserTexture(i, m_userTextureImages[i]);
        node->setUserTextureWrap(i, m_userTextureWraps[i]);
    }

    // ── Uniform extension ────────────────────────────────────────────
    // Push the extension stored on this item (if any) so subclasses that
    // override updatePaintNode() and delegate to syncBasePropertiesToNode()
    // inherit the same install-through-item contract as the base class —
    // avoids a silent no-op on ShaderEffect::setUniformExtension when a
    // subclass forgot to sync.
    node->setUniformExtension(m_uniformExtension);

    // ── Multipass buffer configuration ───────────────────────────────
    QStringList effectivePaths = m_bufferShaderPaths;
    if (effectivePaths.isEmpty() && !m_bufferShaderPath.isEmpty()) {
        effectivePaths.append(m_bufferShaderPath);
    }
    if (effectivePaths.size() > kMaxBufferPasses) {
        effectivePaths.resize(kMaxBufferPasses);
    }
    node->setBufferShaderPaths(effectivePaths);
    node->setBufferFeedback(m_bufferFeedback);
    node->setBufferScale(m_bufferScale);
    node->setHalfFloatBuffers(m_halfFloatBuffers);
    node->setBufferWrap(m_bufferWrap);
    // Pushed unconditionally — an EMPTY list is a meaningful value ("no
    // per-buffer overrides, every slot on the single-value default"), and the
    // node's setters handle it exactly that way. Gating on isEmpty() left the
    // node holding stale per-buffer overrides forever after a hot-reload
    // cleared them (applyEffectStaticConfig sets {} to wipe previous-leg
    // overrides), because the unconditional single-value push above no-ops
    // when the default token itself is unchanged.
    node->setBufferWraps(m_bufferWraps);
    node->setBufferFilter(m_bufferFilter);
    node->setBufferFilters(m_bufferFilters);
}

// ============================================================================
// Scene Graph Integration
// ============================================================================

QSGNode* ShaderEffect::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data)
{
    Q_UNUSED(data)

    if (width() <= 0 || height() <= 0) {
        if (oldNode) {
            // Invalidate oldNode DIRECTLY, not the tracked node: the scene
            // graph handed us this exact pointer, so it is live by definition,
            // and going through the tracking would be a longer route to the
            // same object. Both subclass overrides already do it this way.
            static_cast<ShaderNodeRhi*>(oldNode)->invalidateItem();
            registerRenderNode(nullptr);
            delete oldNode;
        }
        return nullptr;
    }

    auto* node = static_cast<ShaderNodeRhi*>(oldNode);
    bool freshNode = false;
    if (!node) {
        // Scene graph deleted the previous node (e.g. releaseResources), or first call.
        node = createShaderNode();
        freshNode = true;
    }
    // Register unconditionally, not only on the fresh-node path: the scene
    // graph can hand back a node this item is not currently tracking (it
    // deletes and recreates behind our back), and a reuse-path frame is the
    // only chance to pick that up. registerRenderNode early-outs when the node
    // is already the tracked one, so the steady-state cost is one uncontended
    // lock per frame.
    registerRenderNode(node);

    // ── Sync base properties (time, params, colors, audio, multipass, depth, wallpaper, user textures) ──
    syncBasePropertiesToNode(node);

    // ── Sync source texture provider (slot 0 / binding 7 override) ───
    // Pushed every paint pass so a setSourceItem(...) call after the
    // node already exists picks up immediately, and so a torn-down
    // source (QPointer auto-nulls) clears the binding instead of
    // leaving a dangling provider in the node.
    if (m_sourceItem && m_sourceItem->isTextureProvider()) {
        node->setSourceTextureProvider(m_sourceItem->textureProvider());
    } else {
        node->setSourceTextureProvider(nullptr);
    }

    // Uniform extension is synced inside syncBasePropertiesToNode() above.

    // ── Sync shader source (must compile before rendering) ───────────
    // freshNode covers SG-deletion + first-call: a brand-new node has no
    // shader baked, so trigger a load regardless of m_shaderDirty.
    // Do NOT retry on `!node->isShaderReady()` alone — a permanent load
    // failure (missing file, bad GLSL) would otherwise re-attempt every
    // frame and spam the journal at 60Hz. Real device-loss is handled by
    // sceneGraphAboutToStop, which sets m_shaderDirty=true; transient
    // errors retry on the next shaderSource change.
    const bool wasDirty = m_shaderDirty.exchange(false);
    const bool needLoad = wasDirty || freshNode;
    // Set only by a SUCCESSFUL load in this sync. The status block below must
    // not report the node's resident shaderError against a load that just
    // succeeded — that error belongs to the PREVIOUS shader (the node clears
    // it only inside prepare()'s bake, which has not run yet).
    bool loadSucceededThisSync = false;
    if (needLoad) {
        if (m_shaderSource.isValid() && !m_shaderSource.isEmpty()) {
            const QString fragPath = localPathFromShaderUrl(m_shaderSource);

            if (!fragPath.isEmpty()) {
                node->setShaderIncludePaths(m_shaderIncludePaths);
                // Push the generated named-param preamble (T1.1) before the
                // load so loadFragmentShader splices it and keys the bake cache
                // on it. Empty (the default / zone-shader path) is a no-op.
                node->setParamPreamble(m_paramPreamble);
                // Push the T1.4 entry-point scaffold so an entry-only fragment
                // is assembled before expansion. Empty (animation path) no-op.
                node->setEntryScaffold(m_entryPrologue, m_entryCandidates);
                bool vertLoaded = false;
                if (m_vertexShaderUrl.isValid() && !m_vertexShaderUrl.isEmpty()) {
                    const QString vertPath = localPathFromShaderUrl(m_vertexShaderUrl);
                    if (!vertPath.isEmpty()) {
                        vertLoaded = node->loadVertexShader(vertPath);
                        if (!vertLoaded) {
                            qCWarning(lcShaderNode)
                                << "Vertex shader load failed:" << vertPath << "— falling back to built-in default";
                        }
                    }
                }
                if (!vertLoaded) {
                    node->setVertexShaderSource(kDefaultVertexShaderSource);
                }
                if (node->loadFragmentShader(fragPath)) {
                    node->invalidateShader();
                    setStatus(Status::Ready);
                    loadSucceededThisSync = true;
                    // One extra frame so prepare()'s bake can report its real
                    // outcome (Ready stands, or a genuine compile error lands
                    // via the status block on that next sync). Without it a
                    // static (playing=false) item never paints again after
                    // this frame, so a compile failure would go unreported —
                    // and a stale previous-shader error could stand forever.
                    update();
                } else {
                    QString errorMsg = node->shaderError();
                    if (errorMsg.isEmpty()) {
                        errorMsg = QStringLiteral("Shader loading failed");
                    }
                    qCWarning(lcShaderNode) << "Fragment shader load failed:" << fragPath << "—" << errorMsg;
                    // Drop the node's shader before reporting. On the node-REUSE
                    // path a previously good bake is still resident, so
                    // isShaderReady() stays true and the status block below
                    // would set Ready again three lines later, leaving the item
                    // claiming Ready while errorLog holds a real failure.
                    // clearBakedShader (not invalidateShader) clears that
                    // readiness in the same sync AND cancels the rebake the
                    // source clear armed — prepare() would otherwise stamp an
                    // "empty source" error over this one.
                    node->setFragmentShaderSource(QString());
                    node->clearBakedShader();
                    setError(errorMsg);
                }
            } else {
                // The URL passed isLocalShaderUrl() but carries no usable path
                // (a host-only file:// URL, or a qrc: URL with an empty path).
                // m_shaderDirty has already been consumed above, so returning
                // silently here would pin the item at Status::Loading forever
                // with an empty errorLog and no retry on any later frame.
                qCWarning(lcShaderNode) << "Shader URL resolved to an empty local path:" << m_shaderSource;
                // Same reuse-path clobber as the load-failure arm above: drop
                // the resident bake so isShaderReady() cannot revert the error.
                node->setFragmentShaderSource(QString());
                node->clearBakedShader();
                setError(QStringLiteral("Shader URL resolved to an empty local path: ") + m_shaderSource.toString());
            }
        } else {
            // Source cleared — stop rendering the old shader. clearBakedShader
            // drops the resident bake (so the status block below cannot
            // promote Null back to Ready off isShaderReady()) and cancels the
            // rebake, so a deliberate clear ends at Null with an empty
            // errorLog instead of a manufactured "empty source" Error.
            node->setFragmentShaderSource(QString());
            node->clearBakedShader();
            setStatus(Status::Null);
        }
    }

    // ── Update status from node state ────────────────────────────────
    const Status currentStatus = m_status.load(std::memory_order_acquire);
    if (node->isShaderReady() && currentStatus != Status::Ready) {
        setStatus(Status::Ready);
    } else if (!loadSucceededThisSync && !node->shaderError().isEmpty() && currentStatus != Status::Error) {
        setError(node->shaderError());
    }

    node->markDirty(QSGNode::DirtyMaterial);

    return node;
}

// ============================================================================
// Geometry Handling
// ============================================================================

void ShaderEffect::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);

    if (newGeometry.size() != oldGeometry.size()) {
        const QSizeF newSize = newGeometry.size();
        if (m_iResolution != newSize) {
            m_iResolution = newSize;
            Q_EMIT iResolutionChanged();
        }
        update();
    }
}

void ShaderEffect::itemChange(ItemChange change, const ItemChangeData& value)
{
    QQuickItem::itemChange(change, value);

    if (change == ItemVisibleHasChanged && value.boolValue) {
        // Item became visible — force scene graph update (update() calls
        // during the hidden period are lost). Only force a full reload when
        // the node is actually gone: an unconditional m_shaderDirty here
        // re-read the shader file and re-expanded every include synchronously
        // in the sync phase on EVERY show, while the resident node's bake was
        // still perfectly valid. The two real teardown paths are covered
        // independently — scene-graph invalidation (Vulkan window hide) goes
        // through the sceneGraphAboutToStop hook, which raises m_shaderDirty
        // directly; and any path that deletes the node makes the next
        // updatePaintNode's freshNode branch load regardless.
        bool nodeAlive = false;
        withTrackedNode([&nodeAlive](ShaderNodeRhi*) {
            nodeAlive = true;
        });
        if (!nodeAlive) {
            m_shaderDirty = true;
        }
        update();
    } else if (change == ItemSceneChange) {
        // Re-hook the playing-mode tick connection to whatever window the
        // item is now in (or tear it down if value.window is null). Without
        // this, a ShaderEffect created with playing=true before being
        // parented to a window would never tick — the connection in
        // setPlaying short-circuits when window() is null.
        updatePlayingConnection();
        // Same for the output-scale subscriptions: they hang off the window
        // and its screen, so they have to follow the item between windows.
        updateScaleConnections();
    }
}

void ShaderEffect::updateScaleConnections()
{
    // Tear down unconditionally before re-establishing, so moving between
    // windows cannot leave a connection to the previous one behind.
    QObject::disconnect(m_scaleConnection);
    m_scaleConnection = {};
    // Unconditional: a default-constructed Connection disconnects cleanly, and
    // on 6.11+ this is simply always that.
    QObject::disconnect(m_screenDprConnection);
    m_screenDprConnection = {};

    QQuickWindow* w = window();
    if (!w) {
        // Not parented yet. itemChange(ItemSceneChange) re-calls us once the
        // item has a window.
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
    // One connection covers every case, because devicePixelRatioChanged is the
    // NOTIFY for effectiveDevicePixelRatio, which is exactly what
    // effectiveResolutionScale() reads. It fires for a screen move, for a
    // screen rescaled in place, and for a per-surface scale change that never
    // touches a screen at all (Wayland's fractional-scale-v1 preferred scale),
    // which no QScreen signal reports.
    m_scaleConnection = QObject::connect(w, &QQuickWindow::devicePixelRatioChanged, this, [this]() {
        update();
    });
#else
    // Before 6.11 the accessor has no notifier, so approximate it. The window
    // moving to another screen changes effectiveDevicePixelRatio; re-subscribe
    // to the new screen's own signal at the same time.
    m_scaleConnection = QObject::connect(w, &QWindow::screenChanged, this, [this]() {
        updateScaleConnections();
        update();
    });

    // A screen can also be rescaled in place without the window ever moving.
    // logicalDotsPerInch is the value that tracks Qt's high-DPI scale factor.
    // physicalDotsPerInch is derived from geometry over physical size, and a
    // scale change alters neither, so it would simply never fire.
    if (QScreen* s = w->screen()) {
        m_screenDprConnection = QObject::connect(s, &QScreen::logicalDotsPerInchChanged, this, [this]() {
            update();
        });
    }
#endif

    // Kick a frame on the establish path too, exactly as
    // updatePlayingConnection() does. The window we just attached to may have a
    // different scale from the one last pushed to the node, and no change
    // signal fires for that: it is a change from OUR side, not the window's.
    // Leaning on QQuickItem's own add-to-window dirtying would be the same
    // "some later frame happens to repaint" assumption this function exists to
    // remove.
    update();
}

void ShaderEffect::componentComplete()
{
    QQuickItem::componentComplete();

    // Initialize resolution from item size if not explicitly set
    if (m_iResolution.isEmpty() && width() > 0 && height() > 0) {
        m_iResolution = QSizeF(width(), height());
        Q_EMIT iResolutionChanged();
    }

    // Load shader if source is set
    if (m_shaderSource.isValid() && !m_shaderSource.isEmpty()) {
        reloadShader();
    }
}

} // namespace PhosphorRendering
