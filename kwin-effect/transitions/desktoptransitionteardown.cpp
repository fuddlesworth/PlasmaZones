// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "desktoptransitionmanager.h"

#include "plasmazoneseffect/plasmazoneseffect.h"
#include "plasmazoneseffect/shader_internal.h"

#include <core/output.h>
#include <effect/effecthandler.h>
#include <opengl/glshader.h>
#include <opengl/gltexture.h>

// The teardown part of DesktopTransitionManager: how a transition ENDS and how
// its GPU resources go back. desktoptransitionmanager.cpp keeps the drive part
// (resolve, begin, blend), desktoptransitioncapture.cpp the capture part, and
// desktoptransitionshader.cpp the assembly part. Settling one output, releasing
// the fullscreen claim, the wall-clock reap, the removal handlers and the cache
// clears all serve the one question of ending cleanly, so they live here.
//
// The invariant every function below shares: any path that frees a GLTexture or
// GLShader off the paint thread must make the compositor context current first
// (ensureGlContextCurrent), because those destructors issue glDelete* calls.
namespace PlasmaZones {

void DesktopTransitionManager::endOutput(KWin::LogicalOutput* screen)
{
    m_active.erase(screen);
    releaseFullScreenClaimIfIdle();
}

void DesktopTransitionManager::releaseFullScreenClaimIfIdle(bool force)
{
    if (!m_active.empty() || !m_fullScreenClaimed || !KWin::effects) {
        return;
    }
    // Hold the claim while show-desktop is up. setActiveFullScreenEffect cancels
    // show desktop in EITHER direction, so releasing here would cancel a peek the
    // user triggered DURING our switch: the windows hide, then spontaneously come
    // back the moment the switch settles. begin()'s !isShowingDesktop() arm only
    // stops us TAKING the claim across that state; this stops us dropping it.
    // Keeping m_fullScreenClaimed set is what makes the retry work — see the
    // header for the showingDesktopChanged → beginPeek → reap → here path.
    if (!force && PlasmaZonesEffect::isShowingDesktop()) {
        return;
    }
    // Clear the claim only while KWin still reports it as OURS. Overview or Cube
    // can take the screen while our transition is in flight; writing nullptr
    // unconditionally would drop THEIR claim and unsuppress every effect that was
    // bowing out to them. Dropping our own flag either way keeps us from later
    // clearing a claim we no longer hold.
    if (KWin::effects->activeFullScreenEffect() == m_effect) {
        KWin::effects->setActiveFullScreenEffect(nullptr);
    }
    m_fullScreenClaimed = false;
}

void DesktopTransitionManager::ensureGlContextCurrent()
{
    if (KWin::effects) {
        KWin::effects->makeOpenGLContextCurrent();
    }
}

void DesktopTransitionManager::scheduleRepaints()
{
    if (!KWin::effects) {
        return; // only reached from postPaintScreen (effects live), guard for parity
    }
    // Reap expired transitions by WALL CLOCK, not by being painted. paintOutput()
    // is the only other settle path, and it runs only for an output that is
    // actually painting — so an output that stops being painted mid-transition
    // (DPMS-asleep but enabled, or one KWin has stopped rendering) would sit in
    // m_active forever while a sibling kept painting, and the fullscreen claim
    // would never release. postPaintScreen calls us on every frame regardless of
    // which output painted, so expiry is handled here even for an unpainted one.
    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
    bool erasedAny = false;
    for (auto it = m_active.begin(); it != m_active.end();) {
        if (nowMs - it->second.startTimeMs >= it->second.durationMs) {
            if (!erasedAny) {
                // The captured GLTextures free on erase and want a live context.
                ensureGlContextCurrent();
                erasedAny = true;
            }
            // Repaint the output we are about to drop. The blend was the last
            // thing painted on it, and the settled scene still has to be drawn —
            // KWin only composites on damage, so without this the output keeps
            // presenting the frozen final blend frame until something unrelated
            // damages it. paintOutput's settle path does not need this because it
            // returns false and paintScreen falls through to the real scene in the
            // SAME frame; a reap has no such frame to fall through to.
            KWin::effects->addRepaint(it->first->geometry());
            // A reaped SHOW leg also drops its output's bare-desktop cache
            // entry: an expired never-painted PeekShow would otherwise retain
            // an output-sized GPU texture until the next hide leg (the cache
            // is consumed only in paintOutput). PeekShow ONLY — never a hide
            // leg's: all outputs' legs share one expiry, so on multi-monitor
            // the first output to settle in paintOutput reaches this reap
            // before the sibling's own settling paint has run, and erasing the
            // sibling's hide-leg cache here would silently kill its show-leg
            // animation on essentially every peek. A reaped PeekHide's
            // retained cache matches the post-settle retention the design
            // already accepts.
            if (it->second.kind == Kind::PeekShow) {
                m_peekDesktopCache.erase(it->first);
            }
            it = m_active.erase(it);
        } else {
            ++it;
        }
    }
    if (erasedAny) {
        releaseFullScreenClaimIfIdle();
    }
    // Keys are always non-null: begin() skips null outputs and outputRemoved()
    // reaps a disconnected one, so a null screen can never be stored.
    for (const auto& entry : m_active) {
        KWin::effects->addRepaint(entry.first->geometry());
    }
}

void DesktopTransitionManager::outputRemoved(KWin::LogicalOutput* screen)
{
    // A disconnected output must not linger as a key in m_active: paintOutput()
    // and scheduleRepaints() deref the key (UAF), and the fullscreen-effect claim
    // would never release once its output vanished mid-transition. endOutput()
    // drops the entry and releases the claim when the last transition goes. The
    // peek-desktop cache entry goes with it — its texture is output-sized and
    // its key would dangle.
    const bool hasTransition = m_active.find(screen) != m_active.end();
    const bool hasPeekCache = m_peekDesktopCache.find(screen) != m_peekDesktopCache.end();
    if (!hasTransition && !hasPeekCache) {
        return; // nothing held for this output
    }
    // screenRemoved fires off the paint thread; both erases free GLTextures,
    // which want a current GL context (endOutput's other caller, paintOutput, is
    // already on the paint thread with one current).
    ensureGlContextCurrent();
    m_peekDesktopCache.erase(screen);
    if (hasTransition) {
        endOutput(screen);
    }
}

void DesktopTransitionManager::desktopRemoved(KWin::VirtualDesktop* desktop)
{
    // A desktop removed mid-switch leaves any OutputTransition still referencing
    // it as `from` holding a dangling VirtualDesktop*: the deferred
    // captureDesktop(tr.from, ...) only ever compares the pointer (isOnDesktop),
    // never derefs it, so this is not a crash — but a freed pointer must not
    // linger and be compared against a live desktop. Drop every transition whose
    // outgoing desktop just vanished.
    bool erasedAny = false;
    for (auto it = m_active.begin(); it != m_active.end();) {
        // Kind-guarded rather than pointer-matched alone: peek legs carry a null
        // `from` as their sentinel, so a null @p desktop would otherwise match
        // every one of them and reap the whole peek path.
        if (it->second.kind == Kind::Switch && it->second.from == desktop) {
            // Erasing frees the entry's captured GLTextures; desktopRemoved fires
            // off the paint thread, so make the context current before the first
            // free — only when there is actually something to free.
            if (!erasedAny) {
                ensureGlContextCurrent();
                erasedAny = true;
            }
            // Repaint the output for the same reason the wall-clock reap does: the
            // blend was the last thing drawn on it and the settled scene still
            // needs a frame, which nothing else will schedule.
            if (KWin::effects) {
                KWin::effects->addRepaint(it->first->geometry());
            }
            it = m_active.erase(it);
        } else {
            ++it;
        }
    }
    if (erasedAny) {
        releaseFullScreenClaimIfIdle();
    }
}

void DesktopTransitionManager::invalidateShaderCache()
{
    // Fires from the AnimationShaderRegistry file watcher between frames, where the
    // compositor GL context is NOT current. m_shaderCache owns GLShaders whose
    // destruction issues glDelete* calls that want a current context — the same
    // discipline reset() applies. Teardown (!effects) reclaims them regardless.
    ensureGlContextCurrent();
    m_shaderCache.clear();
}

void DesktopTransitionManager::reset()
{
    // Teardown path (compositor reset / plugin unload). Make the compositor
    // context current so the captured GLTextures (m_active) and compiled
    // GLShaders (m_shaderCache) free on a live context; when KWin::effects is
    // already gone the driver is tearing GL down and reclaims them regardless.
    // Clearing m_shaderCache HERE — not leaving it for ~DesktopTransitionManager,
    // which deliberately can't make a context current — is what makes this the
    // real "release GL resources" path the header documents.
    ensureGlContextCurrent();
    m_active.clear();
    m_shaderCache.clear();
    m_peekDesktopCache.clear();
    // force: the show-desktop deferral must not apply here. A claim pointing at an
    // effect the compositor is dropping would outlive it, and the cancel the
    // deferral protects against is moot when the effect is going away anyway.
    releaseFullScreenClaimIfIdle(true);
}

} // namespace PlasmaZones
