// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationshadernoderhi.h"
#include "zoneshadernoderhi/internal.h"

#include <QFile>
#include <QFileInfo>
#include <QQuickWindow>
#include <QSGTexture>
#include <cstring>
#include <mutex>

#include "../../core/logging.h"
#include "../../core/shaderincluderesolver.h"

#include <rhi/qshaderbaker.h>

namespace PlasmaZones {

namespace {

// Default passthrough vertex shader (GLSL 450).
// Used when the animation shader has no custom vertex shader.
const char* defaultVertexShaderSource = R"(#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

layout(std140, binding = 0) uniform AnimationUniforms {
    mat4 qt_Matrix;
    float qt_Opacity;
    float pz_progress;
    float pz_duration;
    float pz_style_param;
    vec2 iResolution;
    int pz_mode;
    int pz_direction;
    vec4 customParams[8];
};

void main() {
    vTexCoord = texCoord;
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
}
)";

// Baked default vertex shader (thread-safe, cached on first use)
QShader s_defaultVertexShader;
std::once_flag s_defaultVertexBakeFlag;
bool s_defaultVertexBakeResult = false;

bool bakeDefaultVertexShader()
{
    std::call_once(s_defaultVertexBakeFlag, [] {
        QShaderBaker baker;
        baker.setGeneratedShaderVariants({QShader::StandardShader});
        baker.setGeneratedShaders(detail::bakeTargets());
        baker.setSourceString(QByteArray(defaultVertexShaderSource), QShader::VertexStage);
        s_defaultVertexShader = baker.bake();
        s_defaultVertexBakeResult = s_defaultVertexShader.isValid();
        if (!s_defaultVertexBakeResult) {
            qCWarning(lcOverlay) << "AnimationShader default vertex bake failed:" << baker.errorMessage();
        }
    });
    return s_defaultVertexBakeResult;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════════════════════

AnimationShaderNodeRhi::AnimationShaderNodeRhi(QQuickItem* item)
    : m_item(item)
{
    Q_ASSERT(item != nullptr);
    std::memset(&m_uniforms, 0, sizeof(m_uniforms));
    QMatrix4x4 identity;
    std::memcpy(m_uniforms.qt_Matrix, identity.constData(), 16 * sizeof(float));
    m_uniforms.qt_Opacity = 1.0f;
    for (auto& p : m_customParams)
        p = QVector4D(-1.0f, -1.0f, -1.0f, -1.0f);
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 4; ++j)
            m_uniforms.customParams[i][j] = -1.0f;
}

AnimationShaderNodeRhi::~AnimationShaderNodeRhi()
{
    releaseRhiResources();
}

void AnimationShaderNodeRhi::invalidateItem()
{
    m_itemValid.store(false, std::memory_order_release);
}

QRhi* AnimationShaderNodeRhi::safeRhi() const
{
    return (m_itemValid.load(std::memory_order_acquire) && m_item && m_item->window()) ? m_item->window()->rhi()
                                                                                       : nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════════
// QSGRenderNode overrides
// ═══════════════════════════════════════════════════════════════════════════════

QSGRenderNode::StateFlags AnimationShaderNodeRhi::changedStates() const
{
    return ViewportState | ScissorState;
}

QSGRenderNode::RenderingFlags AnimationShaderNodeRhi::flags() const
{
    return BoundedRectRendering | DepthAwareRendering | NoExternalRendering;
}

QRectF AnimationShaderNodeRhi::rect() const
{
    if (m_itemValid.load(std::memory_order_acquire) && m_item)
        return QRectF(0, 0, m_item->width(), m_item->height());
    return QRectF();
}

// ═══════════════════════════════════════════════════════════════════════════════
// prepare() — resource init + shader baking + uniform upload
// ═══════════════════════════════════════════════════════════════════════════════

void AnimationShaderNodeRhi::prepare()
{
    if (!m_itemValid.load(std::memory_order_acquire) || !m_item || !m_item->window())
        return;
    QRhi* rhi = m_item->window()->rhi();
    QRhiCommandBuffer* cb = commandBuffer();
    QRhiRenderTarget* rt = renderTarget();
    if (!rhi || !cb || !rt)
        return;

    // One-time initialization
    if (!m_initialized) {
        m_initialized = true;

        m_vbo.reset(
            rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(RhiConstants::QuadVertices)));
        if (!m_vbo->create()) {
            m_shaderError = QStringLiteral("Failed to create vertex buffer");
            return;
        }

        m_ubo.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(AnimationShaderUniforms)));
        if (!m_ubo->create()) {
            m_shaderError = QStringLiteral("Failed to create uniform buffer");
            return;
        }

        m_contentSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                               QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!m_contentSampler->create()) {
            m_shaderError = QStringLiteral("Failed to create content sampler");
            return;
        }
    }

    // Shader baking
    if (m_shaderDirty) {
        m_shaderDirty = false;
        m_shaderReady = false;
        m_shaderError.clear();

        if (m_fragmentShaderSource.isEmpty()) {
            m_shaderError = QStringLiteral("No fragment shader source");
            return;
        }

        // Default passthrough vertex shader
        if (!bakeDefaultVertexShader()) {
            m_shaderError = QStringLiteral("Failed to bake default vertex shader");
            return;
        }
        m_vertexShader = s_defaultVertexShader;

        // Bake the fragment shader from disk
        QShaderBaker fBaker;
        fBaker.setGeneratedShaderVariants({QShader::StandardShader});
        fBaker.setGeneratedShaders(detail::bakeTargets());
        fBaker.setSourceString(m_fragmentShaderSource.toUtf8(), QShader::FragmentStage);
        m_fragmentShader = fBaker.bake();
        if (!m_fragmentShader.isValid()) {
            m_shaderError = QStringLiteral("Fragment shader: ") + fBaker.errorMessage();
            qCWarning(lcOverlay) << "AnimationShader bake failed for" << m_fragmentPath << ":" << m_shaderError;
            return;
        }

        m_shaderReady = true;
        m_pipeline.reset();
        m_srb.reset();
        qCInfo(lcOverlay) << "AnimationShader baked successfully:" << m_fragmentPath;
    }

    if (!m_shaderReady)
        return;

    // Upload VBO (once)
    if (!m_vboUploaded) {
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        batch->uploadStaticBuffer(m_vbo.get(), RhiConstants::QuadVertices);
        cb->resourceUpdate(batch);
        m_vboUploaded = true;
    }

    // Sync + upload uniforms
    if (m_uniformsDirty || m_progressDirty) {
        syncUniforms();
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        if (m_didFullUploadOnce && m_progressDirty && !m_uniformsDirty) {
            batch->updateDynamicBuffer(m_ubo.get(), AnimationShaderUboRegions::K_PROGRESS_OFFSET,
                                       AnimationShaderUboRegions::K_PROGRESS_SIZE, &m_uniforms.pz_progress);
        } else {
            batch->updateDynamicBuffer(m_ubo.get(), 0, sizeof(AnimationShaderUniforms), &m_uniforms);
            m_didFullUploadOnce = true;
        }
        cb->resourceUpdate(batch);
        m_uniformsDirty = false;
        m_progressDirty = false;
    }

    ensurePipeline();
}

void AnimationShaderNodeRhi::render(const RenderState* state)
{
    Q_UNUSED(state)
    if (!m_itemValid.load(std::memory_order_acquire))
        return;
    if (!m_shaderReady || !m_pipeline || !m_srb || !m_sourceTexture)
        return;

    QRhiCommandBuffer* cb = commandBuffer();
    QRhiRenderTarget* rt = renderTarget();
    if (!cb || !rt)
        return;

    QSize outputSize = rt->pixelSize();
    int vpX = 0, vpY = 0;
    int vpW = outputSize.width(), vpH = outputSize.height();

    if (m_itemValid.load(std::memory_order_acquire) && m_item && m_item->window() && m_item->width() > 0
        && m_item->height() > 0) {
        QQuickWindow* win = m_item->window();
        const qreal dpr = win->devicePixelRatio();
        const int itemPxW = qRound(m_item->width() * dpr);
        const int itemPxH = qRound(m_item->height() * dpr);
        const bool isLayerFbo = qAbs(outputSize.width() - itemPxW) <= 1 && qAbs(outputSize.height() - itemPxH) <= 1;
        if (!isLayerFbo) {
            const QPointF topLeft = m_item->mapToItem(win->contentItem(), QPointF(0, 0));
            vpX = qBound(0, qRound(topLeft.x() * dpr), outputSize.width() - 1);
            vpY = qBound(0, qRound(topLeft.y() * dpr), outputSize.height() - 1);
        }
        vpW = qBound(1, itemPxW, outputSize.width() - vpX);
        vpH = qBound(1, itemPxH, outputSize.height() - vpY);
    }

    cb->setViewport(QRhiViewport(vpX, vpY, vpW, vpH));
    cb->setScissor(QRhiScissor(vpX, vpY, vpW, vpH));
    cb->setGraphicsPipeline(m_pipeline.get());
    cb->setShaderResources(m_srb.get());
    QRhiCommandBuffer::VertexInput vbufBinding(m_vbo.get(), 0);
    cb->setVertexInput(0, 1, &vbufBinding);
    cb->draw(4);
}

void AnimationShaderNodeRhi::releaseResources()
{
    releaseRhiResources();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private helpers
// ═══════════════════════════════════════════════════════════════════════════════

bool AnimationShaderNodeRhi::ensurePipeline()
{
    QRhi* rhi = safeRhi();
    QRhiRenderTarget* rt = renderTarget();
    if (!rhi || !rt || !m_shaderReady || !m_sourceTexture)
        return false;

    QRhiRenderPassDescriptor* rpDesc = rt->renderPassDescriptor();
    if (!rpDesc)
        return false;

    QVector<quint32> format = rpDesc->serializedFormat();
    if (m_pipeline && m_renderPassFormat != format) {
        m_pipeline.reset();
        m_srb.reset();
    }
    m_renderPassFormat = format;

    // Recreate SRB — source texture from the layer can change each frame
    {
        m_srb.reset(rhi->newShaderResourceBindings());
        QVector<QRhiShaderResourceBinding> bindings;
        bindings.append(QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo.get()));

        QRhiTexture* rhiTex = m_sourceTexture ? m_sourceTexture->rhiTexture() : nullptr;
        if (rhiTex && m_contentSampler) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                                      rhiTex, m_contentSampler.get()));
        }

        m_srb->setBindings(bindings.begin(), bindings.end());
        if (!m_srb->create()) {
            m_shaderError = QStringLiteral("Failed to create SRB");
            return false;
        }
    }

    if (!m_pipeline) {
        m_pipeline.reset(rhi->newGraphicsPipeline());
        m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_pipeline->setShaderStages(
            {{QRhiShaderStage::Vertex, m_vertexShader}, {QRhiShaderStage::Fragment, m_fragmentShader}});
        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({{4 * sizeof(float)}});
        inputLayout.setAttributes(
            {{0, 0, QRhiVertexInputAttribute::Float2, 0}, {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)}});
        m_pipeline->setVertexInputLayout(inputLayout);
        m_pipeline->setShaderResourceBindings(m_srb.get());
        m_pipeline->setRenderPassDescriptor(rpDesc);

        // Premultiplied alpha blending (Qt Quick convention)
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::One;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        m_pipeline->setTargetBlends({blend});
        m_pipeline->setFlags(QRhiGraphicsPipeline::UsesScissor);
        if (!m_pipeline->create()) {
            m_shaderError = QStringLiteral("Failed to create graphics pipeline");
            m_pipeline.reset();
            return false;
        }
    }
    return true;
}

void AnimationShaderNodeRhi::syncUniforms()
{
    if (m_itemValid.load(std::memory_order_acquire) && m_item) {
        const QMatrix4x4* mat = matrix();
        if (mat)
            std::memcpy(m_uniforms.qt_Matrix, mat->constData(), 16 * sizeof(float));
        m_uniforms.qt_Opacity = inheritedOpacity();
    }

    m_uniforms.pz_progress = m_progress;
    m_uniforms.pz_duration = m_duration;
    m_uniforms.pz_style_param = m_styleParam;
    m_uniforms.iResolution[0] = m_width;
    m_uniforms.iResolution[1] = m_height;
    m_uniforms.pz_mode = 0; // Reserved
    m_uniforms.pz_direction = m_direction;

    for (int i = 0; i < 8; ++i) {
        m_uniforms.customParams[i][0] = m_customParams[i].x();
        m_uniforms.customParams[i][1] = m_customParams[i].y();
        m_uniforms.customParams[i][2] = m_customParams[i].z();
        m_uniforms.customParams[i][3] = m_customParams[i].w();
    }
}

void AnimationShaderNodeRhi::releaseRhiResources()
{
    m_pipeline.reset();
    m_srb.reset();
    m_contentSampler.reset();
    m_ubo.reset();
    m_vbo.reset();
    m_sourceTexture = nullptr;
    m_initialized = false;
    m_vboUploaded = false;
    m_shaderDirty = true;
    m_didFullUploadOnce = false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shader loading
// ═══════════════════════════════════════════════════════════════════════════════

void AnimationShaderNodeRhi::setProgress(float progress)
{
    m_progress = progress;
    m_progressDirty = true;
    // Do NOT set m_uniformsDirty here — let the partial UBO update path handle progress-only changes
}

void AnimationShaderNodeRhi::setDuration(float durationMs)
{
    m_duration = durationMs;
    m_uniformsDirty = true;
}

void AnimationShaderNodeRhi::setStyleParam(float param)
{
    m_styleParam = param;
    m_uniformsDirty = true;
}

void AnimationShaderNodeRhi::setDirection(int direction)
{
    m_direction = direction;
    m_uniformsDirty = true;
}

void AnimationShaderNodeRhi::setResolution(float width, float height)
{
    m_width = width;
    m_height = height;
    m_uniformsDirty = true;
}

void AnimationShaderNodeRhi::setCustomParams(int index, const QVector4D& params)
{
    if (index >= 0 && index < 8) {
        m_customParams[index] = params;
        m_uniformsDirty = true;
    }
}

void AnimationShaderNodeRhi::setSourceTexture(QSGTexture* texture)
{
    if (m_sourceTexture != texture) {
        m_sourceTexture = texture;
        m_srb.reset();
    }
}

bool AnimationShaderNodeRhi::loadFragmentShader(const QString& path)
{
    QString err;
    m_fragmentShaderSource = detail::loadAndExpandShader(path, &err);
    if (m_fragmentShaderSource.isEmpty()) {
        m_shaderError = QStringLiteral("Failed to load animation shader: ") + path
            + (err.isEmpty() ? QString() : QStringLiteral(" — ") + err);
        return false;
    }
    m_fragmentPath = path;
    m_fragmentMtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    m_shaderDirty = true;
    return true;
}

bool AnimationShaderNodeRhi::isShaderReady() const
{
    return m_shaderReady;
}

QString AnimationShaderNodeRhi::shaderError() const
{
    return m_shaderError;
}

} // namespace PlasmaZones
