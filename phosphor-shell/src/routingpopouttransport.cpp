// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RoutingPopoutTransport.h"

#include <PhosphorPopout/PopoutRequest.h>

#include <QLoggingCategory>

#include <utility>

namespace {
Q_LOGGING_CATEGORY(lcPopoutRouter, "phosphorshell.popout.router")
}

namespace PhosphorShellApp {

RoutingPopoutTransport::RoutingPopoutTransport(PhosphorPopout::IPopoutTransport* layer,
                                               PhosphorPopout::IPopoutTransport* socket, QSet<QString> socketHostedIds)
    : m_layer(layer)
    , m_socket(socket)
    , m_socketHostedIds(std::move(socketHostedIds))
{
    // Both inner transports report self-dismissals to us, and we forward
    // them as one stream to whatever the controller installs. The inner
    // callbacks are installed ONCE here rather than re-installed on every
    // setSurfaceDismissedCallback, so a detach from the controller (empty
    // function) leaves the inner wiring intact and only empties m_dismissed;
    // a dismissal arriving after detach then drops on the floor here,
    // which is what the contract wants.
    const auto forward = [this](const QString& handle) {
        m_owners.remove(handle);
        if (m_dismissed) {
            m_dismissed(handle);
        }
    };
    if (m_layer) {
        m_layer->setSurfaceDismissedCallback(forward);
    }
    if (m_socket) {
        m_socket->setSurfaceDismissedCallback(forward);
    }
}

RoutingPopoutTransport::~RoutingPopoutTransport()
{
    // Detach from both inner transports so neither can call into a dead
    // router. Mirrors what the controller does to us.
    if (m_layer) {
        m_layer->setSurfaceDismissedCallback({});
    }
    if (m_socket) {
        m_socket->setSurfaceDismissedCallback({});
    }
}

QString RoutingPopoutTransport::openSurface(const PhosphorPopout::PopoutRequest& request)
{
    PhosphorPopout::IPopoutTransport* target = m_socketHostedIds.contains(request.popoutId) ? m_socket : m_layer;
    if (!target) {
        qCWarning(lcPopoutRouter) << "refusing" << request.popoutId << "— no transport for its route";
        return {};
    }
    const QString handle = target->openSurface(request);
    // An empty handle is the contract's "refused" sentinel. Record nothing
    // for it, or a later close would route a phantom handle back into a
    // transport that never issued it.
    if (!handle.isEmpty()) {
        m_owners.insert(handle, target);
    }
    return handle;
}

void RoutingPopoutTransport::closeSurface(const QString& handle)
{
    // take() rather than value(): a handle is closed exactly once, and
    // forgetting it here keeps the map from growing for the life of the
    // shell. An unknown handle is a no-op per contract.
    if (PhosphorPopout::IPopoutTransport* owner = m_owners.take(handle)) {
        owner->closeSurface(handle);
    }
}

void RoutingPopoutTransport::setSurfaceDismissedCallback(std::function<void(const QString&)> callback)
{
    m_dismissed = std::move(callback);
}

} // namespace PhosphorShellApp
