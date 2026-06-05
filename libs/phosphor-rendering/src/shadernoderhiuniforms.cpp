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
    m_baseUniforms.iIsReversed = m_isReversed ? 1 : 0;
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
    m_baseUniforms.iAudioSpectrumSize = static_cast<int>(m_audioSpectrum.size());

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
//   - Clears the extension's dirty bit (the extension's own isDirty() is
//     the authoritative upload gate — there is no node-side mirror).
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
//                        setCustomColor, setAudioSpectrum, setUserTexture,
//                        setIsReversed
//   m_appFieldsDirty  ← setAppField0, setAppField1
//   extension dirty   ← tracked via m_uniformExtension->isDirty() (set by the
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
        // NDC Y-orientation correction for the fixed-NDC fullscreen quad on the
        // DIRECT-TO-WINDOW path (daemon animation shaders, whose shaderItem is
        // not layer-enabled). Qt-RHI does NOT normalise the NDC Y direction of
        // geometry the shader emits: OpenGL is Y-up-in-NDC, Vulkan is Y-down.
        // The quad + Y-down vTexCoord contract are authored against Vulkan, so
        // on a Y-up-in-NDC backend the quad presents upside down. Bake the
        // correction into qt_Matrix — animation vertex stages apply it as
        // `gl_Position = qt_Matrix * vec4(position, 0, 1)`. Column-major
        // float[16]: index 5 is the Y-scale (m11); negate it only when NDC is
        // Y-up. Overlay/zone shaders (zone.vert) render via layer.enabled and
        // intentionally ignore qt_Matrix — Qt's layer composite already
        // corrects them, so this matrix is harmlessly unread on that path.
        // (The buffer/multipass passes pin qt_Matrix back to identity — see the
        // multipass block in shadernoderhicore.cpp — because their FBO
        // round-trip is already backend-consistent and must not be flipped.)
        std::memset(m_baseUniforms.qt_Matrix, 0, sizeof(m_baseUniforms.qt_Matrix));
        m_baseUniforms.qt_Matrix[0] = 1.0f;
        m_baseUniforms.qt_Matrix[5] = rhi->isYUpInNDC() ? -1.0f : 1.0f;
        m_baseUniforms.qt_Matrix[10] = 1.0f;
        m_baseUniforms.qt_Matrix[15] = 1.0f;
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
                // K_TIME_HI is subsumed by K_SCENE_HEADER. When both flags
                // fire on the same frame, only the broader upload runs;
                // the granular K_TIME_HI write only fires when scene-data
                // is otherwise clean (the time-wrap-only path).
                if (m_timeHiDirty && !m_sceneDataDirty) {
                    batch->updateDynamicBuffer(m_ubo.get(), K_TIME_HI_OFFSET, K_TIME_HI_SIZE,
                                               static_cast<const char*>(static_cast<const void*>(&m_baseUniforms))
                                                   + K_TIME_HI_OFFSET);
                }
                if (m_sceneDataDirty) {
                    // Scene header: iResolution through end of BaseUniforms
                    // (subsumes the appFields, iTimeHi, and iIsReversed
                    // regions — no need for separate uploads when this
                    // fires).
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
                // Extension region: uploaded separately when dirty.
                // Track whether we've already uploaded the extension
                // this frame so the defensive full-base fallback below
                // doesn't re-upload it redundantly.
                bool extensionUploaded = false;
                if (extensionHasData && m_uniformExtension->isDirty()) {
                    uploadExtensionToUbo(batch);
                    extensionUploaded = true;
                }
                // Defensive: if no granular flags set, do full base upload.
                // Per the dirty-flag invariants documented above, this
                // branch is normally unreachable when m_uniformsDirty=true
                // (every setter that dirties m_uniformsDirty also dirties
                // at least one granular flag), but a future setter that
                // forgets the granular flag would silently skip the GPU
                // write entirely without this safety net. Symmetric with
                // the !m_didFullUploadOnce path above: if we fall back
                // to a full base upload we must ALSO refresh the
                // extension region (when present and not already
                // uploaded above), otherwise an extension-only dirty
                // in a future revision would land here and silently
                // miss the upload.
                if (!m_timeDirty && !m_timeHiDirty && !m_sceneDataDirty && !m_appFieldsDirty) {
                    batch->updateDynamicBuffer(m_ubo.get(), 0, sizeof(PhosphorShaders::BaseUniforms), &m_baseUniforms);
                    if (extensionHasData && !extensionUploaded) {
                        uploadExtensionToUbo(batch);
                    }
                }
            }
            if (!m_vboUploaded) {
                batch->uploadStaticBuffer(m_vbo.get(), RhiConstants::QuadVertices);
                m_vboUploaded = true;
            }
            cb->resourceUpdate(batch);
            m_timeDirty = false;
            m_timeHiDirty = false;
            m_sceneDataDirty = false;
            m_appFieldsDirty = false;
            m_uniformsDirty = false;
        }
    } else {
        QRhiResourceUpdateBatch* batch = nullptr;
        // Check extension dirty independently of base uniforms
        if (extensionHasData && m_uniformExtension->isDirty()) {
            batch = rhi->nextResourceUpdateBatch();
            if (batch)
                uploadExtensionToUbo(batch);
        }
        if (!m_vboUploaded) {
            if (!batch)
                batch = rhi->nextResourceUpdateBatch();
            if (batch) {
                batch->uploadStaticBuffer(m_vbo.get(), RhiConstants::QuadVertices);
                m_vboUploaded = true;
            }
        }
        if (batch)
            cb->resourceUpdate(batch);
    }

    if (m_dummyChannelTextureNeedsUpload && m_dummyChannelTexture) {
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        if (batch) {
            batch->uploadTexture(m_dummyChannelTexture.get(), m_transparentFallbackImage);
            cb->resourceUpdate(batch);
            m_dummyChannelTextureNeedsUpload = false;
        }
    }

    if (m_dummyChannelTextureNeedsUpload)
        return;

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
            const QRhiSampler::AddressMode addr = wrapModeToRhiAddress(m_userTextureWraps[i]);
            m_userTextureSamplers[i].reset(
                rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, addr, addr));
            if (!m_userTextureSamplers[i]->create()) {
                continue;
            }
            resetAllBindingsAndPipelines();
        }
        // Sampler is the only hard prerequisite — m_userTextures[i] may be
        // null after a previous failed create() on this slot, in which case
        // the size-mismatch branch below allocates fresh. Without that gate
        // relaxation the failed-create retry promised by the qCWarning text
        // is structurally unreachable: the loop short-circuits on
        // !m_userTextures[i] before line 354 ever runs again.
        if (!m_userTextureDirty[i] || !m_userTextureSamplers[i]) {
            continue;
        }
        const QImage& img = m_userTextureImages[i];
        QSize targetSize = (!img.isNull() && img.width() > 0 && img.height() > 0) ? img.size() : QSize(1, 1);
        // Clamp against the device's hard texture-size limit. A user-supplied
        // PNG larger than the device max (commonly 16384 on desktop GPUs but
        // as low as 4096 on some mobile/Vulkan drivers) would otherwise hit
        // newTexture()->create() == false below. Clamping to the device limit
        // is a graceful degradation — the texture upload below will scale-fit
        // the image into the clamped bounds via QRhi's blit semantics.
        const int textureSizeMax = rhi->resourceLimit(QRhi::TextureSizeMax);
        if (textureSizeMax > 0 && (targetSize.width() > textureSizeMax || targetSize.height() > textureSizeMax)) {
            qCWarning(lcShaderNode) << "user texture slot" << i << "size" << targetSize
                                    << "exceeds device TextureSizeMax" << textureSizeMax << ", clamping";
            targetSize = QSize(qMin(targetSize.width(), textureSizeMax), qMin(targetSize.height(), textureSizeMax));
        }
        // Branch covers two cases uniformly:
        //   (a) initial allocation when m_userTextures[i] is null (after a
        //       prior failed create reset us to nullptr).
        //   (b) resize when the existing texture's pixel size doesn't match
        //       the new target.
        if (!m_userTextures[i] || m_userTextures[i]->pixelSize() != targetSize) {
            m_userTextures[i].reset(rhi->newTexture(QRhiTexture::RGBA8, targetSize));
            if (!m_userTextures[i]->create()) {
                qCWarning(lcShaderNode) << "user texture slot" << i << "create() failed for size" << targetSize
                                        << ", slot will retry next frame";
                // Reset to nullptr so appendUserTextureBindings() skips the
                // slot via its truthiness gate (the SRB must NOT bind a
                // failed texture; it would wire a non-functional GPU resource
                // into the pipeline). Keep dirty=true so the next prepare()
                // pass retries — the relaxed gate above lets a null pointer
                // re-enter this branch on the next frame, so a transient
                // RHI-side condition (OOM, device loss, post-clamp size
                // acceptance) can clear and the slot self-heals.
                m_userTextures[i].reset();
                continue;
            }
            resetAllBindingsAndPipelines();
            if (!ensurePipeline()) {
                return;
            }
        }
        // Clear dirty AFTER successful create so a transient failure above
        // keeps the slot scheduled for retry.
        m_userTextureDirty[i] = false;
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

    // Source-texture-provider plumbing for slot 0 / binding 7. When a
    // provider is set we own a dedicated sampler (separate from the
    // user-texture-0 sampler so callers that mix the two paths don't
    // step on each other) and detect QRhiTexture identity changes —
    // the underlying FBO can be re-allocated on resize / device-loss
    // and the SRB must be rebuilt against the fresh pointer or
    // sampling crashes inside the RHI.
    //
    // The resolved QRhiTexture* is cached into m_lastSourceRhiTexture and
    // becomes the SINGLE source of truth: appendUserTextureBindings() reads
    // m_lastSourceRhiTexture directly rather than re-querying the provider.
    // That closes the second TOCTOU window where the cached identity check
    // here picked one pointer but the SRB build below picked another after
    // an in-between FBO recreation.
    if (m_sourceTextureProvider) {
        if (!m_sourceSampler && !m_sourceSamplerFailed) {
            m_sourceSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                  QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
            if (!m_sourceSampler->create()) {
                // Don't churn: log once, latch the failure, and stop
                // re-attempting per-frame. releaseRhiResources() clears
                // the latch so a device reset can retry.
                qCWarning(lcShaderNode) << "Failed to create source-provider sampler — slot 0 will use the "
                                           "transparent fallback until the next device reset";
                m_sourceSampler.reset();
                m_sourceSamplerFailed = true;
            } else {
                resetAllBindingsAndPipelines();
            }
        }
        QRhiTexture* resolved = nullptr;
        if (QSGTexture* sgTex = m_sourceTextureProvider->texture()) {
            resolved = sgTex->rhiTexture();
            // Cross-window/cross-context guard: the provider may belong to a
            // QQuickItem in a different render context. Binding a QRhiTexture
            // from a foreign QRhi will crash inside the backend at draw time.
            if (resolved && resolved->rhi() != rhi) {
                if (!m_warnedForeignRhi) {
                    m_warnedForeignRhi = true;
                    qCWarning(lcShaderNode)
                        << "Source provider's QRhiTexture belongs to a foreign QRhi — slot 0 will use the "
                           "transparent fallback. (Logged once per node.)";
                }
                resolved = nullptr;
            }
        }
        if (resolved != m_lastSourceRhiTexture) {
            m_lastSourceRhiTexture = resolved;
            resetAllBindingsAndPipelines();
        }
    }

    // Lazy 1×1 transparent fallback texture for slot 0 when a provider is
    // set but its underlying QRhiTexture is not yet ready (or rejected by
    // the foreign-rhi guard above). The contract for setSourceTextureProvider
    // is that it SUPERSEDES whatever QImage was uploaded at slot 0 — falling
    // back to m_userTextures[0] would unmask a stale snapshot. Instead, bind
    // a dedicated all-zero texture so the shader samples transparent and the
    // QImage path stays inert.
    if (m_sourceTextureProvider && !m_lastSourceRhiTexture && !m_transparentFallbackTexture) {
        m_transparentFallbackTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        if (m_transparentFallbackTexture->create()) {
            m_transparentFallbackTextureNeedsUpload = true;
        } else {
            m_transparentFallbackTexture.reset();
        }
    }
    if (m_transparentFallbackTextureNeedsUpload && m_transparentFallbackTexture) {
        QRhiResourceUpdateBatch* fbatch = rhi->nextResourceUpdateBatch();
        if (fbatch) {
            fbatch->uploadTexture(m_transparentFallbackTexture.get(), m_transparentFallbackImage);
            cb->resourceUpdate(fbatch);
            m_transparentFallbackTextureNeedsUpload = false;
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
    // Source-texture sampler is RHI-owned and must be reset on
    // device-loss. The QPointer<QSGTextureProvider> survives — its
    // QRhiTexture pointer will simply differ on the next prepare()
    // (because Qt's layer system rebuilt the FBO too) and the SRB
    // rebuild detection in uploadDirtyTextures will pick it up.
    m_sourceSampler.reset();
    m_lastSourceRhiTexture = nullptr;
    m_sourceSamplerFailed = false; // re-attempt sampler creation on next prepare()
    m_warnedForeignRhi = false; // re-warn (once) after device reset
    m_transparentFallbackTexture.reset();
    m_transparentFallbackTextureNeedsUpload = false;

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
            QString err;
            QString src = loadAndExpandShader(path, &err);
            if (src.isEmpty()) {
                // loadAndExpand already returns empty on missing/unreadable;
                // no separate exists() check needed (and the prior check was
                // itself a TOCTOU race).
                allOk = false;
                break;
            }
            m_multiBufferFragmentShaderSources[i] = src;
            // Buffer-pass shaders are compiled directly via ShaderCompiler::compile
            // and do NOT participate in the filename bake cache (which is keyed
            // off the main vertex+fragment pair). Tracking per-pass mtimes
            // bought nothing; the value was written and never read. Removed
            // rather than left as dead state.
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
            // Skip the redundant exists() check — loadAndExpandShader returns
            // an empty string on missing/unreadable, which is the gate we
            // need. Buffer-pass shaders bypass the filename bake cache (see
            // multi-buffer branch above), so no mtime tracking is needed.
            QString err;
            m_bufferFragmentShaderSource = loadAndExpandShader(m_bufferPath, &err);
            if (m_bufferFragmentShaderSource.isEmpty()) {
                // The eager load in setBufferShaderPaths used to surface
                // load errors at setter time; deferring the load to here
                // means a silent empty-source loop unless we log + retry.
                qCWarning(lcShaderNode) << "Buffer shader load failed:" << m_bufferPath
                                        << "error=" << (err.isEmpty() ? QStringLiteral("(no detail)") : err);
                if (++m_bufferShaderRetries < 3) {
                    m_bufferShaderDirty = true;
                } else {
                    qCWarning(lcShaderNode)
                        << "Buffer shader load failed after 3 attempts; giving up until shader path changes";
                }
                return;
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
