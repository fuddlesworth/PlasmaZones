// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "controladaptor.h"
#include "dbus/snapadaptor/snapadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "compositorbridgeadaptor.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "core/platform/logging.h"
#include "core/utils/geometryutils.h"
#include <PhosphorScreens/Manager.h>
#include "core/platform/supportreport.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtConcurrent>

namespace PlasmaZones {

ControlAdaptor::ControlAdaptor(WindowTrackingAdaptor* wta, SnapAdaptor* snapAdaptor,
                               PhosphorZones::LayoutRegistry* layoutManager,
                               PhosphorEngine::IPlacementEngine* autotileEngine,
                               PhosphorScreens::ScreenManager* screenManager, CompositorBridgeAdaptor* compositorBridge,
                               PhosphorEngine::IPlacementEngine* scrollEngine, const ScreenModeRouter* modeRouter,
                               QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_wta(wta)
    , m_snapAdaptor(snapAdaptor)
    , m_layoutManager(layoutManager)
    , m_autotileEngine(autotileEngine)
    , m_screenManager(screenManager)
    , m_compositorBridge(compositorBridge)
    , m_scrollEngine(scrollEngine)
    , m_modeRouter(modeRouter)
{
}

void ControlAdaptor::snapWindowToZone(const QString& windowId, int zoneNumber, const QString& screenId)
{
    // Any zone the layout has (F85); a missing number is reported as not found.
    if (windowId.isEmpty() || zoneNumber < 1) {
        qCWarning(lcDbusWindow) << "snapWindowToZone: invalid args windowId=" << windowId
                                << "zoneNumber=" << zoneNumber;
        return;
    }
    // The zone is resolved in the layout of the screen the call names, a
    // screen id or a connector name (a split monitor's connector resolves to
    // the virtual screen the window is in), or of the window's own screen when
    // it names none (F22, F26), and snapped there through the bus gate.
    if (m_snapAdaptor) {
        m_snapAdaptor->moveWindowToZoneNumberOnScreen(windowId, zoneNumber, screenId);
    }
}

void ControlAdaptor::toggleAutotileForScreen(const QString& screenId)
{
    if (screenId.isEmpty()) {
        qCWarning(lcDbusWindow) << "toggleAutotileForScreen: empty screenId";
        return;
    }
    // A connector name or a split monitor's id resolves to the virtual screen
    // in question (F22), and the switch itself is the mode-toggle shortcut's
    // own code on that screen: its context, its remembered layout or
    // algorithm, its feature gate and disabled-context notice (L13 Q1).
    const QString resolved = m_wta ? m_wta->resolveBusScreen(screenId) : QString();
    if (resolved.isEmpty()) {
        qCWarning(lcDbusWindow) << "toggleAutotileForScreen: unknown screen" << screenId;
        return;
    }
    if (!m_modeToggle) {
        qCWarning(lcDbusWindow) << "toggleAutotileForScreen: no mode toggle wired";
        return;
    }
    qCInfo(lcDbusWindow) << "toggleAutotileForScreen:" << screenId << "->" << resolved;
    m_modeToggle(resolved);
}

void ControlAdaptor::setModeToggleHandler(std::function<void(const QString& screenId)> handler)
{
    m_modeToggle = std::move(handler);
}

QString ControlAdaptor::getFullState()
{
    QJsonObject state;

    // Layouts
    if (m_layoutManager) {
        QJsonArray layoutArray;
        for (PhosphorZones::Layout* layout : m_layoutManager->layouts()) {
            QJsonObject lo;
            lo[QLatin1String("id")] = layout->id().toString();
            lo[QLatin1String("name")] = layout->name();
            lo[QLatin1String("zoneCount")] = layout->zoneCount();
            layoutArray.append(lo);
        }
        state[QLatin1String("layouts")] = layoutArray;

        PhosphorZones::Layout* active = m_layoutManager->activeLayout();
        if (active) {
            state[QLatin1String("activeLayoutId")] = active->id().toString();
            state[QLatin1String("activeLayoutName")] = active->name();
        }
    }

    // Window states (via WTA)
    if (m_wta) {
        QJsonArray windowsArray;
        for (const auto& ws : m_wta->getAllWindowStates()) {
            QJsonObject wsObj;
            wsObj[QLatin1String("windowId")] = ws.windowId;
            wsObj[QLatin1String("zoneId")] = ws.zoneId;
            wsObj[QLatin1String("screenId")] = ws.screenId;
            wsObj[QLatin1String("isFloating")] = ws.isFloating;
            windowsArray.append(wsObj);
        }
        state[QLatin1String("windows")] = windowsArray;
    }

    // Autotile state
    if (m_autotileEngine) {
        QJsonObject autotile;
        autotile[QLatin1String("enabled")] = m_autotileEngine->isEnabled();
        QJsonArray screens;
        for (const QString& s : m_autotileEngine->activeScreens()) {
            screens.append(s);
        }
        autotile[QLatin1String("screens")] = screens;
        state[QLatin1String("autotile")] = autotile;
    }

    return QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
}

void ControlAdaptor::detach()
{
    m_wta = nullptr;
    m_snapAdaptor = nullptr;
    m_layoutManager = nullptr;
    m_autotileEngine = nullptr;
    m_screenManager = nullptr;
    m_compositorBridge = nullptr;
    m_scrollEngine = nullptr;
    m_modeRouter = nullptr;
    m_shortcutCatalog = nullptr;
    m_modeToggle = nullptr;
}

QString ControlAdaptor::generateSupportReport(int sinceMinutes, const QDBusMessage& message)
{
    qCInfo(lcDbus) << "generateSupportReport: sinceMinutes=" << sinceMinutes;

    // Reject concurrent calls with a proper D-Bus error so callers get a typed failure
    // instead of having to parse the report string for "ERROR:".
    if (m_reportWatcher) {
        message.setDelayedReply(true);
        auto error = message.createErrorReply(QString(PhosphorProtocol::Service::Error::Busy),
                                              QStringLiteral("A support report is already being generated"));
        QDBusConnection::sessionBus().send(error);
        return {};
    }

    // Detach was called (shutdown in progress) — fail the report cleanly
    // instead of feeding null pointers into collectSnapshot.
    if (!m_screenManager || !m_layoutManager || !m_autotileEngine) {
        message.setDelayedReply(true);
        auto error = message.createErrorReply(QString(PhosphorProtocol::Service::Error::Shutdown),
                                              QStringLiteral("Daemon shutting down"));
        QDBusConnection::sessionBus().send(error);
        return {};
    }

    // Delay the D-Bus reply so we don't block the event loop while journalctl runs.
    message.setDelayedReply(true);

    // Snapshot QObject state on the main thread (these pointers are not thread-safe).
    auto snapshot = SupportReport::collectSnapshot(m_screenManager, m_layoutManager, m_autotileEngine, m_scrollEngine,
                                                   m_modeRouter);

    // Compositor bridge state lives in the dbus layer, so SupportReport (core/)
    // cannot read it directly — fold it into the snapshot here. Whether the
    // KWin effect has registered is the single most useful diagnostic for the
    // "dragging and shortcuts do nothing" class of bug.
    if (m_compositorBridge) {
        snapshot.hasBridgeInfo = true;
        snapshot.bridgeRegistered = m_compositorBridge->isBridgeRegistered();
        snapshot.bridgeName = m_compositorBridge->bridgeName();
        snapshot.bridgeVersion = m_compositorBridge->bridgeVersion();
        snapshot.bridgeCapabilities = m_compositorBridge->bridgeCapabilities();
    }

    // Run blocking work (file I/O, journalctl) off the main thread.
    // No parent — lifetime managed explicitly by the two signal handlers below.
    // Parenting to `this` would cause Qt to auto-delete the watcher during ~QObject,
    // racing with our destroyed handler's deleteLater.
    auto* watcher = new QFutureWatcher<QString>();
    m_reportWatcher = watcher;
    // Use QPointer to detect adaptor destruction inside the finished handler,
    // preventing writes to dangling `this` if the adaptor is destroyed while
    // the future is still running but finishes after destruction starts.
    QPointer<ControlAdaptor> guard(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [guard, message, watcher]() {
        if (guard) {
            QDBusConnection::sessionBus().send(message.createReply(watcher->result()));
            guard->m_reportWatcher = nullptr;
        }
        watcher->deleteLater();
    });
    // If the adaptor is destroyed while the future is running, send an error reply
    // so the D-Bus caller doesn't hang until timeout. Disconnect the finished signal
    // first to prevent a double-reply race. Note: cancel() is a no-op on
    // QtConcurrent::run futures but documents the intent.
    // Use QPointer to guard the watcher — if the finished handler already ran and
    // called deleteLater, the event loop may have destroyed the watcher before this
    // destroyed handler fires, so we must check before touching it.
    QPointer<QFutureWatcher<QString>> weakWatcher(watcher);
    connect(this, &QObject::destroyed, watcher, [message, weakWatcher]() {
        if (!weakWatcher)
            return; // Already cleaned up by the finished handler
        QObject::disconnect(weakWatcher, &QFutureWatcher<QString>::finished, nullptr, nullptr);
        weakWatcher->cancel();
        auto error = message.createErrorReply(QString(PhosphorProtocol::Service::Error::Shutdown),
                                              QStringLiteral("Daemon shutting down"));
        QDBusConnection::sessionBus().send(error);
        weakWatcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([snapshot = std::move(snapshot), sinceMinutes]() {
        return SupportReport::generateFromSnapshot(snapshot, sinceMinutes);
    }));

    return {}; // Ignored — reply sent asynchronously
}

void ControlAdaptor::quit()
{
    qCInfo(lcDbus) << "Shutdown requested over D-Bus (org.plasmazones.Control.quit)";
    // Return to main(), which owns the teardown order: Daemon::stop() then the
    // explicit window destroy loop. Doing either from here would run it inside
    // a D-Bus dispatch, with this adaptor's own parent among the objects torn
    // down under it.
    QCoreApplication::quit();
}

} // namespace PlasmaZones
