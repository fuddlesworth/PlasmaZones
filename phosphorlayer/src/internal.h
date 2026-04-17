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
 */
class CompositorLostBroadcaster
{
public:
    using Callback = ILayerShellTransport::CompositorLostCallback;

    /// Register a callback. If fire() has already run, the callback is
    /// invoked immediately (outside the mutex).
    void addCallback(Callback cb)
    {
        if (!cb) {
            return;
        }
        QMutexLocker lock(&m_mutex);
        if (m_fired) {
            lock.unlock();
            cb();
            return;
        }
        m_callbacks.append(std::move(cb));
    }

    /// Fire every registered callback exactly once. Idempotent.
    void fire()
    {
        QList<Callback> snapshot;
        {
            QMutexLocker lock(&m_mutex);
            if (m_fired) {
                return;
            }
            m_fired = true;
            snapshot = m_callbacks;
            m_callbacks.clear();
        }
        // Invoke outside the mutex so callbacks that call back into the
        // transport (or any code that acquires another lock) can't
        // deadlock on re-entry.
        for (const auto& cb : std::as_const(snapshot)) {
            if (cb) {
                cb();
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
    QMutex m_mutex;
    QList<Callback> m_callbacks;
    bool m_fired = false;
};

} // namespace PhosphorLayer
