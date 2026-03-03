// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoneshadernoderhi.h"
#include "zoneshadernoderhi_internal.h"

#include "../../core/logging.h"

#include <QQuickWindow>

namespace PlasmaZones {

// ============================================================================
// Fullscreen Quad Pipeline (file-local helper, shared by buffer + image passes)
// ============================================================================

// Buffer passes render to intermediate textures and should NOT alpha-blend against
// the clear color; use enableBlend=false for those so shaders can freely use the
// alpha channel for data without darkening the RGB output.
static std::unique_ptr<QRhiGraphicsPipeline> createFullscreenQuadPipeline(
    QRhi* rhi, QRhiRenderPassDescriptor* rpDesc, const QShader& vertexShader,
    const QShader& fragmentShader, QRhiShaderResourceBindings* srb,
    bool enableBlend = true)
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
    blend.enable = enableBlend;
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

// ============================================================================
// ensureBufferTarget
// ============================================================================

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

// ============================================================================
// ensureBufferPipeline
// ============================================================================

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
                if (m_labelsTexture && m_labelsSampler) {
                    bindings.append(QRhiShaderResourceBinding::sampledTexture(
                        1, QRhiShaderResourceBinding::FragmentStage, m_labelsTexture.get(), m_labelsSampler.get()));
                }
                for (int j = 0; j < i; ++j) {
                    if (m_multiBufferTextures[j] && m_bufferSampler) {
                        bindings.append(QRhiShaderResourceBinding::sampledTexture(
                            2 + j, QRhiShaderResourceBinding::FragmentStage,
                            m_multiBufferTextures[j].get(), m_bufferSampler.get()));
                    }
                }
                if (m_audioSpectrumTexture && m_audioSpectrumSampler) {
                    bindings.append(QRhiShaderResourceBinding::sampledTexture(
                        6, QRhiShaderResourceBinding::FragmentStage, m_audioSpectrumTexture.get(), m_audioSpectrumSampler.get()));
                }
                appendUserTextureBindings(bindings);
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
                    m_vertexShader, m_multiBufferFragmentShaders[i], m_multiBufferSrbs[i].get(),
                    /*enableBlend=*/false);
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
        if (m_labelsTexture && m_labelsSampler) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage, m_labelsTexture.get(), m_labelsSampler.get()));
        }
        if (channel0Texture && m_bufferSampler) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(
                2, QRhiShaderResourceBinding::FragmentStage, channel0Texture, m_bufferSampler.get()));
        }
        if (m_audioSpectrumTexture && m_audioSpectrumSampler) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(
                6, QRhiShaderResourceBinding::FragmentStage, m_audioSpectrumTexture.get(), m_audioSpectrumSampler.get()));
        }
        appendUserTextureBindings(bindings);
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
        m_bufferPipeline = createFullscreenQuadPipeline(rhi, rpDesc, m_vertexShader, m_bufferFragmentShader, m_bufferSrb.get(),
            /*enableBlend=*/false);
        if (!m_bufferPipeline) {
            m_shaderError = QStringLiteral("Failed to create buffer pipeline");
            return false;
        }
    }
    return true;
}

// ============================================================================
// ensurePipeline
// ============================================================================

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
    const bool hasMultipass = !m_bufferPath.isEmpty() || multiBufferMode;

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
        if (m_audioSpectrumTexture && m_audioSpectrumSampler) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(
                6, QRhiShaderResourceBinding::FragmentStage, m_audioSpectrumTexture.get(), m_audioSpectrumSampler.get()));
        }
        appendUserTextureBindings(bindings);
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
        if (m_audioSpectrumTexture && m_audioSpectrumSampler) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(
                6, QRhiShaderResourceBinding::FragmentStage, m_audioSpectrumTexture.get(), m_audioSpectrumSampler.get()));
        }
        appendUserTextureBindings(bindings);
        srb->setBindings(bindings.begin(), bindings.end());
        return srb->create() ? std::move(srb) : nullptr;
    };

    if (hasMultipass) {
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
    }

    if (!m_srb) {
        if (multiBufferMode) {
            m_srb = createImageSrbMulti();
        } else {
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

} // namespace PlasmaZones
