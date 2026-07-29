// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// D-Bus signal wiring and initial-settings load for AutotileHandler.
// Part of AutotileHandler, in its own translation unit: this file holds the
// connect/load bring-up that onDaemonReady drives, while signals.cpp holds the
// slot bodies.

#include "autotilehandler.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "handlers/snaphandler.h"

#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

void AutotileHandler::connectSignals()
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    // Disconnect first so daemon restarts don't accumulate duplicate match
    // rules. Qt's QDBusConnection::connect can register the same handler
    // twice if called twice with identical args, which would cause each
    // signal to invoke the slot N times after N daemon restarts.
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("windowsTileRequested"), this,
                   SLOT(slotWindowsTileRequested(PhosphorProtocol::TileRequestList)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("focusWindowRequested"), this,
                   SLOT(slotFocusWindowRequested(QString)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("enabledChanged"), this,
                   SLOT(slotEnabledChanged(bool)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("autotileScreensChanged"), this,
                   SLOT(slotScreensChanged(QStringList, bool)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("windowFloatingChanged"), this,
                   SLOT(slotWindowFloatingChanged(QString, bool, QString)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("windowsTileRequested"), this,
                SLOT(slotWindowsTileRequested(PhosphorProtocol::TileRequestList)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("focusWindowRequested"), this,
                SLOT(slotFocusWindowRequested(QString)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("enabledChanged"), this,
                SLOT(slotEnabledChanged(bool)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("autotileScreensChanged"), this,
                SLOT(slotScreensChanged(QStringList, bool)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("windowFloatingChanged"), this,
                SLOT(slotWindowFloatingChanged(QString, bool, QString)));

    qCInfo(lcEffect) << "Connected to autotile D-Bus signals";
}

void AutotileHandler::loadSettings()
{
    // Query initial autotile screen set from daemon asynchronously. The
    // foreign org.freedesktop.DBus.Properties interface is correct for D-Bus
    // property access; ClientHelpers can't be used here because it hard-wires
    // the org.plasmazones interface. Bound by SyncCallTimeoutMs so a wedged
    // daemon doesn't leak a watcher for Qt's default 25 s.
    QDBusMessage msg =
        QDBusMessage::createMethodCall(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << PhosphorProtocol::Service::Interface::Autotile << QStringLiteral("autotileScreens");

    m_initialScreenQueryPending = true;
    const quint64 queryGeneration = ++m_screenQueryGeneration;
    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg, PhosphorProtocol::Service::SyncCallTimeoutMs);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    const quint64 generationAtDispatch = m_screensSignalGeneration;
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generationAtDispatch, queryGeneration](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                if (queryGeneration != m_screenQueryGeneration) {
                    return;
                }
                m_initialScreenQueryPending = false;
                // An autotileScreensChanged signal that landed while this query was
                // in flight carried a NEWER set and already ran the full per-screen
                // transition handling — the raw assignment below would clobber it
                // with the older snapshot.
                if (m_screensSignalGeneration != generationAtDispatch) {
                    qCDebug(lcEffect) << "Autotile screens: property reply superseded by a live signal, discarding";
                    completeDeferredWindowRoutes();
                    return;
                }
                QDBusPendingReply<QDBusVariant> reply = *w;
                if (reply.isValid()) {
                    QStringList screens = reply.value().variant().toStringList();
                    const QSet<QString> added(screens.begin(), screens.end());
                    // Wholesale replacement is safe HERE because the previous
                    // session's set was cleared at daemon loss
                    // (clearTiledTracking) — there is no live removed-screen
                    // delta to tear down at bringup; runtime screen-set
                    // changes go through slotScreensChanged, which handles
                    // removals.
                    m_autotileScreens = added;
                    qCInfo(lcEffect) << "Loaded autotile screens:" << m_autotileScreens;
                    const QSet<QString> completedDeferredRoutes = completeDeferredWindowRoutes();

                    if (!added.isEmpty()) {
                        const auto windows = KWin::effects->stackingOrder();
                        QList<KWin::EffectWindow*> batchWindows;
                        batchWindows.reserve(windows.size());
                        for (KWin::EffectWindow* window : windows) {
                            // isDeleted: close-grabbed dying windows linger in
                            // the stacking order — getWindowId on them would
                            // re-pollute the scrubbed id caches before the
                            // batch's own guards run.
                            if (window && !window->isDeleted()
                                && !completedDeferredRoutes.contains(m_effect->getWindowId(window))) {
                                batchWindows.append(window);
                            }
                        }
                        // Batch-notify all windows on autotile screens in one D-Bus call
                        // instead of per-window windowOpened round-trips.
                        notifyWindowsAddedBatch(batchWindows, added, /*resetNotified=*/true,
                                                /*enteringAutotile=*/false);
                    }
                } else {
                    qCDebug(lcEffect) << "Autotile screens: query failed, daemon may not be running";
                    completeDeferredWindowRoutes();
                }
                // Guarded: m_snapHandler is declared after m_autotileHandler
                // and destroyed first during effect teardown.
                if (SnapHandler* snap = m_effect->snapHandler()) {
                    snap->retryVisibleMinimizeFloats();
                }
            });
}

} // namespace PlasmaZones
