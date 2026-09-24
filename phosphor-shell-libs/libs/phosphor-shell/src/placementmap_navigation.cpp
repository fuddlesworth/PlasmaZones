// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorShell/PlacementMap.h>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>

namespace PhosphorShell {
namespace {
const QString Service = QStringLiteral("org.kde.KWin");
const QString Path = QStringLiteral("/PlasmaZones/ShellOverview");
const QString Interface = QStringLiteral("org.plasmazones.ShellOverview");
}
void PlacementMapScreen::initializeNavigation()
{
    m_navigationRefresh.setSingleShot(true);
    m_navigationRefresh.setInterval(16);
    connect(&m_navigationRefresh, &QTimer::timeout, this, &PlacementMapScreen::fetchNavigation);
    auto bus = QDBusConnection::sessionBus();
    bus.connect(Service, Path, Interface, QStringLiteral("windowsChanged"), this, SLOT(requestNavigation()));
    auto* watcher = new QDBusServiceWatcher(Service, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this] {
        ++m_navigationGeneration;
        m_nativeAvailable = false;
        m_nativeWindows.clear();
        rebuildFromSource();
        requestNavigation();
    });
    requestNavigation();
}
void PlacementMapScreen::requestNavigation()
{
    m_navigationRefresh.start();
}
void PlacementMapScreen::fetchNavigation()
{
    if (m_screenName.isEmpty() || m_workArea.isEmpty())
        return;
    const int generation = ++m_navigationGeneration;
    const int desktop = isPinned() ? m_pinnedDesktop + 1 : m_currentDesktop + 1;
    auto message = QDBusMessage::createMethodCall(Service, Path, Interface, QStringLiteral("windows"));
    message.setArguments({m_screenName, desktop});
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation, desktop](QDBusPendingCallWatcher* call) {
                const QDBusPendingReply<QString> reply = *call;
                call->deleteLater();
                if (generation != m_navigationGeneration
                    || desktop != (isPinned() ? m_pinnedDesktop + 1 : m_currentDesktop + 1))
                    return;
                m_nativeAvailable = !reply.isError();
                m_nativeWindows = reply.isError() ? QList<PlacementMapParser::Cell>()
                                                  : PlacementMapParser::parseNativeWindows(reply.value(), m_workArea);
                rebuildFromSource();
            });
}
}
