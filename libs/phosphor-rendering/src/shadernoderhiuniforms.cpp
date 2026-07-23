// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "internal.h"

#include <PhosphorRendering/ShaderCompiler.h>

#include <QDateTime>
#include <QFileInfo>
#include <QQuickWindow>
#include <cmath>
#include <cstring>
#include <type_traits>

#include <rhi/qshaderbaker.h>

namespace PhosphorRendering {

// ============================================================================
// syncBaseUniforms — shim: snapshot node members into a UboFrameState and hand
// it to the installed UBO profile. The actual UBO byte-fill (the legacy
// syncBaseUniforms body) now lives in BaseUniformProfile::fill(); the node only
// resolves the live channel/texture sizes (which need RHI textures / QImages)
// and supplies the NDC Y-orientation.
// ============================================================================

void ShaderNodeRhi::syncBaseUniforms(QRhi* rhi)
{
    PhosphorShaders::UboFrameState state;

    // UboFrameState's array extents are declared independently of the contract
    // constants that bound the write loops below (phosphor-shaders cannot see
    // the animation contract — dependency direction). Pin them here, the one
    // TU that sees both, so a future kMax* bump cannot silently overrun the
    // state arrays.
    static_assert(std::extent_v<decltype(state.customParams)> == kMaxCustomParams,
                  "UboFrameState::customParams must match the contract's kMaxCustomParams");
    static_assert(std::extent_v<decltype(state.customColors)> == kMaxCustomColors,
                  "UboFrameState::customColors must match the contract's kMaxCustomColors");
    static_assert(std::extent_v<decltype(state.channelResolution)> == kMaxBufferPasses,
                  "UboFrameState::channelResolution must match the contract's kMaxBufferPasses");
    static_assert(std::extent_v<decltype(state.textureResolution)> == kMaxUserTextures,
                  "UboFrameState::textureResolution must match the contract's kMaxUserTextures");

    // Split full-precision m_time (double) into iTime (wrapped lo) + iTimeHi (wrap offset)
    state.time = static_cast<float>(m_time - static_cast<double>(m_timeHi));
    state.timeHi = m_timeHi;
    state.timeDelta = m_timeDelta;
    state.frame = m_frame;
    state.bufferFeedback = m_bufferFeedback;
    state.bufferFeedbackCleared = m_bufferFeedbackCleared;
    state.width = m_width;
    state.height = m_height;
    state.mouseX = static_cast<float>(m_mousePosition.x());
    state.mouseY = static_cast<float>(m_mousePosition.y());
    state.isReversed = m_isReversed;
    state.didFullUploadOnce = m_didFullUploadOnce;
    state.sceneDataDirty = m_sceneDataDirty;
    // The NDC Y-flip is a per-render-TARGET decision, not a per-backend
    // constant. rhi->isYUpInNDC() says "OpenGL", but the flip it calls for is
    // only correct when this draw lands in the WINDOW framebuffer (GL's
    // bottom-origin present is what un-flips it on screen). When Qt Quick
    // renders this item INTO A TEXTURE — a ShaderEffectSource layer: the
    // decoration chain's inter-stage taps, or SurfaceAnimator's hide-source
    // capture — the consumer samples that texture with the top-origin UV
    // convention Qt keeps backend-uniform, so a flipped write inverts the
    // sampled result (surface decorations rendered upside down on OpenGL for
    // every chain of two or more packs, and during show/hide transitions).
    // Mirrors the multipass buffer-pass identity pin in shadernoderhicore.cpp,
    // which corrects the same FBO round-trip for our OWN offscreen targets.
    // renderingIntoTexture() is the same predicate prepare()'s retarget
    // detection uses, so the flip and the forced re-upload cannot disagree.
    state.yUpInNDC = rhi->isYUpInNDC() && !renderingIntoTexture();

    // Surface-only fields — read by a SurfaceUniformProfile, ignored by the
    // BaseUniformProfile (so the overlay/animation UBO bytes are unchanged).
    state.qtOpacity = m_surfaceOpacity;
    state.surfaceScale = m_surfaceScale;
    state.surfaceFocused = m_surfaceFocused ? 1.0f : 0.0f;
    state.surfaceSize[0] = m_surfaceSize[0];
    state.surfaceSize[1] = m_surfaceSize[1];
    state.surfaceFrameTopLeft[0] = m_surfaceFrameTopLeft[0];
    state.surfaceFrameTopLeft[1] = m_surfaceFrameTopLeft[1];
    state.surfaceFrameSize[0] = m_surfaceFrameSize[0];
    state.surfaceFrameSize[1] = m_surfaceFrameSize[1];

    // Custom params
    for (int i = 0; i < kMaxCustomParams; ++i) {
        state.customParams[i][RhiConstants::ComponentX] = m_customParams[i].x();
        state.customParams[i][RhiConstants::ComponentY] = m_customParams[i].y();
        state.customParams[i][RhiConstants::ComponentZ] = m_customParams[i].z();
        state.customParams[i][RhiConstants::ComponentW] = m_customParams[i].w();
    }

    // Custom colors
    for (int i = 0; i < kMaxCustomColors; ++i) {
        state.customColors[i][0] = static_cast<float>(m_customColors[i].redF());
        state.customColors[i][1] = static_cast<float>(m_customColors[i].greenF());
        state.customColors[i][2] = static_cast<float>(m_customColors[i].blueF());
        state.customColors[i][3] = static_cast<float>(m_customColors[i].alphaF());
    }

    // iChannelResolution — resolved node-side (needs live RHI textures).
    const bool multiBufferMode = m_bufferPaths.size() > 1;
    const int numChannels = multiBufferMode ? qMin(m_bufferPaths.size(), static_cast<qsizetype>(kMaxBufferPasses))
                                            : (m_bufferShaderReady && m_bufferTexture ? 1 : 0);
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        if (i < numChannels) {
            if (multiBufferMode && m_multiBufferTextures[i]) {
                QSize ps = m_multiBufferTextures[i]->pixelSize();
                state.channelResolution[i][0] = static_cast<float>(ps.width());
                state.channelResolution[i][1] = static_cast<float>(ps.height());
            } else if (!multiBufferMode && i == 0 && m_bufferTexture) {
                QSize ps = m_bufferTexture->pixelSize();
                state.channelResolution[0][0] = static_cast<float>(ps.width());
                state.channelResolution[0][1] = static_cast<float>(ps.height());
            } else {
                state.channelResolution[i][0] = 1.0f;
                state.channelResolution[i][1] = 1.0f;
            }
        } else {
            state.channelResolution[i][0] = 1.0f;
            state.channelResolution[i][1] = 1.0f;
        }
    }
    state.audioSpectrumSize = static_cast<int>(m_audioSpectrum.size());

    // User texture resolutions (bindings 7-10) — resolved node-side.
    for (int i = 0; i < kMaxUserTextures; ++i) {
        if (m_userTextures[i] && !m_userTextureImages[i].isNull()) {
            state.textureResolution[i][0] = static_cast<float>(m_userTextureImages[i].width());
            state.textureResolution[i][1] = static_cast<float>(m_userTextureImages[i].height());
        } else {
            state.textureResolution[i][0] = 1.0f;
            state.textureResolution[i][1] = 1.0f;
        }
    }

    m_uboProfile->fill(state);
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
//   - clearDirty() runs BEFORE write(): a GUI-thread setter landing between
//     the two then re-arms the flag and the missed value uploads next frame.
//     The old write-then-clear order silently dropped that setter's dirty
//     bit, freezing its value until the next unrelated set.
void ShaderNodeRhi::uploadExtensionToUbo(QRhiResourceUpdateBatch* batch)
{
    const int extSize = m_uniformExtension->extensionSize();
    if (m_extensionStaging.size() != extSize) {
        m_extensionStaging.resize(extSize);
    }
    m_uniformExtension->clearDirty();
    m_uniformExtension->write(m_extensionStaging.data(), 0);
    const int extOffset = m_uboProfile->baseSize();
    batch->updateDynamicBuffer(m_ubo.get(), extOffset, extSize, m_extensionStaging.constData());
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
//                        setIsReversed, and the surface-contract setters
//                        (setSurfaceOpacity, setSurfaceScale, setSurfaceFocused,
//                        setSurfaceSize, setSurfaceFrameTopLeft,
//                        setSurfaceFrameSize)
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
        // syncBaseUniforms(rhi) snapshots node members into a UboFrameState and
        // calls m_uboProfile->fill(state). The profile folds in iFlipBufferY=1
        // and the NDC Y-orientation correction baked into qt_Matrix:
        //
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
        syncBaseUniforms(rhi);
        const void* uboData = m_uboProfile->data();
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        if (batch) {
            if (!m_didFullUploadOnce) {
                // Upload base uniforms (full region(s) from the profile)
                for (const auto& r : m_uboProfile->fullUploadRegions()) {
                    batch->updateDynamicBuffer(m_ubo.get(), r.offset, r.size,
                                               static_cast<const char*>(uboData) + r.offset);
                }
                // Upload extension data if present
                if (extensionHasData) {
                    uploadExtensionToUbo(batch);
                }
                m_didFullUploadOnce = true;
            } else {
                // Map the node's granular dirty flags onto the profile's
                // dirty-region dispatch (which reproduces the exact legacy
                // K_TIME_BLOCK / K_TIME_HI / K_SCENE_HEADER / K_APP_FIELDS
                // broader-subsumes-narrower behaviour).
                const PhosphorShaders::UboDirtyFlags flags{m_timeDirty, m_timeHiDirty, m_sceneDataDirty,
                                                           m_appFieldsDirty};
                for (const auto& r : m_uboProfile->dirtyRegions(flags)) {
                    batch->updateDynamicBuffer(m_ubo.get(), r.offset, r.size,
                                               static_cast<const char*>(uboData) + r.offset);
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
                    for (const auto& r : m_uboProfile->fullUploadRegions()) {
                        batch->updateDynamicBuffer(m_ubo.get(), r.offset, r.size,
                                                   static_cast<const char*>(uboData) + r.offset);
                    }
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
        const int bars = m_audioSpectrum.size();
        QSize targetSize = bars > 0 ? QSize(bars, 1) : QSize(1, 1);
        // Clamp against the device limit for the same reason the user-texture
        // path does: a spectrum wider than TextureSizeMax would fail create()
        // on every frame, and this block's failure path returns BEFORE the
        // user-texture, source-provider and wallpaper blocks below — one
        // oversized spectrum would starve every other texture upload.
        const int audioSizeMax = rhi->resourceLimit(QRhi::TextureSizeMax);
        if (audioSizeMax > 0 && targetSize.width() > audioSizeMax) {
            qCWarning(lcShaderNode) << "audio spectrum bar count" << targetSize.width()
                                    << "exceeds device TextureSizeMax" << audioSizeMax << ", clamping";
            targetSize.setWidth(audioSizeMax);
        }
        if (m_audioSpectrumTexture->pixelSize() != targetSize) {
            // Build the resized texture into a local and only swap it in on a
            // successful create(), so a failed resize keeps the previous working
            // texture and leaves m_audioSpectrumDirty set for a retry next frame
            // (mirrors the self-healing user-texture path below) instead of
            // stranding a non-created texture with dirty already cleared.
            std::unique_ptr<QRhiTexture> resized(rhi->newTexture(QRhiTexture::RGBA8, targetSize));
            if (!resized->create()) {
                qCWarning(lcShaderNode) << "audio spectrum texture create() failed for size" << targetSize
                                        << ", keeping previous texture; will retry next frame";
                return;
            }
            m_audioSpectrumTexture = std::move(resized);
            resetAllBindingsAndPipelines();
            if (!ensurePipeline()) {
                return;
            }
        }
        // Clear dirty only once a batch is in hand: nextResourceUpdateBatch()
        // returns null when Qt's 64-batch pool is exhausted, and clearing
        // first would drop the upload with nothing left to re-arm it.
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        if (batch) {
            m_audioSpectrumDirty = false;
        }
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
                // Reset to null so the `!m_userTextureSamplers[i]` re-create
                // gate above fires again next frame (dirty is still set) —
                // a stranded non-created sampler would otherwise pass the
                // truthiness gates below and be bound into the SRB.
                qCWarning(lcShaderNode) << "user texture slot" << i << "sampler create() failed, will retry next frame";
                m_userTextureSamplers[i].reset();
                continue;
            }
            resetAllBindingsAndPipelines();
        }
        // Sampler is the only hard prerequisite. m_userTextures[i] is null
        // before the slot's first successful allocation (and after
        // releaseRhiResources), and the size-mismatch branch below is what
        // allocates it — so gating on the texture here would make the very
        // first upload unreachable. Defensive beyond that: since the local-
        // swap rework no path in this file leaves the slot null after a
        // failed create.
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
        //   (a) initial allocation when m_userTextures[i] is null (first
        //       upload for the slot, or after releaseRhiResources).
        //   (b) resize when the existing texture's pixel size doesn't match
        //       the new target.
        if (!m_userTextures[i] || m_userTextures[i]->pixelSize() != targetSize) {
            // Build into a local and swap only on a successful create()
            // (mirrors the audio-spectrum and wallpaper paths): the SRB
            // built on the prior frame still binds the old texture raw, so
            // freeing it before the replacement is confirmed would leave
            // that frame's draw sampling a destroyed QRhiTexture. On
            // failure the slot keeps its previous texture (or stays null on
            // initial alloc, which appendUserTextureBindings() skips via
            // its truthiness gate) and dirty stays set, so a transient
            // RHI-side condition (OOM, device loss) self-heals next frame.
            std::unique_ptr<QRhiTexture> resized(rhi->newTexture(QRhiTexture::RGBA8, targetSize));
            if (!resized->create()) {
                qCWarning(lcShaderNode) << "user texture slot" << i << "create() failed for size" << targetSize
                                        << ", slot will retry next frame";
                continue;
            }
            m_userTextures[i] = std::move(resized);
            resetAllBindingsAndPipelines();
            if (!ensurePipeline()) {
                return;
            }
        }
        QRhiResourceUpdateBatch* ubatch = rhi->nextResourceUpdateBatch();
        if (ubatch) {
            // Clear dirty AFTER a successful create AND once a batch is in
            // hand: a null batch (Qt's 64-batch pool exhausted) must leave the
            // slot scheduled, or the upload is dropped with nothing to re-arm.
            m_userTextureDirty[i] = false;
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
        const QImage& img = m_wallpaperImage;
        const QSize targetSize = (!img.isNull() && img.width() > 0 && img.height() > 0) ? img.size() : QSize(1, 1);
        if (m_wallpaperTexture->pixelSize() != targetSize) {
            // Build the resized texture into a local and only swap it in on a
            // successful create() (mirrors the audio-spectrum path above): a
            // failed resize keeps the previous working texture alive — the SRB
            // still references it, so freeing it first would leave the bound
            // pipeline sampling a destroyed QRhiTexture — and leaves
            // m_wallpaperDirty set for a retry next frame. The wallpaper
            // texture is allocated exactly once at node init, so nulling it
            // here with dirty cleared would blank the wallpaper for the
            // node's remaining life.
            std::unique_ptr<QRhiTexture> resized(rhi->newTexture(QRhiTexture::RGBA8, targetSize));
            if (!resized->create()) {
                qCWarning(lcShaderNode) << "wallpaper texture create() failed for size" << targetSize
                                        << ", keeping previous texture; will retry next frame";
                return;
            }
            m_wallpaperTexture = std::move(resized);
            resetAllBindingsAndPipelines();
            if (!ensurePipeline()) {
                return;
            }
        }
        QRhiResourceUpdateBatch* ubatch = rhi->nextResourceUpdateBatch();
        if (ubatch) {
            // Same batch-in-hand rule as the audio and user-texture paths: the
            // wallpaper texture is allocated once at node init, so a dropped
            // upload with dirty already cleared blanks it for the node's life.
            m_wallpaperDirty = false;
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
    // Forget the last observed render-target kind alongside the full-upload
    // reset: the post-reset target may differ, and the fresh full upload
    // already re-covers qt_Matrix, so the retarget detector must not fire a
    // stale comparison against pre-reset state.
    m_lastTargetWasTexture.reset();
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
