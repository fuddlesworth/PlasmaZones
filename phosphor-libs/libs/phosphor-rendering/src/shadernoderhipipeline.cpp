// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "internal.h"

namespace PhosphorRendering {

// ============================================================================
// Fullscreen Quad Pipeline (file-local helper, shared by buffer + image passes)
// ============================================================================

static std::unique_ptr<QRhiGraphicsPipeline>
createFullscreenQuadPipeline(QRhi* rhi, QRhiRenderPassDescriptor* rpDesc, const QShader& vertexShader,
                             const QShader& fragmentShader, QRhiShaderResourceBindings* srb, bool enableBlend = true,
                             int numColorAttachments = 1,
                             QRhiGraphicsPipeline::Topology topology = QRhiGraphicsPipeline::TriangleStrip)
{
    std::unique_ptr<QRhiGraphicsPipeline> pipeline(rhi->newGraphicsPipeline());
    pipeline->setTopology(topology);
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

void ShaderNodeRhi::requestDepthCreateRetry()
{
    // Bounded, because the arm that calls this has just failed a GPU create and the
    // enclosing branch is re-entered from prepare() on every frame while it keeps failing:
    // an unbounded request would repaint at frame rate for as long as the driver refuses.
    // Modelled on m_multiBufferShaderRetries, including the give-up line, so a permanent
    // failure costs three REQUESTED frames and one message rather than a self-sustained
    // spin. What the bound does NOT stop is the create attempt: an item driven from
    // outside (an animated pack, a resize) re-enters the depth block on every frame it
    // paints anyway and calls newTexture() + create() again, silently, for as long as it
    // paints. That is not a leak to plug — it is how the node recovers at all, since a
    // successful create is the only thing on THIS path that clears the latch and the count
    // (releaseRhiResources and setUseDepthBuffer clear it too, from outside this block).
    // Spent budget: return before the increment, so the counter STOPS at the bound rather
    // than rising for as long as the driver keeps refusing. That also makes the give-up line
    // below a one-shot PER RETRY BUDGET, by structure rather than by an equality test a later
    // edit could widen — not a one-shot per node, because the size gate in ensureBufferTarget
    // zeroes this count without touching any latch, so a new buffer size earns a fresh budget
    // and, when it exhausts it, a fresh give-up line. m_depthCreateWarned cannot serve as that
    // latch either way, because the calling arm sets it on the first failure, so a
    // `!m_depthCreateWarned` gate here would never print at all.
    if (m_depthCreateRetries >= 3) {
        return;
    }
    if (++m_depthCreateRetries < 3) {
        requestAnotherFrame();
        return;
    }
    qCWarning(lcShaderNode) << "Depth texture or sampler creation failed after 3 attempts; no longer requesting "
                               "retry frames until the depth setting, the buffer size or the node's resources "
                               "change";
}

void ShaderNodeRhi::clearDepthCreateFailure()
{
    m_depthCreateRetries = 0;
    m_depthCreateWarned = false;
    m_depthCreateFailedSize = QSize();
}

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
    // The size pass i renders at. Every pass shares the depth attachment when
    // one is in play, and a render target's colour and depth attachments must
    // agree in size, so a depth-buffer pack keeps every pass on the
    // single-value scale; per-pass scales are a colour-only feature.
    const auto passSize = [this, bufferSize](int i) -> QSize {
        if (m_useDepthBuffer) {
            return bufferSize;
        }
        const qreal s = m_bufferScales[static_cast<size_t>(i)];
        return QSize(qMax(1, qRound(m_width * s)), qMax(1, qRound(m_height * s)));
    };
    // Whether pass @p i asked for mip sampling. Drives the texture flags so the
    // chain actually exists, and the generateMips call after the pass renders.
    const auto wantsMips = [this](int i) {
        return m_bufferFilters[static_cast<size_t>(i)] == QLatin1String("mipmap");
    };
    // Create or resize depth texture before render targets that reference it
    if (m_useDepthBuffer && (!m_depthTexture || m_depthTexture->pixelSize() != bufferSize)) {
        // A different buffer size is a different GPU request, so it gets its own retry
        // budget: without this a node that burned all three attempts at one size carried the
        // spent budget into the next and got one attempt per externally-driven frame with no
        // requested retry. Keyed on the size here rather than cleared from setResolution,
        // which fires on every frame of an animated resize.
        //
        // THE COUNT ONLY, deliberately not clearDepthCreateFailure(). An earlier version
        // called that, which also clears the warning latch — and bufferSize follows
        // m_width/m_height, so during an animated resize it moves nearly every frame. Every
        // frame then cleared the latch, printed the FIRST-FAILURE line again and reset the
        // count before it could reach three: a vsync-rate flood, with the bound never engaging
        // while the size kept moving. The latch stays owned by the three paths that clear it
        // through clearDepthCreateFailure (the success path, releaseRhiResources,
        // setUseDepthBuffer).
        //
        // What the split costs, stated exactly, because an earlier version of this sentence
        // rounded it to "one diagnostic per node": the first-failure line above is one per
        // LATCH PERIOD (the latch clears on a success, on releaseRhiResources and on a depth
        // toggle, and the success path below says so outright), and requestDepthCreateRetry's
        // give-up line is one per exhausted budget, so a resize that settles at several sizes
        // while failing prints it once per size. The trade is deliberate — a new size that
        // cannot allocate is worth one line — and the give-up text already names the buffer
        // size as a thing that re-arms it. One consequence the earlier version also missed:
        // m_depthCreateWarned is SHARED between the texture line and the sampler line, so a
        // sampler failure that FOLLOWS a texture failure is reported only by the generic
        // give-up line, never by name. A sampler failure on a clear latch IS named, at any
        // size, which is why this says "follows" and not "at a new size".
        if (bufferSize != m_depthCreateFailedSize) {
            m_depthCreateRetries = 0;
            m_depthCreateFailedSize = bufferSize;
        }
        m_depthTexture.reset(rhi->newTexture(QRhiTexture::R32F, bufferSize, 1, QRhiTexture::RenderTarget));
        if (!m_depthTexture->create()) {
            // Latched: this branch is re-entered from prepare() on every frame while it
            // returns false, so an animated depth pack flooded this line at vsync. Same
            // one-shot treatment the buffer-target and buffer-sampler failures take.
            if (!m_depthCreateWarned) {
                m_depthCreateWarned = true;
                qCWarning(lcShaderNode) << "Failed to create depth texture";
            }
            // Drop the failed object for the same reason the sampler below
            // does: this branch is gated on the texture's pixelSize(), which
            // QRhiTexture reports from the requested size whether or not
            // create() succeeded. A latched failed texture would therefore
            // never be retried, yet would still pass appendDepthBinding's
            // null check and reach the SRB uncreated.
            m_depthTexture.reset();
            // The old depth texture was already destroyed by the reset() above,
            // and the SRBs built against it are still installed holding raw
            // pointers to it. prepare() bails on this false, but render() only
            // gates on "is there a pipeline and an srb?" — both non-null here —
            // so without this the next frame draws against freed GPU objects.
            //
            // resetBufferTargets, not resetAllBindingsAndPipelines: the buffer
            // RENDER TARGETS hold the depth texture as a raw second colour
            // attachment (see createTextureAndRT), and the bindings-only reset
            // leaves them installed. They would then survive to a later frame
            // that recreates the depth texture successfully, finds every colour
            // texture already at passSize(i), rebuilds nothing, and renders into
            // a target whose depth attachment was freed here.
            //
            resetBufferTargets();
            // AND ask for the frame that retries, up to a bound. resetBufferTargets nulls
            // the pipeline and SRB, so render() bails and this item paints nothing; with
            // no per-frame input a static depth pack then had nothing to schedule another
            // prepare() and stayed blank after a single transient failure until unrelated
            // damage arrived. An earlier version of this comment argued for NO frame
            // request on the grounds that one would repaint at full rate against a
            // persistently failing driver — a false dichotomy, since the bound is what
            // makes the request safe, and this library already solves the identical shape
            // three times for the shader loads (see m_multiBufferShaderRetries, whose own
            // comment describes this exact blank-until-something-else-repaints failure).
            requestDepthCreateRetry();
            return false;
        }
        if (!m_depthSampler) {
            m_depthSampler.reset(rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                                 QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
            if (!m_depthSampler->create()) {
                // Latched with its sibling above, and for the same reason.
                if (!m_depthCreateWarned) {
                    m_depthCreateWarned = true;
                    qCWarning(lcShaderNode) << "Failed to create depth sampler";
                }
                // Drop the failed object (matching ensureDummyChannelResources
                // and ensureBufferSampler): the enclosing branch is gated on
                // the TEXTURE's state, so a latched failed sampler would never
                // be re-created yet still pass appendDepthBinding's null check.
                m_depthSampler.reset();
                // Drop the freshly created TEXTURE too, and not only for
                // symmetry. This whole block is gated on the texture's
                // existence and pixelSize, both of which the new texture now
                // satisfies, so leaving it installed means the next frame
                // skips the block entirely and the sampler is never retried.
                // The node would then run on with a depth texture and no
                // sampler, which appendDepthBinding cannot bind, producing a
                // resource layout that does not match a depth pack's own
                // SPIR-V. Resetting it makes the next frame re-enter and try
                // again.
                m_depthTexture.reset();
                // The depth TEXTURE was already replaced above, so the render
                // targets and SRBs still installed reference the one it
                // displaced. Same freed-object draw as the texture failure
                // path, and the same reason it takes the full target reset.
                resetBufferTargets();
                requestDepthCreateRetry();
                return false;
            }
        }
        // The whole dependent set, not just the bindings: every buffer render
        // target created while the OLD depth texture was installed holds it as a
        // raw attachment. On the success path it costs nothing on either arm of
        // the enclosing gate, for two different reasons. On a depth RESIZE,
        // bufferSize moved, and bufferSize is what passSize() returns for every
        // pass when m_useDepthBuffer, so each target was going to be rebuilt in
        // this same call regardless. On the depth-texture-ABSENT arm nothing
        // would otherwise have been rebuilt, but every site that nulls
        // m_depthTexture drops the targets in the same breath
        // (setUseDepthBuffer, releaseRhiResources, and both failure arms above),
        // so there is nothing left here to drop. It does not touch
        // m_depthTexture or m_depthSampler, so the objects just created here
        // survive it.
        resetBufferTargets();
        // Both objects exist, so forget any earlier failure: a later one is then retried
        // and reported again rather than swallowed for the node's lifetime.
        clearDepthCreateFailure();
    }

    if (m_useDepthBuffer && multiBufferMode) {
        if (!m_depthMultiBufferWarned) {
            m_depthMultiBufferWarned = true;
            qCWarning(lcShaderNode)
                << "Depth buffer with" << m_bufferPaths.size()
                << "buffer passes: only the last pass's depth output will be available in the image pass";
        }
        // A depth pack pins EVERY pass to the single scale (see passSize above),
        // because the passes share one depth attachment and a render target's
        // colour and depth attachments must agree in size. Silently discarding a
        // pack's declared per-pass scales is the kind of thing an author spends
        // an afternoon on, so say it once. The offline validator lints the same
        // combination; this covers a pack that reaches the node another way.
        if (!m_depthScalesWarned) {
            bool diverged = false;
            for (int i = 0; i < kMaxBufferPasses && !diverged; ++i) {
                diverged = !qFuzzyCompare(m_bufferScales[static_cast<size_t>(i)], m_bufferScale);
            }
            if (diverged) {
                m_depthScalesWarned = true;
                qCWarning(lcShaderNode) << "Depth buffer with per-pass bufferScales: every pass is pinned to"
                                        << m_bufferScale << "because the passes share one depth attachment";
            }
        }
    }
    // Buffer texel format: RGBA16F unless the pack's metadata declares its
    // buffers hold plain clamped [0,1] colour ("halfFloatBuffers": false).
    // At full resolution a half-float buffer pass writes+reads twice the
    // bytes RGBA8 does, and buffer bandwidth is the scarce resource on
    // integrated GPUs — but the format is a per-pack contract, because a
    // buffer can legitimately store HDR radiance, signed data, or a feedback
    // accumulator whose decay quantises to a standstill at 8 bits.
    //
    // RGBA8 is documented as always supported; RGBA16F is not, and asking for
    // an unsupported format does not fail gracefully — create() returns false
    // and the pack goes black with a per-frame warning naming only the size.
    // Ask the backend first and degrade to RGBA8, which is what the pack would
    // have had before it declared the preference, saying so once.
    QRhiTexture::Format bufferFormat = m_halfFloatBuffers ? QRhiTexture::RGBA16F : QRhiTexture::RGBA8;
    if (bufferFormat == QRhiTexture::RGBA16F
        && !rhi->isTextureFormatSupported(QRhiTexture::RGBA16F, QRhiTexture::RenderTarget)) {
        bufferFormat = QRhiTexture::RGBA8;
        if (!m_halfFloatUnsupportedWarned) {
            m_halfFloatUnsupportedWarned = true;
            qCWarning(lcShaderNode) << "RGBA16F buffer textures are not supported as render targets on this backend"
                                    << "— falling back to RGBA8; a pack storing HDR radiance, signed data or a slow"
                                    << "feedback decay will band or clamp";
        }
    }
    // A pass whose filter is "mipmap" needs a texture that HAS a mip chain and
    // may have one generated into it. Without these two flags the token did
    // nothing at all: ensureBufferSampler set the sampler's mip filter, nothing
    // allocated the levels, and nothing generated them, so a pack asking for
    // mipmap sampled level 0 exactly as "linear" does.
    // ONE latched warning for the whole family of buffer-target create
    // failures, naming the size and format that failed. ensureBufferTarget is
    // re-entered from prepare() on every frame while it returns false, so the
    // six call sites below used to emit a warning per frame per failing pass
    // for as long as the condition lasted, which on a persistent failure fills
    // the journal at frame cadence. Those call sites are qCDebug now: they say
    // WHICH buffer, this says WHY, and only the "why" is worth a warning.
    const auto warnCreateFailed = [this, bufferFormat](const QString& what, const QSize& size, bool wantMips) {
        if (m_bufferTargetCreateWarned) {
            return;
        }
        m_bufferTargetCreateWarned = true;
        qCWarning(lcShaderNode) << "Buffer" << what << "create failed at" << size
                                << "format=" << (bufferFormat == QRhiTexture::RGBA16F ? "RGBA16F" : "RGBA8")
                                << "mipmapped=" << wantMips << "— multipass is disabled while this persists";
    };
    auto createTextureAndRT = [rhi, bufferFormat, warnCreateFailed,
                               this](std::unique_ptr<QRhiTexture>& tex, std::unique_ptr<QRhiTextureRenderTarget>& rt,
                                     std::unique_ptr<QRhiRenderPassDescriptor>& rpd, const QSize& size,
                                     bool wantMips) -> bool {
        // RenderTarget alone. UsedWithLoadStore declares that the texture will
        // be used with image load/store, which is a compute-shader facility
        // this library has no compute pipelines to use. Declaring it anyway is
        // not free: a backend honours the declaration by requesting storage
        // usage on the native image, and storage support is a separate format
        // capability, so the flag can turn a perfectly renderable RGBA16F
        // target into a create() failure on a driver that does not advertise
        // storage for it.
        const QRhiTexture::Flags baseFlags = QRhiTexture::RenderTarget;
        const QRhiTexture::Flags mipFlags = QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips;
        tex.reset(rhi->newTexture(bufferFormat, size, 1, wantMips ? (baseFlags | mipFlags) : baseFlags));
        // A backend that will not give us a mipmapped render target for this
        // format must not take the whole pack down with it, because the pack
        // worked (as plain linear) before mipmap meant anything. Retry once
        // without the mip flags and say so.
        if (wantMips && !tex->create()) {
            qCWarning(lcShaderNode) << "Buffer texture with mipmaps unavailable at" << size
                                    << "— falling back to a single level, 'mipmap' will sample as 'linear'";
            tex.reset(rhi->newTexture(bufferFormat, size, 1, baseFlags));
        }
        // Every failure exit clears what it allocated. Callers gate the retry
        // on "does the object exist and is it the right pixelSize?", and
        // pixelSize() answers from the requested size even after a failed
        // create() — so leaving a failed object installed latches the failure
        // permanently and lets ensureBufferPipeline build an SRB and pipeline
        // against an uncreated texture and render target.
        if (!tex->create()) {
            tex.reset();
            // The caller's render target and pass descriptor go with it. They
            // were built against the texture the reset above destroyed and now
            // hold a raw pointer to it, exactly as the render-target failure arm
            // below clears all three together. Nothing draws them today
            // (prepare() bails on this false, render() gates on a pipeline the
            // reset below drops, and the two remaining derefs read only the
            // RT's renderPassDescriptor()), but an installed target must never
            // point at a freed attachment, which is the invariant
            // ZoneShaderNodeRhi's destructor orders its teardown around.
            rt.reset();
            rpd.reset();
            warnCreateFailed(QStringLiteral("texture"), size, wantMips);
            // The caller's old texture is already gone (the reset above
            // replaced it before create() was attempted), and any SRB or
            // pipeline built against it is still installed holding a raw
            // pointer. Every caller propagates this false up through
            // ensureBufferTarget, where prepare() bails — but render() gates
            // only on a non-null pipeline and srb, so the frame would draw
            // against freed GPU objects. Invalidating here makes that gate
            // catch it. Matters most in the multi-buffer loop below, where a
            // failure at pass i>0 leaves passes 0..i-1 already swapped.
            resetAllBindingsAndPipelines();
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
        if (!rt->create()) {
            tex.reset();
            rt.reset();
            rpd.reset();
            warnCreateFailed(QStringLiteral("render target"), size, wantMips);
            // Same as the texture failure above: the old texture and render
            // target are gone, the installed bindings still point at them.
            resetAllBindingsAndPipelines();
            return false;
        }
        // A success clears the latch, so a failure that recurs after a genuine
        // recovery is reported again rather than swallowed for the session.
        m_bufferTargetCreateWarned = false;
        // A new or resized target changes iChannelResolution, which is resolved
        // node-side from the LIVE textures during the UBO upload and is gated on
        // m_uniformsDirty. Without re-arming here, a pass that resized carried on
        // publishing the previous channel size to the shader.
        //
        // requestAnotherFrame() as well, not just the flags: this runs inside
        // prepare() on the render thread, where setting a dirty flag schedules
        // nothing by itself, so a pack with no per-frame input would sit on the
        // stale value until something unrelated happened to repaint it.
        m_uniformsDirty = true;
        m_sceneDataDirty = true;
        requestAnotherFrame();
        return true;
    };

    if (multiBufferMode) {
        const int n = static_cast<int>(qMin(m_bufferPaths.size(), static_cast<qsizetype>(kMaxBufferPasses)));
        bool needCreate = false;
        for (int i = 0; i < n; ++i) {
            if (!m_multiBufferTextures[i] || m_multiBufferTextures[i]->pixelSize() != passSize(i)) {
                needCreate = true;
                break;
            }
        }
        if (needCreate) {
            // Per-pass sizes, not just the single scale. A pack declaring
            // bufferScales renders each pass at its own resolution (a Kawase
            // pyramid is the whole point of the feature), so a log naming only
            // m_bufferScale describes a pass count it does not have and makes
            // a mis-scaled pyramid impossible to read off the journal.
            //
            // Recorded as the loop runs and logged AFTER it, so the line reports what was
            // actually rebuilt and goes silent when the loop bails. Logged BEFORE, it fired
            // on every frame a pass kept failing — needCreate stays true while any pass is
            // missing — and built a QStringList plus n formatted QStrings each time, on the
            // render thread inside prepare(). Note the failing-pass early return can still
            // discard a partly built list, which is a strict improvement on building the
            // whole thing unconditionally rather than a promise of zero allocations.
            QStringList rebuilt;
            for (int i = 0; i < n; ++i) {
                const QSize sz = passSize(i);
                // PER-INDEX, not the whole set. The scan above only asks whether
                // ANY pass needs rebuilding; recreating the ones that already
                // match costs a needless destroy/create of their texture and
                // render target, and it DEFEATS the create-failure latch:
                // createTextureAndRT clears the latch on success, so a healthy
                // pass 0 cleared it on every frame a later pass kept failing,
                // and warnCreateFailed then warned once per frame — exactly the
                // frame-cadence flood the latch exists to stop. A failing pass
                // resets its own texture, so needCreate is true again next frame
                // and the sequence repeated for as long as the condition lasted.
                //
                // Safe to skip a matching pass even with a depth attachment:
                // passSize() returns the shared bufferSize for every pass when
                // m_useDepthBuffer, so a depth resize changes every pass's size
                // and nothing is skipped, and a depth FLIP comes through
                // setUseDepthBuffer, which drops the whole set via
                // resetBufferTargets before this runs.
                if (m_multiBufferTextures[i] && m_multiBufferTextures[i]->pixelSize() == sz) {
                    continue;
                }
                if (!createTextureAndRT(m_multiBufferTextures[i], m_multiBufferRenderTargets[i],
                                        m_multiBufferRenderPassDescriptors[i], sz, wantsMips(i))) {
                    qCDebug(lcShaderNode) << "Failed to create multi-buffer texture" << i << "at" << sz;
                    return false;
                }
                rebuilt.append(QStringLiteral("%1:%2x%3").arg(i).arg(sz.width()).arg(sz.height()));
            }
            qCInfo(lcShaderNode) << "Rebuilt multi-buffer textures:"
                                 << "m_width=" << m_width << "m_height=" << m_height << "bufferScale=" << m_bufferScale
                                 << "passes=" << n << "rebuilt=" << rebuilt.join(QLatin1Char(' '));
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

    // A pack with exactly ONE buffer pass lands here rather than in the
    // multi-buffer branch, and it is still entitled to its per-pass scale: the
    // compositor honours bufferScales[0] for a single-pass pack
    // (passBufferScaleFor in surface_capture.cpp), so sizing from m_bufferScale
    // alone here made the same pack render at two different resolutions
    // depending on which host drew it. The feedback twin shares the size
    // because the two ping-pong.
    const QSize singleSize = passSize(0);
    if (!m_bufferTexture) {
        if (!createTextureAndRT(m_bufferTexture, m_bufferRenderTarget, m_bufferRenderPassDescriptor, singleSize,
                                wantsMips(0))) {
            qCDebug(lcShaderNode) << "Failed to create buffer texture";
            return false;
        }
        if (!ensureBufferSampler(rhi, 0)) {
            return false;
        }
        // Re-arm BEFORE B is attempted, not after both succeed. See the resize
        // branch below for why the ordering is the whole fix.
        m_bufferFeedbackCleared = false;
        if (m_bufferFeedback
            && !createTextureAndRT(m_bufferTextureB, m_bufferRenderTargetB, m_bufferRenderPassDescriptorB, singleSize,
                                   wantsMips(0))) {
            qCDebug(lcShaderNode) << "Failed to create buffer texture B (ping-pong)";
            return false;
        }
        m_srb.reset();
        m_srbB.reset();
        return true;
    }
    if (m_bufferTexture->pixelSize() != singleSize) {
        if (!createTextureAndRT(m_bufferTexture, m_bufferRenderTarget, m_bufferRenderPassDescriptor, singleSize,
                                wantsMips(0))) {
            qCDebug(lcShaderNode) << "Failed to resize buffer texture";
            return false;
        }
        // Re-armed HERE, immediately after A is replaced, rather than at the
        // bottom of the branch. A is now a brand-new texture with undefined
        // contents. If the B create below fails we return early, and next frame
        // A is already the right size so this branch is skipped entirely and
        // the B-only branch below runs instead — which used to leave the flag
        // on whatever it was. A feedback pack would then accumulate from
        // whatever the driver handed back rather than from cleared targets.
        m_bufferFeedbackCleared = false;
        if (m_bufferFeedback
            && !createTextureAndRT(m_bufferTextureB, m_bufferRenderTargetB, m_bufferRenderPassDescriptorB, singleSize,
                                   wantsMips(0))) {
            qCDebug(lcShaderNode) << "Failed to resize buffer texture B";
            return false;
        }
        m_bufferPipeline.reset();
        m_bufferSrb.reset();
        m_bufferSrbB.reset();
        m_srb.reset();
        m_srbB.reset();
    } else if (m_bufferFeedback && !m_bufferTextureB) {
        if (!createTextureAndRT(m_bufferTextureB, m_bufferRenderTargetB, m_bufferRenderPassDescriptorB, singleSize,
                                wantsMips(0))) {
            qCDebug(lcShaderNode) << "Failed to create buffer texture B (ping-pong)";
            return false;
        }
        m_bufferPipeline.reset();
        m_bufferSrb.reset();
        m_srb.reset();
        m_srbB.reset();
        // A fresh B is half of an uncleared ping-pong pair, whichever branch
        // created A. Same reason as above.
        m_bufferFeedbackCleared = false;
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
    // Both arms below log. This is the hardest failure in the file: ensurePipeline
    // treats a false here as fail-closed, prepare() then bails, and the node paints
    // nothing for the rest of its life if the condition persists. Every sibling
    // ensure* already reports its failure, by a warning or by m_shaderError, and this one used to be the silent
    // exception, so the symptom was a blank pack with an empty journal.
    if (!m_dummyChannelTexture) {
        m_dummyChannelTexture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        if (m_dummyChannelTexture->create()) {
            m_dummyChannelTextureNeedsUpload = true;
        } else {
            m_dummyChannelTexture.reset();
            if (!m_dummyChannelWarned) {
                m_dummyChannelWarned = true;
                qCWarning(lcShaderNode) << "Failed to create the 1x1 dummy channel texture"
                                        << "— every unbound channel, user-texture, wallpaper and depth slot "
                                           "substitutes it, so nothing can be drawn";
            }
            return false;
        }
    }
    if (!m_dummyChannelSampler) {
        m_dummyChannelSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                    QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!m_dummyChannelSampler->create()) {
            m_dummyChannelSampler.reset();
            if (!m_dummyChannelWarned) {
                m_dummyChannelWarned = true;
                qCWarning(lcShaderNode) << "Failed to create the dummy channel sampler"
                                        << "— every unbound channel, user-texture, wallpaper and depth slot "
                                           "substitutes it, so nothing can be drawn";
            }
            return false;
        }
    }
    return true;
}

// ============================================================================
// uploadDummyChannelTexture
// ============================================================================

// The dummy 1x1 texture is CREATED inside ensurePipeline, which prepare() runs
// AFTER uploadDirtyTextures. Its transparent-black texel would therefore not
// reach the GPU until the following frame, while the SRBs built in that same
// ensurePipeline already bind it — so on the creating frame every unsupplied
// channel or user-texture slot sampled whatever the driver left in a fresh
// allocation instead of the documented transparent black. prepare() calls this
// again after ensurePipeline for that reason; uploadDirtyTextures still calls it
// first so the common case costs nothing.
void ShaderNodeRhi::uploadDummyChannelTexture(QRhi* rhi, QRhiCommandBuffer* cb)
{
    if (!m_dummyChannelTextureNeedsUpload || !m_dummyChannelTexture) {
        return;
    }
    QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
    if (!batch) {
        // Batch pool exhausted mid-record. Ask for the frame that retries,
        // the way the grid upload in prepare() does.
        requestAnotherFrame();
        return;
    }
    batch->uploadTexture(m_dummyChannelTexture.get(), m_transparentFallbackImage);
    cb->resourceUpdate(batch);
    m_dummyChannelTextureNeedsUpload = false;
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
        // Latched because a false return here aborts prepare(), which is
        // re-entered next frame, so an unlatched warning fills the journal at
        // frame cadence. The clear below cannot be reached for a sampler that
        // already exists (the early return above takes it), so in a persistent
        // mixed failure the clear fires once and the failing index stays silent.
        // That early return is the whole argument; an earlier version of this
        // comment named the wrong freers, and resetBufferTargets is not one of
        // them at all.
        if (!m_bufferSamplerCreateWarned) {
            m_bufferSamplerCreateWarned = true;
            qCWarning(lcShaderNode) << "Failed to create buffer sampler" << index;
        }
        m_bufferSamplers[index].reset();
        return false;
    }
    m_bufferSamplerCreateWarned = false;
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
        const int n = static_cast<int>(qMin(m_bufferPaths.size(), static_cast<qsizetype>(kMaxBufferPasses)));
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
                // Bind EVERY channel slot (bindings kChannelBase..): pass i sees outputs of passes 0..i-1.
                for (int j = 0; j < kMaxBufferPasses; ++j) {
                    QRhiTexture* tex = (j < i && m_multiBufferTextures[j]) ? m_multiBufferTextures[j].get()
                                                                           : m_dummyChannelTexture.get();
                    QRhiSampler* sam =
                        (j < i && m_bufferSamplers[j]) ? m_bufferSamplers[j].get() : m_dummyChannelSampler.get();
                    if (tex && sam) {
                        bindings.append(QRhiShaderResourceBinding::sampledTexture(
                            PhosphorShaders::Bindings::kChannelBase + j, QRhiShaderResourceBinding::FragmentStage, tex,
                            sam));
                    }
                }
                appendCommonTrailerBindings(bindings, DepthAccess::WrittenThisPass);
                srb->setBindings(bindings.begin(), bindings.end());
                if (!srb->create()) {
                    m_shaderError = QStringLiteral("Failed to create multi-buffer pass SRB ") + QString::number(i);
                    return false;
                }
                m_multiBufferSrbs[i] = std::move(srb);
            }
            if (!m_multiBufferPipelines[i]) {
                // rpDesc, resolved and null-checked at the top of this iteration. This
                // used to recompute the identical expression into a second local.
                m_multiBufferPipelines[i] = createFullscreenQuadPipeline(
                    rhi, rpDesc, m_vertexShader, m_multiBufferFragmentShaders[i], m_multiBufferSrbs[i].get(),
                    /*enableBlend=*/false, /*numColorAttachments=*/m_useDepthBuffer ? 2 : 1);
                if (!m_multiBufferPipelines[i]) {
                    m_shaderError = QStringLiteral("Failed to create multi-buffer pipeline ") + QString::number(i);
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
                    PhosphorShaders::Bindings::kChannelBase + ch, QRhiShaderResourceBinding::FragmentStage, tex, sam));
            }
        }
        appendCommonTrailerBindings(bindings, DepthAccess::WrittenThisPass);
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

    auto createImageSrbSingle = [rhi,
                                 this](QRhiTexture* channel0Texture) -> std::unique_ptr<QRhiShaderResourceBindings> {
        // No dummy substitution here: the loop below already falls back to the
        // dummy pair for a null channel-0 texture or sampler, so pre-filling
        // them produced identical bindings by a second route.
        QRhiSampler* channel0Sampler = (channel0Texture && m_bufferSamplers[0]) ? m_bufferSamplers[0].get() : nullptr;
        std::unique_ptr<QRhiShaderResourceBindings> srb(rhi->newShaderResourceBindings());
        QVector<QRhiShaderResourceBinding> bindings;
        appendUboAndExtraBindings(bindings);
        for (int ch = 0; ch < kMaxBufferPasses; ++ch) {
            QRhiTexture* tex = (ch == 0 && channel0Texture) ? channel0Texture : m_dummyChannelTexture.get();
            QRhiSampler* sam = (ch == 0 && channel0Sampler) ? channel0Sampler : m_dummyChannelSampler.get();
            if (tex && sam) {
                bindings.append(QRhiShaderResourceBinding::sampledTexture(
                    PhosphorShaders::Bindings::kChannelBase + ch, QRhiShaderResourceBinding::FragmentStage, tex, sam));
            }
        }
        appendCommonTrailerBindings(bindings, DepthAccess::Sampled);
        srb->setBindings(bindings.begin(), bindings.end());
        return srb->create() ? std::move(srb) : nullptr;
    };
    auto createImageSrbMulti = [rhi, this]() -> std::unique_ptr<QRhiShaderResourceBindings> {
        std::unique_ptr<QRhiShaderResourceBindings> srb(rhi->newShaderResourceBindings());
        QVector<QRhiShaderResourceBinding> bindings;
        appendUboAndExtraBindings(bindings);
        const int n = static_cast<int>(qMin(m_bufferPaths.size(), static_cast<qsizetype>(kMaxBufferPasses)));
        QRhiTexture* dummyTex = m_dummyChannelTexture.get();
        QRhiSampler* dummySam = m_dummyChannelSampler.get();
        for (int i = 0; i < kMaxBufferPasses; ++i) {
            QRhiTexture* tex = (i < n && m_multiBufferTextures[i]) ? m_multiBufferTextures[i].get() : dummyTex;
            QRhiSampler* sam = (tex == dummyTex || !m_bufferSamplers[i]) ? dummySam : m_bufferSamplers[i].get();
            if (tex && sam) {
                bindings.append(QRhiShaderResourceBinding::sampledTexture(
                    PhosphorShaders::Bindings::kChannelBase + i, QRhiShaderResourceBinding::FragmentStage, tex, sam));
            }
        }
        appendCommonTrailerBindings(bindings, DepthAccess::Sampled);
        srb->setBindings(bindings.begin(), bindings.end());
        return srb->create() ? std::move(srb) : nullptr;
    };

    // The dummy 1x1 transparent texture backs UNSUPPLIED user-texture
    // slots 1-3 (appendUserTextureBindings): a shader that references
    // uTexture<N> without a loaded texture must read the documented
    // transparent black rather than leave the declared binding without an
    // SRB entry (strict backends reject the mismatch; lenient ones sample
    // undefined). Every consumer now hard-requires it: a multipass shader
    // binds it into unwritten iChannel slots, and appendWallpaperBinding
    // substitutes it at the wallpaper binding whenever no wallpaper is bound, which a
    // single-pass surface pack declaring uBackdrop relies on. Omitting the
    // binding instead would reproduce the very layout mismatch the dummy
    // exists to prevent, so a failed create fails the build here rather than
    // degrading silently.
    if (!ensureDummyChannelResources(rhi) || !m_dummyChannelTexture || !m_dummyChannelSampler) {
        return false;
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
        // The image pass draws the grid mesh when tessellated (indexed
        // Triangles); the buffer passes above always keep the quad strip.
        m_pipeline = createFullscreenQuadPipeline(rhi, rpDesc, m_vertexShader, m_fragmentShader, m_srb.get(),
                                                  /*enableBlend=*/true, /*numColorAttachments=*/1,
                                                  gridActive() ? QRhiGraphicsPipeline::Triangles
                                                               : QRhiGraphicsPipeline::TriangleStrip);
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
            // bound so the SRB build does not omit the uTexture0 binding entirely. This
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
        // transparent texture so the uTexture0 binding (declared by every
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
            PhosphorShaders::Bindings::kAudioSpectrum, QRhiShaderResourceBinding::FragmentStage,
            m_audioSpectrumTexture.get(), m_audioSpectrumSampler.get()));
    }
}

void ShaderNodeRhi::appendUboAndExtraBindings(QVector<QRhiShaderResourceBinding>& bindings) const
{
    bindings.append(QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo.get()));
    appendExtraBindings(bindings);
}

void ShaderNodeRhi::appendCommonTrailerBindings(QVector<QRhiShaderResourceBinding>& bindings,
                                                DepthAccess depthAccess) const
{
    appendAudioBinding(bindings);
    appendUserTextureBindings(bindings);
    appendWallpaperBinding(bindings);
    appendDepthBinding(bindings, depthAccess);
}

void ShaderNodeRhi::appendWallpaperBinding(QVector<QRhiShaderResourceBinding>& bindings) const
{
    // The wallpaper binding stays POPULATED whether or not a wallpaper is in play,
    // substituting the 1x1 dummy — the same discipline the user-texture slots
    // (the uTexture slots) already follow.
    //
    // It used to be appended only when a wallpaper existed, which was safe
    // while the wallpaper binding belonged solely to overlay packs that opt into the
    // wallpaper module and always enable it. Surface packs broke that
    // assumption: a needsBackdrop pack (the glass / blur family) declares
    // uBackdrop at this binding unconditionally, and its SPIR-V says so on
    // every host, including a daemon surface with nothing behind it. Leaving
    // the binding out there is a resource-binding layout that does not match
    // the shader, which fails the pipeline rather than degrading. The pack's
    // fallback path is selected by uHasBackdrop (0 here), not by the absence
    // of the binding.
    const bool live = wallpaperBindingLive();
    QRhiTexture* tex = live ? m_wallpaperTexture.get() : m_dummyChannelTexture.get();
    QRhiSampler* sam = live ? m_wallpaperSampler.get() : m_dummyChannelSampler.get();
    if (!tex || !sam) {
        // Unreachable in practice: ensurePipeline and ensureBufferPipeline both
        // fail closed when the dummy resources are missing, which is the only
        // way the fallback can be null. Omitting the binding here would
        // reproduce the exact layout mismatch this function exists to prevent,
        // so say so once rather than leaving a silent return to be diagnosed
        // from a pipeline failure.
        if (!m_warnedWallpaperBindingOmitted) {
            m_warnedWallpaperBindingOmitted = true;
            qCWarning(lcShaderNode) << "Wallpaper binding omitted: no wallpaper and no dummy substitute"
                                    << "(texture:" << static_cast<bool>(tex) << "sampler:" << static_cast<bool>(sam)
                                    << ") — a pack declaring uBackdrop will fail its pipeline";
        }
        return;
    }
    bindings.append(QRhiShaderResourceBinding::sampledTexture(PhosphorShaders::Bindings::kWallpaper,
                                                              QRhiShaderResourceBinding::FragmentStage, tex, sam));
}

void ShaderNodeRhi::appendDepthBinding(QVector<QRhiShaderResourceBinding>& bindings, DepthAccess access) const
{
    // The depth binding stays POPULATED whether or not a depth buffer is in play,
    // the same discipline appendWallpaperBinding follows for the wallpaper binding and
    // the user-texture slots follow for 11-14. data/overlays/shared/depth.glsl
    // declares the sampler unconditionally for any pack that includes it, so a
    // pack that includes it without also setting "depthBuffer": true would
    // otherwise present resource bindings that do not match its own SPIR-V —
    // a pipeline-create failure with nothing naming the cause, rather than a
    // graceful degrade to an empty depth read.
    //
    // A pass that WRITES the depth attachment must not also sample it (see
    // DepthAccess), which is the other route to the same substitution.
    const bool writes = access == DepthAccess::WrittenThisPass;
    const bool haveDepth = m_useDepthBuffer && m_depthTexture && m_depthSampler;
    QRhiTexture* tex = (writes || !haveDepth) ? m_dummyChannelTexture.get() : m_depthTexture.get();
    QRhiSampler* sam = (writes || !haveDepth) ? m_dummyChannelSampler.get() : m_depthSampler.get();
    if (!tex || !sam) {
        // Same argument as appendWallpaperBinding's: unreachable in practice
        // because ensurePipeline and ensureBufferPipeline both fail closed
        // when the dummy resources are missing, and omitting the binding here
        // would produce the exact layout mismatch the substitution exists to
        // prevent. Say it once rather than leaving a pipeline-create failure
        // to be diagnosed with nothing naming the cause.
        if (!m_warnedDepthBindingOmitted) {
            m_warnedDepthBindingOmitted = true;
            qCWarning(lcShaderNode) << "Depth binding omitted: no depth resources and no dummy substitute"
                                    << "(texture:" << static_cast<bool>(tex) << "sampler:" << static_cast<bool>(sam)
                                    << ") — a pack including depth.glsl will fail its pipeline";
        }
        return;
    }
    bindings.append(QRhiShaderResourceBinding::sampledTexture(PhosphorShaders::Bindings::kDepth,
                                                              QRhiShaderResourceBinding::FragmentStage, tex, sam));
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
