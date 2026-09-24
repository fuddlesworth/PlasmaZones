// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorCompositor/DaemonClient.h>
#include <PhosphorProtocol/BridgeMarshalling.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/DragMarshalling.h>
#include <PhosphorProtocol/Registration.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <PhosphorProtocol/ZoneMarshalling.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QLoggingCategory>

namespace PhosphorCompositor {

Q_LOGGING_CATEGORY(lcDaemonClient, "phosphor.compositor.daemonclient", QtWarningMsg)

DaemonClient::DaemonClient(QObject* parent)
    : QObject(parent)
{
    // The struct-typed subscriptions below (WindowGeometryList,
    // EmptyZoneList, DragPolicy) demarshal silently to garbage unless the
    // wire types are registered in this process. Idempotent, so a host
    // that already registered pays nothing.
    PhosphorProtocol::registerWireTypes();

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
    // The ctor's daemonReady subscription is not part of the per-session
    // connect/disconnect pair (it must survive re-registration), so drop it
    // here explicitly — Qt would tear it down with the receiver anyway, but
    // the symmetry keeps every bus connection visibly paired.
    QDBusConnection::sessionBus().disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                             PhosphorProtocol::Service::Interface::LayoutRegistry,
                                             QStringLiteral("daemonReady"), this, SLOT(onDaemonReadySignal()));
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
    // The version rides as a STRING: the adaptor's parameter is `s`, so an int
    // here does not merely mis-type — the call fails to dispatch at all and
    // registration never completes, which is what made the under-versioned
    // daemon guard below unreachable.
    msg << compositorId << QString::number(apiVersion) << capabilities;

    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        m_registrationInFlight = false;

        if (w->isError()) {
            Q_EMIT bridgeRejected(w->error().message());
            return;
        }

        // ONE struct out-arg, not two scalars. The adaptor returns
        // BridgeRegistrationResult (apiVersion, bridgeName, sessionId), so
        // reading a (QString, int) pair took the VERSION string as the session
        // id and never saw the version at all.
        QDBusPendingReply<PhosphorProtocol::BridgeRegistrationResult> reply = *w;
        if (!reply.isValid()) {
            Q_EMIT bridgeRejected(QStringLiteral("Invalid reply"));
            return;
        }

        const PhosphorProtocol::BridgeRegistrationResult result = reply.value();
        m_sessionId = result.sessionId;
        const int peerVersion = result.apiVersion.toInt();

        if (m_sessionId == QLatin1String("REJECTED")) {
            // No session was established: a later reader must not see the
            // wire sentinel as a live session id.
            m_sessionId.clear();
            Q_EMIT bridgeRejected(QStringLiteral("Daemon rejected registration"));
            return;
        }

        // Reject an under-versioned DAEMON symmetrically: the daemon's gate
        // covers an old client, but a client newer than the daemon would
        // otherwise register and then hear nothing on the renamed v5
        // lifecycle surface — the exact silent failure the version bump
        // exists to prevent.
        if (peerVersion < PhosphorProtocol::Service::MinPeerApiVersion) {
            m_sessionId.clear(); // rejected: no live session id to expose
            Q_EMIT bridgeRejected(QStringLiteral("Daemon API version %1 is older than the minimum supported %2")
                                      .arg(peerVersion)
                                      .arg(PhosphorProtocol::Service::MinPeerApiVersion));
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
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::Tiling,
                                                QStringLiteral("windowOpened"),
                                                {windowId, screenId, minWidth, minHeight});
}

void DaemonClient::notifyWindowOpenedBatch(const PhosphorProtocol::WindowOpenedList& windows)
{
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::Tiling,
                                                QStringLiteral("windowsOpenedBatch"), {QVariant::fromValue(windows)});
}

void DaemonClient::notifyWindowClosed(const QString& windowId)
{
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::Tiling,
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

void DaemonClient::beginDrag(const QString& windowId, const QRect& frameGeometry, const QString& startScreenId,
                             int mouseButtons)
{
    auto* watcher =
        new QDBusPendingCallWatcher(PhosphorProtocol::ClientHelpers::asyncCall(
                                        PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("beginDrag"),
                                        {windowId, frameGeometry.x(), frameGeometry.y(), frameGeometry.width(),
                                         frameGeometry.height(), startScreenId, mouseButtons}),
                                    this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (w->isError()) {
            qCWarning(lcDaemonClient) << "beginDrag failed for" << windowId << ":" << w->error().message();
            return;
        }
        QDBusPendingReply<PhosphorProtocol::DragPolicy> reply = *w;
        if (!reply.isValid()) {
            return;
        }
        const PhosphorProtocol::DragPolicy policy = reply.value();
        const QString invalid = policy.validationError();
        if (!invalid.isEmpty()) {
            qCWarning(lcDaemonClient) << "beginDrag returned an invalid policy for" << windowId << ":" << invalid;
            return;
        }
        Q_EMIT dragPolicyReceived(windowId, policy);
    });
}

void DaemonClient::updateDragCursor(const QString& windowId, int cursorX, int cursorY, int modifiers, int mouseButtons)
{
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::WindowDrag,
                                                QStringLiteral("updateDragCursor"),
                                                {windowId, cursorX, cursorY, modifiers, mouseButtons});
}

void DaemonClient::endDrag(const QString& windowId, int cursorX, int cursorY, int modifiers, int mouseButtons,
                           bool cancelled)
{
    auto* watcher =
        new QDBusPendingCallWatcher(PhosphorProtocol::ClientHelpers::asyncCall(
                                        PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("endDrag"),
                                        {windowId, cursorX, cursorY, modifiers, mouseButtons, cancelled}),
                                    this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (w->isError()) {
            qCWarning(lcDaemonClient) << "endDrag failed for" << windowId << ":" << w->error().message();
            return;
        }
        QDBusPendingReply<PhosphorProtocol::DragOutcome> reply = *w;
        if (!reply.isValid()) {
            return;
        }
        const PhosphorProtocol::DragOutcome outcome = reply.value();
        const QString invalid = outcome.validationError();
        if (!invalid.isEmpty()) {
            qCWarning(lcDaemonClient) << "endDrag returned an invalid outcome for" << windowId << ":" << invalid;
            return;
        }
        Q_EMIT dragOutcomeReceived(windowId, outcome);
    });
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
    // Daemon process appeared — wait for daemonReady signal before
    // registering. The disconnect-then-reconnect pair below keeps the
    // subscription SINGULAR across repeated service (re)registrations:
    // QDBusConnection::connect stacks duplicate match rules, and a daemon
    // that restarts N times would otherwise deliver daemonReady N times.
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
    // Idempotence guard: registerBridge can succeed more than once without
    // an intervening onServiceUnregistered, and QDBusConnection::connect
    // stacks duplicate match rules — every handler would then fire twice
    // per signal.
    if (m_daemonSignalsConnected) {
        return;
    }
    m_daemonSignalsConnected = true;

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
                SLOT(handleDragPolicyChanged(QString, PhosphorProtocol::DragPolicy)));

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
    m_daemonSignalsConnected = false;

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
                   SLOT(handleDragPolicyChanged(QString, PhosphorProtocol::DragPolicy)));
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

void DaemonClient::handleDragPolicyChanged(const QString& windowId, const PhosphorProtocol::DragPolicy& newPolicy)
{
    if (m_dragHandler) {
        // The IDragHandler API predates the structured policy; forward the
        // routing verdict (the bypass reason) as its integer code.
        m_dragHandler->onDragPolicyChanged(windowId, static_cast<int>(newPolicy.bypassReason));
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

// NOTE: a successful probe reports ready WITHOUT wiring the daemon signal
// subscriptions — only registerBridge's success path calls
// connectDaemonSignals. The probe is a liveness check, not a session
// bring-up; callers that need the signal stream must still register.
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
