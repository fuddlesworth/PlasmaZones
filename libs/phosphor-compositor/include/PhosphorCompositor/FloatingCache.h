// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorIdentity/WindowId.h>

#include <QHash>
#include <QSet>
#include <QString>

namespace PhosphorCompositor {

/**
 * @brief Compositor-agnostic floating window state cache
 *
 * Tracks which windows are floating, dual-keyed so both float kinds resolve:
 *   - Instance-specific floats (a single window the user/rule floated) are keyed
 *     by the STABLE instanceId, like @c ZoneCache. A window's appId can mutate
 *     mid-session (Electron / CEF apps rename their window class), changing the
 *     composite `appId|instanceId` but never the instanceId — keying by it keeps
 *     a floated window resolvable across that rename.
 *   - App-wide floats (a bare appId, e.g. a rule floating every instance of an
 *     app) are keyed by appId and match every instance via the fallback below.
 *
 * setFloating(false) on a specific instance also clears the app-wide entry once
 * no sibling instance remains floating, so the app-wide fallback does not keep
 * reporting a window as floating after its last instance was unfloated.
 */
class FloatingCache
{
public:
    bool isFloating(const QString& windowId) const
    {
        // Instance-specific float — survives appId (class) mutation.
        if (m_byInstance.contains(::PhosphorIdentity::WindowId::extractInstanceId(windowId))) {
            return true;
        }
        // App-wide float — a bare appId matches every instance of that app.
        return m_byAppId.contains(::PhosphorIdentity::WindowId::extractAppId(windowId));
    }

    void setFloating(const QString& windowId, bool floating)
    {
        const QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
        const bool isComposite = (appId != windowId); // bare appId has no separator
        if (floating) {
            if (isComposite) {
                m_byInstance.insert(::PhosphorIdentity::WindowId::extractInstanceId(windowId), appId);
            } else {
                m_byAppId.insert(windowId);
            }
        } else if (isComposite) {
            m_byInstance.remove(::PhosphorIdentity::WindowId::extractInstanceId(windowId));
            // Drop the app-wide entry only once no sibling instance is still
            // floating, mirroring the old single-set guard: without this, clearing
            // "firefox|1" would orphan a bare "firefox" that keeps isFloating()
            // true for every other instance via the fallback.
            bool otherInstanceFloating = false;
            for (const QString& entryAppId : m_byInstance) {
                if (entryAppId == appId) {
                    otherInstanceFloating = true;
                    break;
                }
            }
            if (!otherInstanceFloating) {
                m_byAppId.remove(appId);
            }
        } else {
            m_byAppId.remove(windowId);
        }
    }

    void clear()
    {
        m_byInstance.clear();
        m_byAppId.clear();
    }

    int size() const
    {
        return m_byInstance.size() + m_byAppId.size();
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
    QHash<QString, QString> m_byInstance; ///< stable instanceId → appId (instance floats)
    QSet<QString> m_byAppId; ///< app-wide floats, keyed by appId
};

} // namespace PhosphorCompositor
