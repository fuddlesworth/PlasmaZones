// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWayland/CompositorLost.h>

#include <utility>
#include <vector>

#include <QMutex>
#include <QMutexLocker>

namespace PhosphorWayland {

namespace {

struct Entry
{
    CompositorLostCookie cookie;
    CompositorLostCallback cb;
};

// File-scope broadcaster. Independent of the QPA plugin singleton so that
// callbacks survive plugin reload (Qt may construct/destroy the integration
// across compositor reconnects) and so that consumers can register before
// QGuiApplication starts.
//
// Thread safety: the QPA plugin fires from the Wayland event-dispatch
// context; consumers register from arbitrary threads. The mutex serialises
// register/remove/fire and the callback list snapshot is invoked outside the
// lock so re-entrant calls from inside a callback (e.g. a callback that
// removes itself) don't deadlock.
class Broadcaster
{
public:
    static Broadcaster& instance()
    {
        static Broadcaster b;
        return b;
    }

    CompositorLostCookie add(CompositorLostCallback cb)
    {
        if (!cb) {
            return 0;
        }
        CompositorLostCookie cookie;
        bool fireNow = false;
        {
            QMutexLocker lock(&m_mutex);
            cookie = ++m_nextCookie;
            if (m_fired) {
                fireNow = true;
            } else {
                m_entries.push_back(Entry{cookie, std::move(cb)});
            }
        }
        if (fireNow) {
            cb();
        }
        return cookie;
    }

    void remove(CompositorLostCookie cookie)
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

    void fire()
    {
        std::vector<Entry> snapshot;
        {
            QMutexLocker lock(&m_mutex);
            if (m_fired) {
                return;
            }
            m_fired = true;
            snapshot.swap(m_entries);
        }
        // Invoke outside the mutex: callbacks may re-enter add()/remove().
        for (auto& e : snapshot) {
            if (e.cb) {
                e.cb();
            }
        }
    }

private:
    QMutex m_mutex;
    std::vector<Entry> m_entries;
    CompositorLostCookie m_nextCookie = 0;
    bool m_fired = false;
};

} // namespace

CompositorLostCookie addCompositorLostCallback(CompositorLostCallback cb)
{
    return Broadcaster::instance().add(std::move(cb));
}

void removeCompositorLostCallback(CompositorLostCookie cookie)
{
    Broadcaster::instance().remove(cookie);
}

// Internal entry point invoked by the QPA plugin when it observes the
// `zwlr_layer_shell_v1` global being removed. Hidden from the public header
// so external code can't synthesise the event; the only legitimate caller is
// `LayerShellIntegration::registryRemoveHandler`.
void fireCompositorLost()
{
    Broadcaster::instance().fire();
}

} // namespace PhosphorWayland
