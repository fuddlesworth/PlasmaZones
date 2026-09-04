// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SocketPopoutTransport.h"

#include "ControlCenterController.h"

#include <PhosphorPopout/PopoutRequest.h>

#include <QLoggingCategory>
#include <QScreen>

#include <utility>

namespace {
Q_LOGGING_CATEGORY(lcSocketTransport, "phosphorshell.popout.socket")
}

namespace PhosphorShellApp {

SocketPopoutTransport::SocketPopoutTransport(ControlCenterController* controller)
    : m_controller(controller)
{
}

QString SocketPopoutTransport::openSurface(const PhosphorPopout::PopoutRequest& request)
{
    if (!m_controller) {
        qCWarning(lcSocketTransport) << "refusing" << request.popoutId << "— no controller to drive";
        return {};
    }
    if (!m_openHandle.isEmpty()) {
        qCWarning(lcSocketTransport) << "refusing" << request.popoutId << "— the bar's socket is already open";
        return {};
    }

    // targetScreen names the output whose bar fired. Null means "the
    // transport decides", and for a bar-painted pocket the sensible
    // decision is the primary output's bar. The log line says WHICH leg
    // resolved it: a request that should have carried a screen but shows
    // "primary fallback" here means the QML side handed over null — the
    // symptom of the QScreen* return type reaching QML without its
    // QObject flag (see ControlCenterController.h), which on this machine
    // is invisible by eye because the primary is the only output that
    // matters.
    const bool fromRequest = request.targetScreen != nullptr;
    const QString screenName = fromRequest ? request.targetScreen->name() : m_controller->screenOf(nullptr)->name();
    qCDebug(lcSocketTransport) << "opening" << request.popoutId << "in the socket on" << screenName
                               << (fromRequest ? "(from request)" : "(primary fallback)");

    m_openHandle = QStringLiteral("socket-%1").arg(++m_counter);
    m_controller->setOpenScreen(screenName);
    return m_openHandle;
}

void SocketPopoutTransport::closeSurface(const QString& handle)
{
    // Idempotent, and a no-op for any handle but the live one: a stale
    // handle from an earlier open must not close a later one.
    if (handle.isEmpty() || handle != m_openHandle) {
        return;
    }
    m_openHandle.clear();
    if (m_controller) {
        m_controller->setOpenScreen({});
    }
}

void SocketPopoutTransport::setSurfaceDismissedCallback(std::function<void(const QString&)> callback)
{
    m_dismissed = std::move(callback);
}

} // namespace PhosphorShellApp
