// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../zoneshadernoderhi.h"
#include "../zoneshadercommon.h"
#include "internal.h"
#include "../../../core/logging.h"
#include "../../../core/shaderincluderesolver.h"
#include <QQuickWindow>
#include <cmath>
#include <cstring>

namespace PlasmaZones {

void ZoneShaderNodeRhi::bakeComputeShader()
{
    if (!m_computeShaderDirty || m_computeShaderPath.isEmpty() || !m_computeSupported) {
        return;
    }
    m_computeShaderDirty = false;
    m_computeShaderReady = false;
    m_computePipeline.reset();
    m_computeSrb.reset();

    QString error;
    m_computeShaderSource = detail::loadAndExpandShader(m_computeShaderPath, &error);
    if (m_computeShaderSource.isEmpty()) {
        qCWarning(lcOverlay) << "Failed to load compute shader:" << error;
        return;
    }

    // Compute requires GLSL 430+ / GLES 310+
    static const QList<QShaderBaker::GeneratedShader> computeTargets = {
        {QShader::SpirvShader, QShaderVersion(130)},
        {QShader::GlslShader, QShaderVersion(430)},
        {QShader::GlslShader, QShaderVersion(310, QShaderVersion::GlslEs)},
    };

    QShaderBaker baker;
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders(computeTargets);
    baker.setSourceString(m_computeShaderSource.toUtf8(), QShader::ComputeStage);
    m_computeShader = baker.bake();
    if (!m_computeShader.isValid()) {
        qCWarning(lcOverlay) << "Compute shader bake failed:" << baker.errorMessage();
        return;
    }
    m_computeShaderReady = true;
}

bool ZoneShaderNodeRhi::ensureComputePipeline()
{
    if (!m_computeSupported || !m_computeShaderReady || m_particleCount <= 0) {
        return false;
    }

    QRhi* rhi =
        (m_itemValid.load(std::memory_order_acquire) && m_item && m_item->window()) ? m_item->window()->rhi() : nullptr;
    if (!rhi) {
        return false;
    }

    const int w = qMax(1, static_cast<int>(m_width));
    const int h = qMax(1, static_cast<int>(m_height));
    const QSize texSize(w, h);

    // Create particle texture (binding 13: image2D in compute, sampler2D in fragment)
    if (!m_particleTexture || m_particleTexture->pixelSize() != texSize) {
        m_particleTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, texSize, 1,
                                                QRhiTexture::UsedWithLoadStore | QRhiTexture::RenderTarget));
        if (!m_particleTexture->create()) {
            qCWarning(lcOverlay) << "Failed to create particle texture";
            return false;
        }
        m_computeSrb.reset();
        resetAllSrbs(); // Fragment SRBs need rebuild for new texture
    }
    if (!m_particleSampler) {
        m_particleSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!m_particleSampler->create()) {
            qCWarning(lcOverlay) << "Failed to create particle sampler";
            return false;
        }
    }

    // Create SSBO (binding 14)
    const int ssboSize = m_particleCount * static_cast<int>(sizeof(ParticleData));
    if (!m_particleSsbo || static_cast<int>(m_particleSsbo->size()) != ssboSize) {
        m_particleSsbo.reset(rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, ssboSize));
        if (!m_particleSsbo->create()) {
            qCWarning(lcOverlay) << "Failed to create particle SSBO";
            return false;
        }
        m_particleSsboNeedsInit = true;
        m_computeSrb.reset();
    }

    // Create compute SRB
    if (!m_computeSrb) {
        m_computeSrb.reset(rhi->newShaderResourceBindings());
        QVector<QRhiShaderResourceBinding> bindings;
        // binding 0: UBO
        bindings.append(
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::ComputeStage, m_ubo.get()));
        // binding 6: audio spectrum (for audio-reactive particles)
        if (m_audioSpectrumTexture && m_audioSpectrumSampler) {
            bindings.append(QRhiShaderResourceBinding::sampledTexture(6, QRhiShaderResourceBinding::ComputeStage,
                                                                      m_audioSpectrumTexture.get(),
                                                                      m_audioSpectrumSampler.get()));
        }
        // binding 13: particle texture (image load/store)
        bindings.append(QRhiShaderResourceBinding::imageLoadStore(13, QRhiShaderResourceBinding::ComputeStage,
                                                                  m_particleTexture.get(), 0));
        // binding 14: SSBO
        bindings.append(QRhiShaderResourceBinding::bufferLoadStore(14, QRhiShaderResourceBinding::ComputeStage,
                                                                   m_particleSsbo.get()));
        m_computeSrb->setBindings(bindings.begin(), bindings.end());
        if (!m_computeSrb->create()) {
            qCWarning(lcOverlay) << "Failed to create compute SRB";
            return false;
        }
    }

    // Create compute pipeline
    if (!m_computePipeline) {
        m_computePipeline.reset(rhi->newComputePipeline());
        m_computePipeline->setShaderStage({QRhiShaderStage::Compute, m_computeShader});
        m_computePipeline->setShaderResourceBindings(m_computeSrb.get());
        if (!m_computePipeline->create()) {
            qCWarning(lcOverlay) << "Failed to create compute pipeline";
            m_computePipeline.reset();
            return false;
        }
    }

    return true;
}

void ZoneShaderNodeRhi::dispatchCompute(QRhiCommandBuffer* cb)
{
    if (!m_computePipeline || !m_computeSrb || !m_particleSsbo) {
        return;
    }

    QRhi* rhi =
        (m_itemValid.load(std::memory_order_acquire) && m_item && m_item->window()) ? m_item->window()->rhi() : nullptr;
    if (!rhi) {
        return;
    }

    // Initialize SSBO with random particle data on first frame
    if (m_particleSsboNeedsInit) {
        QVector<ParticleData> initData(m_particleCount);
        for (int i = 0; i < m_particleCount; ++i) {
            auto& p = initData[i];
            float seed = static_cast<float>(i) / static_cast<float>(m_particleCount);
            p.pos[0] = std::fmod(seed * 127.1f, 1.0f);
            p.pos[1] = std::fmod(seed * 311.7f, 1.0f);
            p.vel[0] = 0.0f;
            p.vel[1] = 0.0f;
            p.life = 0.0f; // Start dead, compute shader will respawn
            p.age = 0.0f;
            p.seed = seed;
            p.size = 0.005f;
            p.color[0] = 1.0f;
            p.color[1] = 1.0f;
            p.color[2] = 1.0f;
            p.color[3] = 1.0f;
        }
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        batch->uploadStaticBuffer(m_particleSsbo.get(), 0, m_particleCount * sizeof(ParticleData),
                                  initData.constData());
        cb->resourceUpdate(batch);
        m_particleSsboNeedsInit = false;
    }

    cb->beginComputePass();
    cb->setComputePipeline(m_computePipeline.get());
    cb->setShaderResources(m_computeSrb.get());
    // 64 threads per workgroup
    int groups = (m_particleCount + 63) / 64;
    cb->dispatch(groups, 1, 1);
    cb->endComputePass();
}

void ZoneShaderNodeRhi::appendParticleTextureBinding(QVector<QRhiShaderResourceBinding>& bindings) const
{
    if (m_particleTexture && m_particleSampler) {
        bindings.append(QRhiShaderResourceBinding::sampledTexture(13, QRhiShaderResourceBinding::FragmentStage,
                                                                  m_particleTexture.get(), m_particleSampler.get()));
    }
}

} // namespace PlasmaZones
