// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Overlay-key migration + screen geometry-watch + screen-state invariant
// check. The three functions form a small cohesive cluster: rekeyOverlayState
// invokes installOverlayGeometryWatcher post-move, and the debug-only
// validateScreenStateInvariant verifies the cross-side pointer alignment that
// rekey is the primary risk-source for.

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include "../../core/utils.h"

#include <PhosphorOverlay/ShellHost.h>
#include <PhosphorOverlay/ShellState.h>

#include <PhosphorScreens/Manager.h>
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
    }
    PerScreenOverlayState state = std::move(donor.value());
    m_screenStates.erase(donor);
    auto inserted = m_screenStates.insert(newKey, std::move(state));

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

        // Re-anchor the live layer surface to the new VS's region. The donor's
        // anchors/margins were baked in at attach time for the old key - if the
        // flavor flip changes the target geometry (e.g. bare-physical donor
        // rekeyed to a sub-region VS target) the surface would otherwise keep
        // rendering across the full monitor. wlr-layer-shell v2+ allows
        // set_anchor / set_margin post-attach; push the corrected placement
        // through the mutable transport handle.
        if (rekeyed.shell && rekeyed.shell->shellSurface()) {
            if (auto* handle = rekeyed.shell->shellSurface()->transport()) {
                const QRect targetVsGeom = resolveScreenGeometry(m_screenManager, newKey);
                const auto placement = layerPlacementForVs(isVS ? targetVsGeom : QRect(), physScreen->geometry());
                handle->setAnchors(placement.anchors);
                handle->setMargins(placement.margins);
                if (isVS && targetVsGeom.isValid()) {
                    rekeyed.overlayGeometry = targetVsGeom;
                    if (auto* w = rekeyed.shell->shellWindow()) {
                        w->setWidth(targetVsGeom.width());
                        w->setHeight(targetVsGeom.height());
                    }
                }
            }
        }

        rekeyed.overlayGeomConnection = installOverlayGeometryWatcher(physScreen, newKey, isVS);
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
        if (it.value().shell) {
            // Explicit const-pointer binding to reach the const
            // overload of stateFor (returns pointer-or-nullptr); the
            // non-const overload would materialize an entry on a
            // miss, which we must not do from a debug-only check.
            const PhosphorOverlay::ShellHost* host = m_shellHost.get();
            const PhosphorOverlay::ShellState* libState = host->stateFor(it.key());
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
        if (!st.overlayPhysScreen || !st.shell) {
            return; // Main overlay context not active for this entry
        }
        if (auto* w = st.shell->shellWindow()) {
            if (isVS) {
                // Virtual screen: recompute sub-region geometry from
                // PhosphorScreens::ScreenManager (virtual proportions
                // are relative to the physical screen) and push new
                // margins via the PhosphorLayer transport handle.
                // Anchors (Top|Left) are fixed at attach and can't change.
                const QRect vsGeom = resolveScreenGeometry(m_screenManager, sid);
                if (vsGeom.isValid() && st.shell->shellSurface()) {
                    if (auto* handle = st.shell->shellSurface()->transport()) {
                        handle->setMargins(layerPlacementForVs(vsGeom, newGeom).margins);
                    }
                    w->setWidth(vsGeom.width());
                    w->setHeight(vsGeom.height());
                    st.overlayGeometry = vsGeom;
                    updateOverlayWindow(sid, screenPtr);
                    return;
                }
            } else {
                // Physical screen: AnchorAll auto-sizes to the screen;
                // just mirror the resize to our cached state.
                w->setWidth(newGeom.width());
                w->setHeight(newGeom.height());
                st.overlayGeometry = newGeom;
                updateOverlayWindow(sid, screenPtr);
            }
        }
    });
}

} // namespace PlasmaZones
