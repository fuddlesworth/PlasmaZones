// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Preset sweeps: what the effect has to re-do when a shader preset is retuned,
// renamed or deleted while the desktop is live. The scheduling latch lives here with
// the two sweeps it defers, rather than in lifecycle_wiring.cpp, whose stated concern
// is constructor wiring. The connection that calls schedulePresetSweep is made there.

#include "plasmazoneseffect.h"

#include <PhosphorSurface/DecorationSupportedPaths.h>
#include <PhosphorShaders/ShaderPresetStore.h>

#include "compositor/effectlogging.h"

#include <QLoggingCategory>

#include <effect/effecthandler.h>

namespace PlasmaZones {

PhosphorSurfaceShaders::DecorationProfile PlasmaZonesEffect::resolvedPointerProfile() const
{
    return resolveDecorationProfile(PhosphorSurfaceShaders::decorationPointerPath(),
                                    PhosphorShaders::ShaderFamily::Pointer);
}

// COALESCED, one sweep per family per event-loop turn.
//
// `ShaderPresetRegistry::presetsChanged` is emitted ONCE PER PACK whose set actually
// changed, but the work it drives is whole-registry by nature: the surface sweep drops
// every compiled pack, invalidates every window's fold and runs a full
// `updateAllDecorations()`, and the pointer sweep re-resolves and recompiles. A
// user-preset directory rescan re-reads the whole family on every save, so an N-pack
// change ran N full sweeps on the compositor thread. What the latch does is collapse
// every emission within one event-loop turn into one sweep per family; it is not a
// guard against a sweep some OTHER path has already done.
//
// The duplicate against a pack hot-reload is closed elsewhere, by ordering rather than
// by counting: each registry's `effectsChanged` handler re-seeds its own family as its
// first statement, so that handler's own sweep already sees the new presets and the
// emission this schedules is redundant rather than corrective. See the preset block in
// initRenderingAndRegistries.
//
// Deliberately NOT narrowed by packId: the work cannot be narrowed, only counted. Same
// latch-and-defer idiom as scheduleEffectAudioSync.
void PlasmaZonesEffect::schedulePresetSweep(PhosphorShaders::ShaderFamily family)
{
    // The re-seed inside each effectsChanged handler emits presetsChanged SYNCHRONOUSLY, so
    // a pack reload that really did change a preset set lands here while that handler is
    // still running — and that handler goes on to do the sweep's own work itself. Dropping
    // the request in that window keeps a hot-reload from paying for a second full surface
    // sweep (compiled-pack clear, fold invalidation, addRepaintFull, updateAllDecorations)
    // on the compositor thread. A retune that arrives by any OTHER route (a preset file
    // saved in this process or another) is not inside that window and still schedules.
    if (m_seedingPresetsInline) {
        qCDebug(lcEffect) << "schedulePresetSweep: family" << static_cast<int>(family)
                          << "folded into the inline re-seed already running";
        return;
    }
    // Animation re-resolves per transition, and overlay is the daemon's to render —
    // the effect never reads it. Neither needs a sweep.
    switch (family) {
    case PhosphorShaders::ShaderFamily::Surface:
        if (m_surfacePresetSweepScheduled) {
            return;
        }
        m_surfacePresetSweepScheduled = true;
        // `this` as the context object discards the call if the effect is destroyed
        // before it runs, the same guarantee scheduleEffectAudioSync relies on.
        QMetaObject::invokeMethod(
            this,
            [this]() {
                m_surfacePresetSweepScheduled = false;
                applySurfacePresetSweep();
            },
            Qt::QueuedConnection);
        return;
    case PhosphorShaders::ShaderFamily::Pointer:
        if (m_pointerPresetSweepScheduled) {
            return;
        }
        m_pointerPresetSweepScheduled = true;
        QMetaObject::invokeMethod(
            this,
            [this]() {
                m_pointerPresetSweepScheduled = false;
                applyPointerPresetSweep();
            },
            Qt::QueuedConnection);
        return;
    case PhosphorShaders::ShaderFamily::Animation:
    case PhosphorShaders::ShaderFamily::Overlay:
        return;
    }
}

void PlasmaZonesEffect::applySurfacePresetSweep()
{
    // Surface bakes parameters INTO the compiled pack, cached in m_compiledPacks, so
    // a preset change means the same cache drop a pack edit does.
    //
    // Same GL discipline as the surface effectsChanged handler: this arrives from a
    // file watcher between frames, with no current context, and the caches own
    // GLShaders and GLTextures.
    qCDebug(lcEffect) << "applySurfacePresetSweep: dropping" << m_compiledPacks.size() << "compiled surface packs and"
                      << m_surfaceMultipass.size() << "multipass folds for a preset retune";
    ensureGlContextCurrent();
    m_compiledPacks.clear();
    m_packBufferScaleCache.clear();
    // Invalidate the folds rather than ERASING the entries, the way the
    // decoration-tree loader does. A deleted window's entry is the intended frame for
    // its close leg, and renderSurfaceChainComposite refuses to re-capture a corpse —
    // so erasing it left the close animation undecorated with no path back. A live
    // window recovers on its next fold either way.
    for (auto& [windowId, state] : m_surfaceMultipass) {
        state.compositeValid = false;
        state.prefixValid = false;
        // -1, the field's invalid sentinel, not 0. Harmless while prefixValid is
        // cleared on the line above and the one reader checks it first — but 0 is a
        // legitimate INDEX ("the cached run ends at chain index 0"), so storing it
        // here would be accepted by a future reader that consults the index without
        // the flag. Every other reset site in the repo writes -1.
        state.prefixChainEnd = -1;
    }
    // These two are stale-TRUE only, and the sibling clear sites reset them for the
    // same reason.
    m_anyCompiledPackReadsCursor = false;
    m_opacityTintFallbackWarned = false;
    if (KWin::effects) {
        KWin::effects->addRepaintFull();
    }
    updateAllDecorations();
}

void PlasmaZonesEffect::applyPointerPresetSweep()
{
    // Re-push the profile BEFORE dropping the cache. The pointer pass stores an
    // ALREADY-FLATTENED profile and rebuildChain re-reads that stored copy, so
    // invalidating the cache on its own just recompiled the pack from the same stale
    // parameter values and the retune never reached the screen.
    //
    // invalidateShaderCache only when setProfile did NOT take a change. The two do
    // the same four things (releaseGl, rebuildChain, updateCursorHiding,
    // repaintStale) including a full registry walk over every chain layer, so
    // calling both unconditionally paid for all of it twice whenever the flattened
    // profile really had moved. The unchanged case is the one that still needs it:
    // the compiled pack holds baked parameter values that a retune invalidates even
    // when the profile compares equal.
    const bool profileChanged = m_pointerPass.setProfile(resolvedPointerProfile());
    qCDebug(lcEffect) << "applyPointerPresetSweep: re-pushed the flattened pointer profile, changed=" << profileChanged;
    if (!profileChanged) {
        m_pointerPass.invalidateShaderCache();
    }
    // Both calls above damage only what the trail already occupies, which is EMPTY
    // when the pointer is at rest — exactly the state the user is in while dragging a
    // preset slider. Without this the retune did not reach the screen until the next
    // pointer motion. The reach band, not addRepaintFull: a cursor decoration is not
    // worth a full compositor repaint.
    m_pointerPass.repaintCurrentReach();
}

} // namespace PlasmaZones
