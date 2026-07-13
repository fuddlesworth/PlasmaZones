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
#include "surface_fold.h"
#include "types.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>

#include <QRectF>
#include <QScopeGuard>

#include <optional>

namespace PlasmaZones {

// Milliseconds since an epoch captured on first use, so the value starts near 0 rather
// than at a steady_clock reading since boot.
//
// INTEGERS, and there is no seconds-valued sibling any more. A float's resolution decays
// with the magnitude of the epoch (after a week of uptime its ULP is four frames), and what
// the per-window clock actually wants is a DIFFERENCE of two large near-equal numbers —
// exactly where float cancellation bites. The single cast to float happens at the uniform,
// on a value the per-window offset has already brought back down to a few seconds.
qint64 PlasmaZonesEffect::surfaceShaderTimeMs()
{
    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
    if (m_surfaceTimeEpochMs < 0) {
        m_surfaceTimeEpochMs = nowMs;
    }
    return nowMs - m_surfaceTimeEpochMs;
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
    const auto selfRepaint = selfRepaintScope();
    for (auto it = m_windowDecorations.cbegin(); it != m_windowDecorations.cend(); ++it) {
        if (!it->shaderApplied) {
            continue;
        }
        KWin::EffectWindow* const sw = findWindowByIdExact(it.key());
        // Same exact-id / deleted / on-desktop discipline as the per-frame driver in
        // postPaintScreen: findWindowById's fuzzy appId fallback can resolve a
        // same-app sibling for a stale id, and an off-desktop window has nothing to
        // wake.
        if (!sw || getWindowId(sw) != it.key() || sw->isDeleted() || !sw->isOnCurrentDesktop()) {
            continue;
        }
        sw->addRepaintFull();
        damagePaddedBand(sw, it->outerPadding);
    }
}

// The cursor value a fold would bake in for this window: the live pointer when the chain
// may animate AND the pointer is over the window's canvas, else the outside sentinel.
//
// ONE expression, because the fold keys its cache on it and the repaint driver decides
// whether to drive on it, and the two must agree EXACTLY. When they did not — the driver
// omitted the may-animate factor — nothing caught it, because a short-circuit in a third
// file happened to keep a paused window from ever reaching the comparison. Add one more
// exemption beside that short-circuit and a paused hover window would compare a live
// cursor against a sentinel it can never converge on, and spin a full-window repaint at
// vsync forever.
QPointF PlasmaZonesEffect::foldCursorFor(KWin::EffectWindow* w, const QRectF& canvasGeo, bool mayAnimate,
                                         const QPointF& cursor) const
{
    if (!mayAnimate || !w) {
        return kCursorOutside;
    }
    // HALF-OPEN, and this is now the ONLY containment test in the effect — pushBorderUniforms
    // reads the sentinel this produces rather than re-deriving one, and says so. QRectF::contains
    // is closed on all four edges, so a pointer sitting precisely on the right or bottom edge was
    // keyed as INSIDE while the shader was handed the absent sentinel, and this expression's
    // whole purpose is to BE the value the shader receives.
    const bool inside = cursor.x() >= canvasGeo.left() && cursor.x() < canvasGeo.right()
        && cursor.y() >= canvasGeo.top() && cursor.y() < canvasGeo.bottom();
    return inside ? cursor : kCursorOutside;
}

// Repaint every decorated window whose chain reads the cursor.
//
// Called from the pointer-motion handler. It is the driver that restarts a hover pack's
// repaint loop when the POINTER moves: windowSurfaceAnimates stops driving one as soon as its
// folded cursor matches the live one, and the live one is only sampled during a frame, so
// without this the loop would depend on some other window happening to damage.
//
// It is not the only restart. windowSurfaceAnimates arms the same one-shot and drives too,
// which covers the case where the fold cursor changes with NO pointer motion at all (the gate
// flipping on focus gain, say). The two must therefore agree exactly about the flag and about
// foldCursorFor, and they do.
//
// Flagged as ours, like every other driver repaint: the window's CONTENT has not changed,
// only where the pointer is, and the fold keys on that itself (foldedCursor).
void PlasmaZonesEffect::repaintHoverDecorations(const QPointF& cursor)
{
    // This fires on EVERY pointer-motion event. If no compiled pack reads the cursor at all
    // — the default border-only configuration — nothing here can react, so skip the whole
    // per-window chain scan on one bool read rather than paying it per decoration per motion.
    if (!KWin::effects || m_windowDecorations.isEmpty() || !m_anyCompiledPackReadsCursor) {
        return;
    }
    const auto selfRepaint = selfRepaintScope();
    for (auto it = m_windowDecorations.cbegin(); it != m_windowDecorations.cend(); ++it) {
        if (!it->shaderApplied) {
            continue;
        }
        // Compiled packs only: an uncompiled chain has nothing to hover-react with yet, and
        // its first content paint will compile it and pick the driver up.
        bool readsCursor = false;
        for (const QString& packId : it->chain) {
            const auto cacheIt = m_compiledPacks.find(packId);
            if (cacheIt != m_compiledPacks.end() && cacheIt->second.shader && cacheIt->second.iMouseLoc >= 0) {
                readsCursor = true;
                break;
            }
        }
        if (!readsCursor) {
            continue;
        }
        KWin::EffectWindow* const sw = findWindowByIdExact(it.key());
        if (!sw || getWindowId(sw) != it.key() || sw->isDeleted() || !sw->isOnCurrentDesktop()) {
            continue;
        }
        // A PAUSED chain pins its cursor to the sentinel and cannot change, so waking it
        // buys nothing. Skipping it matters: this fires on every pointer-motion event, and
        // repainting every unfocused hover window on every mouse move is precisely the
        // per-frame cost this PR exists to remove.
        if (!decorationMayAnimate(sw)) {
            continue;
        }
        // Skip a window whose fold would bake the SAME cursor it already has. The pointer
        // moving on the far side of the desktop resolves to the outside sentinel for this
        // window, which is what it is already keyed on — so the fold would reproduce a
        // byte-identical composite and the repaint is pure cost, on the most frequent
        // interaction there is.
        //
        // @p cursor is the position the motion event carries. m_cachedCursorGlobal is a
        // frame stale here (prePaintScreen refreshes it) and would compare against the
        // wrong pointer.
        // A window with NO fold state has never been painted through the chain, so there is
        // no cached composite for a new cursor to invalidate. Skip it: it is not that the
        // repaint would be redundant, it is that it would be answering a question nobody
        // asked. An occluded window that never paints would otherwise take an unconditional
        // addRepaintFull on every single motion event, forever, which is the exact cost this
        // driver was written to avoid. Its first real paint folds it and establishes the
        // cursor.
        const auto sit = m_surfaceMultipass.find(it.key());
        if (sit == m_surfaceMultipass.end()
            || sit->second.foldedCursor == foldCursorFor(sw, sit->second.canvasGeo, /*mayAnimate=*/true, cursor)) {
            continue;
        }
        // A repaint we have already ASKED FOR is not due again — the same one-shot the
        // backdrop driver uses, for the same reason. foldedCursor is advanced only by a
        // FOLD, and KWin will not paint a window fully occluded by an opaque one above it.
        // Such a window can never converge, so without this it would take a full repaint
        // request on every pointer-motion event for the rest of the session. The fold
        // clears the flag on the frame the repaint actually lands.
        if (sit->second.hoverRepaintPending) {
            continue;
        }
        sit->second.hoverRepaintPending = true;
        sw->addRepaintFull();
        // A padded chain draws OUTSIDE the window rect, and addRepaintFull clips to the
        // window item — so without this, a cursor-following glow updates its inner canvas
        // and leaves a stale highlight in its outer band.
        damagePaddedBand(sw, it->outerPadding);
    }
}

// May this window's decoration chain animate right now?
//
// TWO consumers, and both matter. The repaint driver in postPaintScreen stops driving
// a paused window, which is what lets the GPU fall out of its top performance state.
// The FOLD (renderSurfaceChainComposite) freezes a paused window's clock, which is
// what makes its composite cacheable — without that half the pause bought nothing
// whenever anything else on screen was moving, because KWin paints a window whenever
// something overlapping it damages, and each of those paints re-folded the whole chain
// against a live clock.
bool PlasmaZonesEffect::decorationMayAnimate(KWin::EffectWindow* w) const
{
    // No window, or no compositor to animate on. Fail CLOSED: the focus test below
    // reads KWin::effects, and letting a null slip past it would report "may animate"
    // for a window that cannot be painted at all.
    if (!w || !KWin::effects) {
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
    if (m_animateFocusedOnly && KWin::effects->activeWindow() != w) {
        return false;
    }
    return true;
}

// Is this window's focus cross-fade mid-ramp?
//
// The ramp clamps to exactly 0.0 / 1.0 at its ends, so a value strictly between them
// means the fade is still moving and the window must keep repainting until it lands.
// Shared by the repaint driver and windowSurfaceAnimates, which have to agree: one
// decides whether to drive the window, the other whether the chain has anything to
// show for it, and a window driven by one but not the other either stalls mid-fade or
// spins forever.
bool PlasmaZonesEffect::focusRampInFlight(const QString& windowId) const
{
    const auto it = m_focusFade.constFind(windowId);
    return it != m_focusFade.constEnd() && it->value > 0.001f && it->value < 0.999f;
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
    // A focus ramp in flight needs continuous repaints so uSurfaceFocused reaches its
    // target. It self-terminates: the ramp clamps at both ends.
    if (focusRampInFlight(windowId)) {
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
        // A hover-reactive pack has to be driven while the cursor MOVES: there is no
        // per-cursor-move damage path, so if nothing repaints the window its highlight
        // never follows the pointer. Only while it moves, though — the fold bakes the
        // cursor in as a cache key (SurfaceMultipassState::foldedCursor), so once the
        // pointer stops, the composite is current and there is nothing to redraw. This
        // self-terminates the same way every other driver here does: the audio quiets,
        // the focus ramp clamps, the backdrop rate-limits. Driving unconditionally meant
        // a hover pack re-folded its entire chain at vsync forever with the pointer
        // parked motionless on another monitor.
        if (pack->iMouseLoc >= 0) {
            const auto sit = m_surfaceMultipass.find(windowId);
            if (sit == m_surfaceMultipass.end()) {
                // Never folded, so there is nothing to compare and nothing to converge on.
                // Do NOT drive it: a window that is never painted (fully occluded) would
                // never GET a state, and driving it every frame forever on that basis is a
                // spin with no termination condition. Its first real paint runs the fold,
                // which creates the state, and the next postPaintScreen picks the hover up
                // — the same reasoning the not-yet-compiled case already uses.
                continue;
            }
            // A repaint already ASKED FOR is not asked for again. Without this, the one-shot
            // protects repaintHoverDecorations and this driver spins in its place: an occluded
            // window is never painted, so its foldedCursor can never advance, so this
            // comparison is true on every frame and drives a full repaint at vsync forever.
            // The flag is the whole termination condition and both drivers have to honour it.
            if (sit->second.hoverRepaintPending) {
                continue;
            }
            // Compare EXACTLY what the fold keys on. See foldCursorFor.
            KWin::EffectWindow* const sw = findWindowByIdExact(windowId);
            // Same exact-id / deleted discipline as every other findWindowById consumer: the
            // fuzzy appId fallback can resolve a same-app SIBLING for a stale id, and this
            // window's mayAnimate would then be decided by another window entirely — a
            // comparison that can never converge, which is a vsync repaint loop.
            if (!sw || getWindowId(sw) != windowId || sw->isDeleted()) {
                continue;
            }
            if (sit->second.foldedCursor
                != foldCursorFor(sw, sit->second.canvasGeo, decorationMayAnimate(sw),
                                 m_shaderManager.m_cachedCursorGlobal)) {
                sit->second.hoverRepaintPending = true;
                return true;
            }
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
