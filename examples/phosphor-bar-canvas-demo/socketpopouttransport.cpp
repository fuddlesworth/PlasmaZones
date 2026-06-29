// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SocketPopoutTransport.h"

#include <QStringLiteral>

#include <utility>

namespace PhosphorBarCanvasDemo {

QString SocketPopoutTransport::openSurface(const PhosphorPopout::PopoutRequest&)
{
    // A unique non-empty handle tells the controller the open succeeded.
    // The content is drawn by the BarCanvas socket, not here, so the
    // request (including its possibly-null content) is unused.
    return QStringLiteral("socket-%1").arg(++m_counter);
}

void SocketPopoutTransport::closeSurface(const QString&)
{
    // No surface to tear down (the BarCanvas socket is the visible UI,
    // driven by the controller's signals). Idempotent no-op per contract.
}

void SocketPopoutTransport::setSurfaceDismissedCallback(std::function<void(const QString&)> callback)
{
    m_dismissed = std::move(callback);
}

} // namespace PhosphorBarCanvasDemo
