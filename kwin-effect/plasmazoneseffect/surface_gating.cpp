// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Decoration ANIMATION GATING and the queries that drive it.
//
// Split out of surfacelayers.cpp, which owns the composite fold itself, to keep
// that TU under the 800-line limit (the same reason surface_backdrop.cpp and
// surface_audio.cpp were split out of it).
//
// What lives here decides WHETHER a decorated window's chain is driven to repaint
// at all — the Decorations.Performance gates — plus the two queries that feed that
// decision and the shader clock they share. The fold in surfacelayers.cpp is only
// ever reached for a window this file has already let through.

#include "../plasmazoneseffect.h"

#include "shader_internal.h"
#include "types.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>

#include <QRectF>
#include <QScopeGuard>

#include <optional>

namespace PlasmaZones {

// Continuous seconds for the surface `iTime` uniform, relative to an epoch
// captured on first use so the value starts near 0 (a steady_clock value since
// boot is large enough to lose visible sub-frame precision as a float).
float PlasmaZonesEffect::surfaceShaderTimeSeconds()
{
    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
    if (m_surfaceTimeEpochMs < 0) {
        m_surfaceTimeEpochMs = nowMs;
    }
    return static_cast<float>(static_cast<double>(nowMs - m_surfaceTimeEpochMs) / 1000.0);
}

// Wake every decorated window: put it back in the paint loop with one repaint.
//
// A window paused by one of the Decorations.Performance gates emits no damage of
// its own, so when a gate OPENS (a settings flip, the session resuming, the daemon
// dying while we were idle) it would otherwise stay frozen on its last composite
// until something unrelated happened to damage it. From here the normal per-frame
// driver in postPaintScreen takes over.
//
// Flagged as m_selfRepainting, like the per-frame driver: what went stale while the
// chain was paused is the COMPOSITE (iTime advanced), not the capture. The
// windowDamaged connection stays live throughout the pause, so any real client
// damage already cleared captureValid. Letting this repaint invalidate the capture
// too would force the fold's most expensive step — a full effects->drawWindow()
// re-entry — for every decorated window on every gate flip, for nothing.
void PlasmaZonesEffect::repaintAllDecorations()
{
    if (!KWin::effects || m_windowDecorations.isEmpty()) {
        return;
    }
    m_selfRepainting = true;
    auto clearSelfRepaint = qScopeGuard([this] {
        m_selfRepainting = false;
    });
    for (auto it = m_windowDecorations.cbegin(); it != m_windowDecorations.cend(); ++it) {
        if (!it->shaderApplied) {
            continue;
        }
        KWin::EffectWindow* const sw = findWindowById(it.key());
        // Same exact-id / deleted / on-desktop discipline as the per-frame driver in
        // postPaintScreen: findWindowById's fuzzy appId fallback can resolve a
        // same-app sibling for a stale id, and an off-desktop window has nothing to
        // wake.
        if (!sw || getWindowId(sw) != it.key() || sw->isDeleted() || !sw->isOnCurrentDesktop()) {
            continue;
        }
        sw->addRepaintFull();
        if (it->outerPadding > 0) {
            QRectF padded = sw->expandedGeometry();
            if (padded.isEmpty()) {
                padded = sw->frameGeometry();
            }
            const int pad = it->outerPadding;
            KWin::effects->addRepaint(KWin::RectF(padded.adjusted(-pad, -pad, pad, pad)));
        }
    }
}

bool PlasmaZonesEffect::decorationMayAnimate(KWin::EffectWindow* w) const
{
    if (!w) {
        return false;
    }
    // Nobody is watching an animation they walked away from, and this is where the
    // bulk of the wasted power goes: a paused chain lets the GPU fall out of its
    // top performance state entirely, which shrinking the per-frame work cannot.
    if (m_pauseAnimationWhenIdle && m_sessionIdle) {
        return false;
    }
    // Only the window in use shimmers. Divides the continuous redraw by the
    // decorated-window count, which is the single biggest lever available while
    // the user is still at the machine.
    if (m_animateFocusedOnly && KWin::effects && KWin::effects->activeWindow() != w) {
        return false;
    }
    return true;
}

// True when any pack in the window's resolved chain references iTime (main or a
// buffer pass) — i.e. the decoration animates and must be driven to repaint.
// Uses the per-pack compiled cache (a hit after the first paint compiled it), so
// this is a few hash lookups per call. A window whose packs are not yet compiled
// (or all static) returns false; the first content paint compiles them and the
// next postPaintScreen picks the animation up.
bool PlasmaZonesEffect::windowSurfaceAnimates(const QString& windowId)
{
    const auto it = m_windowDecorations.constFind(windowId);
    if (it == m_windowDecorations.constEnd()) {
        return false;
    }
    // A focus ramp in flight (value strictly between 0 and 1) needs continuous
    // repaints so uSurfaceFocused reaches its target; the ramp clamps to 0/1
    // at the ends, so this self-terminates.
    if (const auto fit = m_focusFade.constFind(windowId);
        fit != m_focusFade.constEnd() && fit->value > 0.001f && fit->value < 0.999f) {
        return true;
    }
    // Resolve the decoration profile LAZILY: compiledPack only reads it on a
    // compile-cache miss, and this runs in the per-frame idle-repaint loop for
    // every shader-applied window — in the common case (all packs compiled,
    // e.g. a static border-only window) the tree walk would be pure per-frame
    // waste. Resolved at most once even when several packs miss.
    std::optional<PhosphorSurfaceShaders::DecorationProfile> profile;
    for (const QString& packId : it->chain) {
        CompiledSurfacePack* pack = nullptr;
        if (const auto cacheIt = m_compiledPacks.find(packId); cacheIt != m_compiledPacks.end()) {
            pack = &cacheIt->second;
        } else {
            if (!profile) {
                profile = m_decorationTree.resolve(resolveSurfacePathFor(windowId));
            }
            pack = compiledPack(packId, *profile);
        }
        if (!pack || !pack->shader) {
            continue;
        }
        if (pack->uTimeLoc >= 0) {
            return true;
        }
        // An audio-reactive pack must repaint every frame while a live spectrum
        // flows so getBass* tracks the beat. audioReactiveDriving() self-
        // terminates the loop when the toggle is off, capture stops, OR the
        // spectrum has gone quiet (repeated frames) — so a static border with
        // audio off, and a paused track, both cost nothing.
        if (pack->iAudioSpectrumSizeLoc >= 0 && audioReactiveDriving()) {
            return true;
        }
        for (const CompiledSurfaceBufferPass& bp : pack->bufferPasses) {
            if (bp.uTimeLoc >= 0 || (bp.iAudioSpectrumSizeLoc >= 0 && audioReactiveDriving())) {
                return true;
            }
        }
    }
    return false;
}

} // namespace PlasmaZones
