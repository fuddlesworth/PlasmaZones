// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorCompositor/DaemonClient.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>

namespace PhosphorCompositor {

using namespace PhosphorProtocol;

DaemonClient::DaemonClient(QObject* parent)
    : QObject(parent)
{
    m_serviceWatcher = new QDBusServiceWatcher(
        Service::Name, QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);

    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceRegistered, this, &DaemonClient::onServiceRegistered);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, &DaemonClient::onServiceUnregistered);

    QDBusConnection::sessionBus().connect(Service::Name, Service::ObjectPath, Service::Interface::LayoutRegistry,
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
        Service::Name, Service::ObjectPath, Service::Interface::CompositorBridge, QStringLiteral("registerBridge"));
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
    QDBusMessage msg = QDBusMessage::createMethodCall(Service::Name, Service::ObjectPath, Service::Interface::Autotile,
                                                      QStringLiteral("windowOpened"));
    msg << windowId << screenId << minWidth << minHeight;
    QDBusConnection::sessionBus().send(msg);
}

void DaemonClient::notifyWindowOpenedBatch(const PhosphorProtocol::WindowOpenedList& windows)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(Service::Name, Service::ObjectPath, Service::Interface::Autotile,
                                                      QStringLiteral("windowsOpenedBatch"));
    msg << QVariant::fromValue(windows);
    QDBusConnection::sessionBus().send(msg);
}

void DaemonClient::notifyWindowClosed(const QString& windowId)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(Service::Name, Service::ObjectPath, Service::Interface::Autotile,
                                                      QStringLiteral("windowClosed"));
    msg << windowId;
    QDBusConnection::sessionBus().send(msg);
}

void DaemonClient::notifyWindowActivated(const QString& windowId, const QString& screenId)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        Service::Name, Service::ObjectPath, Service::Interface::WindowTracking, QStringLiteral("windowActivated"));
    msg << windowId << screenId;
    QDBusConnection::sessionBus().send(msg);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Drag operations (plugin → daemon)
// ═══════════════════════════════════════════════════════════════════════════════

void DaemonClient::dragStarted(const QString& windowId, const QString& screenId, const QRect& geometry)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(Service::Name, Service::ObjectPath,
                                                      Service::Interface::WindowDrag, QStringLiteral("dragStarted"));
    msg << windowId << screenId << geometry.x() << geometry.y() << geometry.width() << geometry.height();
    QDBusConnection::sessionBus().send(msg);
}

void DaemonClient::dragMoved(const QString& windowId, int cursorX, int cursorY)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(Service::Name, Service::ObjectPath,
                                                      Service::Interface::WindowDrag, QStringLiteral("dragMoved"));
    msg << windowId << cursorX << cursorY;
    QDBusConnection::sessionBus().send(msg);
}

void DaemonClient::dragStopped(const QString& windowId, const QString& screenId, const QString& zoneId)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(Service::Name, Service::ObjectPath,
                                                      Service::Interface::WindowDrag, QStringLiteral("dragStopped"));
    msg << windowId << screenId << zoneId;
    QDBusConnection::sessionBus().send(msg);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Screen notifications (plugin → daemon)
// ═══════════════════════════════════════════════════════════════════════════════

void DaemonClient::notifyCursorScreenChanged(const QString& screenId)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        Service::Name, Service::ObjectPath, Service::Interface::WindowTracking, QStringLiteral("cursorScreenChanged"));
    msg << screenId;
    QDBusConnection::sessionBus().send(msg);
}

void DaemonClient::notifyPrimaryScreen(const QString& screenName)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(Service::Name, Service::ObjectPath, Service::Interface::Screen,
                                                      QStringLiteral("setPrimaryScreenFromKWin"));
    msg << screenName;
    QDBusConnection::sessionBus().send(msg);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Queries (async, results via signals)
// ═══════════════════════════════════════════════════════════════════════════════

void DaemonClient::queryFloatingWindows()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        Service::Name, Service::ObjectPath, Service::Interface::WindowTracking, QStringLiteral("getFloatingWindows"));
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
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
    QDBusMessage msg = QDBusMessage::createMethodCall(
        Service::Name, Service::ObjectPath, Service::Interface::WindowTracking, QStringLiteral("getSnappedWindows"));
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
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
    QDBusMessage msg =
        QDBusMessage::createMethodCall(Service::Name, Service::ObjectPath, Service::Interface::WindowTracking,
                                       QStringLiteral("getPendingRestoreGeometries"));
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
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
    QDBusMessage msg = QDBusMessage::createMethodCall(Service::Name, Service::ObjectPath, Service::Interface::Screen,
                                                      QStringLiteral("getVirtualScreens"));
    msg << screenId;
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
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
    QDBusMessage msg = QDBusMessage::createMethodCall(
        Service::Name, Service::ObjectPath, Service::Interface::WindowTracking, QStringLiteral("pruneStaleWindows"));
    msg << liveWindowIds;
    QDBusConnection::sessionBus().send(msg);
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
    QDBusConnection::sessionBus().disconnect(Service::Name, Service::ObjectPath, Service::Interface::LayoutRegistry,
                                             QStringLiteral("daemonReady"), this, SLOT(onDaemonReadySignal()));
    QDBusConnection::sessionBus().connect(Service::Name, Service::ObjectPath, Service::Interface::LayoutRegistry,
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

    bus.connect(Service::Name, Service::ObjectPath, Service::Interface::WindowTracking,
                QStringLiteral("applyGeometryRequested"), this,
                SLOT(handleApplyGeometry(QString, int, int, int, int, QString, QString, bool)));

    bus.connect(Service::Name, Service::ObjectPath, Service::Interface::WindowTracking,
                QStringLiteral("applyGeometriesBatch"), this,
                SLOT(handleApplyGeometriesBatch(PhosphorProtocol::WindowGeometryList, QString)));

    bus.connect(Service::Name, Service::ObjectPath, Service::Interface::WindowTracking,
                QStringLiteral("raiseWindowsRequested"), this, SLOT(handleRaiseWindows(QStringList)));

    bus.connect(Service::Name, Service::ObjectPath, Service::Interface::WindowTracking,
                QStringLiteral("activateWindowRequested"), this, SLOT(handleActivateWindow(QString)));

    bus.connect(Service::Name, Service::ObjectPath, Service::Interface::WindowTracking,
                QStringLiteral("windowFloatingChanged"), this,
                SLOT(handleWindowFloatingChanged(QString, bool, QString)));

    bus.connect(Service::Name, Service::ObjectPath, Service::Interface::WindowTracking,
                QStringLiteral("pendingRestoresAvailable"), this, SIGNAL(pendingRestoresAvailable()));

    bus.connect(Service::Name, Service::ObjectPath, Service::Interface::WindowTracking,
                QStringLiteral("reapplyWindowGeometriesRequested"), this, SIGNAL(reapplyGeometriesRequested()));

    bus.connect(Service::Name, Service::ObjectPath, Service::Interface::WindowDrag, QStringLiteral("dragPolicyChanged"),
                this, SLOT(handleDragPolicyChanged(QString, int)));

    bus.connect(Service::Name, Service::ObjectPath, Service::Interface::WindowDrag, QStringLiteral("snapAssistReady"),
                this, SLOT(handleSnapAssistReady(QString, QString, PhosphorProtocol::EmptyZoneList)));

    bus.connect(Service::Name, Service::ObjectPath, Service::Interface::Settings,
                QStringLiteral("runningWindowsRequested"), this, SIGNAL(runningWindowsRequested()));
}

void DaemonClient::disconnectDaemonSignals()
{
    auto bus = QDBusConnection::sessionBus();
    bus.disconnect(Service::Name, Service::ObjectPath, Service::Interface::WindowTracking,
                   QStringLiteral("applyGeometryRequested"), this,
                   SLOT(handleApplyGeometry(QString, int, int, int, int, QString, QString, bool)));
    bus.disconnect(Service::Name, Service::ObjectPath, Service::Interface::WindowTracking,
                   QStringLiteral("applyGeometriesBatch"), this,
                   SLOT(handleApplyGeometriesBatch(PhosphorProtocol::WindowGeometryList, QString)));
    bus.disconnect(Service::Name, Service::ObjectPath, Service::Interface::WindowTracking,
                   QStringLiteral("raiseWindowsRequested"), this, SLOT(handleRaiseWindows(QStringList)));
    bus.disconnect(Service::Name, Service::ObjectPath, Service::Interface::WindowTracking,
                   QStringLiteral("activateWindowRequested"), this, SLOT(handleActivateWindow(QString)));
    bus.disconnect(Service::Name, Service::ObjectPath, Service::Interface::WindowTracking,
                   QStringLiteral("windowFloatingChanged"), this,
                   SLOT(handleWindowFloatingChanged(QString, bool, QString)));
    bus.disconnect(Service::Name, Service::ObjectPath, Service::Interface::WindowDrag,
                   QStringLiteral("dragPolicyChanged"), this, SLOT(handleDragPolicyChanged(QString, int)));
    bus.disconnect(Service::Name, Service::ObjectPath, Service::Interface::WindowDrag,
                   QStringLiteral("snapAssistReady"), this,
                   SLOT(handleSnapAssistReady(QString, QString, PhosphorProtocol::EmptyZoneList)));
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
    Q_UNUSED(windowId)
    Q_UNUSED(width)
    Q_UNUSED(height)
}

void DaemonClient::handleSnapAssistReady(const QString& windowId, const QString& screenId,
                                         const PhosphorProtocol::EmptyZoneList& zones)
{
    Q_EMIT snapAssistReady(windowId, screenId, zones);
}

} // namespace PhosphorCompositor
