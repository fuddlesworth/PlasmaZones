// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// WindowTrackingAdaptor — Phosphor shell surface
//
// The placement map's click/drag verbs and the identity/urgency feed behind
// its cell labels: activateWindow, getWindowMetadata, moveWindowToDesktop,
// getUrgentWindows, and the registry subscribers that keep the urgent set
// and emit windowMetadataChanged / windowUrgencyChanged. Everything here is
// a thin read of the WindowRegistry (setWindowMetadata's store) plus the two
// compositor-bound signals the navigation verbs already emit; no placement
// state is touched.
// ═══════════════════════════════════════════════════════════════════════════════

#include "windowtrackingadaptor.h"
#include "core/platform/logging.h"
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorIdentity/WindowId.h>

namespace PlasmaZones {

bool WindowTrackingAdaptor::isRegistryTracked(const QString& windowId) const
{
    if (windowId.isEmpty() || !m_windowRegistry) {
        return false;
    }
    // The registry is keyed by the BARE instance id; the shell holds the
    // composite `appId|instanceId` the engines' models carry. Same
    // extraction every other registry reader in this adaptor does.
    return m_windowRegistry->contains(PhosphorIdentity::WindowId::extractInstanceId(windowId));
}

QString WindowTrackingAdaptor::shellWindowIdFor(const QString& instanceId) const
{
    // setWindowMetadata freezes the canonical composite before it upserts,
    // so a registered instance always has a mapping; canonicalizeForLookup
    // answers the bare id only for an instance the registry never saw,
    // which none of the subscribers below can be called with.
    return m_windowRegistry ? m_windowRegistry->canonicalizeForLookup(instanceId) : instanceId;
}

void WindowTrackingAdaptor::activateWindow(const QString& windowId)
{
    if (!isRegistryTracked(windowId)) {
        qCDebug(lcDbusWindow) << "activateWindow: ignoring untracked window" << windowId;
        return;
    }
    Q_EMIT activateWindowRequested(windowId);
}

QString WindowTrackingAdaptor::getWindowMetadata(const QString& windowId, QString& title, QString& desktopFile)
{
    title.clear();
    desktopFile.clear();
    if (windowId.isEmpty() || !m_windowRegistry) {
        return QString();
    }
    const std::optional<PhosphorEngine::WindowMetadata> meta =
        m_windowRegistry->metadata(PhosphorIdentity::WindowId::extractInstanceId(windowId));
    if (!meta) {
        return QString();
    }
    title = meta->title;
    desktopFile = meta->desktopFile;
    return meta->appId;
}

void WindowTrackingAdaptor::moveWindowToDesktop(const QString& windowId, int desktop)
{
    // Desktops are 1-based on this interface (screenDesktopChanged and the
    // metadata push agree); 0 means "all / unknown" there and is not a
    // destination. Anything past the compositor's last desktop is its call.
    if (desktop < 1 || !isRegistryTracked(windowId)) {
        qCDebug(lcDbusWindow) << "moveWindowToDesktop: ignoring" << windowId << "desktop" << desktop;
        return;
    }
    Q_EMIT windowDesktopMoveRequested(windowId, desktop);
}

QStringList WindowTrackingAdaptor::getUrgentWindows()
{
    // Sorted for a stable wire answer: QSet order is hash order, and a
    // consumer diffing two reads should not see churn from a rehash.
    QStringList ids(m_urgentWindowIds.cbegin(), m_urgentWindowIds.cend());
    ids.sort();
    return ids;
}

void WindowTrackingAdaptor::onShellRegistryMetadata(const QString& instanceId,
                                                    const PhosphorEngine::WindowMetadata* previous,
                                                    const PhosphorEngine::WindowMetadata& current)
{
    const QString windowId = shellWindowIdFor(instanceId);

    // Identity: announced on first registration (previous null) and on any
    // later change of the two fields the announcement carries. Every other
    // metadata edge (geometry, flags, a caption-only refresh that left the
    // title equal) stays silent, so the signal rate tracks label changes
    // rather than the effect's push rate.
    if (!previous || previous->appId != current.appId || previous->title != current.title) {
        Q_EMIT windowMetadataChanged(windowId, current.appId, current.title);
    }

    // Urgency: a disengaged optional reads as not urgent (the registry's own
    // documented reading), so a compositor that never reports the field can
    // never light a window up, and one edge is one emission.
    const bool urgent = current.isDemandingAttention.value_or(false);
    const bool wasUrgent = m_urgentWindowIds.contains(windowId);
    if (urgent == wasUrgent) {
        return;
    }
    if (urgent) {
        m_urgentWindowIds.insert(windowId);
    } else {
        m_urgentWindowIds.remove(windowId);
    }
    Q_EMIT windowUrgencyChanged(windowId, urgent);
}

void WindowTrackingAdaptor::onShellRegistryWindowGone(const QString& instanceId)
{
    // Runs inside the registry's windowDisappeared emit, while the canonical
    // mapping is still available, so the id resolves to the same composite
    // the urgent set was keyed under.
    const QString windowId = shellWindowIdFor(instanceId);
    if (m_urgentWindowIds.remove(windowId)) {
        Q_EMIT windowUrgencyChanged(windowId, false);
    }
}

} // namespace PlasmaZones
