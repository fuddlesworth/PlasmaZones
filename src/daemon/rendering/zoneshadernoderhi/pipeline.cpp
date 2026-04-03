// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../zoneshadernoderhi.h"
#include "internal.h"

#include "../../../core/logging.h"

#include <QQuickWindow>

namespace PlasmaZones {

// ============================================================================
// Fullscreen Quad Pipeline (file-local helper, shared by buffer + image passes)
// ============================================================================

// Buffer passes render to intermediate textures and should NOT alpha-blend against
// the clear color; use enableBlend=false for those so shaders can freely use the
// alpha channel for data without darkening the RGB output.
static std::unique_ptr<QRhiGraphicsPipeline>
createFullscreenQuadPipeline(QRhi* rhi, QRhiRenderPassDescriptor* rpDesc, const QShader& vertexShader,
                             const QShader& fragmentShader, QRhiShaderResourceBindings* srb, bool enableBlend = true,
                             int numColorAttachments = 1)
{
    std::unique_ptr<QRhiGraphicsPipeline> pipeline(rhi->newGraphicsPipeline());
    pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, vertexShader}, {QRhiShaderStage::Fragment, fragmentShader}});
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{4 * sizeof(float)}});
    inputLayout.setAttributes(
        {{0, 0, QRhiVertexInputAttribute::Float2, 0}, {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)}});
    pipeline->setVertexInputLayout(inputLayout);
    pipeline->setShaderResourceBindings(srb);
    pipeline->setRenderPassDescriptor(rpDesc);
    QList<QRhiGraphicsPipeline::TargetBlend> blends;
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = enableBlend;
    // Premultiplied alpha blending (Qt Quick convention). The fragment shader
    // outputs premultiplied color (rgb * alpha) via clampFragColor(), so srcColor
    // uses One instead of SrcAlpha. This matches the Wayland compositor's
    // expectation for surface alpha compositing on both Vulkan and OpenGL.
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blends.append(blend);
    for (int i = 1; i < numColorAttachments; ++i) {
        QRhiGraphicsPipeline::TargetBlend depthBlend;
        depthBlend.enable = false; // No blending for depth attachment
        blends.append(depthBlend);
    }
    pipeline->setTargetBlends(blends.begin(), blends.end());
    pipeline->setFlags(QRhiGraphicsPipeline::UsesScissor);
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
    QRhi* rhi = safeRhi();
    if (!rhi) {
        return false;
    }
    const bool multiBufferMode = m_bufferPaths.size() > 1;
    const int bufferW = qMax(1, qRound(m_width * m_bufferScale));
    const int bufferH = qMax(1, qRound(m_height * m_bufferScale));
    const QSize bufferSize(bufferW, bufferH);
    // Create or resize depth texture before render targets that reference it
    if (m_useDepthBuffer && (!m_depthTexture || m_depthTexture->pixelSize() != bufferSize)) {
        m_depthTexture.reset(rhi->newTexture(QRhiTexture::R32F, bufferSize, 1, QRhiTexture::RenderTarget));
        if (!m_depthTexture->create()) {
            qCWarning(lcOverlay) << "Failed to create depth texture";
            return false;
        }
        if (!m_depthSampler) {
            m_depthSampler.reset(rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                                 QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
            if (!m_depthSampler->create()) {
                qCWarning(lcOverlay) << "Failed to create depth sampler";
                return false;
            }
        }
        // Force RT recreation since attachment count changed.
        // Depth texture is bound at binding 12 in all SRBs; adding/removing it
        // changes the SRB layout, so all pipelines must be recreated too.
        m_pipeline.reset();
        m_bufferPipeline.reset();
        m_bufferSrb.reset();
        m_bufferSrbB.reset();
        m_srb.reset();
        m_srbB.reset();
        for (int i = 0; i < kMaxBufferPasses; ++i) {
            m_multiBufferPipelines[i].reset();
            m_multiBufferSrbs[i].reset();
        }
    }

    // NOTE: When useDepthBuffer is true, the same m_depthTexture is attached to ALL
    // buffer render targets (single and multi-buffer). In multi-buffer mode, each pass
    // overwrites the depth attachment — only the final buffer pass's depth output survives
    // for the image pass to read at binding 12.
    if (m_useDepthBuffer && multiBufferMode && m_bufferPaths.size() > 1) {
        if (!m_depthMultiBufferWarned) {
            m_depthMultiBufferWarned = true;
            qCWarning(lcOverlay)
                << "Depth buffer with" << m_bufferPaths.size()
                << "buffer passes: only the last pass's depth output will be available in the image pass";
        }
    }
    auto createTextureAndRT = [rhi, bufferSize, this](std::unique_ptr<QRhiTexture>& tex,
                                                      std::unique_ptr<QRhiTextureRenderTarget>& rt,
                                                      std::unique_ptr<QRhiRenderPassDescriptor>& rpd) -> bool {
        // UsedWithLoadStore adds VK_IMAGE_USAGE_STORAGE_BIT, which disables
        // NVIDIA's Delta Color Compression (DCC) on the texture. Without this,
        // compressed render target data isn't decompressed when the texture is
        // sampled in a later pass — Qt RHI's barrier tracking doesn't trigger
        // the DCC decompression, causing rectangular tile artifacts on Vulkan.
        tex.reset(rhi->newTexture(QRhiTexture::RGBA16F, bufferSize, 1,
                                  QRhiTexture::RenderTarget | QRhiTexture::UsedWithLoadStore));
        if (!tex->create()) {
            return false;
        }
        QRhiTextureRenderTargetDescription desc;
        if (m_useDepthBuffer && m_depthTexture) {
            desc.setColorAttachments({QRhiColorAttachment(tex.get()), QRhiColorAttachment(m_depthTexture.get())});
        } else {
            desc.setColorAttachments({QRhiColorAttachment(tex.get())});
        }
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
            qCInfo(lcOverlay) << "Creating multi-buffer textures:"
                              << "bufferSize=" << bufferSize << "m_width=" << m_width << "m_height=" << m_height
                              << "bufferScale=" << m_bufferScale << "passes=" << n;
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
        for (int i = 0; i < n; ++i) {
            if (!ensureBufferSampler(rhi, i)) {
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
        if (!ensureBufferSampler(rhi, 0)) {
            return false;
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
    if (m_bufferTexture && !m_bufferSamplers[0]) {
        if (!ensureBufferSampler(rhi, 0)) {
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
// ensureDummyChannelResources — shared by ensureBufferPipeline (multi/single)
// ============================================================================

bool ZoneShaderNodeRhi::ensureDummyChannelResources(QRhi* rhi)
{
    if (!m_dummyChannelTexture) {
        m_dummyChannelTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        if (m_dummyChannelTexture->create()) {
            m_dummyChannelTextureNeedsUpload = true;
        } else {
            m_dummyChannelTexture.reset();
            return false;
        }
    }
    if (!m_dummyChannelSampler) {
        m_dummyChannelSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                    QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!m_dummyChannelSampler->create()) {
            m_dummyChannelSampler.reset();
            return false;
        }
    }
    return true;
}

// ============================================================================
// ensureBufferSampler — create/recreate sampler for a single buffer channel
// ============================================================================

bool ZoneShaderNodeRhi::ensureBufferSampler(QRhi* rhi, int index)
{
    if (index < 0 || index >= kMaxBufferPasses) {
        return false;
    }
    if (m_bufferSamplers[index]) {
        return true;
    }
    const QRhiSampler::AddressMode addr =
        (m_bufferWraps[index] == QLatin1String("repeat")) ? QRhiSampler::Repeat : QRhiSampler::ClampToEdge;
    QRhiSampler::Filter minF = QRhiSampler::Linear;
    QRhiSampler::Filter magF = QRhiSampler::Linear;
    QRhiSampler::Filter mipF = QRhiSampler::None;
    const QString& filterMode = m_bufferFilters[index];
    if (filterMode == QLatin1String("nearest")) {
        minF = QRhiSampler::Nearest;
        magF = QRhiSampler::Nearest;
    } else if (filterMode == QLatin1String("mipmap")) {
        mipF = QRhiSampler::Linear;
    }
    m_bufferSamplers[index].reset(rhi->newSampler(minF, magF, mipF, addr, addr));
    if (!m_bufferSamplers[index]->create()) {
        qCWarning(lcOverlay) << "Failed to create buffer sampler" << index;
        m_bufferSamplers[index].reset();
        return false;
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
        QRhi* rhi = safeRhi();
        if (!rhi || !m_bufferSamplers[0]) {
            return false;
        }
        if (!ensureDummyChannelResources(rhi)) {
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
                // Bind ALL 4 channel slots (bindings 2-5). The SPIR-V shader
                // declares iChannel0-3 via multipass.glsl; on Vulkan, every
                // declared binding must exist in the descriptor set layout.
                // Unbound slots get a dummy 1×1 texture. OpenGL tolerates
                // missing bindings but Vulkan treats them as undefined behavior.
                for (int j = 0; j < kMaxBufferPasses; ++j) {
                    QRhiTexture* tex = (j < i && m_multiBufferTextures[j]) ? m_multiBufferTextures[j].get()
                                                                           : m_dummyChannelTexture.get();
                    QRhiSampler* sam =
                        (j < i && m_bufferSamplers[j]) ? m_bufferSamplers[j].get() : m_dummyChannelSampler.get();
                    if (tex && sam) {
                        bindings.append(QRhiShaderResourceBinding::sampledTexture(
                            2 + j, QRhiShaderResourceBinding::FragmentStage, tex, sam));
                    }
                }
                if (m_audioSpectrumTexture && m_audioSpectrumSampler) {
                    bindings.append(QRhiShaderResourceBinding::sampledTexture(
                        6, QRhiShaderResourceBinding::FragmentStage, m_audioSpectrumTexture.get(),
                        m_audioSpectrumSampler.get()));
                }
                appendUserTextureBindings(bindings);
                appendWallpaperBinding(bindings);
                appendDepthBinding(bindings);
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
                m_multiBufferPipelines[i] = createFullscreenQuadPipeline(
                    rhi, rpDescI, m_vertexShader, m_multiBufferFragmentShaders[i], m_multiBufferSrbs[i].get(),
                    /*enableBlend=*/false, /*numColorAttachments=*/m_useDepthBuffer ? 2 : 1);
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
    QRhi* rhi = safeRhi();
    if (!rhi) {
        return false;
    }
    if (!ensureDummyChannelResources(rhi)) {
        return false;
    }
    QRhiRenderPassDescriptor* rpDesc = m_bufferRenderPassDescriptor ? m_bufferRenderPassDescriptor.get()
                                                                    : m_bufferRenderTarget->renderPassDescriptor();
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
            bindings.append(QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                                      m_labelsTexture.get(), m_labelsSampler.get()));
        }
        // Bind all 4 channel slots (bindings 2-5) — SPIR-V requires all declared
        // bindings to exist in the descriptor set layout. Channel 0 gets the real
        // feedback texture; channels 1-3 get a dummy 1×1 transparent texture.
        for (int ch = 0; ch < kMaxBufferPasses; ++ch) {
            QRhiTexture* tex = (ch == 0 && channel0Texture) ? channel0Texture : m_dummyChannelTexture.get();
            QRhiSampler* sam = (ch == 0 && channel0Texture && m_bufferSamplers[0]) ? m_bufferSamplers[0].get()
                                                                                   : m_dummyChannelSampler.get();
            if (tex && sam) {
                bindings.append(QRhiShaderResourceBinding::sampledTexture(
                    2 + ch, QRhiShaderResourceBinding::FragmentStage, tex, sam));
            }
        }
        if (m_audioSpectrumTexture && m_audioSpectrumSampler) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(6, QRhiShaderResourceBinding::FragmentStage,
                                                                      m_audioSpectrumTexture.get(),
                                                                      m_audioSpectrumSampler.get()));
        }
        appendUserTextureBindings(bindings);
        appendWallpaperBinding(bindings);
        appendDepthBinding(bindings);
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
        m_bufferPipeline =
            createFullscreenQuadPipeline(rhi, rpDesc, m_vertexShader, m_bufferFragmentShader, m_bufferSrb.get(),
                                         /*enableBlend=*/false, /*numColorAttachments=*/m_useDepthBuffer ? 2 : 1);
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
    QRhi* rhi = safeRhi();
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

    auto createImageSrbSingle = [rhi,
                                 this](QRhiTexture* channel0Texture) -> std::unique_ptr<QRhiShaderResourceBindings> {
        QRhiSampler* channel0Sampler = (channel0Texture && m_bufferSamplers[0]) ? m_bufferSamplers[0].get() : nullptr;
        if (!channel0Texture && !m_bufferPath.isEmpty()) {
            channel0Texture = m_dummyChannelTexture.get();
            channel0Sampler = m_dummyChannelSampler.get();
        }
        std::unique_ptr<QRhiShaderResourceBindings> srb(rhi->newShaderResourceBindings());
        QVector<QRhiShaderResourceBinding> bindings;
        bindings.append(QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo.get()));
        if (m_labelsTexture && m_labelsSampler) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                                      m_labelsTexture.get(), m_labelsSampler.get()));
        }
        // Bind all 4 channel slots (bindings 2-5). SPIR-V shaders that include
        // multipass.glsl declare iChannel0-3; every declared binding must exist
        // in the Vulkan descriptor set layout or the driver reads garbage.
        for (int ch = 0; ch < kMaxBufferPasses; ++ch) {
            QRhiTexture* tex = (ch == 0 && channel0Texture) ? channel0Texture : m_dummyChannelTexture.get();
            QRhiSampler* sam = (ch == 0 && channel0Sampler) ? channel0Sampler : m_dummyChannelSampler.get();
            if (tex && sam) {
                bindings.append(QRhiShaderResourceBinding::sampledTexture(
                    2 + ch, QRhiShaderResourceBinding::FragmentStage, tex, sam));
            }
        }
        if (m_audioSpectrumTexture && m_audioSpectrumSampler) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(6, QRhiShaderResourceBinding::FragmentStage,
                                                                      m_audioSpectrumTexture.get(),
                                                                      m_audioSpectrumSampler.get()));
        }
        appendUserTextureBindings(bindings);
        appendWallpaperBinding(bindings);
        appendDepthBinding(bindings);
        srb->setBindings(bindings.begin(), bindings.end());
        return srb->create() ? std::move(srb) : nullptr;
    };
    auto createImageSrbMulti = [rhi, this]() -> std::unique_ptr<QRhiShaderResourceBindings> {
        std::unique_ptr<QRhiShaderResourceBindings> srb(rhi->newShaderResourceBindings());
        QVector<QRhiShaderResourceBinding> bindings;
        bindings.append(QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo.get()));
        if (m_labelsTexture && m_labelsSampler) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                                      m_labelsTexture.get(), m_labelsSampler.get()));
        }
        const int n = qMin(m_bufferPaths.size(), kMaxBufferPasses);
        QRhiTexture* dummyTex = m_dummyChannelTexture.get();
        QRhiSampler* dummySam = m_dummyChannelSampler.get();
        // Bind ALL 4 channel slots (bindings 2-5), not just 0..n-1.
        for (int i = 0; i < kMaxBufferPasses; ++i) {
            QRhiTexture* tex = (i < n && m_multiBufferTextures[i]) ? m_multiBufferTextures[i].get() : dummyTex;
            QRhiSampler* sam = (tex == dummyTex || !m_bufferSamplers[i]) ? dummySam : m_bufferSamplers[i].get();
            if (tex && sam) {
                bindings.append(QRhiShaderResourceBinding::sampledTexture(
                    2 + i, QRhiShaderResourceBinding::FragmentStage, tex, sam));
            }
        }
        if (m_audioSpectrumTexture && m_audioSpectrumSampler) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(6, QRhiShaderResourceBinding::FragmentStage,
                                                                      m_audioSpectrumTexture.get(),
                                                                      m_audioSpectrumSampler.get()));
        }
        appendUserTextureBindings(bindings);
        appendWallpaperBinding(bindings);
        appendDepthBinding(bindings);
        srb->setBindings(bindings.begin(), bindings.end());
        return srb->create() ? std::move(srb) : nullptr;
    };

    if (hasMultipass) {
        if (!ensureDummyChannelResources(rhi)) {
            return false;
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
