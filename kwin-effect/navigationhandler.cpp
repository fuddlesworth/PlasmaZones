// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "navigationhandler.h"
#include "plasmazoneseffect.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>

#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

NavigationHandler::NavigationHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

// ═══════════════════════════════════════════════════════════════════════════════
// Floating window tracking — sync methods use D-Bus helpers
// ═══════════════════════════════════════════════════════════════════════════════

void NavigationHandler::syncFloatingWindowsFromDaemon()
{
    if (!m_effect->isDaemonReady("sync floating windows")) {
        return;
    }

    QDBusPendingCall pendingCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getFloatingWindows"));
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QDBusPendingReply<QStringList> reply = *w;
        if (!reply.isValid()) {
            qCDebug(lcEffect) << "Failed to get floating windows from daemon";
            return;
        }

        QStringList floatingIds = reply.value();
        m_floatingCache.clear();

        for (const QString& id : floatingIds) {
            m_floatingCache.insert(id);
        }

        qCDebug(lcEffect) << "Synced" << m_floatingCache.size() << "floating windows from daemon";
    });
}

void NavigationHandler::syncFloatingStateForWindow(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }

    if (!m_effect->isDaemonReady("sync floating state")) {
        return;
    }

    QDBusPendingCall pendingCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("queryWindowFloating"), {windowId});
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId](QDBusPendingCallWatcher* w) {
        QDBusPendingReply<bool> reply = *w;
        if (reply.isValid()) {
            bool floating = reply.value();
            if (floating) {
                m_floatingCache.insert(windowId);
                qCDebug(lcEffect) << "Synced floating state for window" << windowId << "- is floating";
            } else {
                m_floatingCache.remove(windowId);
            }
        }
        w->deleteLater();
    });
}

} // namespace PlasmaZones
