// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoneshadernoderhi.h"
#include "zoneshadernoderhi/internal.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTextStream>
#include <cstring>

#include "../../core/logging.h"
#include "../../core/shaderincluderesolver.h"

#include <rhi/qshaderbaker.h>

namespace PlasmaZones {

namespace {

struct ShaderCacheEntry
{
    QShader vertex;
    QShader fragment;
};
// Key is QByteArray so we can use NUL as delimiter (invalid in file paths; avoids newline collision)
using ShaderCache = QHash<QByteArray, ShaderCacheEntry>;
ShaderCache s_shaderCache;
QMutex s_shaderCacheMutex;

constexpr int kShaderCacheMaxSize = 64;

static void shaderCacheEvictOne()
{
    if (s_shaderCache.isEmpty()) {
        return;
    }
    s_shaderCache.erase(s_shaderCache.begin());
}

// NUL delimiter: cannot appear in file paths (Unix/Windows), avoids newline collision in keys
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

const QList<QShaderBaker::GeneratedShader>& detail::bakeTargets()
{
    static const QList<QShaderBaker::GeneratedShader> targets = {
        // SPIR-V 1.3 for Vulkan 1.1. QShaderVersion uses the same major*100 + minor*10
        // encoding as GLSL versions, so SPIR-V 1.3 is represented as 130 (not 13).
        // Vulkan 1.1 guarantees SPIR-V 1.3 support per the Vulkan spec appendix.
        {QShader::SpirvShader, QShaderVersion(130)},
        {QShader::GlslShader, QShaderVersion(330)},
        {QShader::GlslShader, QShaderVersion(300, QShaderVersion::GlslEs)},
        {QShader::GlslShader, QShaderVersion(310, QShaderVersion::GlslEs)},
        {QShader::GlslShader, QShaderVersion(320, QShaderVersion::GlslEs)},
    };
    return targets;
}

QString detail::loadAndExpandShader(const QString& path, QString* outError)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (outError) {
            *outError = QStringLiteral("Failed to open: ") + path;
        }
        return QString();
    }
    const QString raw = QTextStream(&file).readAll();
    const QString currentFileDir = QFileInfo(path).absolutePath();
    const QString shadersRootDir = QFileInfo(currentFileDir).absolutePath();
    const QString systemShaderDir = QStandardPaths::locate(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/shaders"), QStandardPaths::LocateDirectory);
    QStringList includePaths{currentFileDir};
    if (!shadersRootDir.isEmpty() && shadersRootDir != currentFileDir) {
        includePaths.append(shadersRootDir);
    }
    if (!systemShaderDir.isEmpty() && !includePaths.contains(systemShaderDir)) {
        includePaths.append(systemShaderDir);
    }
    QString err;
    const QString expanded = ShaderIncludeResolver::expandIncludes(raw, currentFileDir, includePaths, &err);
    if (!err.isEmpty() && outError) {
        *outError = err;
    }
    return err.isEmpty() ? expanded : QString();
}

ZoneShaderNodeRhi::ZoneShaderNodeRhi(QQuickItem* item)
    : m_item(item)
{
    Q_ASSERT(item != nullptr);
    std::memset(&m_uniforms, 0, sizeof(m_uniforms));
    QMatrix4x4 identity;
    std::memcpy(m_uniforms.qt_Matrix, identity.constData(), 16 * sizeof(float));
    m_uniforms.qt_Opacity = 1.0f;
    m_customParams1 = QVector4D(0.5f, 2.0f, 0.0f, 0.0f);
    m_customParams2 = QVector4D(0.0f, 0.0f, 0.0f, 0.0f);

    // 1×1 transparent fallback for when labels are disabled
    m_transparentFallbackImage = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
    m_transparentFallbackImage.fill(Qt::transparent);
}

ZoneShaderNodeRhi::~ZoneShaderNodeRhi()
{
    releaseRhiResources();
}

void ZoneShaderNodeRhi::invalidateItem()
{
    m_itemValid.store(false, std::memory_order_release);
}

QSGRenderNode::StateFlags ZoneShaderNodeRhi::changedStates() const
{
    return QSGRenderNode::ViewportState | QSGRenderNode::ScissorState;
}

QSGRenderNode::RenderingFlags ZoneShaderNodeRhi::flags() const
{
    return QSGRenderNode::BoundedRectRendering | QSGRenderNode::DepthAwareRendering | QSGRenderNode::OpaqueRendering
        | QSGRenderNode::NoExternalRendering;
}

QRectF ZoneShaderNodeRhi::rect() const
{
    if (m_itemValid.load(std::memory_order_acquire) && m_item) {
        return QRectF(0, 0, m_item->width(), m_item->height());
    }
    return QRectF();
}

// ============================================================================
// prepare() — resource initialization + shader baking + texture/uniform upload
// ============================================================================

void ZoneShaderNodeRhi::prepare()
{
    if (!m_itemValid.load(std::memory_order_acquire) || !m_item || !m_item->window()) {
        return;
    }
    QRhi* rhi = m_item->window()->rhi();
    if (!rhi) {
        return;
    }
    QRhiCommandBuffer* cb = commandBuffer();
    QRhiRenderTarget* rt = renderTarget();
    if (!cb || !rt) {
        return;
    }

    if (!m_initialized) {
        m_initialized = true;
        qCInfo(lcOverlay) << "ZoneShaderNodeRhi initializing — RHI backend:" << rhi->backendName()
                          << "driver:" << rhi->driverInfo().deviceName
                          << "Y-up framebuffer:" << rhi->isYUpInFramebuffer();
        // Create VBO (fullscreen quad)
        m_vbo.reset(
            rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(RhiConstants::QuadVertices)));
        if (!m_vbo->create()) {
            m_shaderError = QStringLiteral("Failed to create vertex buffer");
            return;
        }
        m_ubo.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(ZoneShaderUniforms)));
        if (!m_ubo->create()) {
            m_shaderError = QStringLiteral("Failed to create uniform buffer");
            return;
        }
        // Labels texture (1×1 initially; resized when image uploaded)
        m_labelsTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        if (!m_labelsTexture->create()) {
            m_shaderError = QStringLiteral("Failed to create labels texture");
            return;
        }
        m_labelsSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                              QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!m_labelsSampler->create()) {
            m_shaderError = QStringLiteral("Failed to create labels sampler");
            return;
        }
        // Audio spectrum texture (binding 6): 1x1 dummy when disabled. RGBA8; shader samples .r
        m_audioSpectrumTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        if (!m_audioSpectrumTexture->create()) {
            m_shaderError = QStringLiteral("Failed to create audio spectrum texture");
            return;
        }
        m_audioSpectrumSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                     QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!m_audioSpectrumSampler->create()) {
            m_shaderError = QStringLiteral("Failed to create audio spectrum sampler");
            return;
        }
        // User texture slots (bindings 7-10): 1x1 dummy textures when no user image set
        for (int i = 0; i < kMaxUserTextures; ++i) {
            m_userTextures[i].reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
            if (!m_userTextures[i]->create()) {
                m_shaderError = QStringLiteral("Failed to create user texture ") + QString::number(i);
                return;
            }
            m_userTextureSamplers[i].reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                           QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
            if (!m_userTextureSamplers[i]->create()) {
                m_shaderError = QStringLiteral("Failed to create user texture sampler ") + QString::number(i);
                return;
            }
            m_userTextureDirty[i] = true;
        }
        // Desktop wallpaper texture (binding 11): 1x1 dummy when disabled/unavailable
        m_wallpaperTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        if (!m_wallpaperTexture->create()) {
            m_shaderError = QStringLiteral("Failed to create wallpaper texture");
            return;
        }
        m_wallpaperSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                 QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!m_wallpaperSampler->create()) {
            m_shaderError = QStringLiteral("Failed to create wallpaper sampler");
            return;
        }
        m_wallpaperDirty = true;
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
            const QList<QShaderBaker::GeneratedShader>& targets = detail::bakeTargets();
            QShaderBaker vertexBaker;
            vertexBaker.setGeneratedShaderVariants({QShader::StandardShader});
            vertexBaker.setGeneratedShaders(targets);
            vertexBaker.setSourceString(m_vertexShaderSource.toUtf8(), QShader::VertexStage);
            m_vertexShader = vertexBaker.bake();
            if (!m_vertexShader.isValid()) {
                const QString msg = vertexBaker.errorMessage();
                m_shaderError = QStringLiteral("Vertex shader: ")
                    + (msg.isEmpty() ? QStringLiteral("compilation failed (no details)") : msg);
                return;
            }
            QShaderBaker fragmentBaker;
            fragmentBaker.setGeneratedShaderVariants({QShader::StandardShader});
            fragmentBaker.setGeneratedShaders(targets);
            fragmentBaker.setSourceString(m_fragmentShaderSource.toUtf8(), QShader::FragmentStage);
            m_fragmentShader = fragmentBaker.bake();
            if (!m_fragmentShader.isValid()) {
                const QString msg = fragmentBaker.errorMessage();
                m_shaderError = QStringLiteral("Fragment shader: ")
                    + (msg.isEmpty() ? QStringLiteral("compilation failed (no details)") : msg);
                return;
            }
            m_shaderReady = true;
            m_pipeline.reset();
            m_srb.reset();
            if (!m_vertexPath.isEmpty() && !m_fragmentPath.isEmpty()) {
                QMutexLocker lock(&s_shaderCacheMutex);
                s_shaderCache[cacheKey] = ShaderCacheEntry{m_vertexShader, m_fragmentShader};
            }
        }
    }

    // Multi-pass: bake buffer fragment shader(s) when path(s) set
    bakeBufferShaders();

    if (!m_shaderReady) {
        return;
    }

    // Create buffer targets (single or multi) before the image pass SRB so createImageSrb*()
    // can bind iChannel0/1/2/3 and syncUniformsFromData() sees correct sizes for iChannelResolution.
    const bool multiBufferMode = m_bufferPaths.size() > 1;
    const bool bufferReady = multiBufferMode ? m_multiBufferShadersReady : m_bufferShaderReady;
    if (!m_bufferPath.isEmpty() && bufferReady && !ensureBufferTarget()) {
        return;
    }

    if (!ensurePipeline()) {
        return;
    }

    uploadDirtyTextures(rhi, cb);
}

// ============================================================================
// render() — multi-pass + image pass draw commands
// ============================================================================

void ZoneShaderNodeRhi::render(const RenderState* state)
{
    Q_UNUSED(state)
    if (!m_itemValid.load(std::memory_order_acquire)) {
        return;
    }
    const bool multiBufferMode = m_bufferPaths.size() > 1;
    const bool bufferReady = multiBufferMode ? m_multiBufferShadersReady : m_bufferShaderReady;
    // Multi-buffer: create buffer targets and pipelines before the image pass SRB, so
    // createImageSrbMulti() can bind iChannel0/1/2. If we called ensurePipeline() first,
    // the image SRB would be created with no channel textures bound -> effect samples black.
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
        ensureBufferTarget();
        ensureBufferPipeline();
        if (!m_srb || (!multiBufferMode && m_bufferFeedback && !m_srbB)) {
            ensurePipeline();
        }
    }
    // Image pass pipeline/SRB (after buffer setup so multi-buffer has textures to bind)
    if (m_shaderReady && (!m_pipeline || !m_srb || (m_bufferFeedback && !m_srbB))) {
        ensurePipeline();
    }
    if (!m_shaderReady || !m_pipeline || !m_srb) {
        return;
    }
    QRhiCommandBuffer* cb = commandBuffer();
    QRhiRenderTarget* rt = renderTarget();
    if (!cb || !rt) {
        return;
    }

    const bool multipassSingle = !multiBufferMode && !m_bufferPath.isEmpty() && m_bufferShaderReady && m_bufferPipeline
        && m_bufferRenderTarget && m_bufferTexture;
    const bool multipassMulti =
        multiBufferMode && m_multiBufferShadersReady && m_multiBufferTextures[0] && m_multiBufferPipelines[0];
    const bool multipassActive = multipassSingle || multipassMulti;

    if (multipassActive) {
        const QColor clearColor(0, 0, 0, 0);
        if (multiBufferMode) {
            const int n = qMin(m_bufferPaths.size(), kMaxBufferPasses);
            for (int i = 0; i < n; ++i) {
                if (!m_multiBufferRenderTargets[i] || !m_multiBufferPipelines[i] || !m_multiBufferSrbs[i]) {
                    continue;
                }
                QSize ps = m_multiBufferTextures[i]->pixelSize();
                cb->beginPass(m_multiBufferRenderTargets[i].get(), clearColor, {1.0f, 0});
                cb->setViewport(QRhiViewport(0, 0, ps.width(), ps.height()));
                cb->setGraphicsPipeline(m_multiBufferPipelines[i].get());
                cb->setShaderResources(m_multiBufferSrbs[i].get());
                QRhiCommandBuffer::VertexInput vbufBinding(m_vbo.get(), 0);
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(4);
                cb->endPass();
            }
        } else {
            if (m_bufferFeedback && !m_bufferFeedbackCleared && m_bufferRenderTarget && m_bufferRenderTargetB) {
                cb->beginPass(m_bufferRenderTarget.get(), clearColor, {1.0f, 0});
                cb->endPass();
                cb->beginPass(m_bufferRenderTargetB.get(), clearColor, {1.0f, 0});
                cb->endPass();
                m_bufferFeedbackCleared = true;
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
            cb->setGraphicsPipeline(m_bufferPipeline.get());
            cb->setShaderResources(bufferSrb);
            QRhiCommandBuffer::VertexInput vbufBinding(m_vbo.get(), 0);
            cb->setVertexInput(0, 1, &vbufBinding);
            cb->draw(4);
            cb->endPass();
        }
        // Must begin our own pass: multi-pass shaders read from buffer textures while
        // writing to rt. Drawing inline in the scene graph's pass causes Vulkan error
        // "Texture used with different accesses within the same pass". beginPass(rt)
        // starts a new pass with correct barriers.
        const QColor mainClear(0, 0, 0, 0);
        cb->beginPass(rt, mainClear, {1.0f, 0});
    }

    // Use item's rect in render target (device pixels) so we render only within the item,
    // not full screen. Essential when ZoneShaderItem is in a small preview (e.g. editor dialog).
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
        // When rendering to a layer FBO (QQuickItem::layer.enabled), the render
        // target matches the item size and origin is (0,0). Only compute a
        // window-relative offset when rendering directly to the window surface.
        // Use ±1px tolerance for fractional DPI scaling rounding differences.
        const bool isLayerFbo = qAbs(outputSize.width() - itemPxW) <= 1 && qAbs(outputSize.height() - itemPxH) <= 1;
        if (!isLayerFbo) {
            const QPointF topLeft = m_item->mapToItem(win->contentItem(), QPointF(0, 0));
            vpX = qRound(topLeft.x() * dpr);
            vpY = qRound(topLeft.y() * dpr);
            // Clamp to render target bounds (e.g. when item is partially off-screen)
            vpX = qBound(0, vpX, outputSize.width() - 1);
            vpY = qBound(0, vpY, outputSize.height() - 1);
        }
        vpW = qBound(1, itemPxW, outputSize.width() - vpX);
        vpH = qBound(1, itemPxH, outputSize.height() - vpY);
    }
    cb->setViewport(QRhiViewport(vpX, vpY, vpW, vpH));
    cb->setGraphicsPipeline(m_pipeline.get());
    const int imageWriteIndex = multipassSingle && m_bufferFeedback ? (m_frame % 2) : 0;
    QRhiShaderResourceBindings* imageSrb =
        (multipassSingle && m_bufferFeedback && imageWriteIndex == 1 && m_srbB) ? m_srbB.get() : m_srb.get();
    cb->setShaderResources(imageSrb);
    QRhiCommandBuffer::VertexInput vbufBinding(m_vbo.get(), 0);
    cb->setVertexInput(0, 1, &vbufBinding);
    cb->draw(4);
}

void ZoneShaderNodeRhi::releaseResources()
{
    releaseRhiResources();
}

WarmShaderBakeResult warmShaderBakeCacheForPaths(const QString& vertexPath, const QString& fragmentPath)
{
    WarmShaderBakeResult result;
    if (vertexPath.isEmpty() || fragmentPath.isEmpty()) {
        result.errorMessage = QStringLiteral("Vertex or fragment path is empty");
        return result;
    }
    QString err;
    const QString vertSource = detail::loadAndExpandShader(vertexPath, &err);
    if (vertSource.isEmpty()) {
        result.errorMessage = err.isEmpty() ? QStringLiteral("Failed to load vertex shader") : err;
        return result;
    }
    const QString fragSource = detail::loadAndExpandShader(fragmentPath, &err);
    if (fragSource.isEmpty()) {
        result.errorMessage = err.isEmpty() ? QStringLiteral("Failed to load fragment shader") : err;
        return result;
    }
    const qint64 vertMtime = QFileInfo(vertexPath).lastModified().toMSecsSinceEpoch();
    const qint64 fragMtime = QFileInfo(fragmentPath).lastModified().toMSecsSinceEpoch();

    const QList<QShaderBaker::GeneratedShader>& targets = detail::bakeTargets();
    QShaderBaker vertexBaker;
    vertexBaker.setGeneratedShaderVariants({QShader::StandardShader});
    vertexBaker.setGeneratedShaders(targets);
    vertexBaker.setSourceString(vertSource.toUtf8(), QShader::VertexStage);
    const QShader vertexShader = vertexBaker.bake();
    if (!vertexShader.isValid()) {
        result.errorMessage = vertexBaker.errorMessage();
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = QStringLiteral("Vertex shader bake failed");
        }
        return result;
    }
    QShaderBaker fragmentBaker;
    fragmentBaker.setGeneratedShaderVariants({QShader::StandardShader});
    fragmentBaker.setGeneratedShaders(targets);
    fragmentBaker.setSourceString(fragSource.toUtf8(), QShader::FragmentStage);
    const QShader fragmentShader = fragmentBaker.bake();
    if (!fragmentShader.isValid()) {
        result.errorMessage = fragmentBaker.errorMessage();
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = QStringLiteral("Fragment shader bake failed");
        }
        return result;
    }

    const QByteArray key = shaderCacheKey(vertexPath, vertMtime, fragmentPath, fragMtime);
    QMutexLocker lock(&s_shaderCacheMutex);
    if (s_shaderCache.size() >= kShaderCacheMaxSize) {
        shaderCacheEvictOne();
    }
    s_shaderCache[key] = ShaderCacheEntry{vertexShader, fragmentShader};
    result.success = true;
    return result;
}

} // namespace PlasmaZones
