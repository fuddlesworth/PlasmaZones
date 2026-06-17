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
 * for the IsSnapped / Zone / IsFloating window-rule match fields.
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
        return !m_byInstance.value(::PhosphorIdentity::WindowId::extractInstanceId(windowId)).isEmpty();
    }

    /// Record @p windowId's zone. An empty @p zoneId removes the entry (the
    /// window left its zone — unsnapped / floated / screen-changed).
    void setZone(const QString& windowId, const QString& zoneId)
    {
        const QString instanceId = ::PhosphorIdentity::WindowId::extractInstanceId(windowId);
        if (zoneId.isEmpty()) {
            m_byInstance.remove(instanceId);
        } else {
            m_byInstance.insert(instanceId, zoneId);
        }
    }

    void remove(const QString& windowId)
    {
        m_byInstance.remove(::PhosphorIdentity::WindowId::extractInstanceId(windowId));
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
