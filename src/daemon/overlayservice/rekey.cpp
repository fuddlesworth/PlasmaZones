// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Overlay-key migration + screen geometry-watch + screen-state invariant
// check. The three functions form a small cohesive cluster: rekeyOverlayState
// invokes installOverlayGeometryWatcher post-move, and the debug-only
// validateScreenStateInvariant verifies the cross-side pointer alignment that
// rekey is the primary risk-source for.

#include "internal.h"
#include "daemon/overlayservice.h"
#include "core/platform/logging.h"
#include "core/utils/utils.h"

#include <PhosphorOverlay/ShellHost.h>
#include <PhosphorOverlay/ShellState.h>

#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScreens/VirtualScreen.h>

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/Surface.h>

#include <QPointer>
#include <QQuickWindow>
#include <QScreen>
#include <QSet>
#include <QStringList>

namespace PlasmaZones {

bool OverlayService::rekeyOverlayState(const QString& oldKey, const QString& newKey)
{
    if (oldKey == newKey) {
        return false;
    }
    auto donor = m_screenStates.find(oldKey);
    if (donor == m_screenStates.end() || !donor->overlayPhysScreen) {
        return false;
    }

    // Rekey is only valid when the surface's protocol-level placement
    // (anchors, in particular) does not have to change. wlr-layer-shell
    // advertises v2+ `set_anchor` as mutable, but several compositors
    // (weston, some mutter forks) silently ignore post-attach anchor
    // changes and the surface stays pinned to its original anchors. A
    // physical→virtual (or VS→VS with different anchor set) flip requires
    // AnchorAll → Top|Left - if that anchor change no-ops on the donor
    // compositor, the overlay keeps rendering across the wrong region.
    // Bail out and let the caller destroy+recreate when the anchor set
    // would have to change.
    const bool wasVS = PhosphorIdentity::VirtualScreenId::isVirtual(oldKey);
    const bool willBeVS = PhosphorIdentity::VirtualScreenId::isVirtual(newKey);
    if (wasVS != willBeVS) {
        qCInfo(lcOverlay) << "rekeyOverlayState: refusing flavor flip rekey" << oldKey << "->" << newKey
                          << "(anchors would change; some compositors ignore post-attach set_anchor)";
        return false;
    }

    // If an entry already exists under newKey, refuse the move when the
    // daemon side already considers it live. We do NOT mutate the
    // daemon entry here - the lib's rekey is the authority on whether
    // the move actually lands. Mutating before the lib call would leave
    // a wrecked daemon entry on a lib-refusal path (e.g. a prior warm
    // for OSD/snap-assist gave newKey a live lib shell that the daemon
    // sees as "stale" via overlayPhysScreen == null but the lib refuses
    // to clobber).
    auto existing = m_screenStates.find(newKey);
    if (existing != m_screenStates.end() && existing->overlayPhysScreen) {
        qCWarning(lcOverlay) << "rekeyOverlayState: refusing to clobber live entry under" << newKey << "with donor"
                             << oldKey;
        return false;
    }

    // Drive lib-side rekey FIRST so its liveness check (which may catch
    // a live ShellState under newKey from a prior OSD/snap-assist warm
    // that the daemon's overlayPhysScreen sentinel doesn't reflect) is
    // authoritative. If it refuses, leave daemon-side state untouched
    // and return false - the caller's Phase-2 dismiss + Phase-3 create
    // fallback handles recovery.
    if (!m_shellHost->rekey(oldKey, newKey)) {
        qCWarning(lcOverlay) << "rekeyOverlayState: lib-side rekey refused" << oldKey << "->" << newKey
                             << "(lib has a live shell under target); falling back to recreate";
        return false;
    }

    // Lib accepted - the lib has either deleted its stale newKey entry
    // (heap ShellState the daemon's `existing->shell` borrowed) or
    // simply moved the donor across. The daemon's `existing->shell`
    // pointer is now dangling iff the lib deleted; null it BEFORE the
    // erase runs the daemon-side destructor (defense even though
    // ~PerScreenOverlayState doesn't dereference shell today).
    // Iterators on m_screenStates were not invalidated by the lib call
    // (lib only touches m_states).
    if (existing != m_screenStates.end()) {
        existing->shell = nullptr;
        m_screenStates.erase(existing);
        // That erase destroyed a shell able to host a visible modal, so it is
        // one of the teardown sites resetModalSingletonsForDestroyedId names.
        // The clobber guard above only refuses on a non-null overlayPhysScreen,
        // and that field is written solely by the main-overlay path, never by
        // ensurePassiveShellFor - so a passive-only shell carrying a visible
        // cheatsheet, picker or snap assist reaches here. Left alone, the
        // modal's screen id would name a key with no state at all: the hide
        // path's lookup fails, the slot is never hidden, and the visible flag
        // stays set so the next toggle no-ops.
        //
        // This MUST run before the oldKey remap below. Remapping first would
        // move the donor's still-live modal id onto newKey, and this reset
        // would then dismiss the sheet that actually survived the move.
        resetModalSingletonsForDestroyedId(newKey);
    }

    PerScreenOverlayState state = std::move(donor.value());
    m_screenStates.erase(donor);
    auto inserted = m_screenStates.insert(newKey, std::move(state));

    // Several more per-screen overlay maps are keyed by screen id, and each
    // needs something different from the move. Deliberately not a count: the
    // enumeration below drifted from one twice already, and what matters is
    // that a map is named here at all.
    //  - m_lastScrollDropIndicatorRect MUST follow, for a reason sharper than
    //    the others. It is the drop indicator's CHANGE GATE, and the rekey
    //    preserves the live slot — so an indicator visible under oldKey stays
    //    on screen while its cache entry strands. A later clear addressed to
    //    newKey then finds no entry, takes the "already clear" early return,
    //    and never hides the still-visible slot. The common path self-heals
    //    (the next push hides the old id first), but a rekey with no further
    //    push before the drop leaves the rectangle painted with no drag left
    //    to dismiss it.
    //  - m_scrollDropIndicatorHidePending is NOT an inert bit. The rekey
    //    preserves the slot and the animator track, so a hide in flight still
    //    completes and dropping the bit alone would strand the slot. The
    //    pending teardown is finished here rather than dropped, and
    //    m_scrollDropIndicatorHideGuard is bumped for the OLD key and the new
    //    key is seeded one generation ahead of it, so the in-flight
    //    completion stale-returns instead of running twice and no generation
    //    is ever re-issued under either key.
    //  - m_stripCardFractionsCache is DROPPED for both keys rather than moved:
    //    it is a memo the next trigger-edge probe rebuilds in one call, and
    //    a stale entry under either key would mis-size the keep-visible band.
    //  - m_selectedStripTarget/m_selectedStripScreenId are DROPPED when keyed
    //    to the old key rather than migrated: a rekey renumbers nothing, but
    //    the stored key would never screensMatch the live one again, so the
    //    popup pick would be silently ignored at drop. The next cursor tick
    //    re-selects under the new key.
    // After the rekey the old key has no removal path of its own (the rekeyed
    // key never reaches unwirePassiveShellSlots, and the by-key clears in
    // updateScrollingScreens name the LIVE screen), so failing to move these
    // leaks an entry per rekey rather than merely misbehaving once.
    if (const auto dropRectIt = m_lastScrollDropIndicatorRect.constFind(oldKey);
        dropRectIt != m_lastScrollDropIndicatorRect.constEnd()) {
        m_lastScrollDropIndicatorRect.insert(newKey, dropRectIt.value());
        m_lastScrollDropIndicatorRect.remove(oldKey);
    }
    // The drop indicator's paint overrides follow too: their only writer is a
    // context re-resolve, so an entry left under the dead key means the
    // rekeyed screen silently falls back to config colours until one happens,
    // and the stranded entry never goes away.
    if (const auto dropOverrideIt = m_scrollDropIndicatorOverrides.constFind(oldKey);
        dropOverrideIt != m_scrollDropIndicatorOverrides.constEnd()) {
        m_scrollDropIndicatorOverrides.insert(newKey, dropOverrideIt.value());
        m_scrollDropIndicatorOverrides.remove(oldKey);
    }
    m_stripCardFractionsCache.remove(oldKey);
    m_stripCardFractionsCache.remove(newKey);
    if (PhosphorScreens::ScreenIdentity::screensMatch(m_selectedStripScreenId, oldKey)) {
        m_selectedStripTarget = {};
        m_selectedStripScreenId.clear();
    }
    // The three modal singletons remember which screen they are up on, and
    // those ids are map keys into m_screenStates. The state has just moved to
    // newKey, so an id still naming oldKey addresses a key that no longer
    // exists, and every path that would clean the modal up misses: the hide
    // path's lookup fails, so the slot is never hidden and its surface keeps
    // the input grab, while the cheatsheet's keyboard predicate (which compares
    // its screen id against the id being synced) reads false under the new key
    // and hands the keyboard back out from under a visible search field.
    //
    // Exact equality, not screensMatch: these are map keys, and every other
    // reader compares them with ==. The fuzzy match above is for a target
    // checked against a live cursor id, which is a different question.
    // An id naming neither key belongs to another screen this rekey did not
    // touch, so it is left alone.
    //
    // This has to happen before the conditional sync further down, which reads
    // m_cheatsheetScreenId.
    for (QString* modalScreenId : {&m_cheatsheetScreenId, &m_layoutPickerScreenId, &m_snapAssistScreenId}) {
        if (*modalScreenId == oldKey) {
            *modalScreenId = newKey;
        }
    }
    // A hide in flight under oldKey cannot simply have its bit dropped. The
    // animator's track is keyed by {surface, item}, neither of which the rekey
    // touches, so the completion still fires — and it captured oldKey and the
    // generation it was issued. Left alone, it would pass its own guard check,
    // then fail the m_screenStates lookup (the state now lives under newKey)
    // and return early, never running the teardown it owns. That leaves the
    // slot visible at opacity 0, so the drop indicator stops painting until
    // the drag ends, and that does not self-heal through the normal update
    // path, which early-returns on a slot that is already visible. So finish
    // the teardown here, against the migrated state, and bump the old key's
    // guard so the in-flight completion stale-returns instead of running
    // twice. Deferred to after the surface re-announce below, which needs the
    // pre-teardown state.
    const bool dropHideWasPending = m_scrollDropIndicatorHidePending.remove(oldKey);
    // The old key's counter is bumped IN PLACE and kept (the monotonic
    // exception at overlayservice.h's guard doc: an entry, once issued, never
    // restarts — a later life of oldKey, re-created by the ordinary show path,
    // must not re-issue a generation an abandoned completion still holds),
    // and the new key is seeded strictly ahead of it, so the counter that
    // follows the state is ahead of every generation ever issued under either
    // key. qMax rather than a plain assign because newKey may already carry a
    // counter from an earlier life of its own.
    const quint64 oldGeneration = ++m_scrollDropIndicatorHideGuard[oldKey];
    m_scrollDropIndicatorHideGuard[newKey] = qMax(m_scrollDropIndicatorHideGuard.value(newKey), oldGeneration + 1);

    // The geometryChanged lambda captured the OLD sid by value. After the
    // state moved to newKey, the lambda's m_screenStates.find(oldSid) lookup
    // would return end() and silently drop every subsequent geometry update.
    // Rebuild the connection with the new key so live resizes keep reaching
    // the overlay.
    auto& rekeyed = inserted.value();
    if (rekeyed.overlayGeomConnection) {
        QObject::disconnect(rekeyed.overlayGeomConnection);
        rekeyed.overlayGeomConnection = {};
    }
    QScreen* physScreen = rekeyed.overlayPhysScreen;
    if (physScreen) {
        const bool isVS = PhosphorIdentity::VirtualScreenId::isVirtual(newKey);

        // Re-anchor the live layer surface to the new key's region. The
        // donor's anchors/margins were baked in at attach time for the old
        // key, and the flavor guard above only pins the ANCHOR SET as
        // unchanged - the region can still move, which is exactly the live
        // case here: a VS→VS rekey onto a different sub-region of the same
        // monitor. Without this the surface keeps rendering across the old
        // VS's rectangle. wlr-layer-shell v2+ allows set_anchor / set_margin
        // post-attach; push the corrected placement through the mutable
        // transport handle. (isVS == wasVS by the guard, so the physical
        // branch below is the both-bare-physical case, where AnchorAll
        // already covers the whole monitor and only the margins are reset.)
        const QRect targetVsGeom = resolveScreenGeometry(m_screenManager, newKey);
        const auto placement = layerPlacementForVs(isVS ? targetVsGeom : QRect(), physScreen->geometry());
        // The screen's passive shell carries the placement, so it is what has
        // to be re-anchored; a stranded one would keep drawing over the old
        // VS's rectangle.
        if (rekeyed.shell && rekeyed.shell->shellSurface()) {
            // Same independent-guard split as the geometry watcher: a warmed
            // shell that has not attached yet has a window but no transport,
            // and its window must still take the target VS's size.
            if (auto* handle = rekeyed.shell->shellSurface()->transport()) {
                handle->setAnchors(placement.anchors);
                handle->setMargins(placement.margins);
            }
            if (isVS && targetVsGeom.isValid()) {
                if (auto* w = rekeyed.shell->shellWindow()) {
                    w->setWidth(targetVsGeom.width());
                    w->setHeight(targetVsGeom.height());
                }
            }
        }
        if (isVS && targetVsGeom.isValid() && rekeyed.shell && rekeyed.shell->shellSurface()) {
            rekeyed.overlayGeometry = targetVsGeom;
            // The zone-selector popup converts global cursor coords through
            // its OWN captured rect (updateSelectorPosition prefers it over
            // the window geometry); left at the old VS rect, every later
            // hit-test on this screen is offset by the old->new origin delta
            // for the rest of the drag.
            if (rekeyed.zoneSelectorGeometry.isValid()) {
                rekeyed.zoneSelectorGeometry = targetVsGeom;
            }
        }

        rekeyed.overlayGeomConnection = installOverlayGeometryWatcher(physScreen, newKey, isVS);
    }

    // Finish the hide the rekey inherited (see the pending-bit block above).
    // Same teardown the abandoned completion would have run, addressed to the
    // migrated key.
    if (dropHideWasPending) {
        if (QQuickItem* slot = rekeyed.scrollDropIndicatorSlot()) {
            slot->setVisible(false);
            writeQmlProperty(slot, QStringLiteral("loaded"), false);
        }
        syncPassiveShellSurfaceState(newKey);
    }

    qCInfo(lcOverlay) << "rekeyOverlayState: migrated overlay" << oldKey << "->" << newKey
                      << "(same physical monitor, preserving Vulkan surface)";
    return true;
}

void OverlayService::validateScreenStateInvariant(const QStringList& targetIds) const
{
#ifndef QT_NO_DEBUG
    // Invariant (one-overlay-per-VS): every live overlay's key must be in
    // targetIds. Phase 2 dismisses any stale entries and Phase 3 creates
    // missing targets, so by the end of initializeOverlay every live
    // m_screenStates entry should correspond to an enabled effective
    // screen id. Multiple live entries per physical monitor are NOT a
    // violation in this model - two virtual screens sharing one physical
    // monitor each own their own overlay window, and that's the whole
    // point of the one-per-VS refactor.
    const QSet<QString> targetSet(targetIds.cbegin(), targetIds.cend());
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        // Cross-side pointer alignment: daemon's borrowed `shell`
        // pointer must agree with the lib's view of the same key (or
        // both be null/missing). Catches desync from a failed rekey,
        // an out-of-band lib mutation, or a future code path that
        // forgets to refresh the daemon-side cache.
        // stateFor is a peek: it is declared const-only and returns
        // pointer-or-nullptr, and the accessor that materializes an entry on a
        // miss is separately named getOrCreateStateFor precisely so a
        // debug-only check like this one cannot reach it by accident.
        //
        if (it.value().shell) {
            const PhosphorOverlay::ShellState* libState = m_shellHost->stateFor(it.key());
            if (libState != it.value().shell) {
                qCWarning(lcOverlay) << "validateScreenStateInvariant: daemon/lib ShellState pointer desync for"
                                     << it.key() << "daemon=" << it.value().shell << "lib=" << libState;
                Q_ASSERT_X(false, "OverlayService", "daemon/lib ShellState pointer desync");
            }
        }
        if (!it.value().overlayPhysScreen) {
            continue;
        }
        if (!targetSet.contains(it.key())) {
            qCWarning(lcOverlay) << "validateScreenStateInvariant: live overlay" << it.key()
                                 << "is not in the current target set: orphan";
            Q_ASSERT_X(false, "OverlayService", "orphaned overlay entry");
        }
    }
    // A modal singleton's screen id is a key into m_screenStates, and every
    // path that hides or tears one down looks it up by that key. An id naming
    // a key that no longer exists is therefore unrecoverable by any normal
    // route: the slot stays visible, its surface keeps the input grab, and the
    // visible flag stays set so the toggle no-ops. Nothing in the ordinary
    // show/hide cycle can produce it, which is exactly why it is worth
    // asserting here - the ways in are key migrations and teardowns, and those
    // are the paths that have to remember to carry these three along.
    const std::pair<const QString&, const char*> modalIds[] = {
        {m_cheatsheetScreenId, "cheatsheet"},
        {m_layoutPickerScreenId, "layout picker"},
        {m_snapAssistScreenId, "snap assist"},
    };
    for (const auto& [modalScreenId, modalName] : modalIds) {
        if (!modalScreenId.isEmpty() && !m_screenStates.contains(modalScreenId)) {
            qCWarning(lcOverlay) << "validateScreenStateInvariant:" << modalName << "screen id" << modalScreenId
                                 << "names a key with no screen state";
            Q_ASSERT_X(false, "OverlayService", "modal singleton id names a dead screen key");
        }
    }
#else
    Q_UNUSED(targetIds);
#endif
}

QMetaObject::Connection OverlayService::installOverlayGeometryWatcher(QScreen* physScreen, const QString& screenId,
                                                                      bool isVS)
{
    if (!physScreen) {
        return {};
    }
    QPointer<QScreen> screenPtr = physScreen;
    const QString sid = screenId; // Capture by value - survives rekey.
    return connect(physScreen, &QScreen::geometryChanged, this, [this, screenPtr, sid, isVS](const QRect& newGeom) {
        if (!screenPtr) {
            return;
        }
        auto stateIt = m_screenStates.find(sid);
        if (stateIt == m_screenStates.end()) {
            return; // State was cleaned up, ignore stale geometry signal
        }
        auto& st = stateIt.value();
        // The zone selector is NOT gated on the overlay's active axis, so a
        // live resize while the popup is up must refresh its hit-test frame
        // (zoneSelectorGeometry) and bar layout BEFORE the overlay-context
        // guard below can return early on a selector-only screen. The full
        // update rewrites both in one call. For a physical screen, re-stamp
        // the frame with the SIGNAL's rect afterwards: ScreenManager may not
        // have processed this same geometryChanged yet, and
        // updateZoneSelectorWindow stamps whatever it resolves — the signal
        // value is the authority for THIS resize. (A VS frame is a derived
        // sub-rect, so it keeps the resolved value.) Re-find after the
        // update rather than reusing `st`: updateZoneSelectorWindow writes
        // screen state through non-const operator[], the detach hazard the
        // strip refresh documents.
        if (st.zoneSelectorSlot() && st.zoneSelectorSlot()->isVisible()) {
            updateZoneSelectorWindow(sid);
            if (!isVS) {
                if (const auto zsIt = m_screenStates.find(sid); zsIt != m_screenStates.end()) {
                    zsIt->zoneSelectorGeometry = newGeom;
                }
            }
        }
        // Re-find: the selector refresh above may have detached the hash.
        stateIt = m_screenStates.find(sid);
        if (stateIt == m_screenStates.end()) {
            return;
        }
        auto& stAfter = stateIt.value();
        if (!stAfter.overlayPhysScreen || !stAfter.shell) {
            return; // Main overlay context not active for this entry
        }
        if (auto* w = stAfter.shell->shellWindow()) {
            if (isVS) {
                // Virtual screen: recompute sub-region geometry from
                // PhosphorScreens::ScreenManager (virtual proportions
                // are relative to the physical screen) and push new
                // margins via the PhosphorLayer transport handle.
                // Anchors (Top|Left) are fixed at attach and can't change.
                const QRect vsGeom = resolveScreenGeometry(m_screenManager, sid);
                if (vsGeom.isValid() && stAfter.shell->shellSurface()) {
                    // The passive shell carries the VS margins, so they go
                    // stale with the geometry and a stranded surface keeps
                    // drawing over the old VS rectangle — the same failure
                    // rekeyOverlayState re-anchors to avoid.
                    const auto placement = layerPlacementForVs(vsGeom, newGeom);
                    // Two independent guards on purpose: a warmed shell that
                    // has not attached yet (or just detached) has a window but
                    // no transport, and its window must still follow the VS.
                    if (auto* handle = stAfter.shell->shellSurface()->transport()) {
                        handle->setMargins(placement.margins);
                    }
                    if (auto* sw = stAfter.shell->shellWindow()) {
                        sw->setWidth(vsGeom.width());
                        sw->setHeight(vsGeom.height());
                    }
                    stAfter.overlayGeometry = vsGeom;
                    updateOverlayWindow(sid, screenPtr);
                    return;
                }
            } else {
                // Physical screen: AnchorAll auto-sizes to the screen;
                // just mirror the resize to our cached state.
                w->setWidth(newGeom.width());
                w->setHeight(newGeom.height());
                stAfter.overlayGeometry = newGeom;
                updateOverlayWindow(sid, screenPtr);
            }
        }
    });
}

} // namespace PlasmaZones
