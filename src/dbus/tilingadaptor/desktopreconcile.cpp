// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilingadaptor.h"

#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include "core/platform/logging.h"

#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorPlacement/WindowTrackingService.h>

namespace PlasmaZones {

namespace {
// The window's desktop set as the engines key their states: x11 numbering,
// exactly what the effect stamps into the registry from
// VirtualDesktop::x11DesktopNumber. A multi-desktop span carries the full
// list in virtualDesktops with virtualDesktop equal to its first entry, so
// the list is preferred when present. Empty for a sticky window or one whose
// desktop is unknown; DesktopSpan tells those two apart.
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

// Whether two metadata records describe the same context membership, without
// building a set for either. The registry re-emits for every metadata edit,
// title ticks included, and the overwhelming majority of those leave the
// context fields untouched — so the cheap compare comes first and only a real
// difference pays for the sets.
bool sameContextFields(const PhosphorEngine::WindowMetadata& a, const PhosphorEngine::WindowMetadata& b)
{
    return a.virtualDesktop == b.virtualDesktop && a.virtualDesktops == b.virtualDesktops && a.activity == b.activity
        && a.isSticky == b.isSticky;
}
} // namespace

PhosphorEngine::DesktopSpan TilingAdaptor::spanFor(const PhosphorEngine::WindowMetadata& meta,
                                                   const QString& canonicalWindowId) const
{
    PhosphorEngine::DesktopSpan span;
    span.activity = meta.activity;
    // The effect stamps the on-all-desktops bit into the same metadata push
    // that carries the desktop set, so the two are one snapshot. The
    // service's own sticky report (setWindowSticky) is the fallback for a
    // push that never carried the field, and it arrives BEFORE the metadata
    // push for the same transition, so it is never behind it.
    if (meta.isSticky.has_value()) {
        span.sticky = *meta.isSticky;
    } else if (m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()) {
        span.sticky = m_windowTrackingAdaptor->service()->isWindowSticky(canonicalWindowId);
    }
    span.desktops = span.sticky ? QSet<int>{} : desktopSetOf(meta);
    // A window with no desktop stamped is UNKNOWN, not sticky: reading the
    // empty set as "every desktop" adopted such a window into every desktop
    // the user visited.
    span.known = span.sticky || !span.desktops.isEmpty();
    return span;
}

PhosphorEngine::DesktopSpanQuery TilingAdaptor::desktopSpanQuery() const
{
    return [this](const QString& windowId) -> PhosphorEngine::DesktopSpan {
        if (!m_windowRegistry) {
            return {};
        }
        const auto meta = m_windowRegistry->metadata(PhosphorIdentity::WindowId::extractInstanceId(windowId));
        if (!meta) {
            return {};
        }
        return spanFor(*meta, m_windowRegistry->canonicalizeForLookup(windowId));
    };
}

void TilingAdaptor::setWindowRegistry(PhosphorEngine::WindowRegistry* registry)
{
    QObject::disconnect(m_registryDesktopConnection);
    m_registryDesktopConnection = {};
    m_windowRegistry = registry;
    if (!registry) {
        return;
    }
    m_registryDesktopConnection =
        QObject::connect(registry, &PhosphorEngine::WindowRegistry::metadataChanged, this,
                         [this, registry](const QString& instanceId, const PhosphorEngine::WindowMetadata& oldMeta,
                                          const PhosphorEngine::WindowMetadata& newMeta) {
                             // Only a change to the CONTEXT a window belongs to is a
                             // move. The registry re-emits for every metadata edit,
                             // title ticks included, and those must not cost an engine
                             // walk each.
                             if (sameContextFields(oldMeta, newMeta)) {
                                 return;
                             }
                             // The engines key by the canonical composite id; the registry
                             // signals with the bare instance id.
                             const QString windowId = registry->canonicalizeForLookup(instanceId);
                             reconcileWindowMembership(windowId, spanFor(newMeta, windowId));
                         });
}

void TilingAdaptor::applyMembershipResult(PhosphorEngine::IPlacementEngine* engine, bool lifecycleEngine,
                                          const PhosphorEngine::MembershipReconcileResult& result)
{
    if (result.isEmpty()) {
        return;
    }
    // Windows that lost EVERY context in this engine. For a lifecycle engine
    // that is a full release and carries the pipeline bookkeeping the engine
    // cannot do itself: the replay cache, the float-relay dedup, the parked
    // opens, and the move excuses that keep the next announce from being
    // read as a session restore. The engine's own windowClosed is a no-op
    // by then, so the bookkeeping is the whole point.
    QSet<QString> fullyReleased;
    for (const auto& [windowId, key] : result.released) {
        if (lifecycleEngine && !engine->heldKeyForWindow(windowId)) {
            fullyReleased.insert(windowId);
        }
        if (!lifecycleEngine && m_windowTrackingAdaptor) {
            // Keep the persisted placement record honest. The release changed
            // the window's snap state, and the engine signal that normally
            // reports such a change also clears the tiling engines' float
            // markers, which would be wrong for a window legitimately floating
            // on the desktop it moved TO — so the record is refreshed directly
            // instead. fromStateChange because an engine state change is
            // authoritative even for a minimized window.
            m_windowTrackingAdaptor->captureWindowPlacement(windowId, QString(), /*fromStateChange=*/true);
            // The effect keeps its own per-window zone cache, fed by
            // windowStateChanged, and the IsSnapped / Zone rule-match fields
            // read it. Left unsaid it keeps naming the zone this window has
            // just been released from, so rules scoped to that zone go on
            // matching a window that is no longer in it. This is the second
            // of the two consumers a context release must drive by hand; see
            // relayWindowReleasedFromContext for why the engine signal that
            // would drive all of them at once is wrong here.
            m_windowTrackingAdaptor->relayWindowReleasedFromContext(windowId, key.screenId);
        }
    }
    if (lifecycleEngine) {
        for (const QString& windowId : std::as_const(fullyReleased)) {
            qCInfo(lcDbusTiling) << "reconcileWindowMembership: window" << windowId
                                 << "holds no context in its engine any more — releasing it from the pipeline";
            // Released through the engine that ANSWERED, not by re-resolving
            // the id: engineOwningWindow decides on isWindowTracked, which is
            // not the same predicate across engines. The adaptor's move excuse
            // stays unarmed: every other caller is the effect immediately
            // before a re-announce; this one has no such pairing, and an
            // unconsumed excuse is spent by a later unrelated announce.
            releaseWindowTrackingVia(windowId, engine, /*armMoveExcuse=*/false);
        }
    }
}

void TilingAdaptor::reconcileWindowMembership(const QString& windowId, const PhosphorEngine::DesktopSpan& span)
{
    if (windowId.isEmpty() || !span.known) {
        // A window whose desktop is not known yet has nothing to check; the
        // log line is the only way to tell a deliberate no-op from a missed
        // reconcile in a journal.
        qCDebug(lcDbusTiling) << "reconcileWindowMembership:" << windowId
                              << "has no known desktop yet — nothing to check";
        return;
    }
    const PhosphorEngine::DesktopSpanQuery spanOf = [&span](const QString&) {
        return span;
    };
    // Each engine adopts the window into the context its screen is showing
    // when the span covers it, and releases the contexts the span no longer
    // covers — desktop AND activity, the two axes of the key a window moves
    // along. The screen is the third component and deliberately not part of
    // it: a window does not leave a screen the way it leaves a desktop, and
    // the effect relays an output transfer with its own release. Memberships
    // under a screen's sticky pin are left to the engine's unpin migration.
    for (PhosphorEngine::IPlacementEngine* engine : std::as_const(m_lifecycleEngines)) {
        applyMembershipResult(engine, /*lifecycleEngine=*/true, engine->reconcileWindowMemberships(windowId, spanOf));
    }
    // Snapping, which keeps per-context stores like the tiling engines but is
    // not in their pipeline. A window that left a desktop must stop being an
    // occupant of the zone it was snapped into there: zone occupancy is
    // resolved across EVERY store rather than the one in view, so a stale entry
    // remains a live navigation target and activating it drags the user back to
    // the desktop the window is really on.
    for (PhosphorEngine::IPlacementEngine* engine : std::as_const(m_membershipEngines)) {
        applyMembershipResult(engine, /*lifecycleEngine=*/false, engine->reconcileWindowMemberships(windowId, spanOf));
    }
}

void TilingAdaptor::reconcileDesktopMemberships(const QString& screenId)
{
    if (screenId.isEmpty()) {
        return;
    }
    const PhosphorEngine::DesktopSpanQuery spanOf = desktopSpanQuery();
    for (PhosphorEngine::IPlacementEngine* engine : std::as_const(m_lifecycleEngines)) {
        applyMembershipResult(engine, /*lifecycleEngine=*/true, engine->reconcileDesktopMemberships(screenId, spanOf));
    }
    for (PhosphorEngine::IPlacementEngine* engine : std::as_const(m_membershipEngines)) {
        applyMembershipResult(engine, /*lifecycleEngine=*/false, engine->reconcileDesktopMemberships(screenId, spanOf));
    }
}

} // namespace PlasmaZones
