// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "internal.h"

#include <PhosphorRendering/ShaderCompiler.h>

#include <QDateTime>
#include <QFileInfo>
#include <QQuickWindow>
#include <cmath>
#include <cstring>

#include <rhi/qshaderbaker.h>

namespace PhosphorRendering {

// ============================================================================
// syncBaseUniforms — only writes BaseUniforms fields (NOT extension data)
// ============================================================================

void ShaderNodeRhi::syncBaseUniforms()
{
    // Split full-precision m_time (double) into iTime (wrapped lo) + iTimeHi (wrap offset)
    m_baseUniforms.iTime = static_cast<float>(m_time - static_cast<double>(m_timeHi));
    m_baseUniforms.iTimeHi = m_timeHi;
    m_baseUniforms.iTimeDelta = m_timeDelta;
    // When feedback buffers haven't been cleared yet, override iFrame to 0
    m_baseUniforms.iFrame = (m_bufferFeedback && !m_bufferFeedbackCleared) ? 0 : m_frame;
    m_baseUniforms.iResolution[0] = m_width;
    m_baseUniforms.iResolution[1] = m_height;
    m_baseUniforms.iMouse[0] = static_cast<float>(m_mousePosition.x());
    m_baseUniforms.iMouse[1] = static_cast<float>(m_mousePosition.y());
    m_baseUniforms.iMouse[2] = m_width > 0 ? static_cast<float>(m_mousePosition.x() / m_width) : 0.0f;
    m_baseUniforms.iMouse[3] = m_height > 0 ? static_cast<float>(m_mousePosition.y() / m_height) : 0.0f;
    // iDate only advances once per second. m_sceneDataDirty is set by every
    // mouse-move/resize event, so naïvely recomputing iDate whenever it's
    // true would hit QDateTime::currentDateTime() at 60+ Hz during
    // interaction. Guard with a 1-second cached timestamp — iDate still
    // refreshes during idle (sceneDataDirty remains set for the first frame
    // of each redraw cycle), but we skip ~60 redundant calls per second.
    if (!m_didFullUploadOnce
        || (m_sceneDataDirty
            && (m_lastDateRefreshMs == 0 || (QDateTime::currentMSecsSinceEpoch() - m_lastDateRefreshMs) >= 1000))) {
        const QDateTime now = QDateTime::currentDateTime();
        m_lastDateRefreshMs = now.toMSecsSinceEpoch();
        m_baseUniforms.iDate[0] = static_cast<float>(now.date().year());
        m_baseUniforms.iDate[1] = static_cast<float>(now.date().month());
        m_baseUniforms.iDate[2] = static_cast<float>(now.date().day());
        m_baseUniforms.iDate[3] = static_cast<float>(now.time().hour() * 3600 + now.time().minute() * 60
                                                     + now.time().second() + now.time().msec() / 1000.0);
    }

    // appField0/appField1: left as-is (set by setAppField0/1 or extension)

    // Custom params
    for (int i = 0; i < kMaxCustomParams; ++i) {
        m_baseUniforms.customParams[i][RhiConstants::ComponentX] = m_customParams[i].x();
        m_baseUniforms.customParams[i][RhiConstants::ComponentY] = m_customParams[i].y();
        m_baseUniforms.customParams[i][RhiConstants::ComponentZ] = m_customParams[i].z();
        m_baseUniforms.customParams[i][RhiConstants::ComponentW] = m_customParams[i].w();
    }

    // Custom colors
    for (int i = 0; i < kMaxCustomColors; ++i) {
        m_baseUniforms.customColors[i][0] = static_cast<float>(m_customColors[i].redF());
        m_baseUniforms.customColors[i][1] = static_cast<float>(m_customColors[i].greenF());
        m_baseUniforms.customColors[i][2] = static_cast<float>(m_customColors[i].blueF());
        m_baseUniforms.customColors[i][3] = static_cast<float>(m_customColors[i].alphaF());
    }

    // iChannelResolution
    const bool multiBufferMode = m_bufferPaths.size() > 1;
    const int numChannels = multiBufferMode ? qMin(m_bufferPaths.size(), static_cast<qsizetype>(kMaxBufferPasses))
                                            : (m_bufferShaderReady && m_bufferTexture ? 1 : 0);
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        if (i < numChannels) {
            if (multiBufferMode && m_multiBufferTextures[i]) {
                QSize ps = m_multiBufferTextures[i]->pixelSize();
                m_baseUniforms.iChannelResolution[i][0] = static_cast<float>(ps.width());
                m_baseUniforms.iChannelResolution[i][1] = static_cast<float>(ps.height());
            } else if (!multiBufferMode && i == 0 && m_bufferTexture) {
                QSize ps = m_bufferTexture->pixelSize();
                m_baseUniforms.iChannelResolution[0][0] = static_cast<float>(ps.width());
                m_baseUniforms.iChannelResolution[0][1] = static_cast<float>(ps.height());
            } else {
                m_baseUniforms.iChannelResolution[i][0] = 1.0f;
                m_baseUniforms.iChannelResolution[i][1] = 1.0f;
            }
        } else {
            m_baseUniforms.iChannelResolution[i][0] = 1.0f;
            m_baseUniforms.iChannelResolution[i][1] = 1.0f;
        }
        m_baseUniforms.iChannelResolution[i][2] = 0.0f;
        m_baseUniforms.iChannelResolution[i][3] = 0.0f;
    }
    m_baseUniforms.iAudioSpectrumSize = m_audioSpectrum.size();

    // iFlipBufferY set in uploadDirtyTextures() where rhi is available
    m_baseUniforms._pad_after_audioSpectrum[0] = 0;
    m_baseUniforms._pad_after_audioSpectrum[1] = 0;

    // User texture resolutions (bindings 7-10)
    for (int i = 0; i < kMaxUserTextures; ++i) {
        if (m_userTextures[i] && !m_userTextureImages[i].isNull()) {
            m_baseUniforms.iTextureResolution[i][0] = static_cast<float>(m_userTextureImages[i].width());
            m_baseUniforms.iTextureResolution[i][1] = static_cast<float>(m_userTextureImages[i].height());
        } else {
            m_baseUniforms.iTextureResolution[i][0] = 1.0f;
            m_baseUniforms.iTextureResolution[i][1] = 1.0f;
        }
        m_baseUniforms.iTextureResolution[i][2] = 0.0f;
        m_baseUniforms.iTextureResolution[i][3] = 0.0f;
    }
}

// ============================================================================
// uploadExtensionToUbo — centralized extension upload (DRY)
// ============================================================================
//
// Invariants:
//   - Caller checked m_uniformExtension && extensionSize() > 0.
//   - m_extensionStaging is sized to extensionSize(); we don't re-allocate
//     when called repeatedly for the same extension.
//   - Clears the extension's dirty bit and our m_extensionDirty mirror.
void ShaderNodeRhi::uploadExtensionToUbo(QRhiResourceUpdateBatch* batch)
{
    const int extSize = m_uniformExtension->extensionSize();
    if (m_extensionStaging.size() != extSize) {
        m_extensionStaging.resize(extSize);
    }
    m_uniformExtension->write(m_extensionStaging.data(), 0);
    const int extOffset = static_cast<int>(sizeof(PhosphorShaders::BaseUniforms));
    batch->updateDynamicBuffer(m_ubo.get(), extOffset, extSize, m_extensionStaging.constData());
    m_uniformExtension->clearDirty();
    m_extensionDirty = false;
}

// ============================================================================
// uploadDirtyTextures
// ============================================================================
//
// Ordering contract (load-bearing — do not reorder prepare()'s calls):
//   prepare() ──▶ bakeBufferShaders()
//              ──▶ uploadDirtyTextures()         ← this function
//              ──▶ ensureBufferTarget()
//              ──▶ ensureBufferPipeline()        ← restores buffer pipelines
//              ──▶ ensurePipeline()              ← restores image pipeline
//              ──▶ (multipass draws recorded)
//
// When a texture is resized here we call resetAllBindingsAndPipelines() +
// ensurePipeline() inline, but ensurePipeline() only restores the IMAGE-pass
// pipeline. Buffer-pass pipelines are restored by ensureBufferPipeline() which
// runs after us in prepare() — swapping those two calls would leave the
// multipass SRBs/pipelines null when the draws record below, breaking rendering
// with no compiler error. The inline restoration is just a safety net for the
// image pass; the full prepare() sequence does the real work.
//
// Dirty-flag invariants (who sets what):
//   m_timeDirty       ← setTime, setTimeDelta, setFrame, setBufferFeedback
//                        (toggle), prepare() on feedback-buffer clear
//   m_timeHiDirty     ← setTime (wrap-offset crossing)
//   m_sceneDataDirty  ← setResolution, setMousePosition, setCustomParams,
//                        setCustomColor, setAudioSpectrum, setUserTexture
//   m_appFieldsDirty  ← setAppField0, setAppField1
//   m_extensionDirty  ← tracked via m_uniformExtension->isDirty() (set by the
//                        extension's own updateFromX() methods)
//   m_uniformsDirty   ← mirror: true if any of the five above are true
// A setter that forgets to update m_uniformsDirty will correctly dirty its
// region but skip the upload pass entirely — keep the mirror in sync.
void ShaderNodeRhi::uploadDirtyTextures(QRhi* rhi, QRhiCommandBuffer* cb)
{
    using namespace PhosphorShaders::UboRegions;

    const bool extensionHasData = m_uniformExtension && m_uniformExtension->extensionSize() > 0;

    if (m_uniformsDirty) {
        syncBaseUniforms();
        m_baseUniforms.iFlipBufferY = 1;
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        if (batch) {
            if (!m_didFullUploadOnce) {
                // Upload base uniforms
                batch->updateDynamicBuffer(m_ubo.get(), 0, sizeof(PhosphorShaders::BaseUniforms), &m_baseUniforms);
                // Upload extension data if present
                if (extensionHasData) {
                    uploadExtensionToUbo(batch);
                }
                m_didFullUploadOnce = true;
            } else {
                if (m_timeDirty) {
                    batch->updateDynamicBuffer(m_ubo.get(), K_TIME_BLOCK_OFFSET, K_TIME_BLOCK_SIZE,
                                               static_cast<const char*>(static_cast<const void*>(&m_baseUniforms))
                                                   + K_TIME_BLOCK_OFFSET);
                }
                if (m_timeHiDirty) {
                    batch->updateDynamicBuffer(m_ubo.get(), K_TIME_HI_OFFSET, K_TIME_HI_SIZE,
                                               static_cast<const char*>(static_cast<const void*>(&m_baseUniforms))
                                                   + K_TIME_HI_OFFSET);
                }
                if (m_sceneDataDirty) {
                    // Scene header: iResolution through end of BaseUniforms
                    // (subsumes the appFields region — no need for a separate upload).
                    batch->updateDynamicBuffer(m_ubo.get(), K_SCENE_HEADER_OFFSET, K_SCENE_HEADER_SIZE,
                                               static_cast<const char*>(static_cast<const void*>(&m_baseUniforms))
                                                   + K_SCENE_HEADER_OFFSET);
                } else if (m_appFieldsDirty) {
                    // Only appField0/appField1 changed (8 bytes at offset 88) —
                    // skip the full ~512-byte scene-header upload.
                    batch->updateDynamicBuffer(m_ubo.get(), K_APP_FIELDS_OFFSET, K_APP_FIELDS_SIZE,
                                               static_cast<const char*>(static_cast<const void*>(&m_baseUniforms))
                                                   + K_APP_FIELDS_OFFSET);
                }
                // Extension region: uploaded separately when dirty
                if (extensionHasData && m_uniformExtension->isDirty()) {
                    uploadExtensionToUbo(batch);
                }
                // Defensive: if no granular flags set, do full base upload
                if (!m_timeDirty && !m_timeHiDirty && !m_sceneDataDirty && !m_appFieldsDirty) {
                    batch->updateDynamicBuffer(m_ubo.get(), 0, sizeof(PhosphorShaders::BaseUniforms), &m_baseUniforms);
                }
            }
            if (!m_vboUploaded) {
                batch->uploadStaticBuffer(m_vbo.get(), RhiConstants::QuadVertices);
                m_vboUploaded = true;
            }
            cb->resourceUpdate(batch);
            m_timeDirty = false;
            m_timeHiDirty = false;
            m_extensionDirty = false;
            m_sceneDataDirty = false;
            m_appFieldsDirty = false;
            m_uniformsDirty = false;
        }
    } else {
        // Check extension dirty independently of base uniforms
        if (extensionHasData && m_uniformExtension->isDirty()) {
            QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
            if (batch) {
                uploadExtensionToUbo(batch);
                cb->resourceUpdate(batch);
            }
        }
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
            batch->uploadTexture(m_dummyChannelTexture.get(), m_transparentFallbackImage);
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
            batch->uploadTexture(m_audioSpectrumTexture.get(), m_transparentFallbackImage);
            cb->resourceUpdate(batch);
        }
    }

    // User texture upload (bindings 7-10)
    for (int i = 0; i < kMaxUserTextures; ++i) {
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
                ubatch->uploadTexture(m_userTextures[i].get(), m_transparentFallbackImage);
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
                m_wallpaperTexture.reset();
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
                ubatch->uploadTexture(m_wallpaperTexture.get(), m_transparentFallbackImage);
            }
            cb->resourceUpdate(ubatch);
        }
    }
}

// ============================================================================
// releaseRhiResources
// ============================================================================

void ShaderNodeRhi::releaseRhiResources()
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
    m_bufferShaderReady = false;
    m_bufferShaderDirty = true;
    m_bufferShaderRetries = 0;
    m_multiBufferShadersReady = false;
    m_multiBufferShaderDirty = true;
    m_multiBufferShaderRetries = 0;
    m_uniformsDirty = true;
    m_timeDirty = true;
    m_timeHiDirty = true;
    m_extensionDirty = true;
    m_sceneDataDirty = true;
    m_appFieldsDirty = true;
    m_audioSpectrumDirty = true;
}

// ============================================================================
// bakeBufferShaders
// ============================================================================

void ShaderNodeRhi::bakeBufferShaders()
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
            auto result = ShaderCompiler::compile(src.toUtf8(), QShader::FragmentStage);
            m_multiBufferFragmentShaders[i] = result.shader;
            if (!m_multiBufferFragmentShaders[i].isValid()) {
                qCWarning(lcShaderNode) << "Multi-buffer shader" << i << "compile failed, path=" << path
                                        << "error=" << result.error;
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
                m_multiBufferShaderDirty = true;
            } else {
                qCWarning(lcShaderNode)
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
                m_bufferFragmentShaderSource = loadAndExpandShader(m_bufferPath, &err);
                if (!m_bufferFragmentShaderSource.isEmpty()) {
                    m_bufferMtime = QFileInfo(m_bufferPath).lastModified().toMSecsSinceEpoch();
                }
            }
        }
        if (!m_bufferFragmentShaderSource.isEmpty()) {
            auto result = ShaderCompiler::compile(m_bufferFragmentShaderSource.toUtf8(), QShader::FragmentStage);
            m_bufferFragmentShader = result.shader;
            if (m_bufferFragmentShader.isValid()) {
                m_bufferShaderReady = true;
                m_bufferPipeline.reset();
                m_bufferSrb.reset();
                m_bufferSrbB.reset();
            } else {
                qCWarning(lcShaderNode) << "Buffer shader: compile failed, path=" << m_bufferPath
                                        << "error=" << result.error;
                if (++m_bufferShaderRetries < 3) {
                    m_bufferShaderDirty = true;
                } else {
                    qCWarning(lcShaderNode)
                        << "Buffer shader compilation failed after 3 attempts; giving up until shader path changes";
                }
            }
        }
    }
}

} // namespace PhosphorRendering
