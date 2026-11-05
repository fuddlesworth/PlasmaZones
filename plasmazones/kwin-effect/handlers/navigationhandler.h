// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorCompositor/FloatingCache.h>
#include <PhosphorCompositor/ZoneCache.h>

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QDBusPendingCallWatcher;

namespace PlasmaZones {

// Targeted using-declaration, not a namespace-wide directive: headers must
// not leak the whole PhosphorCompositor namespace into every includer.
using PhosphorCompositor::FloatingCache;
using PhosphorCompositor::ZoneCache;

class PlasmaZonesEffect;

/**
 * @brief Manages floating window state cache for PlasmaZones
 *
 * Navigation operations (move, focus, swap, restore, cycle, rotate, resnap) are now
 * daemon-driven: the daemon computes geometry/targets and emits applyGeometryRequested,
 * activateWindowRequested, or applyGeometriesBatch. The effect applies them directly.
 *
 * This handler retains floating window state tracking (synced from daemon via D-Bus
 * signals, queried locally for fast checks in shouldHandleWindow/isWindowFloating).
 */
class NavigationHandler : public QObject
{
    Q_OBJECT

public:
    explicit NavigationHandler(PlasmaZonesEffect* effect, QObject* parent = nullptr);

    // Floating window tracking — delegates to shared FloatingCache
    bool isWindowFloating(const QString& windowId) const
    {
        return m_floatingCache.isFloating(windowId);
    }
    /// Set @p windowId's floating state and, when it actually changes, re-resolve
    /// the window's appearance / animation rules (IsFloating is a match field).
    /// Out-of-line so the invalidation is welded to the write — no caller has to
    /// remember it. Bulk re-seeds use the cache directly (see the .cpp) because
    /// they pair with invalidateAllRuleCaches instead.
    void setWindowFloating(const QString& windowId, bool floating);
    void clearAllFloatingState()
    {
        m_floatingCache.clear();
    }
    /// Bulk re-seed the floating set from a daemon snapshot (clear + direct
    /// insert, no per-window rule invalidation). Callers pair this with a single
    /// invalidateAllRuleCaches.
    void seedFloatingWindows(const QStringList& floatingIds);
    void syncFloatingStateForWindow(const QString& windowId);
    void syncZonesFromDaemon();

    // Snap-zone tracking — synced from the daemon's windowStateChanged signal,
    // queried by the rule-query builder for the IsSnapped / Zone match fields.
    // Delegates to the shared ZoneCache (sibling of FloatingCache), which keys by
    // the stable instanceId so a class-mutating snapped window stays resolvable.

    /// The snap-zone UUID @p windowId occupies, or empty if it occupies none.
    QString zoneForWindow(const QString& windowId) const
    {
        return m_zoneCache.zoneForWindow(windowId);
    }
    /// True iff @p windowId is snapped into a zone (snap mode only — autotile
    /// tiles carry no zone UUID and never appear here).
    bool isWindowSnapped(const QString& windowId) const
    {
        return m_zoneCache.isSnapped(windowId);
    }
    /// Record @p windowId's zone. An empty @p zoneId removes the entry (the
    /// window left its zone — unsnapped / floated / screen-changed). When the
    /// zone actually changes, re-resolves the window's rules (IsSnapped / Zone /
    /// Mode are match fields). Out-of-line so the invalidation is welded to the
    /// write; bulk re-seeds use the cache directly (see the .cpp). An empty write forgets the zone notices filed
    /// for it, as clearWindowZone does.
    void setWindowZone(const QString& windowId, const QString& zoneId);
    /// Remove @p windowId's zone entry, re-resolving its rules iff one existed. A zone notice target naming it goes
    /// too: its entry is gone, and a clear sent to it later would have nothing to clear.
    void clearWindowZone(const QString& windowId);
    /// A zone notice under @p daemonId was filed for the live window @p liveId (empty: none). One the app-id
    /// fallback reached, another instance than the id names (an id the daemon still holds from another session),
    /// is remembered, so the notice that clears it reaches the same window (F1130), unless that window's own id
    /// filed its zone already (F1140): then the notice is refused, and false says the caller files no zone for it
    /// (F1152). One that reached its own window forgets every other id's target on it, since the daemon now speaks
    /// for it under its own.
    [[nodiscard]] bool noteZoneNoticeTarget(const QString& daemonId, const QString& liveId);
    /// The live window a zone notice under @p daemonId was filed for, and forgets it.
    QString takeZoneNoticeTarget(const QString& daemonId);
    void clearAllZoneState()
    {
        m_zoneCache.clear();
        m_zoneNoticeTargets.clear();
        m_zoneOwnNotices.clear();
    }
    int zoneEntryCount() const
    {
        return m_zoneCache.size();
    }

private:
    void forgetZoneNoticesOn(const QString& liveId); ///< its own-id mark and every daemon id's target naming it

    PlasmaZonesEffect* m_effect;
    FloatingCache m_floatingCache;
    ZoneCache m_zoneCache;
    QHash<QString, QString> m_zoneNoticeTargets; ///< daemon id -> live id, see noteZoneNoticeTarget
    QSet<QString> m_zoneOwnNotices; ///< instance ids whose zone a notice under their own id filed
};

} // namespace PlasmaZones
