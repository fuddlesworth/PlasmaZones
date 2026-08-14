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

#include <optional>

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
        existing->tabShell = nullptr;
        m_screenStates.erase(existing);
    }

    // The tab-indicator shell follows the same key. It is best-effort on
    // purpose: it exists only for a screen that has shown a tabbed column, so
    // "no live shell under oldKey" is the ordinary answer, not a fault. When
    // one IS live and the host refuses the move (a live shell already under
    // newKey), destroy it rather than leave the donor's surface stranded under
    // a key nothing will ever address again — the next strip update recreates
    // it, at the cost of one surface rebuild.
    //
    // The four per-screen tab maps are captured FIRST, because destroyShell
    // fires the PreDestroy hook and unwireScrollTabShellSlots clears every one
    // of them for oldKey — so the migration block below would find nothing
    // left to move. The strip model and input region self-heal on the next
    // engine push, but the context-rule paint overrides do not: their only
    // writer is a rules re-resolve, so the screen would silently fall back to
    // config colours until one happened. Carrying them across means the
    // rebuilt shell comes up already correct.
    const auto savedStrips = m_lastScrollTabStrips.constFind(oldKey) != m_lastScrollTabStrips.constEnd()
        ? std::optional(m_lastScrollTabStrips.value(oldKey))
        : std::nullopt;
    const auto savedRegion = m_scrollTabInputRegions.constFind(oldKey) != m_scrollTabInputRegions.constEnd()
        ? std::optional(m_scrollTabInputRegions.value(oldKey))
        : std::nullopt;
    const auto savedOverrides =
        m_scrollTabIndicatorOverrides.constFind(oldKey) != m_scrollTabIndicatorOverrides.constEnd()
        ? std::optional(m_scrollTabIndicatorOverrides.value(oldKey))
        : std::nullopt;
    if (m_tabShellHost->stateFor(oldKey) != nullptr && !m_tabShellHost->rekey(oldKey, newKey)) {
        qCInfo(lcOverlay) << "rekeyOverlayState: tab shell rekey refused" << oldKey << "->" << newKey
                          << "; destroying it, the next strip update rebuilds it";
        m_tabShellHost->destroyShell(oldKey);
        m_tabShellHost->removeState(oldKey);
        donor->tabShell = nullptr;
        // Put back what the PreDestroy hook just cleared, so the migration
        // below has something to move.
        if (savedStrips) {
            m_lastScrollTabStrips.insert(oldKey, *savedStrips);
        }
        if (savedRegion) {
            m_scrollTabInputRegions.insert(oldKey, *savedRegion);
        }
        if (savedOverrides) {
            m_scrollTabIndicatorOverrides.insert(oldKey, *savedOverrides);
        }
    }
    PerScreenOverlayState state = std::move(donor.value());
    m_screenStates.erase(donor);
    auto inserted = m_screenStates.insert(newKey, std::move(state));

    // Several more per-screen overlay maps are keyed by screen id, and each
    // needs something different from the move. Deliberately not a count: the
    // enumeration below drifted from one twice already, and what matters is
    // that a map is named here at all.
    //  - m_lastScrollTabStrips MUST follow. It is the cached model the
    //    enable toggle replays, so left under the dead key, re-enabling the
    //    indicator would replay nothing until the next structural strip
    //    change.
    //  - m_scrollTabsHidePending is NOT an inert bit. The rekey preserves the
    //    slot and the animator track, so a hide in flight still completes;
    //    dropping the bit alone strands the slot. It is cleared together with
    //    the teardown it owns, below.
    //  - m_scrollTabsHideGuard is deliberately abandoned for the NEW key: it
    //    is monotonic per key, and the new key's counter (absent or already
    //    higher) can only make an old in-flight completion stale-return, which
    //    is the safe direction. The OLD key's counter is bumped when a hide
    //    was pending, so the completion this rekey pre-empts stale-returns
    //    rather than running the teardown twice.
    //  - m_scrollTabInputRegions MUST follow. The rekey preserves the live
    //    surface, so the slot can still be visible; left under the dead key,
    //    syncScrollTabShellSurfaceState(newKey) would read an empty region and
    //    the indicator would be UNCLICKABLE until the next strip update.
    //  - m_scrollTabIndicatorOverrides MUST follow. Otherwise the screen's
    //    context-rule paint overrides silently fall back to the config values
    //    until the next updateScrollingScreens pass re-pushes them.
    //  - m_lastScrollDropIndicatorRect MUST follow, for a reason sharper than
    //    the others. It is the drop indicator's CHANGE GATE, and the rekey
    //    preserves the live slot — so an indicator visible under oldKey stays
    //    on screen while its cache entry strands. A later clear addressed to
    //    newKey then finds no entry, takes the "already clear" early return,
    //    and never hides the still-visible slot. The common path self-heals
    //    (the next push hides the old id first), but a rekey with no further
    //    push before the drop leaves the rectangle painted with no drag left
    //    to dismiss it.
    //  - m_scrollDropIndicatorHidePending is handled like its tab twin: the
    //    pending teardown is finished here rather than dropped, and
    //    m_scrollDropIndicatorHideGuard follows the same bump-the-old-key rule.
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
    if (const auto stripsIt = m_lastScrollTabStrips.constFind(oldKey); stripsIt != m_lastScrollTabStrips.constEnd()) {
        m_lastScrollTabStrips.insert(newKey, stripsIt.value());
        m_lastScrollTabStrips.remove(oldKey);
    }
    if (const auto regionIt = m_scrollTabInputRegions.constFind(oldKey);
        regionIt != m_scrollTabInputRegions.constEnd()) {
        m_scrollTabInputRegions.insert(newKey, regionIt.value());
        m_scrollTabInputRegions.remove(oldKey);
    }
    if (const auto overrideIt = m_scrollTabIndicatorOverrides.constFind(oldKey);
        overrideIt != m_scrollTabIndicatorOverrides.constEnd()) {
        m_scrollTabIndicatorOverrides.insert(newKey, overrideIt.value());
        m_scrollTabIndicatorOverrides.remove(oldKey);
    }
    if (const auto dropRectIt = m_lastScrollDropIndicatorRect.constFind(oldKey);
        dropRectIt != m_lastScrollDropIndicatorRect.constEnd()) {
        m_lastScrollDropIndicatorRect.insert(newKey, dropRectIt.value());
        m_lastScrollDropIndicatorRect.remove(oldKey);
    }
    // The drop indicator's paint overrides follow for the same reason its tab
    // twin does: their only writer is a context re-resolve, so an entry left
    // under the dead key means the rekeyed screen silently falls back to config
    // colours until one happens, and the stranded entry never goes away.
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
    // A hide in flight under oldKey cannot simply have its bit dropped. The
    // animator's track is keyed by {surface, item}, neither of which the rekey
    // touches, so the completion still fires — and it captured oldKey, whose
    // guard the rekey abandons UNCHANGED. It therefore passes its own guard
    // check, then fails the m_screenStates lookup (the state now lives under
    // newKey) and returns early, never running the teardown it owns. That
    // leaves the slot visible at opacity 0: the drop indicator stops painting
    // until the drag ends, and the tab strips stay INVISIBLE BUT CLICKABLE,
    // because syncScrollTabShellSurfaceState keys the input region on
    // slot->isVisible(). Neither self-heals through the normal update path,
    // which early-returns on a slot that is already visible.
    // So finish the teardown here, against the migrated state, and bump the
    // old key's guard so the in-flight completion stale-returns instead of
    // running twice. Deferred to after the surface re-announce below, which
    // needs the pre-teardown state.
    const bool tabsHideWasPending = m_scrollTabsHidePending.remove(oldKey);
    const bool dropHideWasPending = m_scrollDropIndicatorHidePending.remove(oldKey);
    if (tabsHideWasPending) {
        ++m_scrollTabsHideGuard[oldKey];
    }
    if (dropHideWasPending) {
        ++m_scrollDropIndicatorHideGuard[oldKey];
    }

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
        // Both of the screen's surfaces are re-anchored, not just the passive
        // one: the tab shell was attached against the old key's region too, and
        // a stranded one would keep drawing the indicators over the old VS's
        // rectangle.
        const auto reanchor = [&](PhosphorOverlay::ShellState* shellState) {
            if (!shellState || !shellState->shellSurface()) {
                return;
            }
            auto* handle = shellState->shellSurface()->transport();
            if (!handle) {
                return;
            }
            handle->setAnchors(placement.anchors);
            handle->setMargins(placement.margins);
            if (isVS && targetVsGeom.isValid()) {
                if (auto* w = shellState->shellWindow()) {
                    w->setWidth(targetVsGeom.width());
                    w->setHeight(targetVsGeom.height());
                }
            }
        };
        reanchor(rekeyed.shell);
        reanchor(rekeyed.tabShell);
        // Tracked for the passive shell only — overlayGeometry is the zone
        // overlay's hit-testing reference, and the tab indicators have none.
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

    // Re-publish the tab surface under the new key. ShellHost::rekey moves the
    // ShellState between keys without firing PreDestroy, so the wl_surface
    // survives — but the compositor was told about it under oldKey, and the
    // effect's map is keyed by screen with the announcement as its only writer.
    // Left alone, nothing would ever name the surface under newKey, and the
    // eventual teardown would retract newKey, leaving the effect holding an
    // oldKey entry that points at a destroyed object. Wayland reuses ids, so
    // that is the mismatch the announce/retract pairing exists to prevent, not
    // merely a lost indicator. Retract first: the two keys are different, so
    // the change gate cannot collapse the pair.
    announceScrollTabSurface(oldKey, nullptr);
    if (rekeyed.tabShell) {
        announceScrollTabSurface(newKey, rekeyed.tabShell->shellWindow());
    }

    // Finish the hides the rekey inherited (see the pending-bit block above).
    // Same teardown the abandoned completions would have run, addressed to the
    // migrated key. The tab retraction comes after the re-announce on purpose:
    // the announce is unconditional on tabShell, so retracting first would let
    // it re-publish a surface for a slot that is about to go invisible.
    if (tabsHideWasPending) {
        if (QQuickItem* slot = rekeyed.scrollTabsSlot()) {
            slot->setVisible(false);
            writeQmlProperty(slot, QStringLiteral("loaded"), false);
            announceScrollTabSurface(newKey, nullptr);
        }
        syncScrollTabShellSurfaceState(newKey);
    }
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
        // Both borrowed pointers are checked. The tab shell is a second
        // pointer into a second host with the same desync sources — a refused
        // rekey, or a PostCreate that tears itself down.
        if (it.value().shell) {
            const PhosphorOverlay::ShellState* libState = m_shellHost->stateFor(it.key());
            if (libState != it.value().shell) {
                qCWarning(lcOverlay) << "validateScreenStateInvariant: daemon/lib ShellState pointer desync for"
                                     << it.key() << "daemon=" << it.value().shell << "lib=" << libState;
                Q_ASSERT_X(false, "OverlayService", "daemon/lib ShellState pointer desync");
            }
        }
        if (it.value().tabShell) {
            const PhosphorOverlay::ShellState* libTabState = m_tabShellHost->stateFor(it.key());
            if (libTabState != it.value().tabShell) {
                qCWarning(lcOverlay) << "validateScreenStateInvariant: daemon/lib tab ShellState pointer desync for"
                                     << it.key() << "daemon=" << it.value().tabShell << "lib=" << libTabState;
                Q_ASSERT_X(false, "OverlayService", "daemon/lib tab ShellState pointer desync");
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
                    // Both shells carry the VS margins, so both go stale, and
                    // a stranded one keeps drawing over the old VS rectangle —
                    // the same failure rekeyOverlayState re-anchors both shells
                    // to avoid. The tab shell cannot self-heal: its size is set
                    // on the show path, and an already-visible indicator
                    // returns before reaching it.
                    const auto placement = layerPlacementForVs(vsGeom, newGeom);
                    const auto reanchor = [&](PhosphorOverlay::ShellState* shell) {
                        if (!shell || !shell->shellSurface()) {
                            return;
                        }
                        if (auto* handle = shell->shellSurface()->transport()) {
                            handle->setMargins(placement.margins);
                        }
                        if (auto* sw = shell->shellWindow()) {
                            sw->setWidth(vsGeom.width());
                            sw->setHeight(vsGeom.height());
                        }
                    };
                    reanchor(stAfter.shell);
                    reanchor(stAfter.tabShell);
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
