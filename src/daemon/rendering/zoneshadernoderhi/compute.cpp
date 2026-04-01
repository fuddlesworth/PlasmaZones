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
#include <random>

namespace PlasmaZones {

// Shared particle initialization — used by both GPU (SSBO upload) and CPU fallback paths
static void initParticle(ParticleData& p, int index, int total)
{
    const float seed = static_cast<float>(index) / static_cast<float>(total);
    p.pos[0] = std::fmod(seed * 127.1f, 1.0f);
    p.pos[1] = std::fmod(seed * 311.7f, 1.0f);
    p.vel[0] = 0.0f;
    p.vel[1] = 0.0f;
    p.life = 0.0f; // Start dead, respawned by compute/CPU sim
    p.age = 0.0f;
    p.seed = seed;
    p.size = 0.005f;
    p.color[0] = 1.0f;
    p.color[1] = 1.0f;
    p.color[2] = 1.0f;
    p.color[3] = 1.0f;
}

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

    // Compute requires GLSL 430+ / GLES 310+. Include both SPIR-V versions:
    // - 1.0 (version 100): required by Qt 6.11's QRhi Vulkan backend for pipeline lookup
    // - 1.3 (version 130): provides full compute capabilities (imageStore, std430 SSBOs)
    // QShaderBaker generates both; the driver should use the highest compatible version.
    static const QList<QShaderBaker::GeneratedShader> computeTargets = {
        {QShader::SpirvShader, QShaderVersion(100)},
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

bool ZoneShaderNodeRhi::ensureParticleTexture(QRhi* rhi)
{
    const int w = qMax(1, static_cast<int>(m_width));
    const int h = qMax(1, static_cast<int>(m_height));
    const QSize texSize(w, h);

    // For GPU compute, texture needs UsedWithLoadStore + RenderTarget. The RenderTarget flag
    // adds VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT which NVIDIA's Vulkan driver requires for
    // correct barrier handling when a storage image (compute imageStore) also transitions to
    // SHADER_READ_ONLY layout for fragment sampling. For CPU fallback, plain RGBA8 suffices.
    const QRhiTexture::Flags texFlags =
        m_computeSupported ? (QRhiTexture::UsedWithLoadStore | QRhiTexture::RenderTarget) : QRhiTexture::Flags{};

    if (!m_particleTexture || m_particleTexture->pixelSize() != texSize) {
        m_particleTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, texSize, 1, texFlags));
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
    return true;
}

bool ZoneShaderNodeRhi::ensureComputePipeline()
{
    if (!m_computeSupported || !m_computeShaderReady || m_particleCount <= 0) {
        return false;
    }

    QRhi* rhi = safeRhi();
    if (!rhi) {
        return false;
    }

    if (!ensureParticleTexture(rhi)) {
        return false;
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
        // NOTE: common.glsl declares binding 1 (uZoneLabels) and audio.glsl declares
        // binding 6 (uAudioSpectrum), but the SPIR-V compiler strips unused bindings.
        // Only bind what the compute shader actually uses.
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

    // Create compute pipeline (one attempt only — if it fails, disable compute)
    if (!m_computePipeline) {
        auto* pipeline = rhi->newComputePipeline();
        if (!pipeline) {
            qCWarning(lcOverlay) << "RHI does not support compute pipelines (backend:" << rhi->backendName() << ")";
            m_computeSupported = false;
            return false;
        }
        m_computePipeline.reset(pipeline);
        m_computePipeline->setShaderStage({QRhiShaderStage::Compute, m_computeShader});
        m_computePipeline->setShaderResourceBindings(m_computeSrb.get());
        if (!m_computePipeline->create()) {
            qCWarning(lcOverlay) << "Failed to create compute pipeline — disabling compute"
                                 << "(backend:" << rhi->backendName() << ")";
            m_computePipeline.reset();
            m_computeSupported = false;
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

    QRhi* rhi = safeRhi();
    if (!rhi) {
        return;
    }

    // Initialize SSBO with random particle data on first frame.
    // Pass the upload batch to beginComputePass() instead of standalone cb->resourceUpdate()
    // so QRhi applies it with correct Vulkan pipeline barriers at pass start.
    QRhiResourceUpdateBatch* initBatch = nullptr;
    if (m_particleSsboNeedsInit) {
        QVector<ParticleData> initData(m_particleCount);
        for (int i = 0; i < m_particleCount; ++i) {
            initParticle(initData[i], i, m_particleCount);
        }
        initBatch = rhi->nextResourceUpdateBatch();
        initBatch->uploadStaticBuffer(m_particleSsbo.get(), 0, m_particleCount * sizeof(ParticleData),
                                      initData.constData());
        m_particleSsboNeedsInit = false;
    }

    cb->beginComputePass(initBatch);
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

// ============================================================================
// CPU particle fallback — runs when GPU compute is unavailable (OpenGL backend)
// ============================================================================

void ZoneShaderNodeRhi::updateCpuParticles()
{
    // Cap particle count on CPU for performance (GPU can handle more)
    static constexpr int CpuMaxParticles = 4096;
    const int count = qMin(m_particleCount, CpuMaxParticles);
    if (count <= 0) {
        return;
    }

    // Initialize particles on first call or if count changed
    if (m_cpuParticles.size() != count) {
        m_cpuParticles.resize(count);
        for (int i = 0; i < count; ++i) {
            initParticle(m_cpuParticles[i], i, count);
        }
    }

    // Read parameters from uniforms (same layout as compute shader)
    const float spawnRate = m_uniforms.customParams[0][0]; // customParams[0].x
    const float lifetime = qMax(0.1f, m_uniforms.customParams[0][1]); // customParams[0].y
    const float gravity = m_uniforms.customParams[0][2]; // customParams[0].z
    const float turbulence = m_uniforms.customParams[0][3]; // customParams[0].w
    const float particleSize = qMax(0.001f, m_uniforms.customParams[1][0]); // customParams[1].x
    const float driftSpeed = m_uniforms.customParams[1][3]; // customParams[1].w

    // Aurora colors from customColors[1..3]
    const float* auroraColor1 = m_uniforms.customColors[1];
    const float* auroraColor2 = m_uniforms.customColors[2];
    const float* auroraColor3 = m_uniforms.customColors[3];

    const float dt = m_timeDelta;
    const float time = m_time;

    // Simple hash function for deterministic pseudo-randomness per particle per frame
    auto pseudoRandom = [](float seed, float offset) -> float {
        const float v = std::sin(seed * 12.9898f + offset * 78.233f) * 43758.5453f;
        return v - std::floor(v);
    };

    // Physics update
    for (int i = 0; i < count; ++i) {
        auto& p = m_cpuParticles[i];

        if (p.life <= 0.0f) {
            // Respawn check: stochastic based on spawn rate
            const float respawnChance = spawnRate * dt;
            if (pseudoRandom(p.seed, time) < respawnChance) {
                p.pos[0] = pseudoRandom(p.seed, time * 1.1f + 0.1f);
                p.pos[1] = pseudoRandom(p.seed, time * 1.3f + 0.2f);
                p.vel[0] = (pseudoRandom(p.seed, time * 2.1f) - 0.5f) * 0.02f;
                p.vel[1] = (pseudoRandom(p.seed, time * 2.7f) - 0.5f) * 0.02f;
                p.life = 1.0f;
                p.age = 0.0f;
                p.size = particleSize * (0.5f + pseudoRandom(p.seed, time * 3.0f));
                // Pick aurora color based on seed
                const float colorPick = pseudoRandom(p.seed, 42.0f);
                const float* srcColor =
                    colorPick < 0.33f ? auroraColor1 : (colorPick < 0.66f ? auroraColor2 : auroraColor3);
                p.color[0] = srcColor[0];
                p.color[1] = srcColor[1];
                p.color[2] = srcColor[2];
                p.color[3] = 1.0f;
            }
            continue;
        }

        // Apply gravity (downward)
        p.vel[1] += gravity * dt;

        // Apply turbulence
        const float turbX = std::sin(time * 3.0f + p.seed * 17.0f) * turbulence * dt;
        const float turbY = std::cos(time * 2.7f + p.seed * 23.0f) * turbulence * dt;
        p.vel[0] += turbX;
        p.vel[1] += turbY;

        // Apply drift
        p.vel[0] += driftSpeed * dt * std::sin(time + p.seed * 6.28f);

        // Damping
        const float damping = 0.98f;
        p.vel[0] *= damping;
        p.vel[1] *= damping;

        // Integrate position
        p.pos[0] += p.vel[0] * dt;
        p.pos[1] += p.vel[1] * dt;

        // Age and kill
        p.age += dt;
        p.life = qMax(0.0f, 1.0f - p.age / lifetime);

        // Fade alpha with life
        p.color[3] = p.life;
    }

    // Render particles into QImage
    const int w = qMax(1, static_cast<int>(m_width));
    const int h = qMax(1, static_cast<int>(m_height));
    if (m_cpuParticleImage.size() != QSize(w, h)) {
        m_cpuParticleImage = QImage(w, h, QImage::Format_RGBA8888_Premultiplied);
    }
    m_cpuParticleImage.fill(0);

    uchar* bits = m_cpuParticleImage.bits();
    const int stride = m_cpuParticleImage.bytesPerLine();

    // Maximum pixel radius to keep inner loop bounded
    static constexpr int MaxPixelRadius = 16;

    for (int i = 0; i < count; ++i) {
        const auto& p = m_cpuParticles[i];
        if (p.life <= 0.0f) {
            continue;
        }

        // Skip particles that drifted far outside the visible area to avoid
        // integer overflow when converting normalized coords to pixel coords
        if (p.pos[0] < -1.0f || p.pos[0] > 2.0f || p.pos[1] < -1.0f || p.pos[1] > 2.0f) {
            continue;
        }

        const int cx = static_cast<int>(p.pos[0] * static_cast<float>(w));
        const int cy = static_cast<int>(p.pos[1] * static_cast<float>(h));
        const int radius = qBound(1, static_cast<int>(p.size * static_cast<float>(w)), MaxPixelRadius);

        // Premultiplied color components (0-255)
        const float alpha = p.color[3];
        const int pr = static_cast<int>(qBound(0.0f, p.color[0] * alpha * 255.0f, 255.0f));
        const int pg = static_cast<int>(qBound(0.0f, p.color[1] * alpha * 255.0f, 255.0f));
        const int pb = static_cast<int>(qBound(0.0f, p.color[2] * alpha * 255.0f, 255.0f));
        const int pa = static_cast<int>(qBound(0.0f, alpha * 255.0f, 255.0f));

        const float invR2 = 1.0f / static_cast<float>(radius * radius);

        const int yMin = qMax(0, cy - radius);
        const int yMax = qMin(h - 1, cy + radius);
        const int xMin = qMax(0, cx - radius);
        const int xMax = qMin(w - 1, cx + radius);

        for (int py = yMin; py <= yMax; ++py) {
            uchar* row = bits + py * stride;
            const int dy = py - cy;
            const float dy2 = static_cast<float>(dy * dy);
            for (int px = xMin; px <= xMax; ++px) {
                const int dx = px - cx;
                const float dist2 = static_cast<float>(dx * dx) + dy2;
                // Gaussian-ish falloff: exp(-3 * dist2/r2) approximated as linear for speed
                const float t = 1.0f - dist2 * invR2;
                if (t <= 0.0f) {
                    continue;
                }
                // Soft falloff (quadratic)
                const float intensity = t * t;
                const int idx = px * 4;
                // Additive blending (clamped)
                const int sr = static_cast<int>(static_cast<float>(pr) * intensity);
                const int sg = static_cast<int>(static_cast<float>(pg) * intensity);
                const int sb = static_cast<int>(static_cast<float>(pb) * intensity);
                const int sa = static_cast<int>(static_cast<float>(pa) * intensity);
                row[idx + 0] = static_cast<uchar>(qMin(255, row[idx + 0] + sr));
                row[idx + 1] = static_cast<uchar>(qMin(255, row[idx + 1] + sg));
                row[idx + 2] = static_cast<uchar>(qMin(255, row[idx + 2] + sb));
                row[idx + 3] = static_cast<uchar>(qMin(255, row[idx + 3] + sa));
            }
        }
    }
}

} // namespace PlasmaZones
