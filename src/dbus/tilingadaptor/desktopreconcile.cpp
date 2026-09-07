// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilingadaptor.h"

#include "core/platform/logging.h"

#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/WindowRegistry.h>

namespace PlasmaZones {

namespace {
// The window's desktop set as the engines key their states: x11 numbering,
// exactly what the effect stamps into the registry from
// VirtualDesktop::x11DesktopNumber. Empty for a sticky window or one whose
// desktop is unknown (virtualDesktop 0), which the reconcile reads as
// "nothing to check". A multi-desktop span carries the full list in
// virtualDesktops with virtualDesktop equal to its first entry, so the list
// is preferred when present.
QSet<int> desktopSetOf(const PhosphorEngine::WindowMetadata& meta)
{
    QSet<int> desktops;
    for (const int desktop : meta.virtualDesktops) {
        if (desktop > 0) {
            desktops.insert(desktop);
        }
    }
    // Falls through when the span list is absent OR filtered away to nothing,
    // rather than returning an empty set from inside the span branch. The
    // registry only ever stores positive entries, so the second case is
    // unreachable today, but reading a list of junk as "sticky" would be the
    // wrong answer if that ever changed.
    if (desktops.isEmpty() && meta.virtualDesktop > 0) {
        desktops.insert(meta.virtualDesktop);
    }
    return desktops;
}

// Whether two metadata records describe the same desktop membership, without
// building a set for either. The registry re-emits for every metadata edit,
// title ticks included, and the overwhelming majority of those leave the
// desktop fields untouched — so the cheap compare comes first and only a real
// difference pays for the sets.
bool sameDesktopFields(const PhosphorEngine::WindowMetadata& a, const PhosphorEngine::WindowMetadata& b)
{
    return a.virtualDesktop == b.virtualDesktop && a.virtualDesktops == b.virtualDesktops;
}
} // namespace

void TilingAdaptor::setWindowRegistry(PhosphorEngine::WindowRegistry* registry)
{
    QObject::disconnect(m_registryDesktopConnection);
    m_registryDesktopConnection = {};
    if (!registry) {
        return;
    }
    m_registryDesktopConnection =
        QObject::connect(registry, &PhosphorEngine::WindowRegistry::metadataChanged, this,
                         [this, registry](const QString& instanceId, const PhosphorEngine::WindowMetadata& oldMeta,
                                          const PhosphorEngine::WindowMetadata& newMeta) {
                             // Only a change to the desktop set is a move. The registry
                             // re-emits for every metadata edit, title ticks included, and
                             // those must not cost an engine walk each.
                             if (sameDesktopFields(oldMeta, newMeta)) {
                                 return;
                             }
                             const QSet<int> desktops = desktopSetOf(newMeta);
                             if (desktops == desktopSetOf(oldMeta)) {
                                 return;
                             }
                             // The engines key by the canonical composite id; the registry
                             // signals with the bare instance id.
                             reconcileWindowDesktops(registry->canonicalizeForLookup(instanceId), desktops);
                         });
}

void TilingAdaptor::reconcileWindowDesktops(const QString& windowId, const QSet<int>& desktops)
{
    if (windowId.isEmpty()) {
        return;
    }
    if (desktops.isEmpty()) {
        // A sticky window is reported on no desktop in particular, so every
        // hold it has is still valid and there is nothing to check. The same
        // shape arrives when a window's desktop is simply unknown, which the
        // registry cannot distinguish, so the log line is the only way to
        // tell a deliberate no-op from a missed reconcile in a journal.
        qCDebug(lcDbusTiling) << "reconcileWindowDesktops:" << windowId
                              << "is on no particular desktop (sticky, or unknown) — nothing to check";
        return;
    }
    for (PhosphorEngine::IPlacementEngine* engine : std::as_const(m_lifecycleEngines)) {
        const std::optional<PhosphorEngine::PlacementStateKey> held = engine->heldKeyForWindow(windowId);
        if (!held || desktops.contains(held->desktop)) {
            continue;
        }
        // A screen pinned to a desktop keys its states by the PIN, not by the
        // desktop the compositor reports, so a held key naming the pin is not
        // evidence the window went anywhere. The pin is dropped and the state
        // migrated by the engine's own unpin path; releasing here would fight
        // it. Same comparison the daemon's other two pin gates make.
        if (engine->stickyPinnedDesktopForScreen(held->screenId) == held->desktop) {
            qCDebug(lcDbusTiling) << "reconcileWindowDesktops:" << windowId << "is held on screen" << held->screenId
                                  << "under its sticky pin (desktop" << held->desktop
                                  << ") — leaving it to the engine's unpin migration";
            continue;
        }
        qCInfo(lcDbusTiling) << "reconcileWindowDesktops: window" << windowId << "left desktop" << held->desktop
                             << "on screen" << held->screenId << "(now on" << desktops
                             << ") — releasing it from that state";
        // The same live-move release the effect's drag-bypass and desktop
        // arms use: no placement capture, because the window is alive and its
        // frame is not a float-back.
        //
        // Released through the engine that ANSWERED, not by re-resolving the
        // id: engineOwningWindow decides on isWindowTracked, which is not the
        // same predicate across engines, so a phantom reverse-map key in an
        // engine earlier in the list would swallow the release and leave the
        // stale slot this call exists to clear.
        //
        // The adaptor's move excuse stays unarmed here. Every other caller is
        // the effect immediately before a re-announce; this one has no such
        // pairing, and an unconsumed excuse is spent by a later unrelated
        // announce.
        releaseWindowTrackingVia(windowId, engine, /*armMoveExcuse=*/false);
        // Keep walking. Now that the release goes to a named engine, a second
        // engine holding the same window on another stale desktop gets its own
        // turn — stopping here would leave that slot occupied.
    }
}

} // namespace PlasmaZones
