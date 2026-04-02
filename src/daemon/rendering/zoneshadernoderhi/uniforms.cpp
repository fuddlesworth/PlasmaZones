// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../zoneshadernoderhi.h"
#include "internal.h"
#include "../zoneshadercommon.h"

#include "../../../core/logging.h"

#include <QDateTime>
#include <QFileInfo>
#include <QQuickWindow>
#include <cstring>

#include <rhi/qshaderbaker.h>

namespace PlasmaZones {

// ============================================================================
// syncUniformsFromData
// ============================================================================

void ZoneShaderNodeRhi::syncUniformsFromData()
{
    m_uniforms.iTime = m_time;
    m_uniforms.iTimeDelta = m_timeDelta;
    // When feedback buffers haven't been cleared yet (new shader or re-creation),
    // override iFrame to 0 so feedback shaders can seed their initial state.
    // Shaders like ember-trace and neon-phantom check iFrame == 0 for seeding.
    m_uniforms.iFrame = (m_bufferFeedback && !m_bufferFeedbackCleared) ? 0 : m_frame;
    m_uniforms.iResolution[0] = m_width;
    m_uniforms.iResolution[1] = m_height;
    m_uniforms.iMouse[0] = static_cast<float>(m_mousePosition.x());
    m_uniforms.iMouse[1] = static_cast<float>(m_mousePosition.y());
    m_uniforms.iMouse[2] = m_width > 0 ? static_cast<float>(m_mousePosition.x() / m_width) : 0.0f;
    m_uniforms.iMouse[3] = m_height > 0 ? static_cast<float>(m_mousePosition.y() / m_height) : 0.0f;
    const QDateTime now = QDateTime::currentDateTime();
    m_uniforms.iDate[0] = static_cast<float>(now.date().year());
    m_uniforms.iDate[1] = static_cast<float>(now.date().month());
    m_uniforms.iDate[2] = static_cast<float>(now.date().day());
    m_uniforms.iDate[3] = static_cast<float>(now.time().hour() * 3600 + now.time().minute() * 60 + now.time().second()
                                             + now.time().msec() / 1000.0);
    m_uniforms.zoneCount = m_zones.size();
    int highlightedCount = 0;
    for (const auto& zone : m_zones) {
        if (zone.isHighlighted)
            ++highlightedCount;
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
    m_uniforms.customParams[RhiConstants::UniformVecIndex5][RhiConstants::ComponentX] = m_customParams5.x();
    m_uniforms.customParams[RhiConstants::UniformVecIndex5][RhiConstants::ComponentY] = m_customParams5.y();
    m_uniforms.customParams[RhiConstants::UniformVecIndex5][RhiConstants::ComponentZ] = m_customParams5.z();
    m_uniforms.customParams[RhiConstants::UniformVecIndex5][RhiConstants::ComponentW] = m_customParams5.w();
    m_uniforms.customParams[RhiConstants::UniformVecIndex6][RhiConstants::ComponentX] = m_customParams6.x();
    m_uniforms.customParams[RhiConstants::UniformVecIndex6][RhiConstants::ComponentY] = m_customParams6.y();
    m_uniforms.customParams[RhiConstants::UniformVecIndex6][RhiConstants::ComponentZ] = m_customParams6.z();
    m_uniforms.customParams[RhiConstants::UniformVecIndex6][RhiConstants::ComponentW] = m_customParams6.w();
    m_uniforms.customParams[RhiConstants::UniformVecIndex7][RhiConstants::ComponentX] = m_customParams7.x();
    m_uniforms.customParams[RhiConstants::UniformVecIndex7][RhiConstants::ComponentY] = m_customParams7.y();
    m_uniforms.customParams[RhiConstants::UniformVecIndex7][RhiConstants::ComponentZ] = m_customParams7.z();
    m_uniforms.customParams[RhiConstants::UniformVecIndex7][RhiConstants::ComponentW] = m_customParams7.w();
    m_uniforms.customParams[RhiConstants::UniformVecIndex8][RhiConstants::ComponentX] = m_customParams8.x();
    m_uniforms.customParams[RhiConstants::UniformVecIndex8][RhiConstants::ComponentY] = m_customParams8.y();
    m_uniforms.customParams[RhiConstants::UniformVecIndex8][RhiConstants::ComponentZ] = m_customParams8.z();
    m_uniforms.customParams[RhiConstants::UniformVecIndex8][RhiConstants::ComponentW] = m_customParams8.w();

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
    setColor(8, m_customColor9);
    setColor(9, m_customColor10);
    setColor(10, m_customColor11);
    setColor(11, m_customColor12);
    setColor(12, m_customColor13);
    setColor(13, m_customColor14);
    setColor(14, m_customColor15);
    setColor(15, m_customColor16);

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

    // iChannelResolution (std140: vec2[4], each element 16 bytes). Per-channel size for UV scaling.
    // Unbound/dummy channels use (1,1); others use actual buffer size.
    const bool multiBufferMode = m_bufferPaths.size() > 1;
    const int numChannels =
        multiBufferMode ? qMin(m_bufferPaths.size(), 4) : (m_bufferShaderReady && m_bufferTexture ? 1 : 0);
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
                m_uniforms.iChannelResolution[i][0] = 1.0f; // dummy 1x1
                m_uniforms.iChannelResolution[i][1] = 1.0f;
            }
        } else {
            m_uniforms.iChannelResolution[i][0] = 1.0f; // dummy 1x1 for unbound channels
            m_uniforms.iChannelResolution[i][1] = 1.0f;
        }
        m_uniforms.iChannelResolution[i][2] = 0.0f;
        m_uniforms.iChannelResolution[i][3] = 0.0f;
    }
    m_uniforms.iAudioSpectrumSize = m_audioSpectrum.size();

    // iFlipBufferY set in uploadDirtyTextures() where rhi is available
    m_uniforms._pad_after_audioSpectrum[0] = 0;
    m_uniforms._pad_after_audioSpectrum[1] = 0;

    // User texture resolutions (bindings 7-10)
    for (int i = 0; i < 4; ++i) {
        if (i < kMaxUserTextures && m_userTextures[i] && !m_userTextureImages[i].isNull()) {
            m_uniforms.iTextureResolution[i][0] = static_cast<float>(m_userTextureImages[i].width());
            m_uniforms.iTextureResolution[i][1] = static_cast<float>(m_userTextureImages[i].height());
        } else {
            m_uniforms.iTextureResolution[i][0] = 1.0f;
            m_uniforms.iTextureResolution[i][1] = 1.0f;
        }
        m_uniforms.iTextureResolution[i][2] = 0.0f;
        m_uniforms.iTextureResolution[i][3] = 0.0f;
    }
}

// ============================================================================
// uploadDirtyTextures — extracted from prepare() tail (labels, uniforms, audio, user textures)
// ============================================================================

void ZoneShaderNodeRhi::uploadDirtyTextures(QRhi* rhi, QRhiCommandBuffer* cb)
{
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
            // Labels texture is bound in ALL SRBs (image, buffer, multi-buffer).
            // Resetting only m_srb leaves buffer SRBs with a dangling pointer
            // to the old labels texture — crashes NVIDIA Vulkan in endFrame().
            resetAllBindingsAndPipelines();
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
        // Buffer textures rendered via FBO always need Y-flip when sampling.
        // On OpenGL, FBOs are Y-up (row 0 at bottom). On Vulkan, Qt RHI applies
        // a negative-height viewport which reverses the rasterization Y, but the
        // texture data is still stored with row 0 at top in GPU memory — the
        // viewport flip means the BOTTOM of the screen is written to the LAST
        // row, so texture UV must be Y-flipped to read the correct screen position.
        // NOTE: If a future RHI backend (e.g. Metal, Direct3D) does not need this
        // flip, this must become conditional again (check rhi->isYUpInFramebuffer()
        // and backend-specific viewport behavior).
        m_uniforms.iFlipBufferY = 1;
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        if (batch) {
            if (!m_didFullUploadOnce) {
                batch->updateDynamicBuffer(m_ubo.get(), 0, sizeof(ZoneShaderUniforms), &m_uniforms);
                m_didFullUploadOnce = true;
            } else {
                using namespace ZoneShaderUboRegions;
                if (m_timeDirty) {
                    batch->updateDynamicBuffer(m_ubo.get(), K_TIME_BLOCK_OFFSET, K_TIME_BLOCK_SIZE,
                                               static_cast<const char*>(static_cast<const void*>(&m_uniforms))
                                                   + K_TIME_BLOCK_OFFSET);
                }
                if (m_zoneDataDirty) {
                    batch->updateDynamicBuffer(m_ubo.get(), K_SCENE_DATA_OFFSET, K_SCENE_DATA_SIZE,
                                               static_cast<const char*>(static_cast<const void*>(&m_uniforms))
                                                   + K_SCENE_DATA_OFFSET);
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

    // Audio spectrum texture: resize if needed, upload when dirty
    if (m_audioSpectrumDirty && m_audioSpectrumTexture && m_audioSpectrumSampler) {
        m_audioSpectrumDirty = false;
        const int bars = m_audioSpectrum.size();
        const QSize targetSize = bars > 0 ? QSize(bars, 1) : QSize(1, 1);
        if (m_audioSpectrumTexture->pixelSize() != targetSize) {
            m_audioSpectrumTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, targetSize));
            if (!m_audioSpectrumTexture->create()) {
                return;
            }
            // Audio spectrum texture is bound at binding 6 in ALL SRBs (image,
            // buffer, multi-buffer). Resetting only image-pass SRBs leaves buffer
            // SRBs with a dangling pointer to the old texture — crashes NVIDIA
            // Vulkan driver when the buffer pass is recorded.
            resetAllBindingsAndPipelines();
            if (!ensurePipeline()) {
                return;
            }
        }
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        if (batch && bars > 0) {
            QImage img(bars, 1, QImage::Format_RGBA8888);
            for (int i = 0; i < bars; ++i) {
                const float v = qBound(0.0f, m_audioSpectrum[i], 1.0f);
                const quint8 u = static_cast<quint8>(qRound(v * 255.0f));
                img.setPixel(i, 0, qRgba(u, 0, 0, 255));
            }
            batch->uploadTexture(m_audioSpectrumTexture.get(), img);
            cb->resourceUpdate(batch);
        } else if (batch && bars == 0) {
            QImage onePixel(1, 1, QImage::Format_RGBA8888);
            onePixel.fill(0);
            batch->uploadTexture(m_audioSpectrumTexture.get(), onePixel);
            cb->resourceUpdate(batch);
        }
    }

    // User texture upload and sampler management (bindings 7-10)
    for (int i = 0; i < kMaxUserTextures; ++i) {
        // Recreate sampler if reset (e.g. wrap mode change)
        if (m_userTextures[i] && !m_userTextureSamplers[i]) {
            const QRhiSampler::AddressMode addr =
                (m_userTextureWraps[i] == QLatin1String("repeat")) ? QRhiSampler::Repeat : QRhiSampler::ClampToEdge;
            m_userTextureSamplers[i].reset(
                rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, addr, addr));
            if (!m_userTextureSamplers[i]->create()) {
                continue;
            }
            resetAllBindingsAndPipelines();
        }
        if (!m_userTextureDirty[i] || !m_userTextures[i] || !m_userTextureSamplers[i]) {
            continue;
        }
        m_userTextureDirty[i] = false;
        const QImage& img = m_userTextureImages[i];
        const QSize targetSize = (!img.isNull() && img.width() > 0 && img.height() > 0) ? img.size() : QSize(1, 1);
        if (m_userTextures[i]->pixelSize() != targetSize) {
            m_userTextures[i].reset(rhi->newTexture(QRhiTexture::RGBA8, targetSize));
            if (!m_userTextures[i]->create()) {
                continue;
            }
            resetAllBindingsAndPipelines();
            if (!ensurePipeline()) {
                return;
            }
        }
        QRhiResourceUpdateBatch* ubatch = rhi->nextResourceUpdateBatch();
        if (ubatch) {
            if (!img.isNull() && img.width() > 0 && img.height() > 0) {
                ubatch->uploadTexture(m_userTextures[i].get(), img);
            } else {
                QImage onePixel(1, 1, QImage::Format_RGBA8888);
                onePixel.fill(Qt::transparent);
                ubatch->uploadTexture(m_userTextures[i].get(), onePixel);
            }
            cb->resourceUpdate(ubatch);
        }
    }

    // Desktop wallpaper texture upload (binding 11)
    if (m_wallpaperDirty && m_wallpaperTexture && m_wallpaperSampler) {
        m_wallpaperDirty = false;
        const QImage& img = m_wallpaperImage;
        const QSize targetSize = (!img.isNull() && img.width() > 0 && img.height() > 0) ? img.size() : QSize(1, 1);
        if (m_wallpaperTexture->pixelSize() != targetSize) {
            m_wallpaperTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, targetSize));
            if (!m_wallpaperTexture->create()) {
                m_wallpaperTexture.reset(); // Prevent binding a non-created texture
                return;
            }
            resetAllBindingsAndPipelines();
            if (!ensurePipeline()) {
                return;
            }
        }
        QRhiResourceUpdateBatch* ubatch = rhi->nextResourceUpdateBatch();
        if (ubatch) {
            if (!img.isNull() && img.width() > 0 && img.height() > 0) {
                ubatch->uploadTexture(m_wallpaperTexture.get(), img);
            } else {
                QImage onePixel(1, 1, QImage::Format_RGBA8888);
                onePixel.fill(Qt::transparent);
                ubatch->uploadTexture(m_wallpaperTexture.get(), onePixel);
            }
            cb->resourceUpdate(ubatch);
        }
    }
}

// ============================================================================
// releaseRhiResources
// ============================================================================

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
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        m_bufferSamplers[i].reset();
    }
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
    m_audioSpectrumTexture.reset();
    m_audioSpectrumSampler.reset();
    for (int i = 0; i < kMaxUserTextures; ++i) {
        m_userTextures[i].reset();
        m_userTextureSamplers[i].reset();
        m_userTextureDirty[i] = true;
    }
    m_wallpaperTexture.reset();
    m_wallpaperSampler.reset();
    m_wallpaperDirty = true;
    m_depthTexture.reset();
    m_depthSampler.reset();
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
    m_audioSpectrumDirty = true;
    // Next prepare() will re-create all RHI resources and do a full UBO upload
}

// ============================================================================
// bakeBufferShaders — multi-pass buffer shader compilation
// ============================================================================

void ZoneShaderNodeRhi::bakeBufferShaders()
{
    const bool multipass = !m_bufferPath.isEmpty();
    const bool multiBufferMode = m_bufferPaths.size() > 1;

    if (multipass && multiBufferMode && m_multiBufferShaderDirty) {
        m_multiBufferShaderDirty = false;
        m_multiBufferShadersReady = false;
        for (int i = 0; i < kMaxBufferPasses; ++i) {
            m_multiBufferFragmentShaderSources[i].clear();
            m_multiBufferFragmentShaders[i] = QShader();
        }
        const QList<QShaderBaker::GeneratedShader>& targets = detail::bakeTargets();
        bool allOk = true;
        for (int i = 0; i < m_bufferPaths.size() && i < kMaxBufferPasses; ++i) {
            const QString& path = m_bufferPaths.at(i);
            if (!QFileInfo::exists(path)) {
                allOk = false;
                break;
            }
            QString err;
            QString src = detail::loadAndExpandShader(path, &err);
            if (src.isEmpty()) {
                allOk = false;
                break;
            }
            m_multiBufferFragmentShaderSources[i] = src;
            m_multiBufferMtimes[i] = QFileInfo(path).lastModified().toMSecsSinceEpoch();
            QShaderBaker fragmentBaker;
            fragmentBaker.setGeneratedShaderVariants({QShader::StandardShader});
            fragmentBaker.setGeneratedShaders(targets);
            fragmentBaker.setSourceString(src.toUtf8(), QShader::FragmentStage);
            m_multiBufferFragmentShaders[i] = fragmentBaker.bake();
            if (!m_multiBufferFragmentShaders[i].isValid()) {
                qCWarning(lcOverlay) << "Multi-buffer shader" << i << "compile failed, path=" << path
                                     << "error=" << fragmentBaker.errorMessage();
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
            if (++m_multiBufferShaderRetries < 3) {
                m_multiBufferShaderDirty = true; // Retry next frame
            } else {
                qCWarning(lcOverlay)
                    << "Multi-buffer shader compilation failed after 3 attempts; giving up until shader path changes";
            }
        }
    }
    if (multipass && !multiBufferMode && m_bufferShaderDirty) {
        m_bufferShaderDirty = false;
        m_bufferShaderReady = false;
        if (m_bufferFragmentShaderSource.isEmpty()) {
            if (QFileInfo::exists(m_bufferPath)) {
                QString err;
                m_bufferFragmentShaderSource = detail::loadAndExpandShader(m_bufferPath, &err);
                if (!m_bufferFragmentShaderSource.isEmpty()) {
                    m_bufferMtime = QFileInfo(m_bufferPath).lastModified().toMSecsSinceEpoch();
                }
            }
        }
        if (!m_bufferFragmentShaderSource.isEmpty()) {
            const QList<QShaderBaker::GeneratedShader>& targets = detail::bakeTargets();
            QShaderBaker fragmentBaker;
            fragmentBaker.setGeneratedShaderVariants({QShader::StandardShader});
            fragmentBaker.setGeneratedShaders(targets);
            fragmentBaker.setSourceString(m_bufferFragmentShaderSource.toUtf8(), QShader::FragmentStage);
            m_bufferFragmentShader = fragmentBaker.bake();
            if (m_bufferFragmentShader.isValid()) {
                m_bufferShaderReady = true;
                m_bufferPipeline.reset();
                m_bufferSrb.reset();
                m_bufferSrbB.reset();
            } else {
                qCWarning(lcOverlay) << "Buffer shader: compile failed, path=" << m_bufferPath
                                     << "error=" << fragmentBaker.errorMessage();
                if (++m_bufferShaderRetries < 3) {
                    m_bufferShaderDirty = true; // Retry next frame
                } else {
                    qCWarning(lcOverlay)
                        << "Buffer shader compilation failed after 3 attempts; giving up until shader path changes";
                }
            }
        }
    }
}

} // namespace PlasmaZones
