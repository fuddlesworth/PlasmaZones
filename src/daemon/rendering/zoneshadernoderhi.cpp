// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoneshadernoderhi.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTextStream>
#include <cstddef>
#include <cstring>

#include "../../core/logging.h"
#include "../../core/shaderincluderesolver.h"

#include <rhi/qshaderbaker.h>

namespace PlasmaZones {

namespace {

struct ShaderCacheEntry {
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

static const QList<QShaderBaker::GeneratedShader>& bakeTargets()
{
    static const QList<QShaderBaker::GeneratedShader> targets = {
        { QShader::GlslShader, QShaderVersion(330) },
        { QShader::GlslShader, QShaderVersion(300, QShaderVersion::GlslEs) },
        { QShader::GlslShader, QShaderVersion(310, QShaderVersion::GlslEs) },
        { QShader::GlslShader, QShaderVersion(320, QShaderVersion::GlslEs) },
    };
    return targets;
}

static QString loadAndExpandShader(const QString& path, QString* outError)
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
    const QString systemShaderDir = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                          QStringLiteral("plasmazones/shaders"),
                                                          QStandardPaths::LocateDirectory);
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

} // anonymous namespace

namespace RhiConstants {

static constexpr float QuadVertices[] = {
    -1.0f, -1.0f, 0.0f, 0.0f,
    1.0f,  -1.0f, 1.0f, 0.0f,
    -1.0f, 1.0f,  0.0f, 1.0f,
    1.0f,  1.0f,  1.0f, 1.0f,
};

static constexpr int UniformVecIndex1 = 0;
static constexpr int UniformVecIndex2 = 1;
static constexpr int UniformVecIndex3 = 2;
static constexpr int UniformVecIndex4 = 3;
static constexpr int ComponentX = 0;
static constexpr int ComponentY = 1;
static constexpr int ComponentZ = 2;
static constexpr int ComponentW = 3;

} // namespace RhiConstants

namespace {

// Shared fullscreen-quad pipeline setup for both buffer and image passes (DRY).
static std::unique_ptr<QRhiGraphicsPipeline> createFullscreenQuadPipeline(
    QRhi* rhi, QRhiRenderPassDescriptor* rpDesc, const QShader& vertexShader,
    const QShader& fragmentShader, QRhiShaderResourceBindings* srb)
{
    std::unique_ptr<QRhiGraphicsPipeline> pipeline(rhi->newGraphicsPipeline());
    pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    pipeline->setShaderStages({
        { QRhiShaderStage::Vertex, vertexShader },
        { QRhiShaderStage::Fragment, fragmentShader }
    });
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ { 4 * sizeof(float) } });
    inputLayout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
        { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) }
    });
    pipeline->setVertexInputLayout(inputLayout);
    pipeline->setShaderResourceBindings(srb);
    pipeline->setRenderPassDescriptor(rpDesc);
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    pipeline->setTargetBlends({ blend });
    if (!pipeline->create()) {
        return nullptr;
    }
    return pipeline;
}

} // anonymous namespace

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

QSGRenderNode::StateFlags ZoneShaderNodeRhi::changedStates() const
{
    return QSGRenderNode::ViewportState | QSGRenderNode::ScissorState;
}

QSGRenderNode::RenderingFlags ZoneShaderNodeRhi::flags() const
{
    return QSGRenderNode::BoundedRectRendering | QSGRenderNode::DepthAwareRendering
           | QSGRenderNode::OpaqueRendering | QSGRenderNode::NoExternalRendering;
}

QRectF ZoneShaderNodeRhi::rect() const
{
    if (m_item) {
        return QRectF(0, 0, m_item->width(), m_item->height());
    }
    return QRectF();
}

void ZoneShaderNodeRhi::prepare()
{
    if (!m_item || !m_item->window()) {
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
        // Create VBO (fullscreen quad)
        m_vbo.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(RhiConstants::QuadVertices)));
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
            const QList<QShaderBaker::GeneratedShader>& targets = bakeTargets();
            QShaderBaker vertexBaker;
            vertexBaker.setGeneratedShaderVariants({ QShader::StandardShader });
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
            fragmentBaker.setGeneratedShaderVariants({ QShader::StandardShader });
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
                s_shaderCache[cacheKey] = ShaderCacheEntry{ m_vertexShader, m_fragmentShader };
            }
        }
    }

    // Multi-pass: bake buffer fragment shader(s) when path(s) set
    const bool multipass = !m_bufferPath.isEmpty();
    const bool multiBufferMode = m_bufferPaths.size() > 1;
    if (multipass && multiBufferMode && m_multiBufferShaderDirty) {
        m_multiBufferShaderDirty = false;
        m_multiBufferShadersReady = false;
        for (int i = 0; i < kMaxBufferPasses; ++i) {
            m_multiBufferFragmentShaderSources[i].clear();
            m_multiBufferFragmentShaders[i] = QShader();
        }
        const QList<QShaderBaker::GeneratedShader>& targets = bakeTargets();
        bool allOk = true;
        for (int i = 0; i < m_bufferPaths.size() && i < kMaxBufferPasses; ++i) {
            const QString& path = m_bufferPaths.at(i);
            if (!QFileInfo::exists(path)) {
                allOk = false;
                break;
            }
            QString err;
            QString src = loadAndExpandShader(path, &err);
            if (src.isEmpty()) {
                allOk = false;
                break;
            }
            m_multiBufferFragmentShaderSources[i] = src;
            m_multiBufferMtimes[i] = QFileInfo(path).lastModified().toMSecsSinceEpoch();
            QShaderBaker fragmentBaker;
            fragmentBaker.setGeneratedShaderVariants({ QShader::StandardShader });
            fragmentBaker.setGeneratedShaders(targets);
            fragmentBaker.setSourceString(src.toUtf8(), QShader::FragmentStage);
            m_multiBufferFragmentShaders[i] = fragmentBaker.bake();
            if (!m_multiBufferFragmentShaders[i].isValid()) {
                qCWarning(lcOverlay) << "Multi-buffer shader" << i << "compile failed:" << path << fragmentBaker.errorMessage();
                allOk = false;
                break;
            }
        }
        if (allOk && m_bufferPaths.size() > 0) {
            m_multiBufferShadersReady = true;
            for (int i = 0; i < kMaxBufferPasses; ++i) {
                m_multiBufferPipelines[i].reset();
                m_multiBufferSrbs[i].reset();
            }
            m_pipeline.reset();
            m_srb.reset();
            m_srbB.reset();
        } else {
            m_multiBufferShaderDirty = true; // Retry next frame on failure
        }
    }
    if (multipass && !multiBufferMode && m_bufferShaderDirty) {
        m_bufferShaderDirty = false;
        m_bufferShaderReady = false;
        if (m_bufferFragmentShaderSource.isEmpty()) {
            if (QFileInfo::exists(m_bufferPath)) {
                QString err;
                m_bufferFragmentShaderSource = loadAndExpandShader(m_bufferPath, &err);
                if (!m_bufferFragmentShaderSource.isEmpty()) {
                    m_bufferMtime = QFileInfo(m_bufferPath).lastModified().toMSecsSinceEpoch();
                }
            }
        }
        if (!m_bufferFragmentShaderSource.isEmpty()) {
            const QList<QShaderBaker::GeneratedShader>& targets = bakeTargets();
            QShaderBaker fragmentBaker;
            fragmentBaker.setGeneratedShaderVariants({ QShader::StandardShader });
            fragmentBaker.setGeneratedShaders(targets);
            fragmentBaker.setSourceString(m_bufferFragmentShaderSource.toUtf8(), QShader::FragmentStage);
            m_bufferFragmentShader = fragmentBaker.bake();
            if (m_bufferFragmentShader.isValid()) {
                m_bufferShaderReady = true;
                m_bufferPipeline.reset();
                m_bufferSrb.reset();
            } else {
                qCWarning(lcOverlay) << "Buffer shader compile failed:" << m_bufferPath << fragmentBaker.errorMessage();
                m_bufferShaderDirty = true; // Retry next frame on failure
            }
        }
    }

    if (!m_shaderReady) {
        return;
    }

    // Create buffer targets (single or multi) before the image pass SRB so createImageSrb*()
    // can bind iChannel0/1/2/3 and syncUniformsFromData() sees correct sizes for iChannelResolution.
    const bool bufferReady = multiBufferMode ? m_multiBufferShadersReady : m_bufferShaderReady;
    if (!m_bufferPath.isEmpty() && bufferReady && !ensureBufferTarget()) {
        return;
    }

    if (!ensurePipeline()) {
        return;
    }

    // Labels texture: resize if needed, upload when dirty
    if (m_labelsTextureDirty && m_labelsTexture && m_labelsSampler) {
        m_labelsTextureDirty = false;
        const QSize targetSize = (!m_labelsImage.isNull() && m_labelsImage.width() > 0 && m_labelsImage.height() > 0)
                                     ? m_labelsImage.size()
                                     : QSize(1, 1);
        if (m_labelsTexture->pixelSize() != targetSize) {
            m_labelsTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, targetSize));
            if (!m_labelsTexture->create()) {
                m_shaderError = QStringLiteral("Failed to resize labels texture");
                return;
            }
            m_srb.reset(); // Force SRB recreation with new texture
            if (!ensurePipeline()) {
                return;
            }
        }
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        if (batch) {
            const QImage& src = (!m_labelsImage.isNull() && m_labelsImage.width() > 0 && m_labelsImage.height() > 0)
                                    ? m_labelsImage
                                    : m_transparentFallbackImage;
            batch->uploadTexture(m_labelsTexture.get(), src);
            cb->resourceUpdate(batch);
        }
    }

    if (m_uniformsDirty) {
        syncUniformsFromData();
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        if (batch) {
            if (!m_didFullUploadOnce) {
                batch->updateDynamicBuffer(m_ubo.get(), 0, sizeof(ZoneShaderUniforms), &m_uniforms);
                m_didFullUploadOnce = true;
            } else {
                using namespace ZoneShaderUboRegions;
                if (m_timeDirty) {
                    batch->updateDynamicBuffer(m_ubo.get(),
                                               K_TIME_BLOCK_OFFSET,
                                               K_TIME_BLOCK_SIZE,
                                               static_cast<const char*>(static_cast<const void*>(&m_uniforms)) + K_TIME_BLOCK_OFFSET);
                }
                if (m_zoneDataDirty) {
                    batch->updateDynamicBuffer(m_ubo.get(),
                                               K_SCENE_DATA_OFFSET,
                                               K_SCENE_DATA_SIZE,
                                               static_cast<const char*>(static_cast<const void*>(&m_uniforms)) + K_SCENE_DATA_OFFSET);
                }
                // Defensive: if a future setter sets m_uniformsDirty without granular flags, do full upload
                if (!m_timeDirty && !m_zoneDataDirty) {
                    batch->updateDynamicBuffer(m_ubo.get(), 0, sizeof(ZoneShaderUniforms), &m_uniforms);
                }
            }
            if (!m_vboUploaded) {
                batch->uploadStaticBuffer(m_vbo.get(), RhiConstants::QuadVertices);
                m_vboUploaded = true;
            }
            cb->resourceUpdate(batch);
            m_timeDirty = false;
            m_zoneDataDirty = false;
            m_uniformsDirty = false;
        }
    } else {
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        if (batch && !m_vboUploaded) {
            batch->uploadStaticBuffer(m_vbo.get(), RhiConstants::QuadVertices);
            m_vboUploaded = true;
            cb->resourceUpdate(batch);
        }
    }

    if (m_dummyChannelTextureNeedsUpload && m_dummyChannelTexture) {
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        if (batch) {
            QImage onePixel(1, 1, QImage::Format_RGBA8888);
            onePixel.fill(Qt::transparent);
            batch->uploadTexture(m_dummyChannelTexture.get(), onePixel);
            cb->resourceUpdate(batch);
            m_dummyChannelTextureNeedsUpload = false;
        }
    }
}

void ZoneShaderNodeRhi::render(const RenderState* state)
{
    Q_UNUSED(state)
    const bool multiBufferMode = m_bufferPaths.size() > 1;
    const bool bufferReady = multiBufferMode ? m_multiBufferShadersReady : m_bufferShaderReady;
    // Multi-buffer: create buffer targets and pipelines before the image pass SRB, so
    // createImageSrbMulti() can bind iChannel0/1/2. If we called ensurePipeline() first,
    // the image SRB would be created with no channel textures bound -> effect samples black.
    if (!m_bufferPath.isEmpty() && bufferReady) {
        if (!multiBufferMode && m_bufferRenderTarget && !m_bufferRenderPassDescriptor && !m_bufferRenderTarget->renderPassDescriptor()) {
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
        } else if (!multiBufferMode && m_bufferFeedback && m_bufferRenderTargetB && !m_bufferRenderPassDescriptorB && !m_bufferRenderTargetB->renderPassDescriptor()) {
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

    const bool multipassSingle = !multiBufferMode && !m_bufferPath.isEmpty() && m_bufferShaderReady
        && m_bufferPipeline && m_bufferRenderTarget && m_bufferTexture;
    const bool multipassMulti = multiBufferMode && m_multiBufferShadersReady && m_multiBufferTextures[0]
        && m_multiBufferPipelines[0];
    const bool multipass = multipassSingle || multipassMulti;

    if (multipass) {
        const QColor clearColor(0, 0, 0, 0);
        if (multiBufferMode) {
            const int n = qMin(m_bufferPaths.size(), kMaxBufferPasses);
            for (int i = 0; i < n; ++i) {
                if (!m_multiBufferRenderTargets[i] || !m_multiBufferPipelines[i] || !m_multiBufferSrbs[i]) {
                    continue;
                }
                QSize ps = m_multiBufferTextures[i]->pixelSize();
                cb->beginPass(m_multiBufferRenderTargets[i].get(), clearColor, { 1.0f, 0 });
                cb->setViewport(QRhiViewport(0, 0, ps.width(), ps.height()));
                cb->setGraphicsPipeline(m_multiBufferPipelines[i].get());
                cb->setShaderResources(m_multiBufferSrbs[i].get());
                QRhiCommandBuffer::VertexInput vbufBinding(m_vbo.get(), 0);
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(4);
                cb->endPass();
            }
            // iChannelResolution already set and uploaded in prepare() via syncUniformsFromData()
        } else {
            if (m_bufferFeedback && !m_bufferFeedbackCleared && m_bufferRenderTarget && m_bufferRenderTargetB) {
                cb->beginPass(m_bufferRenderTarget.get(), clearColor, { 1.0f, 0 });
                cb->endPass();
                cb->beginPass(m_bufferRenderTargetB.get(), clearColor, { 1.0f, 0 });
                cb->endPass();
                m_bufferFeedbackCleared = true;
            }
            const int writeIndex = m_bufferFeedback ? (m_frame % 2) : 0;
            QRhiTextureRenderTarget* bufferRT = (m_bufferFeedback && writeIndex == 1 && m_bufferRenderTargetB)
                ? m_bufferRenderTargetB.get() : m_bufferRenderTarget.get();
            QRhiShaderResourceBindings* bufferSrb = (m_bufferFeedback && writeIndex == 1 && m_bufferSrbB)
                ? m_bufferSrbB.get() : m_bufferSrb.get();
            QRhiTexture* writtenTexture = (m_bufferFeedback && writeIndex == 1 && m_bufferTextureB)
                ? m_bufferTextureB.get() : m_bufferTexture.get();
            cb->beginPass(bufferRT, clearColor, { 1.0f, 0 });
            cb->setViewport(QRhiViewport(0, 0, writtenTexture->pixelSize().width(), writtenTexture->pixelSize().height()));
            cb->setGraphicsPipeline(m_bufferPipeline.get());
            cb->setShaderResources(bufferSrb);
            QRhiCommandBuffer::VertexInput vbufBinding(m_vbo.get(), 0);
            cb->setVertexInput(0, 1, &vbufBinding);
            cb->draw(4);
            cb->endPass();
        }
        const QColor mainClear(0, 0, 0, 0);
        cb->beginPass(rt, mainClear, { 1.0f, 0 });
    }

    QSize outputSize = rt->pixelSize();
    cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
    cb->setGraphicsPipeline(m_pipeline.get());
    const int imageWriteIndex = multipassSingle && m_bufferFeedback ? (m_frame % 2) : 0;
    QRhiShaderResourceBindings* imageSrb = (multipassSingle && m_bufferFeedback && imageWriteIndex == 1 && m_srbB) ? m_srbB.get() : m_srb.get();
    cb->setShaderResources(imageSrb);
    QRhiCommandBuffer::VertexInput vbufBinding(m_vbo.get(), 0);
    cb->setVertexInput(0, 1, &vbufBinding);
    cb->draw(4);
}

void ZoneShaderNodeRhi::releaseResources()
{
    releaseRhiResources();
}

void ZoneShaderNodeRhi::setZones(const QVector<ZoneData>& zones)
{
    int count = qMin(zones.size(), MaxZones);
    m_zones = zones.mid(0, count);
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}

void ZoneShaderNodeRhi::setZone(int index, const ZoneData& data)
{
    if (index >= 0 && index < MaxZones) {
        if (index >= m_zones.size()) {
            m_zones.resize(index + 1);
        }
        m_zones[index] = data;
        m_uniformsDirty = true;
        m_zoneDataDirty = true;
    }
}

void ZoneShaderNodeRhi::setZoneCount(int count)
{
    if (count >= 0 && count <= MaxZones) {
        m_zones.resize(count);
        m_uniformsDirty = true;
        m_zoneDataDirty = true;
    }
}

void ZoneShaderNodeRhi::setHighlightedZones(const QVector<int>& indices)
{
    m_highlightedIndices = indices;
    for (int i = 0; i < m_zones.size(); ++i) {
        m_zones[i].isHighlighted = indices.contains(i);
    }
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}

void ZoneShaderNodeRhi::clearHighlights()
{
    m_highlightedIndices.clear();
    for (auto& zone : m_zones) {
        zone.isHighlighted = false;
    }
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}

void ZoneShaderNodeRhi::setTime(float time)
{
    m_time = time;
    m_uniformsDirty = true;
    m_timeDirty = true;
}
void ZoneShaderNodeRhi::setTimeDelta(float delta)
{
    m_timeDelta = delta;
    m_uniformsDirty = true;
    m_timeDirty = true;
}
void ZoneShaderNodeRhi::setFrame(int frame)
{
    m_frame = frame;
    m_uniformsDirty = true;
    m_timeDirty = true;
}
void ZoneShaderNodeRhi::setResolution(float width, float height)
{
    if (m_width != width || m_height != height) {
        m_width = width;
        m_height = height;
        m_uniformsDirty = true;
        m_zoneDataDirty = true;
    }
}
void ZoneShaderNodeRhi::setMousePosition(const QPointF& pos)
{
    m_mousePosition = pos;
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}
void ZoneShaderNodeRhi::setCustomParams1(const QVector4D& params)
{
    m_customParams1 = params;
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}
void ZoneShaderNodeRhi::setCustomParams2(const QVector4D& params)
{
    m_customParams2 = params;
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}
void ZoneShaderNodeRhi::setCustomParams3(const QVector4D& params)
{
    m_customParams3 = params;
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}
void ZoneShaderNodeRhi::setCustomParams4(const QVector4D& params)
{
    m_customParams4 = params;
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}
void ZoneShaderNodeRhi::setCustomColor1(const QColor& color)
{
    m_customColor1 = color;
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}
void ZoneShaderNodeRhi::setCustomColor2(const QColor& color)
{
    m_customColor2 = color;
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}
void ZoneShaderNodeRhi::setCustomColor3(const QColor& color)
{
    m_customColor3 = color;
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}
void ZoneShaderNodeRhi::setCustomColor4(const QColor& color)
{
    m_customColor4 = color;
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}
void ZoneShaderNodeRhi::setCustomColor5(const QColor& color)
{
    m_customColor5 = color;
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}
void ZoneShaderNodeRhi::setCustomColor6(const QColor& color)
{
    m_customColor6 = color;
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}
void ZoneShaderNodeRhi::setCustomColor7(const QColor& color)
{
    m_customColor7 = color;
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}
void ZoneShaderNodeRhi::setCustomColor8(const QColor& color)
{
    m_customColor8 = color;
    m_uniformsDirty = true;
    m_zoneDataDirty = true;
}

void ZoneShaderNodeRhi::setLabelsTexture(const QImage& image)
{
    m_labelsImage = image;
    m_labelsTextureDirty = true;
    m_uniformsDirty = true;
}

void ZoneShaderNodeRhi::setBufferShaderPath(const QString& path)
{
    setBufferShaderPaths(path.isEmpty() ? QStringList() : QStringList{path});
}

void ZoneShaderNodeRhi::setBufferShaderPaths(const QStringList& paths)
{
    QStringList trimmed;
    for (int i = 0; i < qMin(paths.size(), kMaxBufferPasses); ++i) {
        if (!paths.at(i).isEmpty()) {
            trimmed.append(paths.at(i));
        }
    }
    if (m_bufferPaths == trimmed) {
        return;
    }
    m_bufferPaths = trimmed;
    m_bufferPath = trimmed.isEmpty() ? QString() : trimmed.constFirst();

    qCDebug(lcOverlay) << "ZoneShaderNodeRhi setBufferShaderPaths count=" << m_bufferPaths.size()
                       << "multiBufferMode=" << (m_bufferPaths.size() > 1);

    m_bufferShaderDirty = true;
    m_bufferShaderReady = false;
    m_bufferFragmentShaderSource.clear();
    m_bufferMtime = 0;
    m_multiBufferShadersReady = false;
    m_multiBufferShaderDirty = true;
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        m_multiBufferFragmentShaderSources[i].clear();
        m_multiBufferFragmentShaders[i] = QShader();
        m_multiBufferMtimes[i] = 0;
    }
    if (m_bufferPaths.size() == 1) {
        const QString& path = m_bufferPaths.constFirst();
        if (QFileInfo::exists(path)) {
            QString err;
            m_bufferFragmentShaderSource = loadAndExpandShader(path, &err);
            if (!m_bufferFragmentShaderSource.isEmpty()) {
                m_bufferMtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();
            }
        }
        m_bufferShaderDirty = true;
    }

    m_bufferPipeline.reset();
    m_bufferSrb.reset();
    m_bufferSrbB.reset();
    m_bufferTexture.reset();
    m_bufferTextureB.reset();
    m_bufferRenderTarget.reset();
    m_bufferRenderTargetB.reset();
    m_bufferRenderPassDescriptor.reset();
    m_bufferRenderPassDescriptorB.reset();
    m_bufferFeedbackCleared = false;
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        m_multiBufferPipelines[i].reset();
        m_multiBufferSrbs[i].reset();
        m_multiBufferTextures[i].reset();
        m_multiBufferRenderTargets[i].reset();
        m_multiBufferRenderPassDescriptors[i].reset();
    }
    m_pipeline.reset();
    m_srb.reset();
    m_srbB.reset();
}

void ZoneShaderNodeRhi::setBufferFeedback(bool enable)
{
    if (m_bufferFeedback == enable) {
        return;
    }
    m_bufferFeedback = enable;
    m_bufferPipeline.reset();
    m_bufferSrb.reset();
    m_bufferSrbB.reset();
    m_srb.reset();
    m_srbB.reset();
    m_bufferTextureB.reset();
    m_bufferRenderTargetB.reset();
    m_bufferRenderPassDescriptorB.reset();
    m_bufferFeedbackCleared = false;
}

void ZoneShaderNodeRhi::setBufferScale(qreal scale)
{
    const qreal clamped = qBound(0.125, scale, 1.0);
    if (qFuzzyCompare(m_bufferScale, clamped)) {
        return;
    }
    m_bufferScale = clamped;
    m_bufferTexture.reset();
    m_bufferTextureB.reset();
    m_bufferRenderTarget.reset();
    m_bufferRenderTargetB.reset();
    m_bufferRenderPassDescriptor.reset();
    m_bufferRenderPassDescriptorB.reset();
    m_bufferPipeline.reset();
    m_bufferSrb.reset();
    m_bufferSrbB.reset();
    m_srb.reset();
    m_srbB.reset();
    m_bufferFeedbackCleared = false;
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        m_multiBufferTextures[i].reset();
        m_multiBufferRenderTargets[i].reset();
        m_multiBufferRenderPassDescriptors[i].reset();
        m_multiBufferPipelines[i].reset();
        m_multiBufferSrbs[i].reset();
    }
}

void ZoneShaderNodeRhi::setBufferWrap(const QString& wrap)
{
    const QString use = (wrap == QLatin1String("repeat")) ? QStringLiteral("repeat") : QStringLiteral("clamp");
    if (m_bufferWrap == use) {
        return;
    }
    m_bufferWrap = use;
    m_bufferSampler.reset();
    m_bufferSrb.reset();
    m_bufferSrbB.reset();
    m_srb.reset();
    m_srbB.reset();
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        m_multiBufferSrbs[i].reset();
    }
}

bool ZoneShaderNodeRhi::loadVertexShader(const QString& path)
{
    QString err;
    m_vertexShaderSource = loadAndExpandShader(path, &err);
    if (m_vertexShaderSource.isEmpty()) {
        m_shaderError = err.startsWith(QStringLiteral("Failed to open:"))
                ? QString(QStringLiteral("Failed to open vertex shader: ") + path)
                : QString(QStringLiteral("Vertex shader include: ") + err);
        return false;
    }
    m_vertexPath = path;
    m_vertexMtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    m_shaderDirty = true;
    return true;
}

bool ZoneShaderNodeRhi::loadFragmentShader(const QString& path)
{
    QString err;
    m_fragmentShaderSource = loadAndExpandShader(path, &err);
    if (m_fragmentShaderSource.isEmpty()) {
        m_shaderError = err.startsWith(QStringLiteral("Failed to open:"))
                ? QString(QStringLiteral("Failed to open fragment shader: ") + path)
                : QString(QStringLiteral("Fragment shader include: ") + err);
        return false;
    }
    m_fragmentPath = path;
    m_fragmentMtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    m_shaderDirty = true;
    return true;
}

void ZoneShaderNodeRhi::setVertexShaderSource(const QString& source)
{
    if (m_vertexShaderSource != source) {
        m_vertexShaderSource = source;
        if (source.isEmpty()) {
            m_vertexPath.clear();
            m_vertexMtime = 0;
        }
        m_shaderDirty = true;
    }
}

void ZoneShaderNodeRhi::setFragmentShaderSource(const QString& source)
{
    if (m_fragmentShaderSource != source) {
        m_fragmentShaderSource = source;
        if (source.isEmpty()) {
            m_fragmentPath.clear();
            m_fragmentMtime = 0;
        }
        m_shaderDirty = true;
    }
}

bool ZoneShaderNodeRhi::isShaderReady() const
{
    return m_shaderReady;
}

QString ZoneShaderNodeRhi::shaderError() const
{
    return m_shaderError;
}

void ZoneShaderNodeRhi::invalidateShader()
{
    m_shaderDirty = true;
}

void ZoneShaderNodeRhi::invalidateUniforms()
{
    m_uniformsDirty = true;
    m_timeDirty = true;
    m_zoneDataDirty = true;
}

bool ZoneShaderNodeRhi::ensureBufferTarget()
{
    if (m_width <= 0 || m_height <= 0) {
        return true;
    }
    QRhi* rhi = m_item ? m_item->window() ? m_item->window()->rhi() : nullptr : nullptr;
    if (!rhi) {
        return false;
    }
    const bool multiBufferMode = m_bufferPaths.size() > 1;
    const int bufferW = qMax(1, static_cast<int>(m_width * m_bufferScale));
    const int bufferH = qMax(1, static_cast<int>(m_height * m_bufferScale));
    const QSize bufferSize(bufferW, bufferH);
    auto createTextureAndRT = [rhi, bufferSize](std::unique_ptr<QRhiTexture>& tex,
                                                std::unique_ptr<QRhiTextureRenderTarget>& rt,
                                                std::unique_ptr<QRhiRenderPassDescriptor>& rpd) -> bool {
        tex.reset(rhi->newTexture(QRhiTexture::RGBA8, bufferSize, 1, QRhiTexture::RenderTarget));
        if (!tex->create()) {
            return false;
        }
        QRhiTextureRenderTargetDescription desc(QRhiColorAttachment(tex.get()));
        rt.reset(rhi->newTextureRenderTarget(desc));
        rpd.reset(rt->newCompatibleRenderPassDescriptor());
        rt->setRenderPassDescriptor(rpd.get());
        return rt->create();
    };

    if (multiBufferMode) {
        const int n = qMin(m_bufferPaths.size(), kMaxBufferPasses);
        bool needCreate = false;
        for (int i = 0; i < n; ++i) {
            if (!m_multiBufferTextures[i] || m_multiBufferTextures[i]->pixelSize() != bufferSize) {
                needCreate = true;
                break;
            }
        }
        if (needCreate) {
            for (int i = 0; i < n; ++i) {
                if (!createTextureAndRT(m_multiBufferTextures[i], m_multiBufferRenderTargets[i],
                                        m_multiBufferRenderPassDescriptors[i])) {
                    qCWarning(lcOverlay) << "Failed to create multi-buffer texture" << i;
                    return false;
                }
            }
            for (int i = 0; i < kMaxBufferPasses; ++i) {
                m_multiBufferPipelines[i].reset();
                m_multiBufferSrbs[i].reset();
            }
            m_pipeline.reset();
            m_srb.reset();
            m_srbB.reset();
        }
        if (!m_bufferSampler) {
            const QRhiSampler::AddressMode addr = (m_bufferWrap == QLatin1String("repeat"))
                ? QRhiSampler::Repeat : QRhiSampler::ClampToEdge;
            m_bufferSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, addr, addr));
            if (!m_bufferSampler->create()) {
                qCWarning(lcOverlay) << "Failed to create buffer sampler";
                return false;
            }
        }
        return true;
    }

    if (!m_bufferTexture) {
        if (!createTextureAndRT(m_bufferTexture, m_bufferRenderTarget, m_bufferRenderPassDescriptor)) {
            qCWarning(lcOverlay) << "Failed to create buffer texture";
            return false;
        }
        if (!m_bufferSampler) {
            const QRhiSampler::AddressMode addr = (m_bufferWrap == QLatin1String("repeat"))
                ? QRhiSampler::Repeat : QRhiSampler::ClampToEdge;
            m_bufferSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, addr, addr));
            if (!m_bufferSampler->create()) {
                qCWarning(lcOverlay) << "Failed to create buffer sampler";
                return false;
            }
        }
        if (m_bufferFeedback
            && !createTextureAndRT(m_bufferTextureB, m_bufferRenderTargetB, m_bufferRenderPassDescriptorB)) {
            qCWarning(lcOverlay) << "Failed to create buffer texture B (ping-pong)";
            return false;
        }
        m_srb.reset();
        m_srbB.reset();
        return true;
    }
    if (m_bufferTexture->pixelSize() != bufferSize) {
        if (!createTextureAndRT(m_bufferTexture, m_bufferRenderTarget, m_bufferRenderPassDescriptor)) {
            qCWarning(lcOverlay) << "Failed to resize buffer texture";
            return false;
        }
        if (m_bufferFeedback
            && !createTextureAndRT(m_bufferTextureB, m_bufferRenderTargetB, m_bufferRenderPassDescriptorB)) {
            qCWarning(lcOverlay) << "Failed to resize buffer texture B";
            return false;
        }
        m_bufferPipeline.reset();
        m_bufferSrb.reset();
        m_bufferSrbB.reset();
        m_srb.reset();
        m_srbB.reset();
    } else if (m_bufferFeedback && !m_bufferTextureB) {
        if (!createTextureAndRT(m_bufferTextureB, m_bufferRenderTargetB, m_bufferRenderPassDescriptorB)) {
            qCWarning(lcOverlay) << "Failed to create buffer texture B (ping-pong)";
            return false;
        }
        m_bufferPipeline.reset();
        m_bufferSrb.reset();
        m_srb.reset();
        m_srbB.reset();
    }
    if (m_bufferTexture && !m_bufferSampler) {
        const QRhiSampler::AddressMode addr = (m_bufferWrap == QLatin1String("repeat"))
            ? QRhiSampler::Repeat : QRhiSampler::ClampToEdge;
        m_bufferSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, addr, addr));
        if (!m_bufferSampler->create()) {
            qCWarning(lcOverlay) << "Failed to create buffer sampler";
            return false;
        }
        m_bufferSrb.reset();
        m_bufferSrbB.reset();
        m_srb.reset();
        m_srbB.reset();
    }
    return true;
}

bool ZoneShaderNodeRhi::ensureBufferPipeline()
{
    const bool multiBufferMode = m_bufferPaths.size() > 1;
    if (multiBufferMode) {
        if (!m_multiBufferShadersReady || !m_multiBufferTextures[0] || !m_multiBufferRenderTargets[0]) {
            return false;
        }
        QRhi* rhi = m_item ? m_item->window() ? m_item->window()->rhi() : nullptr : nullptr;
        if (!rhi || !m_bufferSampler) {
            return false;
        }
        const int n = qMin(m_bufferPaths.size(), kMaxBufferPasses);
        for (int i = 0; i < n; ++i) {
            QRhiRenderPassDescriptor* rpDesc = m_multiBufferRenderPassDescriptors[i]
                ? m_multiBufferRenderPassDescriptors[i].get()
                : m_multiBufferRenderTargets[i]->renderPassDescriptor();
            if (!rpDesc) {
                return false;
            }
            if (!m_multiBufferSrbs[i]) {
                std::unique_ptr<QRhiShaderResourceBindings> srb(rhi->newShaderResourceBindings());
                QVector<QRhiShaderResourceBinding> bindings;
                bindings.append(QRhiShaderResourceBinding::uniformBuffer(
                    0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo.get()));
                for (int j = 0; j < i; ++j) {
                    if (m_multiBufferTextures[j] && m_bufferSampler) {
                        bindings.append(QRhiShaderResourceBinding::sampledTexture(
                            2 + j, QRhiShaderResourceBinding::FragmentStage,
                            m_multiBufferTextures[j].get(), m_bufferSampler.get()));
                    }
                }
                srb->setBindings(bindings.begin(), bindings.end());
                if (!srb->create()) {
                    m_shaderError = QStringLiteral("Failed to create multi-buffer pass SRB ");
                    return false;
                }
                m_multiBufferSrbs[i] = std::move(srb);
            }
            if (!m_multiBufferPipelines[i]) {
                QRhiRenderPassDescriptor* rpDescI = m_multiBufferRenderPassDescriptors[i]
                    ? m_multiBufferRenderPassDescriptors[i].get()
                    : m_multiBufferRenderTargets[i]->renderPassDescriptor();
                m_multiBufferPipelines[i] = createFullscreenQuadPipeline(rhi, rpDescI,
                    m_vertexShader, m_multiBufferFragmentShaders[i], m_multiBufferSrbs[i].get());
                if (!m_multiBufferPipelines[i]) {
                    m_shaderError = QStringLiteral("Failed to create multi-buffer pipeline ");
                    return false;
                }
            }
        }
        return true;
    }

    if (!m_bufferShaderReady || !m_bufferTexture || !m_bufferRenderTarget) {
        return false;
    }
    if (m_bufferFeedback && (!m_bufferTextureB || !m_bufferRenderTargetB)) {
        return false;
    }
    QRhi* rhi = m_item ? m_item->window() ? m_item->window()->rhi() : nullptr : nullptr;
    if (!rhi) {
        return false;
    }
    QRhiRenderPassDescriptor* rpDesc = m_bufferRenderPassDescriptor ? m_bufferRenderPassDescriptor.get() : m_bufferRenderTarget->renderPassDescriptor();
    if (!rpDesc) {
        return false;
    }
    QVector<quint32> format = rpDesc->serializedFormat();
    if (m_bufferPipeline && m_bufferRenderPassFormat != format) {
        m_bufferPipeline.reset();
        m_bufferSrb.reset();
        m_bufferSrbB.reset();
    }
    m_bufferRenderPassFormat = format;

    auto createBufferSrb = [rhi, this](QRhiTexture* channel0Texture) -> std::unique_ptr<QRhiShaderResourceBindings> {
        std::unique_ptr<QRhiShaderResourceBindings> srb(rhi->newShaderResourceBindings());
        QVector<QRhiShaderResourceBinding> bindings;
        bindings.append(QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo.get()));
        if (channel0Texture && m_bufferSampler) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(
                2, QRhiShaderResourceBinding::FragmentStage, channel0Texture, m_bufferSampler.get()));
        }
        srb->setBindings(bindings.begin(), bindings.end());
        return srb->create() ? std::move(srb) : nullptr;
    };

    if (!m_bufferSrb) {
        QRhiTexture* prevFrame = m_bufferFeedback ? m_bufferTextureB.get() : nullptr;
        m_bufferSrb = createBufferSrb(prevFrame);
        if (!m_bufferSrb) {
            m_shaderError = QStringLiteral("Failed to create buffer pass SRB");
            return false;
        }
    }
    if (m_bufferFeedback && !m_bufferSrbB) {
        m_bufferSrbB = createBufferSrb(m_bufferTexture.get());
        if (!m_bufferSrbB) {
            m_shaderError = QStringLiteral("Failed to create buffer pass SRB B");
            return false;
        }
    }

    if (!m_bufferPipeline) {
        m_bufferPipeline = createFullscreenQuadPipeline(rhi, rpDesc, m_vertexShader, m_bufferFragmentShader, m_bufferSrb.get());
        if (!m_bufferPipeline) {
            m_shaderError = QStringLiteral("Failed to create buffer pipeline");
            return false;
        }
    }
    return true;
}

bool ZoneShaderNodeRhi::ensurePipeline()
{
    QRhi* rhi = m_item ? m_item->window() ? m_item->window()->rhi() : nullptr : nullptr;
    QRhiRenderTarget* rt = renderTarget();
    if (!rhi || !rt || !m_shaderReady) {
        return false;
    }

    QRhiRenderPassDescriptor* rpDesc = rt->renderPassDescriptor();
    if (!rpDesc) {
        return false;
    }

    QVector<quint32> format = rpDesc->serializedFormat();
    if (m_pipeline && m_renderPassFormat != format) {
        m_pipeline.reset();
        m_srb.reset();
        m_srbB.reset();
    }
    m_renderPassFormat = format;

    const bool multiBufferMode = m_bufferPaths.size() > 1;
    auto createImageSrbSingle = [rhi, this](QRhiTexture* channel0Texture) -> std::unique_ptr<QRhiShaderResourceBindings> {
        QRhiSampler* channel0Sampler = (channel0Texture && m_bufferSampler) ? m_bufferSampler.get() : nullptr;
        if (!channel0Texture && !m_bufferPath.isEmpty()) {
            channel0Texture = m_dummyChannelTexture.get();
            channel0Sampler = m_dummyChannelSampler.get();
        }
        std::unique_ptr<QRhiShaderResourceBindings> srb(rhi->newShaderResourceBindings());
        QVector<QRhiShaderResourceBinding> bindings;
        bindings.append(QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo.get()));
        if (m_labelsTexture && m_labelsSampler) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage, m_labelsTexture.get(), m_labelsSampler.get()));
        }
        if (channel0Texture && channel0Sampler) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(
                2, QRhiShaderResourceBinding::FragmentStage, channel0Texture, channel0Sampler));
        }
        srb->setBindings(bindings.begin(), bindings.end());
        return srb->create() ? std::move(srb) : nullptr;
    };
    auto createImageSrbMulti = [rhi, this]() -> std::unique_ptr<QRhiShaderResourceBindings> {
        std::unique_ptr<QRhiShaderResourceBindings> srb(rhi->newShaderResourceBindings());
        QVector<QRhiShaderResourceBinding> bindings;
        bindings.append(QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo.get()));
        if (m_labelsTexture && m_labelsSampler) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage, m_labelsTexture.get(), m_labelsSampler.get()));
        }
        const int n = qMin(m_bufferPaths.size(), kMaxBufferPasses);
        QRhiSampler* bufferSam = m_bufferSampler.get();
        QRhiTexture* dummyTex = m_dummyChannelTexture.get();
        QRhiSampler* dummySam = m_dummyChannelSampler.get();
        for (int i = 0; i < n; ++i) {
            QRhiTexture* tex = m_multiBufferTextures[i] ? m_multiBufferTextures[i].get() : dummyTex;
            QRhiSampler* sam = (tex == dummyTex) ? dummySam : bufferSam;
            if (tex && sam) {
                bindings.append(QRhiShaderResourceBinding::sampledTexture(
                    2 + i, QRhiShaderResourceBinding::FragmentStage, tex, sam));
            }
        }
        srb->setBindings(bindings.begin(), bindings.end());
        return srb->create() ? std::move(srb) : nullptr;
    };

    if (!m_srb) {
        if (multiBufferMode) {
            if (!m_dummyChannelTexture) {
                m_dummyChannelTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
                if (m_dummyChannelTexture->create()) {
                    m_dummyChannelTextureNeedsUpload = true;
                } else {
                    m_dummyChannelTexture.reset();
                }
            }
            if (!m_dummyChannelSampler && m_dummyChannelTexture) {
                m_dummyChannelSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                                             QRhiSampler::None, QRhiSampler::ClampToEdge,
                                                             QRhiSampler::ClampToEdge));
                if (!m_dummyChannelSampler->create()) {
                    m_dummyChannelSampler.reset();
                }
            }
            m_srb = createImageSrbMulti();
        } else {
            if (!m_bufferPath.isEmpty() && !m_bufferTexture && !m_dummyChannelTexture) {
                m_dummyChannelTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
                if (m_dummyChannelTexture->create()) {
                    m_dummyChannelTextureNeedsUpload = true;
                } else {
                    m_dummyChannelTexture.reset();
                }
                if (!m_dummyChannelSampler) {
                    m_dummyChannelSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                                                QRhiSampler::None, QRhiSampler::ClampToEdge,
                                                                QRhiSampler::ClampToEdge));
                    if (!m_dummyChannelSampler->create()) {
                        m_dummyChannelSampler.reset();
                    }
                }
            }
            m_srb = createImageSrbSingle(m_bufferTexture.get());
        }
        if (!m_srb) {
            m_shaderError = QStringLiteral("Failed to create shader resource bindings");
            return false;
        }
    }
    if (!multiBufferMode && m_bufferFeedback && m_bufferTextureB && !m_srbB) {
        m_srbB = createImageSrbSingle(m_bufferTextureB.get());
        if (!m_srbB) {
            m_shaderError = QStringLiteral("Failed to create image pass SRB B");
            return false;
        }
    }

    if (!m_pipeline) {
        m_pipeline = createFullscreenQuadPipeline(rhi, rpDesc, m_vertexShader, m_fragmentShader, m_srb.get());
        if (!m_pipeline) {
            m_shaderError = QStringLiteral("Failed to create graphics pipeline");
            return false;
        }
    }
    return true;
}

void ZoneShaderNodeRhi::syncUniformsFromData()
{
    m_uniforms.iTime = m_time;
    m_uniforms.iTimeDelta = m_timeDelta;
    m_uniforms.iFrame = m_frame;
    m_uniforms.iResolution[0] = m_width;
    m_uniforms.iResolution[1] = m_height;
    m_uniforms.iMouse[0] = static_cast<float>(m_mousePosition.x());
    m_uniforms.iMouse[1] = static_cast<float>(m_mousePosition.y());
    m_uniforms.iMouse[2] = m_width > 0 ? static_cast<float>(m_mousePosition.x() / m_width) : 0.0f;
    m_uniforms.iMouse[3] = m_height > 0 ? static_cast<float>(m_mousePosition.y() / m_height) : 0.0f;
    m_uniforms.zoneCount = m_zones.size();
    int highlightedCount = 0;
    for (const auto& zone : m_zones) {
        if (zone.isHighlighted) ++highlightedCount;
    }
    m_uniforms.highlightedCount = highlightedCount;

    m_uniforms.customParams[RhiConstants::UniformVecIndex1][RhiConstants::ComponentX] = m_customParams1.x();
    m_uniforms.customParams[RhiConstants::UniformVecIndex1][RhiConstants::ComponentY] = m_customParams1.y();
    m_uniforms.customParams[RhiConstants::UniformVecIndex1][RhiConstants::ComponentZ] = m_customParams1.z();
    m_uniforms.customParams[RhiConstants::UniformVecIndex1][RhiConstants::ComponentW] = m_customParams1.w();
    m_uniforms.customParams[RhiConstants::UniformVecIndex2][RhiConstants::ComponentX] = m_customParams2.x();
    m_uniforms.customParams[RhiConstants::UniformVecIndex2][RhiConstants::ComponentY] = m_customParams2.y();
    m_uniforms.customParams[RhiConstants::UniformVecIndex2][RhiConstants::ComponentZ] = m_customParams2.z();
    m_uniforms.customParams[RhiConstants::UniformVecIndex2][RhiConstants::ComponentW] = m_customParams2.w();
    m_uniforms.customParams[RhiConstants::UniformVecIndex3][RhiConstants::ComponentX] = m_customParams3.x();
    m_uniforms.customParams[RhiConstants::UniformVecIndex3][RhiConstants::ComponentY] = m_customParams3.y();
    m_uniforms.customParams[RhiConstants::UniformVecIndex3][RhiConstants::ComponentZ] = m_customParams3.z();
    m_uniforms.customParams[RhiConstants::UniformVecIndex3][RhiConstants::ComponentW] = m_customParams3.w();
    m_uniforms.customParams[RhiConstants::UniformVecIndex4][RhiConstants::ComponentX] = m_customParams4.x();
    m_uniforms.customParams[RhiConstants::UniformVecIndex4][RhiConstants::ComponentY] = m_customParams4.y();
    m_uniforms.customParams[RhiConstants::UniformVecIndex4][RhiConstants::ComponentZ] = m_customParams4.z();
    m_uniforms.customParams[RhiConstants::UniformVecIndex4][RhiConstants::ComponentW] = m_customParams4.w();

    auto setColor = [this](int idx, const QColor& c) {
        m_uniforms.customColors[idx][0] = static_cast<float>(c.redF());
        m_uniforms.customColors[idx][1] = static_cast<float>(c.greenF());
        m_uniforms.customColors[idx][2] = static_cast<float>(c.blueF());
        m_uniforms.customColors[idx][3] = static_cast<float>(c.alphaF());
    };
    setColor(0, m_customColor1);
    setColor(1, m_customColor2);
    setColor(2, m_customColor3);
    setColor(3, m_customColor4);
    setColor(4, m_customColor5);
    setColor(5, m_customColor6);
    setColor(6, m_customColor7);
    setColor(7, m_customColor8);

    for (int i = 0; i < MaxZones; ++i) {
        if (i < m_zones.size()) {
            const ZoneData& zone = m_zones[i];
            m_uniforms.zoneRects[i][0] = static_cast<float>(zone.rect.x());
            m_uniforms.zoneRects[i][1] = static_cast<float>(zone.rect.y());
            m_uniforms.zoneRects[i][2] = static_cast<float>(zone.rect.width());
            m_uniforms.zoneRects[i][3] = static_cast<float>(zone.rect.height());
            m_uniforms.zoneFillColors[i][0] = static_cast<float>(zone.fillColor.redF());
            m_uniforms.zoneFillColors[i][1] = static_cast<float>(zone.fillColor.greenF());
            m_uniforms.zoneFillColors[i][2] = static_cast<float>(zone.fillColor.blueF());
            m_uniforms.zoneFillColors[i][3] = static_cast<float>(zone.fillColor.alphaF());
            m_uniforms.zoneBorderColors[i][0] = static_cast<float>(zone.borderColor.redF());
            m_uniforms.zoneBorderColors[i][1] = static_cast<float>(zone.borderColor.greenF());
            m_uniforms.zoneBorderColors[i][2] = static_cast<float>(zone.borderColor.blueF());
            m_uniforms.zoneBorderColors[i][3] = static_cast<float>(zone.borderColor.alphaF());
            m_uniforms.zoneParams[i][0] = zone.borderRadius;
            m_uniforms.zoneParams[i][1] = zone.borderWidth;
            m_uniforms.zoneParams[i][2] = zone.isHighlighted ? 1.0f : 0.0f;
            m_uniforms.zoneParams[i][3] = static_cast<float>(zone.zoneNumber);
        } else {
            std::memset(m_uniforms.zoneRects[i], 0, sizeof(m_uniforms.zoneRects[i]));
            std::memset(m_uniforms.zoneFillColors[i], 0, sizeof(m_uniforms.zoneFillColors[i]));
            std::memset(m_uniforms.zoneBorderColors[i], 0, sizeof(m_uniforms.zoneBorderColors[i]));
            std::memset(m_uniforms.zoneParams[i], 0, sizeof(m_uniforms.zoneParams[i]));
        }
    }

    // iChannelResolution (std140: vec2[4], each element 16 bytes)
    const bool multiBufferMode = m_bufferPaths.size() > 1;
    const int numChannels = multiBufferMode ? qMin(m_bufferPaths.size(), 4) : (m_bufferShaderReady && m_bufferTexture ? 1 : 0);
    for (int i = 0; i < 4; ++i) {
        if (i < numChannels) {
            if (multiBufferMode && m_multiBufferTextures[i]) {
                QSize ps = m_multiBufferTextures[i]->pixelSize();
                m_uniforms.iChannelResolution[i][0] = static_cast<float>(ps.width());
                m_uniforms.iChannelResolution[i][1] = static_cast<float>(ps.height());
            } else if (!multiBufferMode && i == 0 && m_bufferTexture && m_width > 0 && m_height > 0) {
                const int bufferW = qMax(1, static_cast<int>(m_width * m_bufferScale));
                const int bufferH = qMax(1, static_cast<int>(m_height * m_bufferScale));
                m_uniforms.iChannelResolution[0][0] = static_cast<float>(bufferW);
                m_uniforms.iChannelResolution[0][1] = static_cast<float>(bufferH);
            } else {
                m_uniforms.iChannelResolution[i][0] = 0.0f;
                m_uniforms.iChannelResolution[i][1] = 0.0f;
            }
        } else {
            m_uniforms.iChannelResolution[i][0] = 0.0f;
            m_uniforms.iChannelResolution[i][1] = 0.0f;
        }
        m_uniforms.iChannelResolution[i][2] = 0.0f;
        m_uniforms.iChannelResolution[i][3] = 0.0f;
    }
}

void ZoneShaderNodeRhi::releaseRhiResources()
{
    m_bufferPipeline.reset();
    m_bufferSrb.reset();
    m_bufferSrbB.reset();
    m_bufferTexture.reset();
    m_bufferTextureB.reset();
    m_bufferRenderTarget.reset();
    m_bufferRenderTargetB.reset();
    m_bufferRenderPassDescriptor.reset();
    m_bufferRenderPassDescriptorB.reset();
    m_bufferSampler.reset();
    m_bufferRenderPassFormat.clear();
    m_bufferFeedbackCleared = false;
    m_srbB.reset();
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        m_multiBufferPipelines[i].reset();
        m_multiBufferSrbs[i].reset();
        m_multiBufferTextures[i].reset();
        m_multiBufferRenderTargets[i].reset();
        m_multiBufferRenderPassDescriptors[i].reset();
    }
    m_dummyChannelTexture.reset();
    m_dummyChannelSampler.reset();
    m_dummyChannelTextureNeedsUpload = false;
    m_pipeline.reset();
    m_srb.reset();
    m_labelsTexture.reset();
    m_labelsSampler.reset();
    m_ubo.reset();
    m_vbo.reset();
    m_vertexShader = QShader();
    m_fragmentShader = QShader();
    m_bufferFragmentShader = QShader();
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        m_multiBufferFragmentShaders[i] = QShader();
    }
    m_renderPassFormat.clear();
    m_initialized = false;
    m_vboUploaded = false;
    m_didFullUploadOnce = false;
    m_shaderReady = false;
    m_shaderDirty = true;
    m_uniformsDirty = true;
    m_timeDirty = true;
    m_zoneDataDirty = true;
    m_labelsTextureDirty = true;
    // Next prepare() will re-create all RHI resources and do a full UBO upload
}

WarmShaderBakeResult warmShaderBakeCacheForPaths(const QString& vertexPath, const QString& fragmentPath)
{
    WarmShaderBakeResult result;
    if (vertexPath.isEmpty() || fragmentPath.isEmpty()) {
        result.errorMessage = QStringLiteral("Vertex or fragment path is empty");
        return result;
    }
    QString err;
    const QString vertSource = loadAndExpandShader(vertexPath, &err);
    if (vertSource.isEmpty()) {
        result.errorMessage = err.isEmpty() ? QStringLiteral("Failed to load vertex shader") : err;
        return result;
    }
    const QString fragSource = loadAndExpandShader(fragmentPath, &err);
    if (fragSource.isEmpty()) {
        result.errorMessage = err.isEmpty() ? QStringLiteral("Failed to load fragment shader") : err;
        return result;
    }
    const qint64 vertMtime = QFileInfo(vertexPath).lastModified().toMSecsSinceEpoch();
    const qint64 fragMtime = QFileInfo(fragmentPath).lastModified().toMSecsSinceEpoch();

    const QList<QShaderBaker::GeneratedShader>& targets = bakeTargets();
    QShaderBaker vertexBaker;
    vertexBaker.setGeneratedShaderVariants({ QShader::StandardShader });
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
    fragmentBaker.setGeneratedShaderVariants({ QShader::StandardShader });
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
    s_shaderCache[key] = ShaderCacheEntry{ vertexShader, fragmentShader };
    result.success = true;
    return result;
}

} // namespace PlasmaZones
