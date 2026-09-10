// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilingadaptor.h"

#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

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
    m_registryDesktopConnection = QObject::connect(
        registry, &PhosphorEngine::WindowRegistry::metadataChanged, this,
        [this, registry](const QString& instanceId, const PhosphorEngine::WindowMetadata& oldMeta,
                         const PhosphorEngine::WindowMetadata& newMeta) {
            // Only a change to the CONTEXT a window belongs to is a
            // move. The registry re-emits for every metadata edit,
            // title ticks included, and those must not cost an engine
            // walk each.
            const bool activityMoved = oldMeta.activity != newMeta.activity;
            if (!activityMoved && sameDesktopFields(oldMeta, newMeta)) {
                return;
            }
            const QSet<int> desktops = desktopSetOf(newMeta);
            if (!activityMoved && desktops == desktopSetOf(oldMeta)) {
                return;
            }
            // The engines key by the canonical composite id; the registry
            // signals with the bare instance id.
            reconcileWindowMembership(registry->canonicalizeForLookup(instanceId), desktops, newMeta.activity);
        });
}

void TilingAdaptor::reconcileWindowMembership(const QString& windowId, const QSet<int>& desktops,
                                              const QString& activity)
{
    if (windowId.isEmpty()) {
        return;
    }
    // Whether a held key names a context the window has LEFT. Desktop and
    // activity are the two axes of the key that a window can move along; the
    // screen is the third, but a window does not "leave" a screen the way it
    // leaves a desktop — the effect relays that as an output transfer with its
    // own release.
    //
    // An EMPTY value on either axis means "all of them, or unknown", on the
    // window's side and on the key's side alike, and neither can be read as a
    // mismatch: a sticky window is on every desktop, a window with no activity
    // is on every activity, and a state keyed with no activity covers them all.
    const auto leftTheContext = [&desktops, &activity](const PhosphorEngine::PlacementStateKey& held) {
        if (!desktops.isEmpty() && !desktops.contains(held.desktop)) {
            return true;
        }
        return !activity.isEmpty() && !held.activity.isEmpty() && held.activity != activity;
    };
    if (desktops.isEmpty() && activity.isEmpty()) {
        // A sticky window is reported on no desktop in particular, so every
        // hold it has is still valid and there is nothing to check. The same
        // shape arrives when a window's desktop is simply unknown, which the
        // registry cannot distinguish, so the log line is the only way to
        // tell a deliberate no-op from a missed reconcile in a journal.
        qCDebug(lcDbusTiling) << "reconcileWindowMembership:" << windowId
                              << "is on no particular desktop or activity (sticky, or unknown) — nothing to check";
        return;
    }
    for (PhosphorEngine::IPlacementEngine* engine : std::as_const(m_lifecycleEngines)) {
        const std::optional<PhosphorEngine::PlacementStateKey> held = engine->heldKeyForWindow(windowId);
        if (!held || !leftTheContext(*held)) {
            continue;
        }
        // A screen pinned to a desktop keys its states by the PIN, not by the
        // desktop the compositor reports, so a held key naming the pin is not
        // evidence the window went anywhere. The pin is dropped and the state
        // migrated by the engine's own unpin path; releasing here would fight
        // it. Same comparison the daemon's other two pin gates make.
        if (engine->stickyPinnedDesktopForScreen(held->screenId) == held->desktop) {
            qCDebug(lcDbusTiling) << "reconcileWindowMembership:" << windowId << "is held on screen" << held->screenId
                                  << "under its sticky pin (desktop" << held->desktop
                                  << ") — leaving it to the engine's unpin migration";
            continue;
        }
        qCInfo(lcDbusTiling) << "reconcileWindowMembership: window" << windowId
                             << "left the context it was held in (desktop" << held->desktop << "activity"
                             << held->activity << ") on screen" << held->screenId << "— it is now on desktops"
                             << desktops << "activity" << activity << "— releasing it from that state";
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
    // Snapping, which keeps per-context stores like the tiling engines but is
    // not in their pipeline. A window that left a desktop must stop being an
    // occupant of the zone it was snapped into there: zone occupancy is
    // resolved across EVERY store rather than the one in view, so a stale entry
    // remains a live navigation target and activating it drags the user back to
    // the desktop the window is really on.
    //
    // Released directly rather than through releaseWindowTracking, which exists
    // to carry the tiling dispatch's move-excuse and replay-cache work. A
    // snapped window has none of that, and arming it would suppress a
    // legitimate reclaim later.
    for (PhosphorEngine::IPlacementEngine* engine : std::as_const(m_membershipEngines)) {
        const std::optional<PhosphorEngine::PlacementStateKey> held = engine->heldKeyForWindow(windowId);
        if (!held || !leftTheContext(*held)) {
            continue;
        }
        // Inert for snapping, which neither pins screens nor overrides the
        // accessor, so this reads 0 against a key whose desktop is never 0.
        // Kept so the two loops answer the same question, and so an engine
        // joining this list later inherits the gate rather than the omission.
        if (engine->stickyPinnedDesktopForScreen(held->screenId) == held->desktop) {
            continue;
        }
        qCInfo(lcDbusTiling) << "reconcileWindowMembership: window" << windowId
                             << "left the context it was held in (desktop" << held->desktop << "activity"
                             << held->activity << ") on screen" << held->screenId
                             << "— dropping its zone assignment there";
        engine->releaseFromContext(*held, windowId);
        // Keep the persisted placement record honest. The release changed the
        // window's snap state, and the engine signal that normally reports such
        // a change also clears the tiling engines' float markers, which would
        // be wrong for a window legitimately floating on the desktop it moved
        // TO — so the record is refreshed directly instead. Without this the
        // store still reads "snapped in that zone on that desktop" and a later
        // restore puts the window back where it no longer is. fromStateChange
        // because an engine state change is authoritative even for a minimized
        // window, which is exactly what this is.
        if (m_windowTrackingAdaptor) {
            m_windowTrackingAdaptor->captureWindowPlacement(windowId, QString(), /*fromStateChange=*/true);
            // The effect keeps its own per-window zone cache, fed by
            // windowStateChanged, and the IsSnapped / Zone rule-match fields
            // read it. Left unsaid it keeps naming the zone this window has
            // just been released from, so rules scoped to that zone go on
            // matching a window that is no longer in it. This is the second of
            // the two consumers a context release must drive by hand; see
            // relayWindowReleasedFromContext for why the engine signal that
            // would drive all of them at once is wrong here.
            m_windowTrackingAdaptor->relayWindowReleasedFromContext(windowId, held->screenId);
        }
    }
}

} // namespace PlasmaZones
