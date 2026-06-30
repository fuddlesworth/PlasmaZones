// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "navigationhandler.h"
#include "plasmazoneseffect.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>

#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

NavigationHandler::NavigationHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

// ═══════════════════════════════════════════════════════════════════════════════
// Floating window tracking — sync methods use D-Bus helpers
// ═══════════════════════════════════════════════════════════════════════════════

void NavigationHandler::syncFloatingWindowsFromDaemon()
{
    if (!m_effect->isDaemonReady("sync floating windows")) {
        return;
    }

    QDBusPendingCall pendingCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getFloatingWindows"));
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QDBusPendingReply<QStringList> reply = *w;
        if (!reply.isValid()) {
            qCDebug(lcEffect) << "Failed to get floating windows from daemon";
            return;
        }

        QStringList floatingIds = reply.value();
        m_floatingCache.clear();

        for (const QString& id : floatingIds) {
            m_floatingCache.insert(id);
        }

        // Report the count of synced entries, not m_floatingCache.size() — the
        // latter sums the instance and app-wide keyspaces and would misreport when
        // both carry entries for the same app.
        qCDebug(lcEffect) << "Synced" << floatingIds.size() << "floating windows from daemon";
    });
}

void NavigationHandler::syncFloatingStateForWindow(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }

    if (!m_effect->isDaemonReady("sync floating state")) {
        return;
    }

    QDBusPendingCall pendingCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("queryWindowFloating"), {windowId});
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId](QDBusPendingCallWatcher* w) {
        QDBusPendingReply<bool> reply = *w;
        if (reply.isValid()) {
            bool floating = reply.value();
            // Per-window sync goes through setWindowFloating so a real change
            // re-resolves this window's IsFloating-scoped rules.
            setWindowFloating(windowId, floating);
            if (floating) {
                qCDebug(lcEffect) << "Synced floating state for window" << windowId << "- is floating";
            }
        }
        w->deleteLater();
    });
}

void NavigationHandler::syncZonesFromDaemon()
{
    if (!m_effect->isDaemonReady("sync window zones")) {
        return;
    }

    QDBusPendingCall pendingCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getAllWindowStates"));
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        // Authoritative refresh: drop any stale entries from a prior daemon session
        // up front — this is the ONLY place daemon-ready clears the zone cache, so a
        // failed reply leaves it empty (no stale matches) rather than keeping last
        // session's zones, and there is no clear racing the seed below.
        clearAllZoneState();

        QDBusPendingReply<PhosphorProtocol::WindowStateList> reply = *w;
        if (!reply.isValid()) {
            qCDebug(lcEffect) << "Failed to get window states from daemon";
            return;
        }

        // Seed every snapped window's zone. A window with an empty zoneId (floating
        // / unmanaged) carries no entry. Write the cache DIRECTLY (not setWindowZone)
        // so this bulk re-seed doesn't enqueue a per-window rule invalidation for
        // each window — the single invalidateAllRuleCaches below covers the whole
        // batch. setZone keys by instanceId (see ZoneCache).
        const PhosphorProtocol::WindowStateList states = reply.value();
        for (const PhosphorProtocol::WindowStateEntry& state : states) {
            if (!state.zoneId.isEmpty()) {
                m_zoneCache.setZone(state.windowId, state.zoneId);
            }
        }
        qCDebug(lcEffect) << "Synced" << zoneEntryCount() << "snapped-window zones from daemon";
        // Re-seeding the zone cache changes the IsSnapped / Zone match inputs; drop
        // the stale placement-scoped opacity verdicts so a `WHEN isSnapped` /
        // `Zone(...)` SetOpacity rule re-resolves against the fresh state on the
        // next frame (mirrors the daemon-loss invalidation).
        m_effect->invalidateAllRuleCaches();
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// Placement-state writes — each re-resolves the window's rules on a real change.
//
// These are the single chokepoints for the IsFloating / IsSnapped / Zone / Mode
// rule-match inputs. Welding the invalidation to the write (rather than asking
// every caller to remember a follow-up invalidateRuleCacheForStateChange) is what
// keeps a placement change from silently leaving a scoped border / title-bar /
// opacity rule resolved against stale state. The cache setters return whether the
// value actually changed, so a redundant re-assert costs nothing.
// ═══════════════════════════════════════════════════════════════════════════════

void NavigationHandler::setWindowFloating(const QString& windowId, bool floating)
{
    if (m_floatingCache.setFloating(windowId, floating)) {
        m_effect->invalidateRuleCacheForStateChange(windowId);
    }
}

void NavigationHandler::setWindowZone(const QString& windowId, const QString& zoneId)
{
    if (m_zoneCache.setZone(windowId, zoneId)) {
        m_effect->invalidateRuleCacheForStateChange(windowId);
    }
}

void NavigationHandler::clearWindowZone(const QString& windowId)
{
    if (m_zoneCache.remove(windowId)) {
        m_effect->invalidateRuleCacheForStateChange(windowId);
    }
}

} // namespace PlasmaZones
