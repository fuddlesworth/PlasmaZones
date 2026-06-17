// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWorkspaces/VirtualDesktopManager.h>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>

namespace PhosphorWorkspaces {

VirtualDesktopManager::VirtualDesktopManager(QObject* parent)
    : QObject(parent)
{
}

VirtualDesktopManager::~VirtualDesktopManager()
{
    stop();
}

bool VirtualDesktopManager::init()
{
    initKWinDBus();
    return true;
}

void VirtualDesktopManager::initKWinDBus()
{
    m_kwinVDInterface =
        new QDBusInterface(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                           QStringLiteral("org.kde.KWin.VirtualDesktopManager"), QDBusConnection::sessionBus(), this);

    if (m_kwinVDInterface->isValid()) {
        m_useKWinDBus = true;

        refreshFromKWin();

        QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                                              QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                              QStringLiteral("currentChanged"), this,
                                              SLOT(onKWinCurrentChanged(QString)));

        QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                                              QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                              QStringLiteral("countChanged"), this,
                                              SLOT(onNumberOfDesktopsChanged(int)));

        QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                                              QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                              QStringLiteral("desktopCreated"), this, SLOT(onKWinDesktopCreated()));

        QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                                              QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                              QStringLiteral("desktopRemoved"), this, SLOT(onKWinDesktopRemoved()));

        // A live grid reshape (e.g. 1×4 → 2×2) changes `rows` WITHOUT changing
        // the desktop count, so it fires neither countChanged nor created/removed
        // — without this the cached row count goes stale and cross-desktop
        // directional navigation computes neighbours against the wrong grid shape.
        QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                                              QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                              QStringLiteral("rowsChanged"), this, SLOT(onKWinDesktopRowsChanged()));
    } else {
        delete m_kwinVDInterface;
        m_kwinVDInterface = nullptr;
    }
}

void VirtualDesktopManager::applyDesktopListReply(const QDBusMessage& reply, const QString& currentId)
{
    m_desktopNames.clear();
    m_desktopIds.clear();

    struct DesktopInfo
    {
        int position;
        QString id;
        QString name;
    };
    QList<DesktopInfo> desktops;

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QVariant outerVariant = reply.arguments().at(0);
        QDBusVariant dbusVariant = outerVariant.value<QDBusVariant>();
        QVariant innerVariant = dbusVariant.variant();

        if (innerVariant.userType() == qMetaTypeId<QDBusArgument>()) {
            const QDBusArgument& arg = *static_cast<const QDBusArgument*>(innerVariant.constData());

            arg.beginArray();
            while (!arg.atEnd()) {
                DesktopInfo info;
                arg.beginStructure();
                arg >> info.position >> info.id >> info.name;
                arg.endStructure();
                desktops.append(info);
            }
            arg.endArray();

            std::sort(desktops.begin(), desktops.end(), [](const DesktopInfo& a, const DesktopInfo& b) {
                return a.position < b.position;
            });

            for (const auto& desktop : desktops) {
                m_desktopIds.append(desktop.id);
                QString name = desktop.name;
                if (name.isEmpty()) {
                    name = QStringLiteral("Desktop %1").arg(desktop.position + 1);
                }
                m_desktopNames.append(name);
            }
        }
    }

    if (!m_desktopIds.isEmpty() && m_desktopIds.size() != m_desktopCount) {
        m_desktopCount = m_desktopIds.size();
    }

    if (!currentId.isEmpty() && !m_desktopIds.isEmpty()) {
        int idx = m_desktopIds.indexOf(currentId);
        if (idx >= 0) {
            m_currentDesktop = idx + 1;
        }
    }

    while (m_desktopNames.size() < m_desktopCount) {
        m_desktopNames.append(QStringLiteral("Desktop %1").arg(m_desktopNames.size() + 1));
    }
}

void VirtualDesktopManager::refreshFromKWin()
{
    if (!m_kwinVDInterface || !m_kwinVDInterface->isValid()) {
        return;
    }

    QVariant countVar = m_kwinVDInterface->property("count");
    if (countVar.isValid()) {
        m_desktopCount = countVar.toInt();
    }

    // Grid row count — drives the shape cross-desktop directional navigation
    // walks. Clamp to >= 1 so a missing / zero property can't divide the grid
    // arithmetic by zero.
    QVariant rowsVar = m_kwinVDInterface->property("rows");
    if (rowsVar.isValid()) {
        // Read-only cache for the on-demand desktopRows() pull used by
        // cross-desktop directional navigation; no NOTIFY signal — nothing
        // subscribes to row-shape changes, the value is re-read per navigation.
        m_desktopRows = qMax(1, rowsVar.toInt());
    }

    QVariant currentVar = m_kwinVDInterface->property("current");
    QString currentId;
    if (currentVar.isValid()) {
        currentId = currentVar.toString();
    }

    QDBusMessage getDesktopsMsg =
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    getDesktopsMsg << QStringLiteral("org.kde.KWin.VirtualDesktopManager") << QStringLiteral("desktops");

    if (!m_running) {
        QDBusMessage reply = QDBusConnection::sessionBus().call(getDesktopsMsg, QDBus::Block, 1000);
        applyDesktopListReply(reply, currentId);
        return;
    }

    ++m_refreshGeneration;
    const uint thisGeneration = m_refreshGeneration;

    QDBusPendingCall pendingCall = QDBusConnection::sessionBus().asyncCall(getDesktopsMsg);
    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, currentId, thisGeneration](QDBusPendingCallWatcher* w) {
                if (thisGeneration != m_refreshGeneration) {
                    w->deleteLater();
                    return;
                }

                QDBusPendingReply<QDBusVariant> reply = *w;
                applyDesktopListReply(reply.reply(), currentId);

                w->deleteLater();
            });
}

void VirtualDesktopManager::onKWinCurrentChanged(const QString& desktopId)
{
    int idx = m_desktopIds.indexOf(desktopId);
    int newDesktop = (idx >= 0) ? idx + 1 : 1;

    if (m_currentDesktop == newDesktop) {
        return;
    }

    m_currentDesktop = newDesktop;
    Q_EMIT currentDesktopChanged(m_currentDesktop);
}

void VirtualDesktopManager::onKWinDesktopCreated()
{
    refreshFromKWin();
    Q_EMIT desktopCountChanged(m_desktopCount);
}

void VirtualDesktopManager::onKWinDesktopRemoved()
{
    refreshFromKWin();
    Q_EMIT desktopCountChanged(m_desktopCount);
}

void VirtualDesktopManager::onKWinDesktopRowsChanged()
{
    // Re-read the grid shape so the on-demand desktopRows() pull stays fresh
    // after a live grid reshape (the desktop count is unaffected here).
    refreshFromKWin();
}

void VirtualDesktopManager::start()
{
    if (m_running) {
        return;
    }

    m_running = true;

    if (m_useKWinDBus) {
        refreshFromKWin();
    }
}

void VirtualDesktopManager::stop()
{
    m_running = false;
}

int VirtualDesktopManager::currentDesktop() const
{
    return m_useKWinDBus ? m_currentDesktop : 1;
}

void VirtualDesktopManager::setCurrentDesktop(int desktop)
{
    if (desktop < 1 || desktop > m_desktopCount) {
        return;
    }

    if (m_useKWinDBus && m_kwinVDInterface) {
        int idx = desktop - 1;
        if (idx >= 0 && idx < m_desktopIds.size()) {
            m_kwinVDInterface->setProperty("current", m_desktopIds.at(idx));
        }
    }
}

void VirtualDesktopManager::onNumberOfDesktopsChanged(int count)
{
    if (m_desktopCount == count) {
        return;
    }

    m_desktopCount = count;

    if (m_useKWinDBus) {
        refreshFromKWin();
    }

    // Clamp against the (possibly refreshed) live count, not the signal arg:
    // refreshFromKWin() may have re-read m_desktopCount from KWin's property,
    // and the current desktop must stay within whatever count is now authoritative.
    if (m_currentDesktop > m_desktopCount) {
        m_currentDesktop = m_desktopCount;
        Q_EMIT currentDesktopChanged(m_currentDesktop);
    }

    Q_EMIT desktopCountChanged(count);
}

int VirtualDesktopManager::desktopCount() const
{
    return m_useKWinDBus ? m_desktopCount : 1;
}

int VirtualDesktopManager::desktopRows() const
{
    return m_useKWinDBus ? qMax(1, m_desktopRows) : 1;
}

QStringList VirtualDesktopManager::desktopNames() const
{
    if (m_useKWinDBus && !m_desktopNames.isEmpty()) {
        return m_desktopNames;
    }

    QStringList names;
    int count = desktopCount();
    for (int i = 1; i <= count; ++i) {
        names.append(QStringLiteral("Desktop %1").arg(i));
    }
    return names;
}

} // namespace PhosphorWorkspaces
