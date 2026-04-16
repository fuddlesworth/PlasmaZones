// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorLayer/TopologyCoordinator.h>

#include "internal.h"

#include <QMap>
#include <QPointer>
#include <QTimer>

namespace PhosphorLayer {

class TopologyCoordinator::Impl
{
public:
    Impl(TopologyCoordinator* q, IScreenProvider* screens, ILayerShellTransport* transport, TopologyConfig cfg)
        : m_q(q)
        , m_screens(screens)
        , m_transport(transport)
        , m_cfg(cfg)
    {
        m_debounceTimer.setSingleShot(true);
        m_debounceTimer.setInterval(qMax(0, cfg.debounceMs));
        QObject::connect(&m_debounceTimer, &QTimer::timeout, q, [this] {
            fireSync();
        });

        if (m_screens) {
            if (auto* n = m_screens->notifier()) {
                QObject::connect(n, &ScreenProviderNotifier::screensChanged, q, [this] {
                    onScreensChangedFromProvider();
                });
            } else {
                qCWarning(lcPhosphorLayer)
                    << "TopologyCoordinator: IScreenProvider::notifier() returned nullptr; topology response disabled";
            }
        }

        if (m_transport) {
            // Callback runs on Wayland dispatch context; hop to GUI thread.
            QPointer<TopologyCoordinator> qp = m_q;
            m_transport->addCompositorLostCallback([qp] {
                if (!qp) {
                    return;
                }
                QMetaObject::invokeMethod(
                    qp.data(),
                    [qp] {
                        Q_EMIT qp->compositorRestarted();
                    },
                    Qt::QueuedConnection);
            });
        }
    }

    void onScreensChangedFromProvider()
    {
        if (m_cfg.debugLogDiffs) {
            qCDebug(lcPhosphorLayer) << "TopologyCoordinator: screensChanged from provider, debouncing"
                                     << m_cfg.debounceMs << "ms";
        }
        if (!m_debouncing) {
            m_debouncing = true;
            Q_EMIT m_q->screensChanging();
        }
        m_debounceTimer.start();
    }

    void fireSync()
    {
        m_debouncing = false;
        if (m_cfg.debugLogDiffs) {
            qCDebug(lcPhosphorLayer) << "TopologyCoordinator: firing" << m_callbacks.size() << "sync callbacks";
        }
        // Copy to a local list so callbacks can detach themselves during the loop.
        const auto cbs = m_callbacks;
        for (auto it = cbs.cbegin(); it != cbs.cend(); ++it) {
            if (m_callbacks.contains(it.key())) {
                it.value()();
            }
        }
        Q_EMIT m_q->screensChanged();
    }

    TopologyCoordinator* const m_q;
    IScreenProvider* m_screens;
    ILayerShellTransport* m_transport;
    TopologyConfig m_cfg;

    QTimer m_debounceTimer;
    bool m_debouncing = false;

    CallbackId m_nextId = 1;
    // QMap (ordered by key) so iteration honours registration order —
    // CallbackId is monotonically increasing so insertion order ≡ key order.
    // QHash would work for lookup/removal but its iteration order is hash-
    // dependent, which broke the documented "callbacks fire in registration
    // order" contract tests rely on.
    QMap<CallbackId, SyncCallback> m_callbacks;
};

TopologyCoordinator::TopologyCoordinator(IScreenProvider* screens, ILayerShellTransport* transport, TopologyConfig cfg,
                                         QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this, screens, transport, cfg))
{
}

TopologyCoordinator::~TopologyCoordinator() = default;

TopologyCoordinator::CallbackId TopologyCoordinator::attachSyncCallback(SyncCallback cb)
{
    const auto id = m_impl->m_nextId++;
    m_impl->m_callbacks.insert(id, std::move(cb));
    return id;
}

void TopologyCoordinator::detachSyncCallback(CallbackId id)
{
    m_impl->m_callbacks.remove(id);
}

} // namespace PhosphorLayer
