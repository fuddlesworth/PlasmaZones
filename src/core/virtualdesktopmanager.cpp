// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "virtualdesktopmanager.h"
#include "layoutmanager.h"
#include "logging.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QGuiApplication>
#include <QScreen>

namespace PlasmaZones {

VirtualDesktopManager::VirtualDesktopManager(LayoutManager* layoutManager, QObject* parent)
    : QObject(parent)
    , m_layoutManager(layoutManager)
{
}

VirtualDesktopManager::~VirtualDesktopManager()
{
    stop();
    // m_kwinVDInterface is auto-deleted via Qt parent-child ownership
}

bool VirtualDesktopManager::init()
{
    // Initialize KWin D-Bus interface for virtual desktop management
    initKWinDBus();

    if (!m_useKWinDBus) {
        qCWarning(lcCore) << "KWin D-Bus unavailable, virtual desktop support limited";
    }

    return true;
}

void VirtualDesktopManager::initKWinDBus()
{
    // Connect to KWin's VirtualDesktopManager D-Bus interface
    m_kwinVDInterface =
        new QDBusInterface(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                           QStringLiteral("org.kde.KWin.VirtualDesktopManager"), QDBusConnection::sessionBus(), this);

    if (m_kwinVDInterface->isValid()) {
        m_useKWinDBus = true;
        qCInfo(lcCore) << "Using KWin D-Bus interface for virtual desktops";

        // Get initial state
        refreshFromKWin();

        // Connect to KWin D-Bus signals for changes
        // currentChanged emits QString (desktop UUID), not int
        QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                                              QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                              QStringLiteral("currentChanged"), this,
                                              SLOT(onKWinCurrentChanged(QString)));

        QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                                              QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                              QStringLiteral("countChanged"), this,
                                              SLOT(onNumberOfDesktopsChanged(int)));

        // Connect to desktop changes for name updates
        QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                                              QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                              QStringLiteral("desktopCreated"), this, SLOT(onKWinDesktopCreated()));

        QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                                              QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                              QStringLiteral("desktopRemoved"), this, SLOT(onKWinDesktopRemoved()));
    } else {
        qCWarning(lcCore) << "KWin D-Bus interface not available";
        delete m_kwinVDInterface;
        m_kwinVDInterface = nullptr;
    }
}

void VirtualDesktopManager::refreshFromKWin()
{
    if (!m_kwinVDInterface || !m_kwinVDInterface->isValid()) {
        return;
    }

    // Read properties using QDBusInterface::property()
    // count property (uint)
    QVariant countVar = m_kwinVDInterface->property("count");
    if (countVar.isValid()) {
        int newCount = countVar.toInt();
        if (newCount != m_desktopCount) {
            m_desktopCount = newCount;
            qCDebug(lcCore) << "Desktop count= " << m_desktopCount;
        }
    }

    // current property (QString - desktop UUID)
    QVariant currentVar = m_kwinVDInterface->property("current");
    QString currentId;
    if (currentVar.isValid()) {
        currentId = currentVar.toString();
    }

    // desktops property - array of structs (position, id, name)
    // Use raw D-Bus message to avoid QDBusInterface::property() triggering type conversion warnings
    // Use ASYNC call to avoid blocking the main thread during startup
    QDBusMessage getDesktopsMsg =
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    getDesktopsMsg << QStringLiteral("org.kde.KWin.VirtualDesktopManager") << QStringLiteral("desktops");

    // Increment generation to invalidate any pending callbacks from previous refreshFromKWin() calls
    // This prevents race conditions when refreshFromKWin() is called rapidly (e.g., multiple desktop
    // created/removed signals in quick succession)
    ++m_refreshGeneration;
    const uint thisGeneration = m_refreshGeneration;

    QDBusPendingCall pendingCall = QDBusConnection::sessionBus().asyncCall(getDesktopsMsg);
    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, currentId, thisGeneration](QDBusPendingCallWatcher* w) {
        // Check if this callback is stale (a newer refresh was started)
        if (thisGeneration != m_refreshGeneration) {
            qCDebug(lcCore) << "Ignoring stale virtual desktop refresh callback";
            w->deleteLater();
            return;
        }

        QDBusPendingReply<QDBusVariant> reply = *w;

        // Clear and prepare for new data
        m_desktopNames.clear();
        m_desktopIds.clear();

        // Store (position, id, name) tuples for sorting
        struct DesktopInfo
        {
            int position;
            QString id;
            QString name;
        };
        QList<DesktopInfo> desktops;

        if (reply.isValid()) {
            // The reply is a QDBusVariant containing the actual value
            // We need to carefully extract without triggering type conversion issues
            const QDBusVariant& dbusVariant = reply.value();
            const QVariant& innerVariant = dbusVariant.variant();

            if (innerVariant.userType() == qMetaTypeId<QDBusArgument>()) {
                const QDBusArgument& arg = *static_cast<const QDBusArgument*>(innerVariant.constData());

                // Parse array of structs manually using const reference
                arg.beginArray();
                while (!arg.atEnd()) {
                    DesktopInfo info;
                    arg.beginStructure();
                    arg >> info.position >> info.id >> info.name;
                    arg.endStructure();
                    desktops.append(info);
                }
                arg.endArray();

                // Sort by position
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
                    qCDebug(lcCore) << "Desktop " << (desktop.position + 1) << " id= " << desktop.id << " name= " << name;
                }
            }
        } else {
            qCWarning(lcCore) << "Failed to get virtual desktops:" << reply.error().message();
        }

        // Update count if desktops property gave us more accurate info
        if (!m_desktopIds.isEmpty() && m_desktopIds.size() != m_desktopCount) {
            m_desktopCount = m_desktopIds.size();
        }

        // Convert current UUID to 1-based position
        if (!currentId.isEmpty() && !m_desktopIds.isEmpty()) {
            int idx = m_desktopIds.indexOf(currentId);
            if (idx >= 0) {
                m_currentDesktop = idx + 1; // Convert to 1-based
                qCDebug(lcCore) << "Current desktop= " << m_currentDesktop << " id= " << currentId;
            }
        }

        // Fallback if we couldn't get names
        while (m_desktopNames.size() < m_desktopCount) {
            m_desktopNames.append(QStringLiteral("Desktop %1").arg(m_desktopNames.size() + 1));
        }

        w->deleteLater();
    });
}

void VirtualDesktopManager::onKWinCurrentChanged(const QString& desktopId)
{
    // Convert UUID to 1-based desktop number
    int idx = m_desktopIds.indexOf(desktopId);
    int newDesktop = (idx >= 0) ? idx + 1 : 1;

    if (m_currentDesktop == newDesktop) {
        return;
    }

    m_currentDesktop = newDesktop;
    qCInfo(lcCore) << "Virtual desktop changed desktop= " << m_currentDesktop << " id= " << desktopId;

    updateActiveLayout();
    Q_EMIT currentDesktopChanged(m_currentDesktop);
}

void VirtualDesktopManager::onKWinDesktopCreated()
{
    qCInfo(lcCore) << "Desktop created, refreshing";
    refreshFromKWin();
    Q_EMIT desktopCountChanged(m_desktopCount);
}

void VirtualDesktopManager::onKWinDesktopRemoved()
{
    qCInfo(lcCore) << "Desktop removed, refreshing";
    refreshFromKWin();
    Q_EMIT desktopCountChanged(m_desktopCount);
}

void VirtualDesktopManager::start()
{
    if (m_running) {
        return;
    }

    m_running = true;

    if (m_useKWinDBus) {
        refreshFromKWin();
    } else {
        m_currentDesktop = currentDesktop();
    }

    updateActiveLayout();
}

void VirtualDesktopManager::stop()
{
    if (!m_running) {
        return;
    }

    m_running = false;
    // Note: KWin D-Bus signals stay connected for lifetime
}

int VirtualDesktopManager::currentDesktop() const
{
    if (m_useKWinDBus) {
        // Use cached value from KWin D-Bus (updated by signals)
        return m_currentDesktop;
    }

    return 1;
}

void VirtualDesktopManager::setCurrentDesktop(int desktop)
{
    if (desktop < 1) {
        qCWarning(lcCore) << "Invalid desktop number:" << desktop;
        return;
    }

    if (m_useKWinDBus && m_kwinVDInterface) {
        // Use KWin D-Bus to switch desktops
        if (desktop > m_desktopCount) {
            qCWarning(lcCore) << "Desktop number" << desktop << "exceeds maximum" << m_desktopCount;
            return;
        }
        // Convert 1-based number to UUID
        int idx = desktop - 1;
        if (idx >= 0 && idx < m_desktopIds.size()) {
            m_kwinVDInterface->setProperty("current", m_desktopIds.at(idx));
        }
        return;
    }

    qCWarning(lcCore) << "setCurrentDesktop: KWin D-Bus unavailable";
}

void VirtualDesktopManager::onCurrentDesktopChanged(int desktop)
{
    if (m_currentDesktop == desktop) {
        return;
    }

    m_currentDesktop = desktop;
    qCInfo(lcCore) << "Virtual desktop changed to:" << desktop;

    updateActiveLayout();
    Q_EMIT currentDesktopChanged(desktop);
}

void VirtualDesktopManager::onNumberOfDesktopsChanged(int count)
{
    if (m_desktopCount == count) {
        return;
    }

    qCInfo(lcCore) << "Number of virtual desktops changed to:" << count;
    m_desktopCount = count;

    // Refresh names when count changes
    if (m_useKWinDBus) {
        refreshFromKWin();
    }

    // Ensure current desktop is still valid
    if (m_currentDesktop > count) {
        m_currentDesktop = count;
        updateActiveLayout();
    }

    Q_EMIT desktopCountChanged(count);
}

int VirtualDesktopManager::desktopCount() const
{
    if (m_useKWinDBus) {
        return m_desktopCount;
    }

    return 1;
}

QStringList VirtualDesktopManager::desktopNames() const
{
    if (m_useKWinDBus && !m_desktopNames.isEmpty()) {
        return m_desktopNames;
    }

    // Generate defaults
    QStringList names;
    int count = desktopCount();
    for (int i = 1; i <= count; ++i) {
        names.append(QStringLiteral("Desktop %1").arg(i));
    }

    return names;
}

void VirtualDesktopManager::connectSignals()
{
    // KWin D-Bus signals are connected in initKWinDBus()
}

void VirtualDesktopManager::disconnectSignals()
{
    // Note: KWin D-Bus signals stay connected for lifetime
}

void VirtualDesktopManager::updateActiveLayout()
{
    if (!m_layoutManager) {
        return;
    }

    // Get primary screen name
    const auto* screen = qGuiApp->primaryScreen();
    if (!screen) {
        return;
    }

    // Find layout for current screen and desktop
    // Note: We use screen name and current desktop, activity is empty (all activities)
    // ActivityManager handles activity-specific layouts separately
    auto* layout = m_layoutManager->layoutForScreen(screen->name(), m_currentDesktop, QString());

    if (layout && layout != m_layoutManager->activeLayout()) {
        qCDebug(lcCore) << "Switching to layout" << layout->name() << "for desktop" << m_currentDesktop << "on screen"
                        << screen->name();
        m_layoutManager->setActiveLayout(layout);
    }
}

} // namespace PlasmaZones
