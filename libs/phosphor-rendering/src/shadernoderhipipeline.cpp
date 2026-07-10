// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "internal.h"

#include <QQuickWindow>

namespace PhosphorRendering {

// ============================================================================
// Fullscreen Quad Pipeline (file-local helper, shared by buffer + image passes)
// ============================================================================

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
    // Premultiplied alpha blending (Qt Quick convention).
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blends.append(blend);
    for (int i = 1; i < numColorAttachments; ++i) {
        QRhiGraphicsPipeline::TargetBlend depthBlend;
        depthBlend.enable = false;
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

bool ShaderNodeRhi::ensureBufferTarget()
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
            qCWarning(lcShaderNode) << "Failed to create depth texture";
            return false;
        }
        if (!m_depthSampler) {
            m_depthSampler.reset(rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                                 QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
            if (!m_depthSampler->create()) {
                qCWarning(lcShaderNode) << "Failed to create depth sampler";
                return false;
            }
        }
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

    if (m_useDepthBuffer && multiBufferMode && m_bufferPaths.size() > 1) {
        if (!m_depthMultiBufferWarned) {
            m_depthMultiBufferWarned = true;
            qCWarning(lcShaderNode)
                << "Depth buffer with" << m_bufferPaths.size()
                << "buffer passes: only the last pass's depth output will be available in the image pass";
        }
    }
    auto createTextureAndRT = [rhi, bufferSize, this](std::unique_ptr<QRhiTexture>& tex,
                                                      std::unique_ptr<QRhiTextureRenderTarget>& rt,
                                                      std::unique_ptr<QRhiRenderPassDescriptor>& rpd) -> bool {
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
            qCInfo(lcShaderNode) << "Creating multi-buffer textures:"
                                 << "bufferSize=" << bufferSize << "m_width=" << m_width << "m_height=" << m_height
                                 << "bufferScale=" << m_bufferScale << "passes=" << n;
            for (int i = 0; i < n; ++i) {
                if (!createTextureAndRT(m_multiBufferTextures[i], m_multiBufferRenderTargets[i],
                                        m_multiBufferRenderPassDescriptors[i])) {
                    qCWarning(lcShaderNode) << "Failed to create multi-buffer texture" << i;
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
            qCWarning(lcShaderNode) << "Failed to create buffer texture";
            return false;
        }
        if (!ensureBufferSampler(rhi, 0)) {
            return false;
        }
        if (m_bufferFeedback
            && !createTextureAndRT(m_bufferTextureB, m_bufferRenderTargetB, m_bufferRenderPassDescriptorB)) {
            qCWarning(lcShaderNode) << "Failed to create buffer texture B (ping-pong)";
            return false;
        }
        m_srb.reset();
        m_srbB.reset();
        return true;
    }
    if (m_bufferTexture->pixelSize() != bufferSize) {
        if (!createTextureAndRT(m_bufferTexture, m_bufferRenderTarget, m_bufferRenderPassDescriptor)) {
            qCWarning(lcShaderNode) << "Failed to resize buffer texture";
            return false;
        }
        if (m_bufferFeedback
            && !createTextureAndRT(m_bufferTextureB, m_bufferRenderTargetB, m_bufferRenderPassDescriptorB)) {
            qCWarning(lcShaderNode) << "Failed to resize buffer texture B";
            return false;
        }
        m_bufferPipeline.reset();
        m_bufferSrb.reset();
        m_bufferSrbB.reset();
        m_srb.reset();
        m_srbB.reset();
        m_bufferFeedbackCleared = false; // New textures need clearing
    } else if (m_bufferFeedback && !m_bufferTextureB) {
        if (!createTextureAndRT(m_bufferTextureB, m_bufferRenderTargetB, m_bufferRenderPassDescriptorB)) {
            qCWarning(lcShaderNode) << "Failed to create buffer texture B (ping-pong)";
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
// ensureDummyChannelResources
// ============================================================================

bool ShaderNodeRhi::ensureDummyChannelResources(QRhi* rhi)
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
// ensureBufferSampler
// ============================================================================

bool ShaderNodeRhi::ensureBufferSampler(QRhi* rhi, int index)
{
    if (index < 0 || index >= kMaxBufferPasses) {
        return false;
    }
    if (m_bufferSamplers[index]) {
        return true;
    }
    const QRhiSampler::AddressMode addr = wrapModeToRhiAddress(m_bufferWraps[index]);
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
        qCWarning(lcShaderNode) << "Failed to create buffer sampler" << index;
        m_bufferSamplers[index].reset();
        return false;
    }
    return true;
}

// ============================================================================
// ensureBufferPipeline
// ============================================================================

bool ShaderNodeRhi::ensureBufferPipeline()
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
                appendUboAndExtraBindings(bindings);
                // Bind ALL 4 channel slots (bindings 2-5): pass i sees outputs of passes 0..i-1.
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
                appendCommonTrailerBindings(bindings);
                srb->setBindings(bindings.begin(), bindings.end());
                if (!srb->create()) {
                    m_shaderError = QStringLiteral("Failed to create multi-buffer pass SRB ") + QString::number(i);
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
        appendUboAndExtraBindings(bindings);
        for (int ch = 0; ch < kMaxBufferPasses; ++ch) {
            QRhiTexture* tex = (ch == 0 && channel0Texture) ? channel0Texture : m_dummyChannelTexture.get();
            QRhiSampler* sam = (ch == 0 && channel0Texture && m_bufferSamplers[0]) ? m_bufferSamplers[0].get()
                                                                                   : m_dummyChannelSampler.get();
            if (tex && sam) {
                bindings.append(QRhiShaderResourceBinding::sampledTexture(
                    2 + ch, QRhiShaderResourceBinding::FragmentStage, tex, sam));
            }
        }
        appendCommonTrailerBindings(bindings);
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

bool ShaderNodeRhi::ensurePipeline()
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
        appendUboAndExtraBindings(bindings);
        for (int ch = 0; ch < kMaxBufferPasses; ++ch) {
            QRhiTexture* tex = (ch == 0 && channel0Texture) ? channel0Texture : m_dummyChannelTexture.get();
            QRhiSampler* sam = (ch == 0 && channel0Sampler) ? channel0Sampler : m_dummyChannelSampler.get();
            if (tex && sam) {
                bindings.append(QRhiShaderResourceBinding::sampledTexture(
                    2 + ch, QRhiShaderResourceBinding::FragmentStage, tex, sam));
            }
        }
        appendCommonTrailerBindings(bindings);
        srb->setBindings(bindings.begin(), bindings.end());
        return srb->create() ? std::move(srb) : nullptr;
    };
    auto createImageSrbMulti = [rhi, this]() -> std::unique_ptr<QRhiShaderResourceBindings> {
        std::unique_ptr<QRhiShaderResourceBindings> srb(rhi->newShaderResourceBindings());
        QVector<QRhiShaderResourceBinding> bindings;
        appendUboAndExtraBindings(bindings);
        const int n = qMin(m_bufferPaths.size(), kMaxBufferPasses);
        QRhiTexture* dummyTex = m_dummyChannelTexture.get();
        QRhiSampler* dummySam = m_dummyChannelSampler.get();
        for (int i = 0; i < kMaxBufferPasses; ++i) {
            QRhiTexture* tex = (i < n && m_multiBufferTextures[i]) ? m_multiBufferTextures[i].get() : dummyTex;
            QRhiSampler* sam = (tex == dummyTex || !m_bufferSamplers[i]) ? dummySam : m_bufferSamplers[i].get();
            if (tex && sam) {
                bindings.append(QRhiShaderResourceBinding::sampledTexture(
                    2 + i, QRhiShaderResourceBinding::FragmentStage, tex, sam));
            }
        }
        appendCommonTrailerBindings(bindings);
        srb->setBindings(bindings.begin(), bindings.end());
        return srb->create() ? std::move(srb) : nullptr;
    };

    if (hasMultipass) {
        if (!ensureDummyChannelResources(rhi)) {
            return false;
        }
    }
    // The dummy 1x1 transparent texture also backs UNSUPPLIED user-texture
    // slots 1-3 (appendUserTextureBindings): a shader that references
    // uTexture<N> without a loaded texture must read the documented
    // transparent black rather than leave the declared binding without an
    // SRB entry (strict backends reject the mismatch; lenient ones sample
    // undefined). Best-effort — a failed create falls back to the previous
    // omit-the-binding behaviour.
    ensureDummyChannelResources(rhi);

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

void ShaderNodeRhi::appendUserTextureBindings(QVector<QRhiShaderResourceBinding>& bindings) const
{
    // Slot 0 (binding kUserTextureBaseBinding) override: a
    // `setSourceTextureProvider`-supplied live texture takes precedence over
    // any QImage-uploaded user texture at the same slot. We read the
    // pre-resolved m_lastSourceRhiTexture (snapshot taken under the same
    // sampler/identity check in uploadDirtyTextures()) rather than querying
    // the provider again — calling provider->texture()->rhiTexture() here
    // would re-open the TOCTOU window between the change-detection step in
    // uploadDirtyTextures and this SRB build, where an FBO recreation could
    // hand back a different QRhiTexture* than the cache observed.
    //
    // When slot 0 is overridden but the resolved texture is null (provider
    // not yet ready, foreign-RHI guard fired, or sampler create failed) we
    // bind the dedicated 1×1 transparent fallback rather than fall through
    // to the QImage path — the documented contract is that
    // setSourceTextureProvider SUPERSEDES the QImage at slot 0, and a
    // transient null must not unmask a stale snapshot.
    if (m_sourceTextureProvider) {
        QRhiTexture* slot0Tex = nullptr;
        QRhiSampler* slot0Sam = nullptr;
        if (m_lastSourceRhiTexture && m_sourceSampler) {
            slot0Tex = m_lastSourceRhiTexture;
            slot0Sam = m_sourceSampler.get();
        } else if (m_transparentFallbackTexture && m_userTextureSamplers[0]) {
            slot0Tex = m_transparentFallbackTexture.get();
            slot0Sam = m_userTextureSamplers[0].get();
        } else if (m_userTextures[0] && m_userTextureSamplers[0]) {
            // Last resort: provider set but no fallback yet (sampler create
            // failed and fallback texture create also failed) — keep slot 0
            // bound so the SRB build doesn't omit binding 7 entirely. This
            // path should only be hit on a degraded RHI.
            slot0Tex = m_userTextures[0].get();
            slot0Sam = m_userTextureSamplers[0].get();
        }
        if (slot0Tex && slot0Sam) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(
                kUserTextureBaseBinding, QRhiShaderResourceBinding::FragmentStage, slot0Tex, slot0Sam));
        }
    } else if (m_userTextures[0] && m_userTextureSamplers[0]) {
        // No override: standard QImage user-texture path.
        bindings.append(
            QRhiShaderResourceBinding::sampledTexture(kUserTextureBaseBinding, QRhiShaderResourceBinding::FragmentStage,
                                                      m_userTextures[0].get(), m_userTextureSamplers[0].get()));
    } else if (m_dummyChannelTexture && m_dummyChannelSampler) {
        // Neither a provider nor a QImage at slot 0: bind the dummy 1x1
        // transparent texture so binding 7 (uTexture0, declared by every
        // family's shared header and referenced by most packs) always has an
        // SRB entry — same rationale as the slots 1-3 fallback below. Hosts
        // normally wire a source before first paint; this covers the gap on
        // strict backends.
        bindings.append(
            QRhiShaderResourceBinding::sampledTexture(kUserTextureBaseBinding, QRhiShaderResourceBinding::FragmentStage,
                                                      m_dummyChannelTexture.get(), m_dummyChannelSampler.get()));
    }

    // Slots 1-3 always come from the QImage path; the source-provider
    // override is slot-0-only by design. An unsupplied slot binds the dummy
    // 1x1 transparent texture instead of omitting the binding: the shared
    // GLSL headers declare uTexture1..3 unconditionally, and a shader that
    // references one without a loaded texture must read the documented
    // transparent black — an absent SRB entry for a declared-and-referenced
    // binding is rejected by strict backends and samples undefined on
    // lenient ones. Extra SRB entries for bindings a shader never declares
    // are ignored by QRhi, so the unconditional dummy append is safe.
    for (int t = 1; t < kMaxUserTextures; ++t) {
        if (m_userTextures[t] && m_userTextureSamplers[t]) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(
                kUserTextureBaseBinding + t, QRhiShaderResourceBinding::FragmentStage, m_userTextures[t].get(),
                m_userTextureSamplers[t].get()));
        } else if (m_dummyChannelTexture && m_dummyChannelSampler) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(
                kUserTextureBaseBinding + t, QRhiShaderResourceBinding::FragmentStage, m_dummyChannelTexture.get(),
                m_dummyChannelSampler.get()));
        }
    }
}

void ShaderNodeRhi::appendAudioBinding(QVector<QRhiShaderResourceBinding>& bindings) const
{
    if (m_audioSpectrumTexture && m_audioSpectrumSampler) {
        bindings.append(QRhiShaderResourceBinding::sampledTexture(
            6, QRhiShaderResourceBinding::FragmentStage, m_audioSpectrumTexture.get(), m_audioSpectrumSampler.get()));
    }
}

void ShaderNodeRhi::appendUboAndExtraBindings(QVector<QRhiShaderResourceBinding>& bindings) const
{
    bindings.append(QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo.get()));
    appendExtraBindings(bindings);
}

void ShaderNodeRhi::appendCommonTrailerBindings(QVector<QRhiShaderResourceBinding>& bindings) const
{
    appendAudioBinding(bindings);
    appendUserTextureBindings(bindings);
    appendWallpaperBinding(bindings);
    appendDepthBinding(bindings);
}

void ShaderNodeRhi::appendWallpaperBinding(QVector<QRhiShaderResourceBinding>& bindings) const
{
    if (m_useWallpaper && m_wallpaperTexture && m_wallpaperSampler) {
        bindings.append(QRhiShaderResourceBinding::sampledTexture(11, QRhiShaderResourceBinding::FragmentStage,
                                                                  m_wallpaperTexture.get(), m_wallpaperSampler.get()));
    }
}

void ShaderNodeRhi::appendDepthBinding(QVector<QRhiShaderResourceBinding>& bindings) const
{
    if (m_useDepthBuffer && m_depthTexture && m_depthSampler) {
        bindings.append(QRhiShaderResourceBinding::sampledTexture(12, QRhiShaderResourceBinding::FragmentStage,
                                                                  m_depthTexture.get(), m_depthSampler.get()));
    }
}

void ShaderNodeRhi::appendExtraBindings(QVector<QRhiShaderResourceBinding>& bindings) const
{
    for (const auto& [binding, extra] : m_extraBindings) {
        if (!extra.texture || !extra.sampler)
            continue;
        bindings.append(QRhiShaderResourceBinding::sampledTexture(binding, QRhiShaderResourceBinding::FragmentStage,
                                                                  extra.texture, extra.sampler));
    }
}

void ShaderNodeRhi::resetAllBindingsAndPipelines()
{
    m_srb.reset();
    m_srbB.reset();
    m_bufferSrb.reset();
    m_bufferSrbB.reset();
    m_pipeline.reset();
    m_bufferPipeline.reset();
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        m_multiBufferSrbs[i].reset();
        m_multiBufferPipelines[i].reset();
    }
}

} // namespace PhosphorRendering
