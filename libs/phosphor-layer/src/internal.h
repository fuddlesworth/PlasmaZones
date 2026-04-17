// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/ILayerShellTransport.h>

#include <QGuiApplication>
#include <QList>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>

namespace PhosphorLayer {

Q_DECLARE_LOGGING_CATEGORY(lcPhosphorLayer)

/**
 * Shared scaffolding for transport classes that need to broadcast
 * compositor-lost to a registered callback list. Thread-safe; callbacks
 * are invoked OUTSIDE the mutex so a callback that re-registers (or
 * otherwise re-enters the transport) can't deadlock. Once fired, future
 * addCallback() invocations fire immediately so late registrants still
 * see the event.
 *
 * Each registration returns an opaque `Cookie`; pass it to
 * @ref removeCallback to unsubscribe before the callback's captures
 * become invalid. Without removal, long-lived transports leak stale
 * callback entries for the lifetime of the process.
 */
class CompositorLostBroadcaster
{
public:
    using Callback = ILayerShellTransport::CompositorLostCallback;
    using Cookie = ILayerShellTransport::CompositorLostCookie;

    /// Register a callback. Returns a non-zero cookie; 0 indicates the
    /// callback was rejected (null). If fire() has already run, the
    /// callback is invoked immediately (outside the mutex) and the cookie
    /// is still valid-but-inert — passing it to @ref removeCallback is a
    /// harmless no-op. Cookies are monotonically assigned and never
    /// reused within a single broadcaster.
    [[nodiscard]] Cookie addCallback(Callback cb)
    {
        if (!cb) {
            return 0;
        }
        Cookie cookie;
        bool fireNow = false;
        {
            QMutexLocker lock(&m_mutex);
            cookie = ++m_nextCookie;
            if (m_fired) {
                fireNow = true;
            } else {
                m_entries.append(Entry{cookie, std::move(cb)});
            }
        }
        if (fireNow) {
            cb();
        }
        return cookie;
    }

    /// Remove a previously-registered callback. No-op for unknown cookies
    /// (including 0 and cookies whose callbacks have already fired).
    void removeCallback(Cookie cookie)
    {
        if (cookie == 0) {
            return;
        }
        QMutexLocker lock(&m_mutex);
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if (it->cookie == cookie) {
                m_entries.erase(it);
                return;
            }
        }
    }

    /// Fire every registered callback exactly once. Idempotent.
    void fire()
    {
        QList<Entry> snapshot;
        {
            QMutexLocker lock(&m_mutex);
            if (m_fired) {
                return;
            }
            m_fired = true;
            snapshot = m_entries;
            m_entries.clear();
        }
        // Invoke outside the mutex so callbacks that call back into the
        // transport (or any code that acquires another lock) can't
        // deadlock on re-entry.
        for (const auto& e : std::as_const(snapshot)) {
            if (e.cb) {
                e.cb();
            }
        }
    }

    /// Hook QGuiApplication::aboutToQuit to fire(). Returns the
    /// connection handle so the caller can disconnect in its dtor. Safe
    /// to call with `qGuiApp == nullptr` (returns an empty Connection).
    QMetaObject::Connection hookAboutToQuit()
    {
        if (auto* app = qGuiApp) {
            return QObject::connect(app, &QGuiApplication::aboutToQuit, [this] {
                fire();
            });
        }
        return {};
    }

private:
    struct Entry
    {
        Cookie cookie;
        Callback cb;
    };

    QMutex m_mutex;
    QList<Entry> m_entries;
    Cookie m_nextCookie = 0;
    bool m_fired = false;
};

} // namespace PhosphorLayer
