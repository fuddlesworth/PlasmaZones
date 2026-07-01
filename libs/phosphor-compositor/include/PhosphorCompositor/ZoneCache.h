// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorIdentity/WindowId.h>

#include <QHash>
#include <QString>

namespace PhosphorCompositor {

/**
 * @brief Compositor-agnostic snap-zone state cache.
 *
 * Maps a window to the snap-zone UUID it occupies. A window is "snapped" iff it
 * has a non-empty zone entry; a floating or unmanaged window carries no entry.
 * Sibling of @c FloatingCache — together the two describe a window's placement
 * for the IsSnapped / Zone / IsFloating window-rule match fields. Both caches key
 * per-instance state by the stable instanceId (below); FloatingCache additionally
 * carries an app-wide appId keyspace (a bare appId, e.g. a session-restore marker
 * floating every instance of an app).
 *
 * Keyed by the STABLE instanceId, not the full composite windowId. A window's
 * appId can mutate mid-session (Electron / CEF apps rename their window class),
 * which changes the composite `appId|instanceId` but never the instanceId.
 * Keying by instanceId keeps a snapped window's zone resolvable across that
 * rename, so a Zone / IsSnapped rule keeps matching it. All accessors take the
 * composite windowId and extract the instanceId internally, so callers stay
 * composite-id-based. A bare appId (no `|` separator) is its own instanceId per
 * @c extractInstanceId, so it degrades gracefully.
 */
class ZoneCache
{
public:
    /// The snap-zone UUID @p windowId occupies, or empty if it occupies none.
    QString zoneForWindow(const QString& windowId) const
    {
        return m_byInstance.value(::PhosphorIdentity::WindowId::extractInstanceId(windowId));
    }

    /// True iff @p windowId is snapped into a zone (has a non-empty entry).
    bool isSnapped(const QString& windowId) const
    {
        return !zoneForWindow(windowId).isEmpty();
    }

    /// Record @p windowId's zone. An empty @p zoneId removes the entry (the
    /// window left its zone — unsnapped / floated / screen-changed). Returns true
    /// iff the stored zone actually changed, so a caller can drive a side effect
    /// (e.g. re-resolving IsSnapped / Zone rules) only when the stored zone changed.
    bool setZone(const QString& windowId, const QString& zoneId)
    {
        const QString instanceId = ::PhosphorIdentity::WindowId::extractInstanceId(windowId);
        // A composite with an empty instanceId ("appId|") is malformed: keying it
        // would alias every empty-instance window onto one wildcard slot. Ignore.
        if (instanceId.isEmpty()) {
            return false;
        }
        if (zoneId.isEmpty()) {
            return m_byInstance.remove(instanceId) > 0;
        }
        const auto it = m_byInstance.constFind(instanceId);
        if (it != m_byInstance.constEnd() && it.value() == zoneId) {
            return false;
        }
        m_byInstance.insert(instanceId, zoneId);
        return true;
    }

    /// Remove @p windowId's entry. Returns true iff an entry was actually removed.
    bool remove(const QString& windowId)
    {
        return m_byInstance.remove(::PhosphorIdentity::WindowId::extractInstanceId(windowId)) > 0;
    }

    void clear()
    {
        m_byInstance.clear();
    }

    int size() const
    {
        return m_byInstance.size();
    }

private:
    QHash<QString, QString> m_byInstance; ///< stable instanceId → snap-zone UUID
};

} // namespace PhosphorCompositor
