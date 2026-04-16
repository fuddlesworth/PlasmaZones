// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorLayer/TopologyCoordinator.h>

#include "internal.h"

#include <QHash>

namespace PhosphorLayer {

class TopologyCoordinator::Impl
{
public:
    Impl(IScreenProvider* screens, ILayerShellTransport* transport, TopologyConfig cfg)
        : m_screens(screens)
        , m_transport(transport)
        , m_cfg(cfg)
    {
    }

    IScreenProvider* m_screens;
    ILayerShellTransport* m_transport;
    TopologyConfig m_cfg;

    CallbackId m_nextId = 1;
    QHash<CallbackId, SyncCallback> m_callbacks;
};

TopologyCoordinator::TopologyCoordinator(IScreenProvider* screens, ILayerShellTransport* transport, TopologyConfig cfg,
                                         QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(screens, transport, cfg))
{
    // Wiring to screens->notifier() and transport->addCompositorLostCallback
    // is deferred: both require the debounce timer + per-registry sync
    // machinery that phase 5 introduces. Step 1 only establishes the API
    // surface (and callback registration, which phase 5 consumes).
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
