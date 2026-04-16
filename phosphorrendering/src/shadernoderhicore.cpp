// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "internal.h"

#include <PhosphorRendering/ShaderCompiler.h>

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickWindow>
#include <QStandardPaths>
#include <cstring>

#include <rhi/qshaderbaker.h>

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
ShaderCache s_shaderCache;
QMutex s_shaderCacheMutex;

constexpr int kShaderCacheMaxSize = 64;

static void shaderCacheEvictOne()
{
    if (s_shaderCache.isEmpty())
        return;
    s_shaderCache.erase(s_shaderCache.begin());
}

static constexpr char kShaderCacheKeyDelim = '\0';

static QByteArray shaderCacheKey(const QString& vertPath, qint64 vertMtime, const QString& fragPath, qint64 fragMtime)
{
    QByteArray key = vertPath.toUtf8();
    key.append(kShaderCacheKeyDelim);
    key.append(QByteArray::number(vertMtime));
    key.append(kShaderCacheKeyDelim);
    key.append(fragPath.toUtf8());
    key.append(kShaderCacheKeyDelim);
    key.append(QByteArray::number(fragMtime));
    return key;
}

} // anonymous namespace

QString ShaderNodeRhi::loadAndExpandShader(const QString& path, QString* outError)
{
    return ShaderCompiler::loadAndExpand(path, m_shaderIncludePaths, outError);
}

QRhi* ShaderNodeRhi::safeRhi() const
{
    return (m_itemValid.load(std::memory_order_acquire) && m_item && m_item->window()) ? m_item->window()->rhi()
                                                                                       : nullptr;
}

ShaderNodeRhi::ShaderNodeRhi(QQuickItem* item)
    : m_item(item)
{
    Q_ASSERT(item != nullptr);
    std::memset(&m_baseUniforms, 0, sizeof(m_baseUniforms));
    QMatrix4x4 identity;
    std::memcpy(m_baseUniforms.qt_Matrix, identity.constData(), 16 * sizeof(float));
    m_baseUniforms.qt_Opacity = 1.0f;
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
}

ShaderNodeRhi::~ShaderNodeRhi()
{
    releaseRhiResources();
}

void ShaderNodeRhi::invalidateItem()
{
    m_itemValid.store(false, std::memory_order_release);
}

QSGRenderNode::StateFlags ShaderNodeRhi::changedStates() const
{
    return QSGRenderNode::ViewportState | QSGRenderNode::ScissorState;
}

QSGRenderNode::RenderingFlags ShaderNodeRhi::flags() const
{
    return QSGRenderNode::BoundedRectRendering | QSGRenderNode::DepthAwareRendering | QSGRenderNode::OpaqueRendering
        | QSGRenderNode::NoExternalRendering;
}

QRectF ShaderNodeRhi::rect() const
{
    if (m_itemValid.load(std::memory_order_acquire) && m_item) {
        return QRectF(0, 0, m_item->width(), m_item->height());
    }
    return QRectF();
}

// ============================================================================
// prepare() — resource initialization + shader baking + texture/uniform upload
// ============================================================================

void ShaderNodeRhi::prepare()
{
    if (!m_itemValid.load(std::memory_order_acquire) || !m_item || !m_item->window()) {
        qCDebug(lcShaderNode) << "prepare(): bail — itemValid:" << m_itemValid.load() << "item:" << (m_item != nullptr)
                              << "window:" << (m_item && m_item->window());
        return;
    }
    QRhi* rhi = m_item->window()->rhi();
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

    const int uboSize = static_cast<int>(sizeof(PhosphorShell::BaseUniforms))
        + (m_uniformExtension ? m_uniformExtension->extensionSize() : 0);

    if (!m_initialized) {
        // Do NOT set m_initialized = true until every resource below has been
        // created successfully. Flipping the flag up front would leave the node
        // in a half-initialized state forever if any create() below fails
        // (device lost, OOM): subsequent prepare() calls would skip this block
        // and later draw with nullptr m_ubo / m_vbo.
        qCInfo(lcShaderNode) << "ShaderNodeRhi INIT — backend:" << rhi->backendName()
                             << "driver:" << rhi->driverInfo().deviceName
                             << "Y-up framebuffer:" << rhi->isYUpInFramebuffer() << "RT pixelSize:" << rt->pixelSize()
                             << "item size:" << m_item->width() << "x" << m_item->height()
                             << "DPR:" << (m_item->window() ? m_item->window()->devicePixelRatio() : -1)
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
            return;
        }

        const QByteArray cacheKey = shaderCacheKey(m_vertexPath, m_vertexMtime, m_fragmentPath, m_fragmentMtime);
        if (!m_vertexPath.isEmpty() && !m_fragmentPath.isEmpty()) {
            QMutexLocker lock(&s_shaderCacheMutex);
            auto it = s_shaderCache.constFind(cacheKey);
            if (it != s_shaderCache.constEnd()) {
                m_vertexShader = it->vertex;
                m_fragmentShader = it->fragment;
                m_shaderReady = true;
                m_pipeline.reset();
                m_srb.reset();
            }
        }

        if (!m_shaderReady) {
            const QList<QShaderBaker::GeneratedShader>& targets = ShaderCompiler::bakeTargets();
            QShaderBaker vertexBaker;
            vertexBaker.setGeneratedShaderVariants({QShader::StandardShader});
            vertexBaker.setGeneratedShaders(targets);
            auto vertResult = ShaderCompiler::compile(m_vertexShaderSource.toUtf8(), QShader::VertexStage);
            m_vertexShader = vertResult.shader;
            if (!m_vertexShader.isValid()) {
                m_shaderError = QStringLiteral("Vertex shader: ")
                    + (vertResult.error.isEmpty() ? QStringLiteral("compilation failed (no details)")
                                                  : vertResult.error);
                return;
            }
            auto fragResult = ShaderCompiler::compile(m_fragmentShaderSource.toUtf8(), QShader::FragmentStage);
            m_fragmentShader = fragResult.shader;
            if (!m_fragmentShader.isValid()) {
                m_shaderError = QStringLiteral("Fragment shader: ")
                    + (fragResult.error.isEmpty() ? QStringLiteral("compilation failed (no details)")
                                                  : fragResult.error);
                return;
            }
            m_shaderReady = true;
            m_pipeline.reset();
            m_srb.reset();
            if (!m_vertexPath.isEmpty() && !m_fragmentPath.isEmpty()) {
                QMutexLocker lock(&s_shaderCacheMutex);
                if (s_shaderCache.size() >= kShaderCacheMaxSize) {
                    shaderCacheEvictOne();
                }
                s_shaderCache[cacheKey] = ShaderCacheEntry{m_vertexShader, m_fragmentShader};
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
                    QRhiResourceUpdateBatch* barrier = rhi->nextResourceUpdateBatch();
                    if (barrier) {
                        barrier->updateDynamicBuffer(m_ubo.get(), 0, 4, &m_baseUniforms);
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

        // Resource flush after buffer passes (Vulkan barrier hint).
        if (m_ubo) {
            QRhiResourceUpdateBatch* barrier = rhi->nextResourceUpdateBatch();
            if (barrier) {
                barrier->updateDynamicBuffer(m_ubo.get(), 0, 4, &m_baseUniforms);
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
    if (!m_itemValid.load(std::memory_order_acquire)) {
        qCDebug(lcShaderNode) << "render(): bail — item invalid";
        return;
    }
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
    if (m_itemValid.load(std::memory_order_acquire) && m_item && m_item->window() && m_item->width() > 0
        && m_item->height() > 0) {
        QQuickWindow* win = m_item->window();
        const qreal dpr = win->devicePixelRatio();
        const int itemPxW = qRound(m_item->width() * dpr);
        const int itemPxH = qRound(m_item->height() * dpr);
        const bool isLayerFbo = qAbs(outputSize.width() - itemPxW) <= 1 && qAbs(outputSize.height() - itemPxH) <= 1;
        if (!isLayerFbo) {
            const QPointF topLeft = m_item->mapToItem(win->contentItem(), QPointF(0, 0));
            vpX = qRound(topLeft.x() * dpr);
            vpY = qRound(topLeft.y() * dpr);
            vpX = qBound(0, vpX, outputSize.width() - 1);
            vpY = qBound(0, vpY, outputSize.height() - 1);
        }
        vpW = qBound(1, itemPxW, outputSize.width() - vpX);
        vpH = qBound(1, itemPxH, outputSize.height() - vpY);
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
                                                 const QStringList& includePaths)
{
    WarmShaderBakeResult result;
    if (vertexPath.isEmpty() || fragmentPath.isEmpty()) {
        result.errorMessage = QStringLiteral("Vertex or fragment path is empty");
        return result;
    }
    QString err;
    const QString vertSource = ShaderCompiler::loadAndExpand(vertexPath, includePaths, &err);
    if (vertSource.isEmpty()) {
        result.errorMessage = err.isEmpty() ? QStringLiteral("Failed to load vertex shader") : err;
        return result;
    }
    const QString fragSource = ShaderCompiler::loadAndExpand(fragmentPath, includePaths, &err);
    if (fragSource.isEmpty()) {
        result.errorMessage = err.isEmpty() ? QStringLiteral("Failed to load fragment shader") : err;
        return result;
    }
    const qint64 vertMtime = QFileInfo(vertexPath).lastModified().toMSecsSinceEpoch();
    const qint64 fragMtime = QFileInfo(fragmentPath).lastModified().toMSecsSinceEpoch();

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

    const QByteArray key = shaderCacheKey(vertexPath, vertMtime, fragmentPath, fragMtime);
    QMutexLocker lock(&s_shaderCacheMutex);
    if (s_shaderCache.size() >= kShaderCacheMaxSize) {
        shaderCacheEvictOne();
    }
    s_shaderCache[key] = ShaderCacheEntry{vertResult.shader, fragResult.shader};
    result.success = true;
    return result;
}

} // namespace PhosphorRendering
