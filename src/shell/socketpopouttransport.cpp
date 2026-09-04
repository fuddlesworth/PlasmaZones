// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SocketPopoutTransport.h"

#include "ControlCenterController.h"

#include <PhosphorPopout/PopoutRequest.h>

#include <QGuiApplication>
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
    // The layer sibling gets this repair from Surface::screenLost. This
    // transport owns no surface, so it has to watch the outputs directly:
    // when the one holding the open pocket goes away, its BarHost is
    // destroyed and nothing paints, but the handle and openScreen would
    // otherwise stay set forever, swallowing the next toggle and leaving the
    // IPC show verb permanently inert.
    if (auto* app = qGuiApp) {
        QObject::connect(app, &QGuiApplication::screenRemoved, app, [this](QScreen* screen) {
            if (!screen || m_openHandle.isEmpty() || screen->name() != m_openScreenName) {
                return;
            }
            qCInfo(lcSocketTransport) << "output" << m_openScreenName << "went away with the socket open; dismissing";
            selfDismiss();
        });
    }
}

SocketPopoutTransport::~SocketPopoutTransport()
{
    drain();
}

void SocketPopoutTransport::drain()
{
    m_openHandle.clear();
    m_openScreenName.clear();
    if (m_controller) {
        m_controller->setOpenScreen({});
    }
}

void SocketPopoutTransport::selfDismiss()
{
    const QString handle = m_openHandle;
    drain();
    if (m_dismissed && !handle.isEmpty()) {
        m_dismissed(handle);
    }
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
    const QScreen* const target = fromRequest ? request.targetScreen : m_controller->screenOf(nullptr);
    // screenOf() falls back to the primary and guards its own use of the
    // result, so it is nullable when the session has no outputs at all.
    const QString screenName = target ? target->name() : QString();
    // An empty name means "closed everywhere" to the controller, so opening
    // with one would set a live handle that paints nothing and then block
    // every later open. Refuse instead, before anything is recorded.
    if (screenName.isEmpty()) {
        qCWarning(lcSocketTransport) << "refusing" << request.popoutId << "— no named output to open the socket on";
        return {};
    }
    qCDebug(lcSocketTransport) << "opening" << request.popoutId << "in the socket on" << screenName
                               << (fromRequest ? "(from request)" : "(primary fallback)");

    m_openHandle = QStringLiteral("socket-%1").arg(++m_counter);
    m_openScreenName = screenName;
    m_controller->setOpenScreen(screenName);
    // Deliberately returns the member rather than a pre-notify copy.
    // setOpenScreen drives live QML bindings, so a reaction can round-trip
    // back through closeSurface and clear it; reporting the refusal sentinel
    // in that case is correct, because by the time this returns the socket is
    // genuinely closed and the caller must not record a row for it.
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
