// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "desktopstage.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QUuid>
namespace PhosphorShellDashboard {
namespace {
QDBusMessage request(const QString& method)
{
    return QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/PlasmaZones/ShellOverview"),
                                          QStringLiteral("org.plasmazones.ShellOverview"), method);
}
}
DesktopStage::DesktopStage(QObject* parent)
    : QObject(parent)
    , m_token(QUuid::createUuid().toString())
{
    auto* watcher = new QDBusServiceWatcher(QStringLiteral("org.kde.KWin"), QDBusConnection::sessionBus(),
                                            QDBusServiceWatcher::WatchForUnregistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        ++m_generation;
        m_requested = false;
        setActive(false);
    });
}
DesktopStage::~DesktopStage()
{
    hide();
}
void DesktopStage::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    Q_EMIT activeChanged();
}
void DesktopStage::show(const QString& screen, const QRectF& rect, bool animate)
{
    if (screen.isEmpty())
        return;
    m_requested = true;
    const int generation = ++m_generation;
    auto call = request(QStringLiteral("begin"));
    call.setArguments({screen, rect.x(), rect.y(), rect.width(), rect.height(), animate, m_token});
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher* result) {
        const QDBusPendingReply<bool> reply = *result;
        result->deleteLater();
        if (generation == m_generation)
            setActive(!reply.isError() && reply.value());
    });
}
void DesktopStage::hide()
{
    ++m_generation;
    if (m_requested) {
        // Closing surfaces can outlive their replacement during an exit
        // animation. A token keeps their late destructor from closing the
        // new preview on the process's shared D-Bus connection.
        auto call = request(QStringLiteral("end"));
        call.setArguments({m_token});
        QDBusConnection::sessionBus().asyncCall(call);
    }
    m_requested = false;
    setActive(false);
}
}
