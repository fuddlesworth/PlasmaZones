// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SocketPopoutTransport.h"

#include <PhosphorPopout/PopoutRequest.h>

#include <QStringLiteral>

#include <utility>

namespace PhosphorBarCanvasDemo {

QString SocketPopoutTransport::openSurface(const PhosphorPopout::PopoutRequest& request)
{
    // A non-empty handle tells the controller the open succeeded. The
    // content is drawn by the BarCanvas socket, not here, so request
    // content may be null.
    const QString handle = QStringLiteral("socket-%1").arg(++m_counter);
    m_open.insert(handle, request.popoutId);
    return handle;
}

void SocketPopoutTransport::closeSurface(const QString& handle)
{
    // Idempotent per the IPopoutTransport contract.
    m_open.remove(handle);
}

void SocketPopoutTransport::setSurfaceDismissedCallback(std::function<void(const QString&)> callback)
{
    m_dismissed = std::move(callback);
}

} // namespace PhosphorBarCanvasDemo
