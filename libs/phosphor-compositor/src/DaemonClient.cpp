// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorCompositor/DaemonClient.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <PhosphorProtocol/ZoneMarshalling.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>

namespace PhosphorCompositor {

DaemonClient::DaemonClient(QObject* parent)
    : QObject(parent)
{
    m_serviceWatcher = new QDBusServiceWatcher(
        PhosphorProtocol::Service::Name, QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);

    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceRegistered, this, &DaemonClient::onServiceRegistered);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, &DaemonClient::onServiceUnregistered);

    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::LayoutRegistry,
                                          QStringLiteral("daemonReady"), this, SLOT(onDaemonReadySignal()));
}

DaemonClient::~DaemonClient()
{
    disconnectDaemonSignals();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Registration
// ═══════════════════════════════════════════════════════════════════════════════

void DaemonClient::registerBridge(const QString& compositorId, int apiVersion, const QStringList& capabilities)
{
    if (m_registrationInFlight) {
        return;
    }
    m_registrationInFlight = true;

    QDBusMessage msg = QDBusMessage::createMethodCall(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::CompositorBridge, QStringLiteral("registerBridge"));
    msg << compositorId << apiVersion << capabilities;

    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        m_registrationInFlight = false;

        if (w->isError()) {
            Q_EMIT bridgeRejected(w->error().message());
            return;
        }

        QDBusPendingReply<QString, int> reply = *w;
        if (!reply.isValid()) {
            Q_EMIT bridgeRejected(QStringLiteral("Invalid reply"));
            return;
        }

        m_sessionId = reply.argumentAt<0>();
        int peerVersion = reply.argumentAt<1>();

        if (m_sessionId == QLatin1String("REJECTED")) {
            Q_EMIT bridgeRejected(QStringLiteral("Daemon rejected registration"));
            return;
        }

        m_daemonReady = true;
        connectDaemonSignals();
        Q_EMIT bridgeRegistered(m_sessionId, peerVersion);
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window lifecycle (plugin → daemon)
// ═══════════════════════════════════════════════════════════════════════════════

void DaemonClient::notifyWindowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight)
{
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::Autotile,
                                                QStringLiteral("windowOpened"),
                                                {windowId, screenId, minWidth, minHeight});
}

void DaemonClient::notifyWindowOpenedBatch(const PhosphorProtocol::WindowOpenedList& windows)
{
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::Autotile,
                                                QStringLiteral("windowsOpenedBatch"), {QVariant::fromValue(windows)});
}

void DaemonClient::notifyWindowClosed(const QString& windowId)
{
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::Autotile,
                                                QStringLiteral("windowClosed"), {windowId});
}

void DaemonClient::notifyWindowActivated(const QString& windowId, const QString& screenId)
{
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::WindowTracking,
                                                QStringLiteral("windowActivated"), {windowId, screenId});
}

// ═══════════════════════════════════════════════════════════════════════════════
// Drag operations (plugin → daemon)
// ═══════════════════════════════════════════════════════════════════════════════

void DaemonClient::dragStarted(const QString& windowId, const QString& screenId, const QRect& geometry)
{
    PhosphorProtocol::ClientHelpers::sendOneWay(
        PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("dragStarted"),
        {windowId, screenId, geometry.x(), geometry.y(), geometry.width(), geometry.height()});
}

void DaemonClient::dragMoved(const QString& windowId, int cursorX, int cursorY)
{
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::WindowDrag,
                                                QStringLiteral("dragMoved"), {windowId, cursorX, cursorY});
}

void DaemonClient::dragStopped(const QString& windowId, const QString& screenId, const QString& zoneId)
{
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::WindowDrag,
                                                QStringLiteral("dragStopped"), {windowId, screenId, zoneId});
}

// ═══════════════════════════════════════════════════════════════════════════════
// Screen notifications (plugin → daemon)
// ═══════════════════════════════════════════════════════════════════════════════

void DaemonClient::notifyCursorScreenChanged(const QString& screenId)
{
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::WindowTracking,
                                                QStringLiteral("cursorScreenChanged"), {screenId});
}

void DaemonClient::notifyPrimaryScreen(const QString& screenName)
{
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::Screen,
                                                QStringLiteral("setPrimaryScreenFromKWin"), {screenName});
}

// ═══════════════════════════════════════════════════════════════════════════════
// Queries (async, results via signals)
// ═══════════════════════════════════════════════════════════════════════════════

void DaemonClient::queryFloatingWindows()
{
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("getFloatingWindows")),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (w->isError())
            return;
        QDBusPendingReply<QStringList> reply = *w;
        if (reply.isValid()) {
            Q_EMIT floatingWindowsReceived(reply.value());
        }
    });
}

void DaemonClient::querySnappedWindows()
{
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("getSnappedWindows")),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (w->isError())
            return;
        QDBusPendingReply<QStringList> reply = *w;
        if (reply.isValid()) {
            Q_EMIT snappedWindowsReceived(reply.value());
        }
    });
}

void DaemonClient::queryPendingRestoreGeometries()
{
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("getPendingRestoreGeometries")),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (w->isError())
            return;
        QDBusPendingReply<QString> reply = *w;
        if (reply.isValid()) {
            Q_EMIT pendingRestoreGeometriesReceived(reply.value());
        }
    });
}

void DaemonClient::queryVirtualScreens(const QString& screenId)
{
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::Screen,
                                                   QStringLiteral("getVirtualScreens"), {screenId}),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, screenId](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (w->isError())
            return;
        QDBusPendingReply<PhosphorProtocol::WindowGeometryList> reply = *w;
        if (reply.isValid()) {
            Q_EMIT virtualScreensReceived(screenId, reply.value());
        }
    });
}

void DaemonClient::pruneStaleWindows(const QStringList& liveWindowIds)
{
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::WindowTracking,
                                                QStringLiteral("pruneStaleWindows"), {liveWindowIds});
}

// ═══════════════════════════════════════════════════════════════════════════════
// Service lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

void DaemonClient::onDaemonReadySignal()
{
    Q_EMIT daemonReady();
}

void DaemonClient::onServiceRegistered()
{
    // Daemon process appeared — wait for daemonReady signal before registering
    QDBusConnection::sessionBus().disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                             PhosphorProtocol::Service::Interface::LayoutRegistry,
                                             QStringLiteral("daemonReady"), this, SLOT(onDaemonReadySignal()));
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::LayoutRegistry,
                                          QStringLiteral("daemonReady"), this, SLOT(onDaemonReadySignal()));
}

void DaemonClient::onServiceUnregistered()
{
    m_daemonReady = false;
    m_registrationInFlight = false;
    m_sessionId.clear();
    disconnectDaemonSignals();
    Q_EMIT daemonDisconnected();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Daemon signal wiring
// ═══════════════════════════════════════════════════════════════════════════════

void DaemonClient::connectDaemonSignals()
{
    auto bus = QDBusConnection::sessionBus();

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("applyGeometryRequested"), this,
                SLOT(handleApplyGeometry(QString, int, int, int, int, QString, QString, bool)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("applyGeometriesBatch"), this,
                SLOT(handleApplyGeometriesBatch(PhosphorProtocol::WindowGeometryList, QString)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("raiseWindowsRequested"), this,
                SLOT(handleRaiseWindows(QStringList)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("activateWindowRequested"), this,
                SLOT(handleActivateWindow(QString)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("windowFloatingChanged"), this,
                SLOT(handleWindowFloatingChanged(QString, bool, QString)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("pendingRestoresAvailable"), this,
                SIGNAL(pendingRestoresAvailable()));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::WindowTracking,
                QStringLiteral("reapplyWindowGeometriesRequested"), this, SIGNAL(reapplyGeometriesRequested()));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("dragPolicyChanged"), this,
                SLOT(handleDragPolicyChanged(QString, int)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("snapAssistReady"), this,
                SLOT(handleSnapAssistReady(QString, QString, PhosphorProtocol::EmptyZoneList)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("restoreSizeDuringDragChanged"), this,
                SLOT(handleRestoreSizeDuringDrag(QString, int, int)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::WindowTracking,
                QStringLiteral("moveSpecificWindowToZoneRequested"), this,
                SLOT(handleMoveWindowToZone(QString, QString, int, int, int, int)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("snapAllWindowsRequested"), this,
                SLOT(handleSnapAllWindows(QString)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Settings, QStringLiteral("settingsChanged"), this,
                SIGNAL(settingsChanged()));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Screen, QStringLiteral("virtualScreensChanged"), this,
                SIGNAL(virtualScreensChanged(QString)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Settings, QStringLiteral("runningWindowsRequested"), this,
                SIGNAL(runningWindowsRequested()));
}

void DaemonClient::disconnectDaemonSignals()
{
    auto bus = QDBusConnection::sessionBus();
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("applyGeometryRequested"), this,
                   SLOT(handleApplyGeometry(QString, int, int, int, int, QString, QString, bool)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("applyGeometriesBatch"), this,
                   SLOT(handleApplyGeometriesBatch(PhosphorProtocol::WindowGeometryList, QString)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("raiseWindowsRequested"), this,
                   SLOT(handleRaiseWindows(QStringList)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("activateWindowRequested"),
                   this, SLOT(handleActivateWindow(QString)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("windowFloatingChanged"), this,
                   SLOT(handleWindowFloatingChanged(QString, bool, QString)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("pendingRestoresAvailable"),
                   this, SIGNAL(pendingRestoresAvailable()));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::WindowTracking,
                   QStringLiteral("reapplyWindowGeometriesRequested"), this, SIGNAL(reapplyGeometriesRequested()));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("dragPolicyChanged"), this,
                   SLOT(handleDragPolicyChanged(QString, int)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("snapAssistReady"), this,
                   SLOT(handleSnapAssistReady(QString, QString, PhosphorProtocol::EmptyZoneList)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("restoreSizeDuringDragChanged"),
                   this, SLOT(handleRestoreSizeDuringDrag(QString, int, int)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::WindowTracking,
                   QStringLiteral("moveSpecificWindowToZoneRequested"), this,
                   SLOT(handleMoveWindowToZone(QString, QString, int, int, int, int)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("snapAllWindowsRequested"),
                   this, SLOT(handleSnapAllWindows(QString)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Settings, QStringLiteral("settingsChanged"), this,
                   SIGNAL(settingsChanged()));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Screen, QStringLiteral("virtualScreensChanged"), this,
                   SIGNAL(virtualScreensChanged(QString)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Settings, QStringLiteral("runningWindowsRequested"), this,
                   SIGNAL(runningWindowsRequested()));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Handler dispatch
// ═══════════════════════════════════════════════════════════════════════════════

void DaemonClient::handleApplyGeometry(const QString& windowId, int x, int y, int w, int h, const QString& zoneId,
                                       const QString& screenId, bool sizeOnly)
{
    if (m_geometryHandler) {
        m_geometryHandler->onApplyGeometry({windowId, QRect(x, y, w, h), zoneId, screenId, sizeOnly});
    }
}

void DaemonClient::handleApplyGeometriesBatch(const PhosphorProtocol::WindowGeometryList& geometries,
                                              const QString& action)
{
    if (!m_geometryHandler)
        return;

    QVector<GeometryRequest> requests;
    requests.reserve(geometries.size());
    for (const auto& entry : geometries) {
        requests.append(
            {entry.windowId, QRect(entry.x, entry.y, entry.width, entry.height), QString(), entry.screenId, false});
    }

    BatchAction batchAction = BatchAction::Resnap;
    if (action == QLatin1String("rotate"))
        batchAction = BatchAction::Rotate;
    else if (action == QLatin1String("autotile"))
        batchAction = BatchAction::Autotile;

    m_geometryHandler->onApplyGeometriesBatch(requests, batchAction);
}

void DaemonClient::handleRaiseWindows(const QStringList& windowIds)
{
    if (m_geometryHandler) {
        m_geometryHandler->onRaiseWindows(windowIds);
    }
}

void DaemonClient::handleActivateWindow(const QString& windowId)
{
    if (m_geometryHandler) {
        m_geometryHandler->onActivateWindow(windowId);
    }
}

void DaemonClient::handleDragPolicyChanged(const QString& windowId, int newPolicy)
{
    if (m_dragHandler) {
        m_dragHandler->onDragPolicyChanged(windowId, newPolicy);
    }
}

void DaemonClient::handleWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId)
{
    if (m_lifecycleHandler) {
        m_lifecycleHandler->onWindowFloatingChanged(windowId, isFloating, screenId);
    }
}

void DaemonClient::handleRestoreSizeDuringDrag(const QString& windowId, int width, int height)
{
    if (m_dragHandler) {
        m_dragHandler->onRestoreSizeDuringDrag(windowId, width, height);
    }
}

void DaemonClient::handleMoveWindowToZone(const QString& windowId, const QString& screenId, int x, int y, int w, int h)
{
    if (m_geometryHandler) {
        m_geometryHandler->onMoveWindowToZone(windowId, screenId, x, y, w, h);
    }
}

void DaemonClient::handleSnapAllWindows(const QString& screenId)
{
    if (m_geometryHandler) {
        m_geometryHandler->onSnapAllWindows(screenId);
    }
}

void DaemonClient::handleSnapAssistReady(const QString& windowId, const QString& screenId,
                                         const PhosphorProtocol::EmptyZoneList& zones)
{
    Q_EMIT snapAssistReady(windowId, screenId, zones);
}

void DaemonClient::probeDaemonAvailable(int timeoutMs)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, timeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QString> reply = *w;
        if (reply.isValid() && !m_daemonReady) {
            m_daemonReady = true;
            Q_EMIT daemonReady();
        }
    });
}

} // namespace PhosphorCompositor
