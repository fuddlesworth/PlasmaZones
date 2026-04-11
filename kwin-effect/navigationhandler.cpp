// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "navigationhandler.h"
#include "plasmazoneseffect.h"
#include "dbus_constants.h"

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
// Floating window tracking
// ═══════════════════════════════════════════════════════════════════════════════

bool NavigationHandler::isWindowFloating(const QString& windowId) const
{
    if (m_floatingWindows.contains(windowId)) {
        return true;
    }
    QString appId = m_effect->appIdForInstance(windowId);
    return (appId != windowId && m_floatingWindows.contains(appId));
}

void NavigationHandler::setWindowFloating(const QString& windowId, bool floating)
{
    if (floating) {
        m_floatingWindows.insert(windowId);
    } else {
        m_floatingWindows.remove(windowId);
        QString appId = m_effect->appIdForInstance(windowId);
        if (appId != windowId) {
            m_floatingWindows.remove(appId);
        }
    }
}

void NavigationHandler::syncFloatingWindowsFromDaemon()
{
    if (!m_effect->isDaemonReady("sync floating windows")) {
        return;
    }

    QDBusPendingCall pendingCall =
        m_effect->asyncMethodCall(PlasmaZones::DBus::Interface::WindowTracking, QStringLiteral("getFloatingWindows"));
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QDBusPendingReply<QStringList> reply = *w;
        if (!reply.isValid()) {
            qCDebug(lcEffect) << "Failed to get floating windows from daemon";
            return;
        }

        QStringList floatingIds = reply.value();
        m_floatingWindows.clear();

        for (const QString& id : floatingIds) {
            m_floatingWindows.insert(id);
        }

        qCDebug(lcEffect) << "Synced" << m_floatingWindows.size() << "floating windows from daemon";
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

    QDBusPendingCall pendingCall = m_effect->asyncMethodCall(PlasmaZones::DBus::Interface::WindowTracking,
                                                             QStringLiteral("queryWindowFloating"), {windowId});
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId](QDBusPendingCallWatcher* w) {
        QDBusPendingReply<bool> reply = *w;
        if (reply.isValid()) {
            bool floating = reply.value();
            if (floating) {
                m_floatingWindows.insert(windowId);
                qCDebug(lcEffect) << "Synced floating state for window" << windowId << "- is floating";
            } else {
                m_floatingWindows.remove(windowId);
            }
        }
        w->deleteLater();
    });
}

} // namespace PlasmaZones
