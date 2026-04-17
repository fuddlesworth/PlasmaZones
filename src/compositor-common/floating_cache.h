// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "window_id.h"

#include <QSet>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Compositor-agnostic floating window state cache
 *
 * Tracks which windows are floating using full windowId with appId fallback.
 * Shared by all compositor plugins to avoid duplicating the lookup logic.
 *
 * setFloating(false) removes the exact windowId and only removes the bare
 * appId key if no other full-ID entry shares it. This ensures clearing
 * "firefox|1" does not affect "firefox|2"'s float state via the appId
 * fallback in isFloating().
 */
class FloatingCache
{
public:
    bool isFloating(const QString& windowId) const
    {
        if (m_floatingWindows.contains(windowId)) {
            return true;
        }
        QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
        return (appId != windowId && m_floatingWindows.contains(appId));
    }

    void setFloating(const QString& windowId, bool floating)
    {
        if (floating) {
            m_floatingWindows.insert(windowId);
        } else {
            m_floatingWindows.remove(windowId);
            QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
            if (appId != windowId) {
                // Only remove bare appId key if no other full-ID entry shares it.
                // Without this guard, clearing "firefox|1" would also clear the bare
                // "firefox" key, making isFloating("firefox|2") return false via the
                // appId fallback even though "firefox|2" is still floating.
                bool otherFullIdExists = false;
                for (const QString& entry : m_floatingWindows) {
                    if (entry != appId && ::PhosphorIdentity::WindowId::extractAppId(entry) == appId) {
                        otherFullIdExists = true;
                        break;
                    }
                }
                if (!otherFullIdExists) {
                    m_floatingWindows.remove(appId);
                }
            }
        }
    }

    void clear()
    {
        m_floatingWindows.clear();
    }

    int size() const
    {
        return m_floatingWindows.size();
    }

    /// Convenience alias for setFloating(windowId, true). Kept symmetric
    /// with remove() so both entry points share the same guard logic.
    void insert(const QString& windowId)
    {
        setFloating(windowId, true);
    }

    /// Convenience alias for setFloating(windowId, false).
    void remove(const QString& windowId)
    {
        setFloating(windowId, false);
    }

private:
    QSet<QString> m_floatingWindows;
};

} // namespace PlasmaZones
