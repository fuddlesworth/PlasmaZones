// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "controladaptor.h"
#include "windowtrackingadaptor.h"
#include "layoutadaptor.h"
#include "../core/layoutmanager.h"
#include "../core/layout.h"
#include "../core/zone.h"
#include "../core/logging.h"
#include "../core/geometryutils.h"
#include "../core/screenmanager.h"
#include "../core/supportreport.h"
#include "../autotile/AutotileEngine.h"

#include <QDBusConnection>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtConcurrent>

namespace PlasmaZones {

ControlAdaptor::ControlAdaptor(WindowTrackingAdaptor* wta, LayoutAdaptor* layoutAdaptor, LayoutManager* layoutManager,
                               AutotileEngine* autotileEngine, ScreenManager* screenManager, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_wta(wta)
    , m_layoutAdaptor(layoutAdaptor)
    , m_layoutManager(layoutManager)
    , m_autotileEngine(autotileEngine)
    , m_screenManager(screenManager)
{
}

void ControlAdaptor::snapWindowToZone(const QString& windowId, int zoneNumber, const QString& screenId)
{
    if (windowId.isEmpty() || zoneNumber < 1 || zoneNumber > 9) {
        qCWarning(lcDbusWindow) << "snapWindowToZone: invalid args windowId=" << windowId
                                << "zoneNumber=" << zoneNumber;
        return;
    }

    // Resolve zone from screen's current layout
    Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    if (!layout) {
        qCWarning(lcDbusWindow) << "snapWindowToZone: no layout for screen" << screenId;
        return;
    }

    Zone* zone = layout->zoneByNumber(zoneNumber);
    if (!zone) {
        qCWarning(lcDbusWindow) << "snapWindowToZone: zone" << zoneNumber << "not found in layout" << layout->name();
        return;
    }

    // Delegate to WTA's moveWindowToZone convenience method
    if (m_wta) {
        m_wta->moveWindowToZone(windowId, zone->id().toString());
    }
}

void ControlAdaptor::toggleAutotileForScreen(const QString& screenId)
{
    if (screenId.isEmpty()) {
        qCWarning(lcDbusWindow) << "toggleAutotileForScreen: empty screenId";
        return;
    }

    // Determine current mode and toggle
    bool isAutotile = m_autotileEngine && m_autotileEngine->isAutotileScreen(screenId);
    int newMode = isAutotile ? 0 : 1; // 0=Snapping, 1=Autotile

    // Use the LayoutAdaptor's assignment system to toggle mode
    // This is a simplified toggle — uses current desktop/activity context
    qCInfo(lcDbusWindow) << "toggleAutotileForScreen:" << screenId << "from" << (isAutotile ? "autotile" : "snapping")
                         << "to" << (newMode == 1 ? "autotile" : "snapping");

    if (m_layoutAdaptor) {
        // setAssignmentEntry(screenId, desktop=0 (current), activity="" (current), mode, layout, algorithm)
        m_layoutAdaptor->setAssignmentEntry(screenId, 0, QString(), newMode, QString(), QString());
        m_layoutAdaptor->applyAssignmentChanges();
    } else {
        qCWarning(lcDbusWindow) << "toggleAutotileForScreen: LayoutAdaptor not available";
    }
}

QString ControlAdaptor::getFullState()
{
    QJsonObject state;

    // Layouts
    if (m_layoutManager) {
        QJsonArray layoutArray;
        for (Layout* layout : m_layoutManager->layouts()) {
            QJsonObject lo;
            lo[QLatin1String("id")] = layout->id().toString();
            lo[QLatin1String("name")] = layout->name();
            lo[QLatin1String("zoneCount")] = layout->zoneCount();
            layoutArray.append(lo);
        }
        state[QLatin1String("layouts")] = layoutArray;

        Layout* active = m_layoutManager->activeLayout();
        if (active) {
            state[QLatin1String("activeLayoutId")] = active->id().toString();
            state[QLatin1String("activeLayoutName")] = active->name();
        }
    }

    // Window states (via WTA)
    if (m_wta) {
        state[QLatin1String("windows")] = QJsonDocument::fromJson(m_wta->getAllWindowStates().toUtf8()).array();
    }

    // Autotile state
    if (m_autotileEngine) {
        QJsonObject autotile;
        autotile[QLatin1String("enabled")] = m_autotileEngine->isEnabled();
        QJsonArray screens;
        for (const QString& s : m_autotileEngine->autotileScreens()) {
            screens.append(s);
        }
        autotile[QLatin1String("screens")] = screens;
        state[QLatin1String("autotile")] = autotile;
    }

    return QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
}

QString ControlAdaptor::generateSupportReport(int sinceMinutes, const QDBusMessage& message)
{
    qCInfo(lcDbus) << "generateSupportReport: sinceMinutes=" << sinceMinutes;

    // Reject concurrent calls — journalctl is rate-limited and we only need one report at a time.
    if (m_reportWatcher) {
        message.setDelayedReply(true);
        auto error = message.createErrorReply(QStringLiteral("org.plasmazones.Error.Busy"),
                                              QStringLiteral("A support report is already being generated"));
        QDBusConnection::sessionBus().send(error);
        return {};
    }

    // Delay the D-Bus reply so we don't block the event loop while journalctl runs.
    message.setDelayedReply(true);

    // Snapshot QObject state on the main thread (these pointers are not thread-safe).
    auto snapshot = SupportReport::collectSnapshot(m_screenManager, m_layoutManager, m_autotileEngine);

    // Run blocking work (file I/O, journalctl) off the main thread.
    auto* watcher = new QFutureWatcher<QString>(this);
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
    // first to prevent a double-reply race.
    connect(this, &QObject::destroyed, watcher, [message, watcher]() {
        QObject::disconnect(watcher, &QFutureWatcher<QString>::finished, nullptr, nullptr);
        watcher->cancel();
        auto error = message.createErrorReply(QStringLiteral("org.plasmazones.Error.Shutdown"),
                                              QStringLiteral("Daemon shutting down"));
        QDBusConnection::sessionBus().send(error);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([snapshot = std::move(snapshot), sinceMinutes]() {
        return SupportReport::generateFromSnapshot(snapshot, sinceMinutes);
    }));

    return {}; // Ignored — reply sent asynchronously
}

} // namespace PlasmaZones
