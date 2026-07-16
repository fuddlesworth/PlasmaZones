// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include "shader_internal.h"

#include <effect/effecthandler.h>
#include <opengl/glshader.h>
#include <opengl/gltexture.h>

#include <PhosphorAudio/CavaSpectrumProvider.h>
#include <PhosphorAudio/IAudioSpectrumProvider.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>

#include <QImage>
#include <QLoggingCategory>

#include <memory>
#include <epoxy/gl.h>

// Audio-reactive surface decorations and animation packs (CAVA). The
// compositor has no daemon-style audio path, so the effect runs its own
// CavaSpectrumProvider and feeds the spectrum to audio-reactive decoration
// packs (surface_audio.glsl) and animation packs (the animation family's
// audio.glsl module, bound from paint_pipeline's transition draw). Split out
// of surfacelayers.cpp, where the fold itself (which binds the uploaded
// texture) still lives. See the member docs in plasmazoneseffect.h.

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace {
// A spectrum that has not changed for this long is treated as silence: the
// per-vsync recomposite stands down (audioReactiveDriving) and the next changed
// frame is treated as an idle→active edge that re-primes the paint loop
// (onEffectAudioSpectrum). One threshold shared by both so the "still driving"
// and "was idle" tests are exact complements.
constexpr qint64 kAudioIdleQuietMs = 200;
} // namespace

bool PlasmaZonesEffect::audioActive() const
{
    return m_enableAudioVisualizer && m_audioProvider && m_audioProvider->isRunning() && m_audioSpectrumSize > 0;
}

bool PlasmaZonesEffect::audioReactiveDriving() const
{
    if (!audioActive() || m_audioSpectrumLastChangeMs < 0) {
        return false;
    }
    // Quiet window: once the spectrum stops changing (a paused / silent track
    // settles to repeated frames, deduped in onEffectAudioSpectrum) let the
    // per-vsync recomposite idle. A fresh spectrum re-stamps the change time and
    // primes a repaint, so the loop resumes the instant audio returns.
    return (ShaderInternal::shaderClockNowMs() - m_audioSpectrumLastChangeMs) < kAudioIdleQuietMs;
}

bool PlasmaZonesEffect::hasAudioReactiveDecoration() const
{
    // Read the audio flag from pack METADATA (SurfaceShaderEffect::audio), not a
    // compiled shader, so the run gate resolves before a window's first paint.
    // A decoration entry existing (regardless of shaderApplied, which is set a
    // step later by reconcileDecorationShader) means the window will render this
    // chain — that is the gate; a shaderApplied check here would miss the entry
    // updateWindowDecoration just inserted and delay cava startup a cycle.
    for (auto it = m_windowDecorations.cbegin(); it != m_windowDecorations.cend(); ++it) {
        for (const QString& packId : it->chain) {
            if (m_surfaceShaderRegistry.effect(packId).audio) {
                return true;
            }
        }
    }
    return false;
}

bool PlasmaZonesEffect::hasAudioReactiveAnimation() const
{
    // Metadata-only scan, mirroring hasAudioReactiveDecoration: collect every
    // effect id a transition could resolve (tree baseline + direct overrides +
    // animation-rule effectId payloads) and test the pack's audio flag. The
    // scan is small (a handful of overrides and rules) and only runs from
    // syncEffectAudioState, so no caching is needed.
    const auto& registry = m_shaderManager.shaderRegistry();
    const auto isAudioPack = [&registry](const QString& id) {
        return !id.isEmpty() && registry.effect(id).useAudio;
    };
    const auto& tree = m_shaderManager.profileTree();
    // Built-in per-event DEFAULT packs (resolveShaderWithDefault injects
    // window-morph / fade for a truly-unset path) are deliberately NOT
    // scanned: no built-in default pack declares `audio: true`, so an unset
    // path can never resolve to an audio pack. If a future built-in default
    // ever declares audio, this scan must route through
    // resolveShaderWithDefault instead of raw overrides or cava never warms
    // for it.
    if (isAudioPack(tree.baseline().effectiveEffectId())) {
        return true;
    }
    const QStringList paths = tree.overriddenPaths();
    for (const QString& path : paths) {
        if (isAudioPack(tree.directOverride(path).effectiveEffectId())) {
            return true;
        }
    }
    // Rules: any enabled rule action carrying an EffectId param can route a
    // transition to that pack (the anim-shader slot). Reading the param off
    // every action over-approximates slightly (a non-shader action never
    // carries EffectId, so the extra reads are cheap misses), which errs on
    // the safe side for a run gate.
    for (const PhosphorRules::Rule& rule : m_shaderManager.m_ruleAnimationRules) {
        // Defensive only: loadRuleAnimationsFromDbus already prunes disabled
        // rules before populating this list; the check stays so this scan
        // remains correct if that upstream filter ever changes.
        if (!rule.enabled) {
            continue;
        }
        for (const PhosphorRules::RuleAction& action : rule.actions) {
            if (isAudioPack(action.params.value(PhosphorRules::ActionParam::EffectId).toString())) {
                return true;
            }
        }
    }
    return false;
}

void PlasmaZonesEffect::onEffectAudioSpectrum(const QVector<float>& spectrum)
{
    // Store on the compositor thread; the composite fold uploads it to the GL
    // texture on the next paint (m_audioSpectrumDirty). Skip an identical frame
    // (cava repeats the last value on a silent track): no upload/repaint churn,
    // and the un-stamped change time lets audioReactiveDriving() settle to idle.
    if (m_audioSpectrum == spectrum) {
        return;
    }
    const qint64 now = ShaderInternal::shaderClockNowMs();
    // Was the paint loop idle (first spectrum, or a silence gap past the quiet
    // window)? Only the idle→active EDGE needs a manual kick; steady-state
    // repaints are carried by postPaintScreen's windowSurfaceAnimates loop, which
    // is visibility-gated (isOnCurrentDesktop). Priming every frame would
    // full-recomposite the screen at the cava framerate even for a minimized or
    // off-desktop audio border — exactly what the visibility gate avoids.
    const bool wasIdle = m_audioSpectrumLastChangeMs < 0 || (now - m_audioSpectrumLastChangeMs) >= kAudioIdleQuietMs;
    m_audioSpectrum = spectrum;
    m_audioSpectrumSize = static_cast<int>(spectrum.size());
    m_audioSpectrumDirty = true;
    m_audioSpectrumLastChangeMs = now;
    // An audio border emits no damage of its own, so on the resume edge damage
    // the screen once to restart the self-sustaining loop.
    if (wasIdle && KWin::effects && audioActive()) {
        KWin::effects->addRepaintFull();
    }
}

void PlasmaZonesEffect::scheduleEffectAudioSync()
{
    if (m_audioSyncScheduled) {
        return;
    }
    m_audioSyncScheduled = true;
    // Defer to event-loop return so a remove-then-readd (focus change removes
    // then re-adds a decoration) or the two async settings replies collapse to
    // ONE syncEffectAudioState — no blocking cava stop()+respawn on the
    // synchronous refresh path, and no kill when a decoration is re-added in the
    // same turn. `this` as the context object discards the call if the effect is
    // destroyed before it runs.
    QMetaObject::invokeMethod(
        this,
        [this]() {
            m_audioSyncScheduled = false;
            syncEffectAudioState();
        },
        Qt::QueuedConnection);
}

void PlasmaZonesEffect::syncEffectAudioState()
{
    const bool wantRun = m_enableAudioVisualizer && (hasAudioReactiveDecoration() || hasAudioReactiveAnimation());
    if (wantRun) {
        if (!m_audioProvider) {
            // Lazily created on first need so a session that never uses an audio
            // decoration spawns no cava process.
            auto provider = std::make_unique<PhosphorAudio::CavaSpectrumProvider>();
            connect(provider.get(), &PhosphorAudio::IAudioSpectrumProvider::spectrumUpdated, this,
                    &PlasmaZonesEffect::onEffectAudioSpectrum);
            m_audioProvider = std::move(provider);
        }
        if (!m_audioProvider->isAvailable()) {
            // Graceful fallback: the pack already renders as a plain static
            // border when iAudioSpectrumSize stays 0 (no spectrum ever arrives).
            // Warn ONCE — this path is re-entered on every sync while the audio
            // pack is present, so an unconditional warn would spam the log.
            if (!m_audioUnavailableWarned) {
                qCWarning(lcEffect) << "Audio-reactive pack active (decoration or animation) but cava is not "
                                       "installed; audio-reactive visuals render static";
                m_audioUnavailableWarned = true;
            }
            return;
        }
        m_audioProvider->setOptions(m_audioOptions);
        if (!m_audioProvider->isRunning()) {
            m_audioProvider->start();
            // Kick one repaint so the first spectrum has a consumer; the
            // per-window animate loop then keeps the border repainting.
            if (KWin::effects) {
                KWin::effects->addRepaintFull();
            }
        }
        return;
    }
    // Not wanted (toggle off, or no audio decoration or animation pack left):
    // stop capture and clear
    // the spectrum so audioActive() goes false and any lingering audio border
    // falls back to static on its next paint.
    const bool wasLive = (m_audioProvider && m_audioProvider->isRunning()) || m_audioSpectrumSize > 0;
    if (m_audioProvider && m_audioProvider->isRunning()) {
        m_audioProvider->stop();
    }
    m_audioSpectrum.clear();
    m_audioSpectrumSize = 0;
    m_audioSpectrumDirty = false;
    m_audioSpectrumLastChangeMs = -1;
    // Re-arm the not-installed warning so a later session (cava installed
    // meanwhile) can report again if it is still missing.
    m_audioUnavailableWarned = false;
    // If audio was live, damage the screen once so a visible reactive border
    // repaints to its static (silent) look now. windowSurfaceAnimates returns
    // false the instant audioActive() drops, so without this a pure toggle-off
    // (which lands no window damage of its own) leaves the border frozen on its
    // last pulsed frame until unrelated damage arrives. The last-decoration path
    // already damages the window, so this is idempotent there.
    if (wasLive && KWin::effects) {
        KWin::effects->addRepaintFull();
    }
}

bool PlasmaZonesEffect::ensureAudioSpectrumTexture()
{
    if (m_audioSpectrumSize <= 0) {
        return false;
    }
    if (m_audioSpectrumDirty || !m_audioSpectrumTex) {
        // Rebuild the whole `bars×1` texture — it is tiny (≤256 texels) and a
        // fresh upload sidesteps a resize/allocate dance on a bar-count change.
        // R = bar value in 0..1, matching the daemon's RGBA8 layout so a pack
        // reads the same texel on either runtime.
        const int bars = m_audioSpectrumSize;
        QImage img(bars, 1, QImage::Format_RGBA8888);
        for (int i = 0; i < bars; ++i) {
            const float v = qBound(0.0f, m_audioSpectrum[i], 1.0f);
            const int u = qRound(v * 255.0f);
            img.setPixel(i, 0, qRgba(u, 0, 0, 255));
        }
        std::unique_ptr<KWin::GLTexture> tex = KWin::GLTexture::upload(img);
        if (!tex) {
            // Leave m_audioSpectrumDirty set so the next frame retries instead of
            // stranding a stale/absent texture.
            qCWarning(lcEffect) << "audio spectrum GLTexture::upload failed for" << bars << "bars; retry next frame";
            return false;
        }
        tex->setFilter(GL_LINEAR); // audioBarSmooth() samples with texture()
        tex->setWrapMode(GL_CLAMP_TO_EDGE);
        m_audioSpectrumTex = std::move(tex);
        m_audioSpectrumDirty = false;
    }
    return m_audioSpectrumTex != nullptr;
}

KWin::GLTexture* PlasmaZonesEffect::transparentFallbackTexture()
{
    // Shared 1x1 transparent texture bound in place of a REFERENCED but
    // unsupplied user-texture sampler (surface fold units 7-9, animation
    // units 1-3), and in place of an iChannel the surface fold declares but
    // rendered no buffer for (units 1-4).
    // A classic default-block sampler with no bind reads unit 0
    // — live window content — instead of the contract's documented
    // transparent black; this fallback makes the documented behaviour real.
    // Lazily created on a paint path (GL context current); freed with the
    // effect. Mirrors the daemon's dummy-channel fallback in ShaderNodeRhi.
    if (!m_transparentFallbackTex) {
        QImage img(1, 1, QImage::Format_RGBA8888);
        img.fill(Qt::transparent);
        m_transparentFallbackTex = KWin::GLTexture::upload(img);
        if (m_transparentFallbackTex) {
            m_transparentFallbackTex->setFilter(GL_LINEAR);
            m_transparentFallbackTex->setWrapMode(GL_CLAMP_TO_EDGE);
        }
    }
    return m_transparentFallbackTex.get();
}

bool PlasmaZonesEffect::bindSurfaceAudio(KWin::GLShader* shader, int iAudioSpectrumSizeLoc, int uAudioSpectrumLoc,
                                         bool animating)
{
    // This helper only binds the ready texture — it never uploads. The fold
    // uploads up front (renderSurfaceChainComposite, see the note there); the
    // animation path uploads inline just before calling, with the active unit
    // parked on kSurfaceAudioUnit so the upload lands on its destination
    // (paint_pipeline.cpp's audio block).
    //
    // @p animating is false for a chain Decorations.Performance has PAUSED, and the
    // spectrum is then treated exactly as it is when no audio is playing: bar count 0,
    // the pack renders its silent look. This is not cosmetic tidying. packVariesPerFrame
    // stops counting audio as per-frame while paused so the chain becomes CACHEABLE, and
    // a cached composite fed by a live spectrum is a contradiction: the window would hold
    // still on its cached frame yet bake a different beat into it on every fold the
    // capture forced, which for the archetypal case (an unfocused music player, redrawing
    // its own UI constantly) is most frames. Take the input away or do not cache the
    // fold — not one without the other.
    //
    // SILENCED, not frozen — the pack settles rather than holding one arbitrary beat.
    // Freezing would mean a per-window snapshot of the spectrum, and a bar stuck at
    // whatever level the last beat left it is not obviously the better picture. See the
    // note on packVariesPerFrame.
    const bool live = animating && audioActive() && m_audioSpectrumTex != nullptr;
    // Push the bar count (0 when not live) so surface_audio.glsl's helpers gate
    // themselves off and the pack renders static without a bound sampler. Derive
    // the count from the BOUND texture's width, not m_audioSpectrumSize: if an
    // upload failed during a bar-count change the retained texture is still the
    // old size, and pushing the new (larger) size would let audioBar() index past
    // the texture for one frame (undefined texelFetch). The dirty flag reuploads
    // at the new size next frame.
    if (iAudioSpectrumSizeLoc >= 0) {
        shader->setUniform(iAudioSpectrumSizeLoc, live ? m_audioSpectrumTex->width() : 0);
    }
    if (uAudioSpectrumLoc < 0) {
        return false;
    }
    // Bind SOMETHING to the sampler either way. An unset sampler2D reads texture unit 0,
    // which during the fold holds the RUNNING COMPOSITE — a pack that samples the
    // spectrum without first checking iAudioSpectrumSize would sample the window itself.
    // The helpers in surface_audio.glsl do all check, so this is inert today, but it is
    // the same hazard the iChannel and user-texture slots bind a transparent fallback to
    // prevent, and silence is now the steady state for every paused audio pack rather
    // than a rarity.
    //
    // Park the destination unit BEFORE resolving the texture. transparentFallbackTexture()
    // creates it lazily on first use, and GLTexture::upload binds the new texture to
    // whatever unit is ACTIVE — which here is TEXTURE0, holding the running composite. Do
    // it the other way round and the first paused audio pack of the session rebinds unit 0
    // to a 1x1 transparent texture and samples the window as transparent black: one frame
    // of a vanished window. The sibling call sites park first for exactly this reason, and
    // the fold hoists the spectrum upload to the top for it too.
    glActiveTexture(GL_TEXTURE0 + ShaderInternal::kSurfaceAudioUnit);
    KWin::GLTexture* const bound = live ? m_audioSpectrumTex.get() : transparentFallbackTexture();
    if (bound) {
        bound->bind();
    }
    glActiveTexture(GL_TEXTURE0);
    if (!bound) {
        return false;
    }
    shader->setUniform(uAudioSpectrumLoc, ShaderInternal::kSurfaceAudioUnit);
    return true;
}

} // namespace PlasmaZones
