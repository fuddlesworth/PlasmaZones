// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorIdentity/WindowId.h>

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
 *   - App-wide floats (a bare appId, e.g. a session-restore marker floating every
 *     instance of an app) are keyed by appId and match every instance via the
 *     fallback in @c isFloating.
 *
 * Mirrors the authoritative @c WindowTrackingService::setFloating: unfloating a
 * specific instance ALSO drops the coarse app-wide (bare appId) entry. Still-
 * floating siblings keep their own instance entries, so this never clears them;
 * it only removes the session-restore appId marker (matching the daemon, which
 * removes the appId entry unconditionally on instance unfloat — no sibling scan,
 * which would not survive a mid-session class rename anyway).
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
        if (isComposite) {
            const QString instanceId = ::PhosphorIdentity::WindowId::extractInstanceId(windowId);
            // A composite with an empty instanceId ("appId|") is malformed: keying
            // it would alias every other empty-instance window onto one wildcard
            // slot. Reject rather than corrupt the cache.
            if (instanceId.isEmpty()) {
                return;
            }
            if (floating) {
                m_byInstance.insert(instanceId);
            } else {
                m_byInstance.remove(instanceId);
                // Mirror WindowTrackingService::setFloating: an explicit instance
                // unfloat also drops the coarse app-wide fallback. Siblings keep
                // their own instance entries, so this only clears the appId marker.
                m_byAppId.remove(appId);
            }
        } else if (floating) {
            m_byAppId.insert(windowId);
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
    QSet<QString> m_byInstance; ///< instance floats, keyed by stable instanceId
    QSet<QString> m_byAppId; ///< app-wide floats, keyed by appId
};

} // namespace PhosphorCompositor
