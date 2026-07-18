// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "internal.h"

#include <PhosphorRendering/ShaderCompiler.h>
#include <PhosphorShaders/BaseUniformProfile.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTextStream>
#include <QtMath>
#include <cstring>

namespace PhosphorRendering {

Q_LOGGING_CATEGORY(lcShaderNode, "phosphorrendering.shadernode")

namespace {

struct ShaderCacheEntry
{
    QShader vertex;
    QShader fragment;
};
// Filename-based cache (vertex+fragment pair, keyed by path+mtime).
// Separate from ShaderCompiler's source-based cache — this one avoids
// re-reading and re-expanding files when the mtime hasn't changed.
using ShaderCache = QHash<QByteArray, ShaderCacheEntry>;

// Access both the cache and its mutex through accessors so ShaderCompiler::
// clearCache() (defined in shadercompiler.cpp) can fan out to both caches
// in one place. Without this fan-out, a hot-reload that called clearCache()
// would wipe the source-hash cache while the filename cache kept serving
// stale baked shaders.
ShaderCache& filenameShaderCache()
{
    static ShaderCache c;
    return c;
}
QMutex& filenameShaderCacheMutex()
{
    static QMutex m;
    return m;
}

// Bound on the final vertex+fragment QShader pairs for actively-rendering
// shaders. A miss re-bakes via ShaderCompiler, which now hits the persistent
// on-disk cache (a fast deserialize, not a glslang compile), so a tight bound
// trades a little disk I/O on shader switches for markedly lower resident memory
// (each entry is two multi-variant QShaders). Eviction here is arbitrary-order
// (shaderCacheEvictOne erases an unspecified QHash entry, not the LRU), so the
// bound must comfortably exceed the live working set — one pair per screen per
// active shader/animation pass — to avoid evicting a hot entry. 24 covers
// realistic multi-monitor setups with margin; a disk-backed re-bake is the
// worst case if it ever overflows.
constexpr int kShaderCacheMaxSize = 24;

static void shaderCacheEvictOne()
{
    auto& cache = filenameShaderCache();
    if (cache.isEmpty())
        return;
    cache.erase(cache.begin());
}

static constexpr char kShaderCacheKeyDelim = '\0';

/// Build a fingerprint over a list of canonical include paths. Each
/// included file contributes its canonical path + mtime, separated by
/// the standard delimiter. Stable across runs (mtime is fs metadata)
/// but invalidates the cache the moment ANY transitively-included
/// header is modified — closing the gap that previously let an edit
/// to `data/overlays/shared/common.glsl` (e.g. a UBO layout change) be
/// silently masked by a per-`.frag`/`.vert` cache hit because the
/// top-level shader file's mtime didn't change.
///
/// Sort the input first so the order in which the resolver walked
/// nested `#include` directives doesn't affect the fingerprint —
/// otherwise a re-ordering of `#include` lines inside the same shader
/// would produce a fresh key even when the included content is
/// identical, costing a needless re-bake. The set of files matters,
/// the visit order doesn't.
static QByteArray includeFingerprint(QStringList paths)
{
    QByteArray fp;
    paths.sort();
    paths.removeDuplicates();
    for (const QString& p : paths) {
        const qint64 mtime = QFileInfo(p).lastModified().toMSecsSinceEpoch();
        fp.append(p.toUtf8());
        fp.append(kShaderCacheKeyDelim);
        fp.append(QByteArray::number(mtime));
        fp.append(kShaderCacheKeyDelim);
    }
    return fp;
}

static QByteArray shaderCacheKey(const QString& vertPath, qint64 vertMtime, const QByteArray& vertIncludeFp,
                                 const QString& fragPath, qint64 fragMtime, const QByteArray& fragIncludeFp,
                                 const QString& paramPreamble, const QByteArray& entryFp)
{
    QByteArray key = vertPath.toUtf8();
    key.append(kShaderCacheKeyDelim);
    key.append(QByteArray::number(vertMtime));
    key.append(kShaderCacheKeyDelim);
    key.append(vertIncludeFp);
    key.append(kShaderCacheKeyDelim);
    key.append(fragPath.toUtf8());
    key.append(kShaderCacheKeyDelim);
    key.append(QByteArray::number(fragMtime));
    key.append(kShaderCacheKeyDelim);
    key.append(fragIncludeFp);
    key.append(kShaderCacheKeyDelim);
    // T1.1: the generated named-param preamble is spliced into the fragment
    // source before baking, so it is part of the compiled SPIR-V and MUST be
    // part of the key. Without this, a metadata param edit (which rewrites the
    // preamble but leaves the .frag mtime untouched) would serve stale SPIR-V,
    // and the live load + warm-bake could disagree on the spliced source while
    // colliding on the same key. The preamble is small (a handful of #defines)
    // so appending it verbatim — mirroring how includeFingerprint appends raw
    // paths — keeps the key honest without a hashing round-trip.
    key.append(paramPreamble.toUtf8());
    key.append(kShaderCacheKeyDelim);
    // T1.4: the entry-point scaffold (prologue + generated main()) is assembled
    // into the fragment source before baking, so — like the preamble — it is
    // part of the SPIR-V and must be part of the key. Empty for the
    // animation / traditional path, so the key is unchanged there.
    key.append(entryFp);
    return key;
}

} // anonymous namespace

// Fan-out hook called from ShaderCompiler::clearCache(). Defined in this TU
// because the filename cache is private here; declared in internal.h so the
// compiler TU can see it without cross-cpp forward references.
void clearFilenameShaderCache()
{
    QMutexLocker lock(&filenameShaderCacheMutex());
    filenameShaderCache().clear();
}

QString ShaderNodeRhi::loadAndExpandShader(const QString& path, QString* outError)
{
    return ShaderCompiler::loadAndExpand(path, m_shaderIncludePaths, outError);
}

QString ShaderNodeRhi::loadAndExpandShaderTracked(const QString& path, QStringList* outIncludedPaths, QString* outError)
{
    return ShaderCompiler::loadAndExpand(path, m_shaderIncludePaths, outError, outIncludedPaths);
}

QRhi* ShaderNodeRhi::safeRhi() const
{
    // Hold m_itemMutex across the dereference so a concurrent invalidateItem()
    // can't slip in between the m_item->window() call and m_item being
    // destroyed. Without the lock, a TOCTOU window between the m_itemValid
    // check and the deref crashes inside QQuickItem::window().
    std::lock_guard<std::mutex> guard(m_itemMutex);
    return (m_itemValid.load(std::memory_order_acquire) && m_item && m_item->window()) ? m_item->window()->rhi()
                                                                                       : nullptr;
}

ShaderNodeRhi::ShaderNodeRhi(QQuickItem* item, std::unique_ptr<PhosphorShaders::IUboProfile> profile)
    : m_item(item)
    , m_uboProfile(profile ? std::move(profile) : std::make_unique<PhosphorShaders::BaseUniformProfile>())
{
    Q_ASSERT(item != nullptr);
    // The UBO profile's ctor seeds an identity qt_Matrix + qt_Opacity=1.0 (the
    // init that used to live here, moved into BaseUniformProfile so the
    // surface profile gets the same lead-in for free).
    // Initialize all customParams to -1.0 (the "unset" sentinel).
    // Shaders use `>= 0.0` checks to distinguish set values from defaults.
    for (int i = 0; i < kMaxCustomParams; ++i) {
        m_customParams[i] = QVector4D(-1.0f, -1.0f, -1.0f, -1.0f);
    }
    // Initialize all customColors to white
    for (int i = 0; i < kMaxCustomColors; ++i) {
        m_customColors[i] = Qt::white;
    }

    // 1x1 transparent fallback for when textures are disabled
    m_transparentFallbackImage = QImage(1, 1, QImage::Format_RGBA8888);
    m_transparentFallbackImage.fill(Qt::transparent);

    // preprocess() pulls the source provider's layer current before this
    // node renders (see the override's doc in the header). Without the
    // flag the scene graph never calls it.
    setFlag(QSGNode::UsePreprocess, true);
}

ShaderNodeRhi::~ShaderNodeRhi()
{
    QObject::disconnect(m_sourceTextureChangedConn);
    releaseRhiResources();
}

void ShaderNodeRhi::preprocess()
{
    // Stock-parity dependency pull (QSGRhiShaderEffectNode does the same
    // for its sampled sources): if the source is a dynamic texture (a
    // ShaderEffectSource layer), bring it current NOW — during the
    // preprocess phase of WHICHEVER renderer owns this node, main pass or
    // an enclosing QSGLayer. This gives deterministic same-frame
    // convergence through nested capture chains (card snapshot → stage →
    // tap → stage → transition capture): each consumer pulls its upstream
    // layer before rendering, instead of racing undefined layer-grab
    // ordering. updateTexture() is a cheap no-op on a clean layer.
    if (QSGTextureProvider* provider = m_sourceTextureProvider.data()) {
        if (auto* dynamicTex = qobject_cast<QSGDynamicTexture*>(provider->texture())) {
            dynamicTex->updateTexture();
        }
    }
}

void ShaderNodeRhi::invalidateItem()
{
    // Acquire m_itemMutex first — this blocks until any in-flight
    // prepare()/render()/rect()/safeRhi() call on the render thread releases
    // it. Only THEN do we set the flag and null the pointer. After
    // invalidateItem() returns the GUI-thread caller can safely destroy the
    // QQuickItem, knowing no dereference can race against the destruction.
    // We also null m_item itself so any future dereference (against a missed
    // flag check) is a clean nullptr crash rather than a dangling-pointer
    // UB read.
    std::lock_guard<std::mutex> guard(m_itemMutex);
    m_itemValid.store(false, std::memory_order_release);
    m_item = nullptr;
}

QSGRenderNode::StateFlags ShaderNodeRhi::changedStates() const
{
    return QSGRenderNode::ViewportState | QSGRenderNode::ScissorState;
}

QSGRenderNode::RenderingFlags ShaderNodeRhi::flags() const
{
    // Declare ONLY what's actually true. Earlier code returned
    // `OpaqueRendering | DepthAwareRendering`; both are lies given the
    // pipeline this node builds:
    //
    //   - `OpaqueRendering` claims this node fully covers its rect()
    //     with opaque pixels, letting Qt's batched renderer skip
    //     primitives behind it. Our pipeline blends with premultiplied
    //     alpha (`shadernoderhipipeline.cpp` createFullscreenQuadPipeline
    //     sets srcColor=One/dstColor=OneMinusSrcAlpha and enable=true),
    //     and the shader's fragColor.a is content-dependent. Most
    //     animation shaders output transparent pixels along their crop
    //     mask. Claiming opacity makes Qt assume the rect is fully
    //     covered, which permits batch reordering and depth-based
    //     culling that does NOT preserve sibling-node state restoration
    //     across two concurrent ShaderEffect instances.
    //
    //   - `DepthAwareRendering` claims the node writes depth and
    //     respects scene-graph Z via depth-buffer occlusion. Our
    //     pipeline has no depth attachment in its render-pass
    //     descriptor, no depth-write/test in setDepthOp/setDepthTest,
    //     and the fragment shader writes only fragColor. With this
    //     flag set, Qt's renderer may pre-write a depth value at the
    //     node's bounds that culls siblings behind, again reordering
    //     batches in a way that breaks state isolation.
    //
    // Symptom traced to these lies: when two ShaderEffect siblings
    // render concurrently (an OSD aura-glow hide leg + a snap-assist
    // morph show leg), the second-rendered node observes corrupted
    // per-fragment output (visibly: crop-mask shape goes wrong with
    // verified-clean UBO/VBO/customParams inputs). With both flags
    // dropped Qt treats each ShaderEffect as a translucent overlay
    // and isolates batch state correctly across siblings.
    //
    // Keep `BoundedRectRendering` (we DO render only within rect()) and
    // `NoExternalRendering` (we don't render via an external API
    // outside Qt's command buffer).
    return QSGRenderNode::BoundedRectRendering | QSGRenderNode::NoExternalRendering;
}

QRectF ShaderNodeRhi::rect() const
{
    // Lock-free fast path: serve cached geometry written by prepare() under
    // m_itemMutex. The cull pass calls rect() on every node every frame; if
    // we locked here we'd contend with prepare()/render() (which do real
    // work) for the same mutex. The atomics drift by at most one frame, which
    // the cull pass tolerates — and once the item is invalidated both fall
    // back to 0 because invalidateItem() callers stop submitting frames.
    if (!m_itemValid.load(std::memory_order_acquire)) {
        return QRectF();
    }
    const float w = m_cachedWidth.load(std::memory_order_acquire);
    const float h = m_cachedHeight.load(std::memory_order_acquire);
    return QRectF(0, 0, w, h);
}

// ============================================================================
// prepare() — resource initialization + shader baking + texture/uniform upload
// ============================================================================

bool ShaderNodeRhi::renderingIntoTexture() const
{
    QRhiRenderTarget* rt = renderTarget();
    return rt && rt->resourceType() == QRhiResource::TextureRenderTarget;
}

void ShaderNodeRhi::prepare()
{
    // Snapshot every m_item-derived value we need under the lock, then drop
    // the lock for the long resource-creation/upload body below. Holding the
    // lock for the entire prepare() would needlessly block GUI-thread item
    // destruction; holding it only across the deref window is enough — once
    // we have local copies of the window/size/dpr, the item can disappear
    // safely (we never touch m_item again in this call).
    QRhi* rhi = nullptr;
    qreal itemWidth = 0.0;
    qreal itemHeight = 0.0;
    qreal itemDpr = -1.0;
    {
        std::lock_guard<std::mutex> guard(m_itemMutex);
        if (!m_itemValid.load(std::memory_order_acquire) || !m_item || !m_item->window()) {
            qCDebug(lcShaderNode) << "prepare(): bail — itemValid:" << m_itemValid.load()
                                  << "item:" << (m_item != nullptr) << "window:" << (m_item && m_item->window());
            return;
        }
        QQuickWindow* win = m_item->window();
        rhi = win->rhi();
        itemWidth = m_item->width();
        itemHeight = m_item->height();
        itemDpr = win->devicePixelRatio();
    }
    // Publish the latest geometry to the lock-free rect() reader. Writing
    // outside the lock is safe — these atomics are independent of the
    // m_item pointer and rect() never touches m_item.
    m_cachedWidth.store(static_cast<float>(itemWidth), std::memory_order_release);
    m_cachedHeight.store(static_cast<float>(itemHeight), std::memory_order_release);
    if (!rhi) {
        qCDebug(lcShaderNode) << "prepare(): bail — rhi is null";
        return;
    }
    QRhiCommandBuffer* cb = commandBuffer();
    QRhiRenderTarget* rt = renderTarget();
    if (!cb || !rt) {
        qCDebug(lcShaderNode) << "prepare(): bail — cb:" << (cb != nullptr) << "rt:" << (rt != nullptr);
        return;
    }

    // Window-vs-texture retarget detection. The qt_Matrix NDC Y-flip is
    // per-render-target (see syncBaseUniforms): toggling a ShaderEffectSource's
    // hideSource (SurfaceAnimator attaching to / detaching from a decoration
    // stage) retargets this SAME node between the window and a layer texture
    // with no property edit, so nothing else would mark the matrix region
    // dirty. Force a full re-upload on the transition — qt_Matrix sits before
    // every granular dirty region, so only the full path re-covers it. Rare
    // (capture attach/detach), so the full upload costs nothing steady-state.
    const bool intoTextureTarget = renderingIntoTexture();
    if (m_lastTargetWasTexture.has_value() && *m_lastTargetWasTexture != intoTextureTarget) {
        m_uniformsDirty = true;
        m_didFullUploadOnce = false;
    }
    m_lastTargetWasTexture = intoTextureTarget;

    const int uboSize = m_uboProfile->baseSize() + (m_uniformExtension ? m_uniformExtension->extensionSize() : 0);

    if (!m_initialized) {
        // Do NOT set m_initialized = true until every resource below has been
        // created successfully. Flipping the flag up front would leave the node
        // in a half-initialized state forever if any create() below fails
        // (device lost, OOM): subsequent prepare() calls would skip this block
        // and later draw with nullptr m_ubo / m_vbo.
        qCInfo(lcShaderNode) << "ShaderNodeRhi INIT — backend:" << rhi->backendName()
                             << "driver:" << rhi->driverInfo().deviceName
                             << "Y-up framebuffer:" << rhi->isYUpInFramebuffer() << "RT pixelSize:" << rt->pixelSize()
                             << "item size:" << itemWidth << "x" << itemHeight << "DPR:" << itemDpr
                             << "iResolution:" << m_width << "x" << m_height << "UBO size:" << uboSize;
        // Create VBO (fullscreen quad)
        m_vbo.reset(
            rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(RhiConstants::QuadVertices)));
        if (!m_vbo->create()) {
            m_shaderError = QStringLiteral("Failed to create vertex buffer");
            m_vbo.reset();
            return;
        }
        m_ubo.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, uboSize));
        if (!m_ubo->create()) {
            m_shaderError = QStringLiteral("Failed to create uniform buffer");
            m_vbo.reset();
            m_ubo.reset();
            return;
        }
        // Audio spectrum texture (binding 6): 1x1 dummy when disabled
        m_audioSpectrumTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        if (!m_audioSpectrumTexture->create()) {
            m_shaderError = QStringLiteral("Failed to create audio spectrum texture");
            m_vbo.reset();
            m_ubo.reset();
            m_audioSpectrumTexture.reset();
            return;
        }
        m_audioSpectrumSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                     QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!m_audioSpectrumSampler->create()) {
            m_shaderError = QStringLiteral("Failed to create audio spectrum sampler");
            m_vbo.reset();
            m_ubo.reset();
            m_audioSpectrumTexture.reset();
            m_audioSpectrumSampler.reset();
            return;
        }
        // User texture slots (bindings 7-10): 1x1 dummy textures
        bool userTexturesOk = true;
        for (int i = 0; i < kMaxUserTextures; ++i) {
            m_userTextures[i].reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
            if (!m_userTextures[i]->create()) {
                m_shaderError = QStringLiteral("Failed to create user texture ") + QString::number(i);
                userTexturesOk = false;
                break;
            }
            m_userTextureSamplers[i].reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                           QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
            if (!m_userTextureSamplers[i]->create()) {
                m_shaderError = QStringLiteral("Failed to create user texture sampler ") + QString::number(i);
                userTexturesOk = false;
                break;
            }
            m_userTextureDirty[i] = true;
        }
        if (!userTexturesOk) {
            m_vbo.reset();
            m_ubo.reset();
            m_audioSpectrumTexture.reset();
            m_audioSpectrumSampler.reset();
            for (int i = 0; i < kMaxUserTextures; ++i) {
                m_userTextures[i].reset();
                m_userTextureSamplers[i].reset();
            }
            return;
        }
        // Desktop wallpaper texture (binding 11): 1x1 dummy
        m_wallpaperTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        if (!m_wallpaperTexture->create()) {
            m_shaderError = QStringLiteral("Failed to create wallpaper texture");
            releaseRhiResources();
            return;
        }
        m_wallpaperSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                 QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!m_wallpaperSampler->create()) {
            m_shaderError = QStringLiteral("Failed to create wallpaper sampler");
            releaseRhiResources();
            return;
        }
        m_wallpaperDirty = true;
        // Every create() succeeded — commit the init flag last.
        m_initialized = true;
    }

    if (m_shaderDirty) {
        m_shaderDirty = false;
        m_shaderReady = false;
        m_shaderError.clear();
        if (m_vertexShaderSource.isEmpty() || m_fragmentShaderSource.isEmpty()) {
            m_shaderError = QStringLiteral("Vertex or fragment shader source is empty");
            // Match the warning level used by the compile-failure paths below
            // (lines further down). Without this, a fragment-only consumer
            // that lands here loops at "render(): bail — shaderReady: false"
            // forever with no diagnostic indicating which stage is missing.
            qCWarning(lcShaderNode) << "Shader bake aborted:" << m_shaderError
                                    << "vertSrcEmpty=" << m_vertexShaderSource.isEmpty()
                                    << "fragSrcEmpty=" << m_fragmentShaderSource.isEmpty();
            return;
        }

        const QByteArray vertIncludeFp = includeFingerprint(m_vertexIncludedPaths);
        const QByteArray fragIncludeFp = includeFingerprint(m_fragmentIncludedPaths);
        const QByteArray entryFp = entryScaffoldFingerprint(m_entryPrologue, m_entryCandidates);
        const QByteArray cacheKey = shaderCacheKey(m_vertexPath, m_vertexMtime, vertIncludeFp, m_fragmentPath,
                                                   m_fragmentMtime, fragIncludeFp, m_paramPreamble, entryFp);
        if (!m_vertexPath.isEmpty() && !m_fragmentPath.isEmpty()) {
            QMutexLocker lock(&filenameShaderCacheMutex());
            auto& cache = filenameShaderCache();
            auto it = cache.constFind(cacheKey);
            if (it != cache.constEnd()) {
                m_vertexShader = it->vertex;
                m_fragmentShader = it->fragment;
                m_shaderReady = true;
                m_pipeline.reset();
                m_srb.reset();
                // Reset the feedback-pair SRB too: leaving it pointing at the
                // previous pipeline format risks a stale binding when feedback
                // toggles concurrently with a shader swap.
                m_srbB.reset();
            }
        }

        if (!m_shaderReady) {
            auto vertResult = ShaderCompiler::compile(m_vertexShaderSource.toUtf8(), QShader::VertexStage);
            m_vertexShader = vertResult.shader;
            if (!m_vertexShader.isValid()) {
                m_shaderError = QStringLiteral("Vertex shader: ")
                    + (vertResult.error.isEmpty() ? QStringLiteral("compilation failed (no details)")
                                                  : vertResult.error);
                // Surface compile errors at warning level — without this,
                // the only sign of a broken shader is the per-frame
                // "render(): bail — shaderReady: false" debug message,
                // which buries the actual GLSL diagnostic.
                qCWarning(lcShaderNode) << "Shader compile failed for" << m_vertexPath << ":" << m_shaderError;
                return;
            }
            auto fragResult = ShaderCompiler::compile(m_fragmentShaderSource.toUtf8(), QShader::FragmentStage);
            m_fragmentShader = fragResult.shader;
            if (!m_fragmentShader.isValid()) {
                m_shaderError = QStringLiteral("Fragment shader: ")
                    + (fragResult.error.isEmpty() ? QStringLiteral("compilation failed (no details)")
                                                  : fragResult.error);
                qCWarning(lcShaderNode) << "Shader compile failed for" << m_fragmentPath << ":" << m_shaderError;
                return;
            }
            m_shaderReady = true;
            m_pipeline.reset();
            m_srb.reset();
            m_srbB.reset();
            if (!m_vertexPath.isEmpty() && !m_fragmentPath.isEmpty()) {
                QMutexLocker lock(&filenameShaderCacheMutex());
                auto& cache = filenameShaderCache();
                if (cache.size() >= kShaderCacheMaxSize) {
                    shaderCacheEvictOne();
                }
                cache[cacheKey] = ShaderCacheEntry{m_vertexShader, m_fragmentShader};
            }
        }
    }

    // Multi-pass: bake buffer fragment shader(s) when path(s) set
    bakeBufferShaders();

    if (!m_shaderReady) {
        return;
    }

    // Upload textures FIRST — before any SRB or pipeline creation.
    uploadDirtyTextures(rhi, cb);

    // Create buffer targets before the image pass SRB
    const bool multiBufferMode = m_bufferPaths.size() > 1;
    const bool bufferReady = multiBufferMode ? m_multiBufferShadersReady : m_bufferShaderReady;
    if (!m_bufferPath.isEmpty() && bufferReady && !ensureBufferTarget()) {
        return;
    }

    // Late pipeline recovery
    if (!m_bufferPath.isEmpty() && bufferReady) {
        if (!multiBufferMode && m_bufferRenderTarget && !m_bufferRenderPassDescriptor
            && !m_bufferRenderTarget->renderPassDescriptor()) {
            m_bufferPipeline.reset();
            m_bufferSrb.reset();
            m_bufferSrbB.reset();
            m_bufferRenderTarget.reset();
            m_bufferRenderTargetB.reset();
            m_bufferRenderPassDescriptor.reset();
            m_bufferRenderPassDescriptorB.reset();
            m_bufferTexture.reset();
            m_bufferTextureB.reset();
            m_srb.reset();
            m_srbB.reset();
        } else if (!multiBufferMode && m_bufferFeedback && m_bufferRenderTargetB && !m_bufferRenderPassDescriptorB
                   && !m_bufferRenderTargetB->renderPassDescriptor()) {
            m_bufferPipeline.reset();
            m_bufferSrb.reset();
            m_bufferSrbB.reset();
            m_bufferRenderTargetB.reset();
            m_bufferRenderPassDescriptorB.reset();
            m_bufferTextureB.reset();
            m_srbB.reset();
        }
        if (!ensureBufferTarget() || !ensureBufferPipeline()) {
            return;
        }
        if (!m_srb || (!multiBufferMode && m_bufferFeedback && !m_srbB)) {
            ensurePipeline();
        }
    }
    if (m_shaderReady && (!m_pipeline || !m_srb || (m_bufferFeedback && !m_srbB))) {
        ensurePipeline();
    }

    if (!ensurePipeline()) {
        return;
    }

    // ========================================================================
    // Multipass buffer passes recorded in prepare()
    // ========================================================================
    const bool multipassSingle = !multiBufferMode && !m_bufferPath.isEmpty() && m_bufferShaderReady && m_bufferPipeline
        && m_bufferSrb && m_bufferRenderTarget && m_bufferTexture;
    const bool multipassMulti =
        multiBufferMode && m_multiBufferShadersReady && m_multiBufferTextures[0] && m_multiBufferPipelines[0];
    const bool multipassActive = multipassSingle || multipassMulti;

    if (multipassActive) {
        const QColor clearColor(0, 0, 0, 0);
        // Pin qt_Matrix to identity for the buffer passes. The image pass
        // carries an NDC Y-flip in qt_Matrix on Y-up-in-NDC backends (see
        // shadernoderhiuniforms.cpp), but the buffer/multipass FBOs are our
        // own offscreen targets whose texels the image pass later samples via
        // channelUv()/iFlipBufferY — that round-trip is already
        // backend-consistent. Flipping the buffer-write geometry would invert
        // the stored orientation and double-flip the sampled result. Restore
        // the flip-carrying value right after the loop, before render() draws.
        if (m_ubo) {
            static constexpr float kIdentity4x4[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                                       0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
            QRhiResourceUpdateBatch* identityBatch = rhi->nextResourceUpdateBatch();
            if (identityBatch) {
                identityBatch->updateDynamicBuffer(m_ubo.get(), 0, sizeof(kIdentity4x4), kIdentity4x4);
                cb->resourceUpdate(identityBatch);
            }
        }
        if (multiBufferMode) {
            const int n = qMin(m_bufferPaths.size(), kMaxBufferPasses);
            for (int i = 0; i < n; ++i) {
                if (!m_multiBufferTextures[i] || !m_multiBufferRenderTargets[i] || !m_multiBufferPipelines[i]
                    || !m_multiBufferSrbs[i]) {
                    continue;
                }
                QSize ps = m_multiBufferTextures[i]->pixelSize();
                cb->beginPass(m_multiBufferRenderTargets[i].get(), clearColor, {1.0f, 0});
                cb->setViewport(QRhiViewport(0, 0, ps.width(), ps.height()));
                cb->setScissor(QRhiScissor(0, 0, ps.width(), ps.height()));
                cb->setGraphicsPipeline(m_multiBufferPipelines[i].get());
                cb->setShaderResources(m_multiBufferSrbs[i].get());
                QRhiCommandBuffer::VertexInput vbufBinding(m_vbo.get(), 0);
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(4);
                cb->endPass();

                if (i + 1 < n && m_ubo) {
                    // Inter-pass write→read barrier only: re-uploading 4 bytes at
                    // offset 0 (the first float of qt_Matrix) forces the backend to
                    // serialize pass i's writes before pass i+1 samples its output.
                    // The value is immediately re-pinned by the next pass / final
                    // restore, so this is a sync hint, not a meaningful data update.
                    QRhiResourceUpdateBatch* barrier = rhi->nextResourceUpdateBatch();
                    if (barrier) {
                        barrier->updateDynamicBuffer(m_ubo.get(), 0, 4, m_uboProfile->mutableData());
                        cb->resourceUpdate(barrier);
                    }
                }
            }
        } else {
            if (m_bufferFeedback && !m_bufferFeedbackCleared && m_bufferRenderTarget && m_bufferRenderTargetB) {
                cb->beginPass(m_bufferRenderTarget.get(), clearColor, {1.0f, 0});
                cb->endPass();
                cb->beginPass(m_bufferRenderTargetB.get(), clearColor, {1.0f, 0});
                cb->endPass();
                m_bufferFeedbackCleared = true;
                // iFrame is computed as `cleared ? m_frame : 0` in syncBaseUniforms()
                // and lives in K_TIME_BLOCK (offsets 68-79). The transition we just
                // made invalidates whatever iFrame was uploaded at the top of this
                // prepare() pass, so force a time-block re-upload on the next
                // frame. Without this, the first post-clear frame would render
                // with iFrame stuck at 0 on the GPU.
                m_timeDirty = true;
                m_uniformsDirty = true;
            }
            const int writeIndex = m_bufferFeedback ? (m_frame % 2) : 0;
            QRhiTextureRenderTarget* bufferRT = (m_bufferFeedback && writeIndex == 1 && m_bufferRenderTargetB)
                ? m_bufferRenderTargetB.get()
                : m_bufferRenderTarget.get();
            QRhiShaderResourceBindings* bufferSrb =
                (m_bufferFeedback && writeIndex == 1 && m_bufferSrbB) ? m_bufferSrbB.get() : m_bufferSrb.get();
            QRhiTexture* writtenTexture = (m_bufferFeedback && writeIndex == 1 && m_bufferTextureB)
                ? m_bufferTextureB.get()
                : m_bufferTexture.get();
            cb->beginPass(bufferRT, clearColor, {1.0f, 0});
            cb->setViewport(
                QRhiViewport(0, 0, writtenTexture->pixelSize().width(), writtenTexture->pixelSize().height()));
            cb->setScissor(
                QRhiScissor(0, 0, writtenTexture->pixelSize().width(), writtenTexture->pixelSize().height()));
            cb->setGraphicsPipeline(m_bufferPipeline.get());
            cb->setShaderResources(bufferSrb);
            QRhiCommandBuffer::VertexInput vbufBinding(m_vbo.get(), 0);
            cb->setVertexInput(0, 1, &vbufBinding);
            cb->draw(4);
            cb->endPass();
        }

        // Resource flush after buffer passes (Vulkan barrier hint). Doubles as
        // the restore of the image-pass qt_Matrix (carrying the NDC Y-flip)
        // that the buffer passes above pinned to identity — render() draws the
        // image pass against this value. Uploads the full 64-byte qt_Matrix
        // (offset 0) rather than the prior 4-byte touch.
        if (m_ubo) {
            QRhiResourceUpdateBatch* barrier = rhi->nextResourceUpdateBatch();
            if (barrier) {
                // qt_Matrix is the leading mat4 of every UBO profile (offset 0),
                // so mutableData() points directly at it.
                barrier->updateDynamicBuffer(m_ubo.get(), 0, 16 * sizeof(float), m_uboProfile->mutableData());
                cb->resourceUpdate(barrier);
            }
        }
    }
}

// ============================================================================
// render() — image pass draw
// ============================================================================

void ShaderNodeRhi::render(const RenderState* state)
{
    Q_UNUSED(state)
    // Single authoritative gating point lives inside the locked viewport
    // block below — see the m_itemMutex guard around the m_item dereference.
    // The previous unlocked early-return here was redundant and gave a
    // false sense of safety: a stale `true` could still slip through to the
    // locked block, which (correctly) re-checks. One obvious gate, no two.
    if (!m_shaderReady || !m_pipeline || !m_srb) {
        qCDebug(lcShaderNode) << "render(): bail — shaderReady:" << m_shaderReady
                              << "pipeline:" << (m_pipeline != nullptr) << "srb:" << (m_srb != nullptr);
        return;
    }
    QRhiCommandBuffer* cb = commandBuffer();
    QRhiRenderTarget* rt = renderTarget();
    if (!cb || !rt) {
        return;
    }

    QSize outputSize = rt->pixelSize();
    int vpX = 0;
    int vpY = 0;
    int vpW = outputSize.width();
    int vpH = outputSize.height();
    bool offScreen = false;
    // Compute viewport/scissor from item geometry under the lock — every
    // m_item dereference (width, height, mapToItem, window) needs to happen
    // atomically with respect to invalidateItem(). After this block we use
    // only the local viewport ints, which are immune to item destruction.
    //
    // CRITICAL: an invalidated or zero-sized item must skip the draw
    // entirely. The previous logic fell through to the default
    // outputSize-sized viewport and issued a full-framebuffer draw with
    // the last-bound shader — visible as a stray full-screen paint
    // covering the window after the QML item was destroyed but before
    // the next sync removes the QSGRenderNode. Force offScreen on any
    // bail condition so the early-return below skips the draw.
    {
        std::lock_guard<std::mutex> guard(m_itemMutex);
        if (!m_itemValid.load(std::memory_order_acquire) || !m_item || !m_item->window() || m_item->width() <= 0
            || m_item->height() <= 0) {
            offScreen = true;
        } else {
            QQuickWindow* win = m_item->window();
            const qreal dpr = win->devicePixelRatio();
            const int itemPxW = qRound(m_item->width() * dpr);
            const int itemPxH = qRound(m_item->height() * dpr);
            // Tolerance is `qCeil(dpr) + 1` so 3× DPR + sub-pixel item
            // sizes don't push qRound noise past the heuristic and
            // misclassify a layer FBO as a direct-to-window draw (which
            // would route through the Y-flip path and mount the viewport
            // at the wrong framebuffer offset). The original `<= 1` was
            // tight enough to break at DPR≥2 with item sizes that
            // happen to round near framebuffer edges.
            const int isLayerTolerance = qCeil(dpr) + 1;
            const bool isLayerFbo = qAbs(outputSize.width() - itemPxW) <= isLayerTolerance
                && qAbs(outputSize.height() - itemPxH) <= isLayerTolerance;
            if (!isLayerFbo) {
                // Convert Qt scene-graph coords (top-left origin, Y-down) to
                // QRhiViewport coords (bottom-left origin, Y-up). Without this
                // flip, an item at scene-graph y=10 (near top of window)
                // would mount its viewport at y=10 from the BOTTOM of the
                // framebuffer — the shader would render at the bottom of the
                // screen even though its item is anchored at the top. The bug
                // is invisible for `isLayerFbo` consumers (zone-shader items
                // with layer.enabled=true render to a per-item FBO whose size
                // matches the item, so vp{X,Y}=0 is always correct) but
                // visible for direct-to-window shader items — which is
                // exactly the SurfaceAnimator's animation-shader case
                // (attachShaderToAnchor doesn't enable layer on shaderItem).
                //
                // Compute the unclamped item rect in framebuffer pixels (Y-up),
                // then intersect with the framebuffer; if the intersection is
                // empty the item is fully off-screen and we skip the draw
                // (qBound-only would have collapsed the rect to a 1px strip
                // glued to the nearest framebuffer edge).
                const QPointF topLeft = m_item->mapToItem(win->contentItem(), QPointF(0, 0));
                const int rawVpX = qRound(topLeft.x() * dpr);
                const int rawVpY = outputSize.height() - qRound(topLeft.y() * dpr) - itemPxH;
                const int leftPx = qMax(0, rawVpX);
                const int bottomPx = qMax(0, rawVpY);
                const int rightPx = qMin(outputSize.width(), rawVpX + itemPxW);
                const int topPx = qMin(outputSize.height(), rawVpY + itemPxH);
                if (leftPx >= rightPx || bottomPx >= topPx) {
                    offScreen = true;
                } else {
                    vpX = leftPx;
                    vpY = bottomPx;
                    vpW = rightPx - leftPx;
                    vpH = topPx - bottomPx;
                }
            } else {
                vpW = itemPxW;
                vpH = itemPxH;
            }
        }
    }
    if (offScreen) {
        // Item is fully outside the framebuffer — issuing a draw with a
        // 1×1 sliver clamped to the nearest edge would still cost a draw
        // call AND paint a stray pixel (Vulkan accepts but waste and
        // potential visual glitch). Skip rendering entirely; the next
        // sync that brings the item back on-screen will re-issue.
        return;
    }
    cb->setViewport(QRhiViewport(vpX, vpY, vpW, vpH));
    cb->setScissor(QRhiScissor(vpX, vpY, vpW, vpH));
    cb->setGraphicsPipeline(m_pipeline.get());

    const bool multiBufferMode = m_bufferPaths.size() > 1;
    const bool multipassSingle = !multiBufferMode && !m_bufferPath.isEmpty() && m_bufferShaderReady && m_bufferPipeline
        && m_bufferRenderTarget && m_bufferTexture;
    const int imageWriteIndex = multipassSingle && m_bufferFeedback ? (m_frame % 2) : 0;
    QRhiShaderResourceBindings* imageSrb =
        (multipassSingle && m_bufferFeedback && imageWriteIndex == 1 && m_srbB) ? m_srbB.get() : m_srb.get();
    cb->setShaderResources(imageSrb);
    QRhiCommandBuffer::VertexInput vbufBinding(m_vbo.get(), 0);
    cb->setVertexInput(0, 1, &vbufBinding);
    cb->draw(4);
}

void ShaderNodeRhi::releaseResources()
{
    qCInfo(lcShaderNode) << "ShaderNodeRhi::releaseResources() called — releasing all RHI resources";
    releaseRhiResources();
}

WarmShaderBakeResult warmShaderBakeCacheForPaths(const QString& vertexPath, const QString& fragmentPath,
                                                 const QStringList& includePaths, const QString& paramPreamble,
                                                 const QString& entryPrologue,
                                                 const QList<PhosphorShaders::EntryCandidate>& entryCandidates)
{
    WarmShaderBakeResult result;
    if (vertexPath.isEmpty() || fragmentPath.isEmpty()) {
        result.errorMessage = QStringLiteral("Vertex or fragment path is empty");
        return result;
    }
    // Capture mtimes BEFORE the reads so the cache key reflects the file
    // version we actually loaded (not whatever the file is at after the
    // open). Reading mtime after loadAndExpand opens a TOCTOU window where
    // a concurrent edit lands new mtime against old content in the cache.
    const qint64 vertMtime = QFileInfo(vertexPath).lastModified().toMSecsSinceEpoch();
    const qint64 fragMtime = QFileInfo(fragmentPath).lastModified().toMSecsSinceEpoch();
    QString err;
    QStringList vertIncludedPaths;
    QStringList fragIncludedPaths;
    const QString vertSource = ShaderCompiler::loadAndExpand(vertexPath, includePaths, &err, &vertIncludedPaths);
    if (vertSource.isEmpty()) {
        result.errorMessage = err.isEmpty() ? QStringLiteral("Failed to load vertex shader") : err;
        return result;
    }
    // Fragment: read raw, apply the T1.4 entry-point assembly, THEN expand —
    // identical to the live `loadFragmentShader`, so warm + live produce the
    // same pre-expansion source (and key) for an entry-only pack. For a
    // traditional pack (or the animation path with no scaffold) this is the
    // exact equivalent of the old `loadAndExpand(fragmentPath)`.
    QString fragRaw;
    {
        QFile fragFile(fragmentPath);
        if (!fragFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            result.errorMessage = QStringLiteral("Failed to open: ") + fragmentPath;
            return result;
        }
        fragRaw = QTextStream(&fragFile).readAll();
    }
    const QString fragAssembled = applyEntryAssembly(fragRaw, entryPrologue, entryCandidates);
    QString fragSource = ShaderCompiler::expandSource(fragAssembled, QFileInfo(fragmentPath).absolutePath(),
                                                      includePaths, &err, &fragIncludedPaths);
    if (fragSource.isEmpty()) {
        result.errorMessage = err.isEmpty() ? QStringLiteral("Failed to load fragment shader") : err;
        return result;
    }
    // Splice the same fragment-stage preamble the live `loadFragmentShader`
    // path applies (T1.1), so the SPIR-V this warm entry caches is byte-for-byte
    // what the live load would produce for the same key. Empty = no-op.
    fragSource = PhosphorShaders::spliceAfterVersion(fragSource, paramPreamble);

    auto vertResult = ShaderCompiler::compile(vertSource.toUtf8(), QShader::VertexStage);
    if (!vertResult.shader.isValid()) {
        result.errorMessage =
            vertResult.error.isEmpty() ? QStringLiteral("Vertex shader bake failed") : vertResult.error;
        return result;
    }
    auto fragResult = ShaderCompiler::compile(fragSource.toUtf8(), QShader::FragmentStage);
    if (!fragResult.shader.isValid()) {
        result.errorMessage =
            fragResult.error.isEmpty() ? QStringLiteral("Fragment shader bake failed") : fragResult.error;
        return result;
    }

    // Fingerprint includes so the warm-bake cache entry matches what
    // a per-stage `loadVertexShader` / `loadFragmentShader` later
    // computes — without this, the warm-baked entry would key on
    // {top-level path, mtime} alone and diverge from the runtime
    // bake's now-fingerprint-aware key, producing a cache miss every
    // first frame and erasing the warm-bake's purpose.
    const QByteArray vertIncludeFp = includeFingerprint(vertIncludedPaths);
    const QByteArray fragIncludeFp = includeFingerprint(fragIncludedPaths);
    const QByteArray entryFp = entryScaffoldFingerprint(entryPrologue, entryCandidates);
    const QByteArray key = shaderCacheKey(vertexPath, vertMtime, vertIncludeFp, fragmentPath, fragMtime, fragIncludeFp,
                                          paramPreamble, entryFp);
    QMutexLocker lock(&filenameShaderCacheMutex());
    auto& cache = filenameShaderCache();
    if (cache.size() >= kShaderCacheMaxSize) {
        shaderCacheEvictOne();
    }
    cache[key] = ShaderCacheEntry{vertResult.shader, fragResult.shader};
    result.success = true;
    return result;
}

} // namespace PhosphorRendering
