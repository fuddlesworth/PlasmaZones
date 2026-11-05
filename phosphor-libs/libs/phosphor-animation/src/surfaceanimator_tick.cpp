// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "surfaceanimator_p.h"

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorRendering/ShaderEffect.h>

#include <private/qquickhoverhandler_p.h>

#include <vector>

namespace PhosphorAnimationLayer {

/// Drive every active animation by one tick. By-key snapshot
/// iteration is safe against re-entrant erase from legCompleted.
void SurfaceAnimator::Private::tickAll()
{
    // Drain the graveyard — AVs parked here last tick are now safe
    // to destroy (the spec.onComplete advance() frame has unwound).
    m_pendingDestroy.clear();

    // Sweep the reuse cache for stale entries — surfaces whose
    // QPointers all auto-nulled because the QML scene tore the
    // shader pieces down independently of our cancel paths
    // (consumer-driven anchor reparent, Loader unload outside the
    // animator's notice). The destroyed-signal cleanup catches
    // most cases, but it fires on Surface destruction, not on
    // shaderItem/Source/Anchor destruction in isolation. Without
    // this sweep, a surface that survives but loses its anchor
    // sits with a dead reuse entry indefinitely; the next leg's
    // reuse-mismatch path destroys it then, but the entry stays
    // wasting a hash slot until then.
    for (auto it = m_pendingReuse.begin(); it != m_pendingReuse.end();) {
        const PendingReuseShader& p = it->second;
        if (!p.shaderItem && !p.shaderSource && !p.shaderAnchor) {
            it = m_pendingReuse.erase(it);
        } else {
            ++it;
        }
    }

    // Snapshot — legCompleted may erase from m_tracks while iterating.
    std::vector<TrackKey> keys;
    keys.reserve(m_tracks.size());
    for (auto& [k, _] : m_tracks) {
        keys.push_back(k);
    }
    // Real-time delta for shader iTimeDelta — measured between
    // SurfaceAnimator ticks (which fire at kTickIntervalMs cadence
    // while any track is in flight). First tick after a quiescent
    // period reports 0 instead of a stale wall-clock gap, matching
    // OverlayService::updateShaderUniforms's clamp-on-resume idiom
    // for the overlay path. Capped at the shared
    // PhosphorAnimation::Limits::MaxShaderTimeDeltaSeconds (100 ms)
    // so a sleep/resume hiccup doesn't push a multi-second jump
    // into the shader. Both runtimes reference the same constant —
    // bumping one without the other was the prior drift risk.
    const qreal kMaxShaderDeltaSecs = static_cast<qreal>(PhosphorAnimation::Limits::MaxShaderTimeDeltaSeconds);
    const qint64 nowNs = m_clock.now().count();
    qreal shaderDeltaSecs = 0.0;
    if (m_lastShaderTickNs > 0) {
        shaderDeltaSecs = qBound(qreal(0.0), qreal(nowNs - m_lastShaderTickNs) / 1.0e9, kMaxShaderDeltaSecs);
    }
    m_lastShaderTickNs = nowNs;

    for (const auto& k : keys) {
        auto it = m_tracks.find(k);
        if (it == m_tracks.end()) {
            continue;
        }
        if (it->second.opacity) {
            it->second.opacity->advance();
        }
        // Re-find: advance may have completed + erased via legCompleted.
        it = m_tracks.find(k);
        if (it == m_tracks.end()) {
            continue;
        }
        if (it->second.scale) {
            it->second.scale->advance();
        }
        it = m_tracks.find(k);
        if (it == m_tracks.end()) {
            continue;
        }
        if (it->second.shaderTime) {
            it->second.shaderTime->advance();
        }
        // Re-find again: shaderTime->advance may have completed +
        // erased via legCompleted. Then push per-frame dynamic
        // uniforms (iTimeDelta, iFrame, iMouse, audio spectrum)
        // so the next paint sees the latest values. Cheap on
        // identity (each setter early-returns when unchanged).
        it = m_tracks.find(k);
        if (it == m_tracks.end()) {
            continue;
        }
        if (it->second.shaderItem) {
            pushDynamicShaderUniforms(it->second, shaderDeltaSecs);
        }
    }
    // Stop the driver if every track completed during this tick.
    if (m_tracks.empty() && m_driverTimer.isActive()) {
        m_driverTimer.stop();
        // Reset the tick-time anchor so the first tick after the
        // next ensureDriving() reports 0 delta instead of the
        // wall-clock gap accrued while the driver was idle.
        m_lastShaderTickNs = 0;
    }
}

/// Push per-frame dynamic shader uniforms onto an active track's
/// shader item. Mirrors the overlay path's per-frame
/// `OverlayService::updateShaderUniforms` writes for `iTimeDelta`
/// and `iFrame`, plus the on-update CAVA `audioSpectrum` dispatch —
/// animation shaders attached programmatically by `SurfaceAnimator`
/// have no QML scene to bind through, so the animator pumps these
/// directly each tick. Cheap on identity: every `ShaderEffect`
/// setter early-returns on unchanged input.
///
/// Note: `iMouse` is NOT pumped here — it's auto-driven by the
/// per-shader-item `QQuickHoverHandler` installed in
/// `attachShaderToAnchor`, mirroring the overlay path's QML-side
/// `MouseArea`/`HoverHandler` wiring exactly.
void SurfaceAnimator::Private::pushDynamicShaderUniforms(Track& track, qreal deltaSecs)
{
    if (!track.shaderItem) {
        return;
    }
    track.shaderItem->setITimeDelta(deltaSecs);
    // Post-increment so the FIRST push after attach reports
    // iFrame=0, matching overlay's `m_frameCount.fetch_add(1)`
    // post-increment semantics.
    track.shaderItem->setIFrame(track.shaderFrameCount++);
    if (!m_audioSpectrum.isEmpty()) {
        track.shaderItem->setAudioSpectrum(m_audioSpectrum);
    }
}

/// Seed the dynamic uniforms (audio spectrum, iMouse) onto a freshly
/// attached or reused shader item BEFORE its first paint, so the
/// initial frame doesn't render silent or with stale cursor data
/// when the consumer already has audio flowing or the shader is
/// being reused after a parked idle phase. Resets the per-leg
/// `iFrame` counter to 0 — a reused shader item starts a new leg's
/// frame counter from scratch even though the underlying
/// QQuickShaderEffect is the same instance.
///
/// `iMouse` is seeded by querying the persistent
/// `QQuickHoverHandler` installed by `attachShaderToAnchor`. On
/// fresh attach the handler reports `isHovered()` false until Qt
/// delivers the first hover event, so the seed lands `(-1, -1)` —
/// matching the GLSL contract's off-region sentinel. On reuse the
/// handler reflects the live cursor state at wake time, which
/// prevents a stale value from the previous leg (set while the
/// item was parked invisible) from leaking into the new leg's
/// first frame.
void SurfaceAnimator::Private::seedShaderUniformsAtAttach(Track& track)
{
    track.shaderFrameCount = 0;
    if (!track.shaderItem) {
        return;
    }
    if (!m_audioSpectrum.isEmpty()) {
        track.shaderItem->setAudioSpectrum(m_audioSpectrum);
    }
    // FindDirectChildrenOnly: the hover handler is parented
    // directly to the shaderItem in attachShaderToAnchor; a
    // recursive search could pick up an unrelated handler from a
    // QQuickItem subtree (e.g. a future scene-graph internal that
    // installs its own handler) and seed iMouse from the wrong
    // source.
    QQuickHoverHandler* hover = track.shaderItem->findChild<QQuickHoverHandler*>(QString{}, Qt::FindDirectChildrenOnly);
    if (hover && hover->isHovered()) {
        track.shaderItem->setIMouse(hover->point().position());
    } else {
        track.shaderItem->setIMouse(QPointF(-1.0, -1.0));
    }
}

/// Ensure the driver timer is running. Idempotent.
void SurfaceAnimator::Private::ensureDriving()
{
    if (!m_driverTimer.isActive()) {
        m_driverTimer.start();
    }
}

} // namespace PhosphorAnimationLayer
