// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorLayer/TopologyCoordinator.h>

#include "internal.h"

#include <QCoreApplication>
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
        if (cfg.debounceMs < 0) {
            qCWarning(lcPhosphorLayer) << "TopologyCoordinator: negative debounceMs" << cfg.debounceMs
                                       << "— clamping to 0";
        }
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
            // Callback runs on the Wayland dispatch thread (not the GUI
            // thread). Marshal unconditionally to qApp — QPointer is NOT
            // thread-safe for reads concurrent with GUI-thread destruction,
            // so testing it here would race. Liveness check happens in the
            // GUI-thread lambda under QPointer's documented single-thread
            // invariant. The removeCompositorLostCallback in ~Impl severs
            // the subscription before ~TopologyCoordinator returns, so any
            // already-queued invocation with a stale raw pointer is safe
            // because `qp` is re-captured here by value and the auto-null
            // happens on ~QObject (GUI thread), which cannot race with a
            // callback that has already transitioned to GUI-thread context.
            QPointer<TopologyCoordinator> qp = m_q;
            m_compositorLostCookie = m_transport->addCompositorLostCallback([qp]() mutable {
                QMetaObject::invokeMethod(
                    qApp,
                    [qp]() {
                        if (!qp) {
                            return;
                        }
                        Q_EMIT qp->compositorRestarted();
                    },
                    Qt::QueuedConnection);
            });
        }
    }

    ~Impl()
    {
        // Stop the debounce timer eagerly. QTimer's dtor stops itself, but the
        // stop happens during member destruction — AFTER this body runs and
        // AFTER the compositor-lost unsubscribe below. Stopping up-front makes
        // the teardown order explicit: no new fireSync() can start between
        // unsubscribe and member destruction, even if a nested event loop (e.g.
        // inside a consumer's aboutToQuit slot) pumps events mid-teardown.
        m_debounceTimer.stop();

        // Unsubscribe before destruction so the broadcaster doesn't invoke a
        // stale lambda capturing our (now-destroyed) QPointer<Self> after
        // aboutToQuit fires. Without this, a transport outliving its
        // coordinator leaked a dead entry per instance.
        if (m_transport && m_compositorLostCookie != 0) {
            m_transport->removeCompositorLostCallback(m_compositorLostCookie);
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
        // Callbacks attached during this fireSync() DO NOT run for this cycle —
        // consumers relying on that behaviour should be aware. Callbacks MUST
        // NOT throw: the library is compiled with -fno-exceptions so any
        // thrown exception is undefined behaviour at the ABI boundary. This
        // invariant is documented on TopologyCoordinator::attachSyncCallback.
        const auto cbs = m_callbacks;
        for (auto it = cbs.cbegin(); it != cbs.cend(); ++it) {
            if (!m_callbacks.contains(it.key())) {
                continue;
            }
            it.value()();
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

    ILayerShellTransport::CompositorLostCookie m_compositorLostCookie = 0;
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
