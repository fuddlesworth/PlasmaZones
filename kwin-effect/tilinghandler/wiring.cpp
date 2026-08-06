// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// D-Bus signal wiring and initial-settings load for TilingHandler.
// Part of TilingHandler, in its own translation unit: this file holds the
// connect/load bring-up that onDaemonReady drives, while signals.cpp holds the
// slot bodies.

#include "tilinghandler.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "handlers/snaphandler.h"

#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

void TilingHandler::connectSignals()
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    // Disconnect first so daemon restarts don't accumulate duplicate match
    // rules. Qt's QDBusConnection::connect can register the same handler
    // twice if called twice with identical args, which would cause each
    // signal to invoke the slot N times after N daemon restarts.
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("windowsTileRequested"), this,
                   SLOT(slotWindowsTileRequested(PhosphorProtocol::TileRequestList)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("focusWindowRequested"), this,
                   SLOT(slotFocusWindowRequested(QString)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("enabledChanged"), this,
                   SLOT(slotEnabledChanged(bool)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("managedScreensChanged"), this,
                   SLOT(slotScreensChanged(QStringList, bool)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("windowFloatingChanged"), this,
                   SLOT(slotWindowFloatingChanged(QString, bool, QString)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Scrolling, QStringLiteral("scrollingScreensChanged"), this,
                   SLOT(slotScrollingScreensChanged(QStringList)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("activeLayoutsChanged"), this,
                   SLOT(slotActiveLayoutsChanged(QVariantMap)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("windowsTileRequested"), this,
                SLOT(slotWindowsTileRequested(PhosphorProtocol::TileRequestList)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("focusWindowRequested"), this,
                SLOT(slotFocusWindowRequested(QString)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("enabledChanged"), this,
                SLOT(slotEnabledChanged(bool)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("managedScreensChanged"), this,
                SLOT(slotScreensChanged(QStringList, bool)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("windowFloatingChanged"), this,
                SLOT(slotWindowFloatingChanged(QString, bool, QString)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Scrolling, QStringLiteral("scrollingScreensChanged"), this,
                SLOT(slotScrollingScreensChanged(QStringList)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("activeLayoutsChanged"), this,
                SLOT(slotActiveLayoutsChanged(QVariantMap)));

    qCInfo(lcEffect) << "Connected to tiling D-Bus signals";
}

void TilingHandler::loadSettings()
{
    // Query initial engine-managed screen set from daemon asynchronously. The
    // foreign org.freedesktop.DBus.Properties interface is correct for D-Bus
    // property access; ClientHelpers can't be used here because it hard-wires
    // the org.plasmazones interface. Bound by SyncCallTimeoutMs so a wedged
    // daemon doesn't leak a watcher for Qt's default 25 s.
    QDBusMessage msg =
        QDBusMessage::createMethodCall(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << PhosphorProtocol::Service::Interface::Tiling << QStringLiteral("managedScreens");

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
                // A managedScreensChanged signal that landed while this query was
                // in flight carried a NEWER set and already ran the full per-screen
                // transition handling — the raw assignment below would clobber it
                // with the older snapshot.
                if (m_screensSignalGeneration != generationAtDispatch) {
                    qCDebug(lcEffect) << "Managed screens: property reply superseded by a live signal, discarding";
                    completeDeferredWindowRoutes();
                    return;
                }
                QDBusPendingReply<QDBusVariant> reply = *w;
                if (reply.isValid()) {
                    QStringList screens = reply.value().variant().toStringList();
                    // The ENTIRE published set, not a diff. The raw replace
                    // is safe only because removals are covered elsewhere:
                    // onDaemonReady's clears (and, when the daemon exits
                    // outright, the serviceUnregistered teardown) already
                    // reset the per-window tracking before loadSettings
                    // re-queries, so there is never a carried-over screen for
                    // this reply to miss. If those resets ever stop being
                    // load-bearing, this handler must route through
                    // slotScreensChanged's removed/added diff instead.
                    const QSet<QString> published(screens.begin(), screens.end());
                    // Same intersection hazard as slotScreensChanged: this
                    // reply and the scrollingScreens reply race, so whichever
                    // lands second changes the Mode discriminator without
                    // going through setScrollingScreens' invalidation.
                    const QSet<QString> scrollingBefore = scrollingScreenIntersection();
                    m_managedScreens = published;
                    if (scrollingScreenIntersection() != scrollingBefore) {
                        m_effect->invalidateAllRuleCaches();
                        m_effect->scheduleBorderSweep();
                    }
                    qCInfo(lcEffect) << "Loaded managed screens:" << m_managedScreens;
                    const QSet<QString> completedDeferredRoutes = completeDeferredWindowRoutes();

                    if (!published.isEmpty()) {
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
                        // Batch-notify all windows on managed screens in one D-Bus call
                        // instead of per-window windowOpened round-trips.
                        notifyWindowsAddedBatch(batchWindows, published, /*resetNotified=*/true,
                                                /*enteringAutotile=*/false);
                    }
                } else {
                    qCDebug(lcEffect) << "Managed screens: query failed, daemon may not be running";
                    completeDeferredWindowRoutes();
                }
                // Guarded: m_snapHandler is declared after m_tilingHandler
                // and destroyed first during effect teardown.
                if (SnapHandler* snap = m_effect->snapHandler()) {
                    snap->retryVisibleMinimizeFloats();
                }
            });

    // Scrolling screen subset — the Mode-stamp discriminator only, no
    // lifecycle transitions to run, so the reply handling is a guarded
    // plain assignment.
    QDBusMessage scrollMsg =
        QDBusMessage::createMethodCall(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    scrollMsg << PhosphorProtocol::Service::Interface::Scrolling << QStringLiteral("scrollingScreens");
    QDBusPendingCall scrollCall =
        QDBusConnection::sessionBus().asyncCall(scrollMsg, PhosphorProtocol::Service::SyncCallTimeoutMs);
    auto* scrollWatcher = new QDBusPendingCallWatcher(scrollCall, this);
    const quint64 scrollGenerationAtDispatch = m_scrollingScreensGeneration;
    // Per-dispatch guard, the managedScreens fetch's pattern: two loadSettings
    // runs across a daemon restart put two Gets in flight with no authoritative
    // write between them, so the write-generation check alone lets whichever
    // reply lands FIRST win and then discards the newer one.
    const quint64 scrollQueryGeneration = ++m_scrollingScreensQueryGeneration;
    connect(scrollWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, scrollGenerationAtDispatch, scrollQueryGeneration](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                if (scrollQueryGeneration != m_scrollingScreensQueryGeneration) {
                    return; // a newer query superseded this one
                }
                if (m_scrollingScreensGeneration != scrollGenerationAtDispatch) {
                    return; // a live signal carried a newer set
                }
                QDBusPendingReply<QDBusVariant> reply = *w;
                if (reply.isValid()) {
                    const QStringList screens = reply.value().variant().toStringList();
                    // Through the chokepoint: the initial load must run the
                    // same rule-cache invalidate + border sweep as a live
                    // signal, or a Mode "scrolling" rule verdict memoised
                    // before the reply landed would stick.
                    // announceFlipped=false: this is a BRING-UP load, which is
                    // exactly the case the contract reserves it for. The
                    // managed-screens reply owns the re-announce, and letting
                    // this one flip too bumped the per-screen stagger epoch
                    // mid-flight, voiding the tile batch the daemon had already
                    // started delivering and leaving the screen half-tiled.
                    setScrollingScreens(QSet<QString>(screens.cbegin(), screens.cend()),
                                        /*announceFlipped=*/false);
                    qCInfo(lcEffect) << "Loaded scrolling screens:" << m_scrollingScreens;
                } else {
                    // Without this trail, "Mode == scrolling rules never
                    // match" has no diagnostic at all.
                    qCDebug(lcEffect) << "Scrolling screens: query failed, daemon may not be running";
                }
            });

    // Rules-visible active layout map — a pure ruleQuery input like the
    // scrolling subset, so the reply handling is a guarded assignment
    // through the setActiveLayouts chokepoint (which owns the rule-cache
    // invalidate). This closes the bring-up window where a rule verdict was
    // memoised with an empty ActiveLayout stamp before the daemon's first
    // push landed.
    QDBusMessage layoutsMsg =
        QDBusMessage::createMethodCall(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    layoutsMsg << PhosphorProtocol::Service::Interface::Tiling << QStringLiteral("activeLayouts");
    QDBusPendingCall layoutsCall =
        QDBusConnection::sessionBus().asyncCall(layoutsMsg, PhosphorProtocol::Service::SyncCallTimeoutMs);
    auto* layoutsWatcher = new QDBusPendingCallWatcher(layoutsCall, this);
    const quint64 layoutsGenerationAtDispatch = m_activeLayoutsGeneration;
    // Same per-dispatch guard as the scrolling fetch above.
    const quint64 layoutsQueryGeneration = ++m_activeLayoutsQueryGeneration;
    connect(layoutsWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, layoutsGenerationAtDispatch, layoutsQueryGeneration](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                if (layoutsQueryGeneration != m_activeLayoutsQueryGeneration) {
                    return; // a newer query superseded this one
                }
                if (m_activeLayoutsGeneration != layoutsGenerationAtDispatch) {
                    return; // a live signal carried a newer map
                }
                QDBusPendingReply<QDBusVariant> reply = *w;
                if (reply.isValid()) {
                    // a{sv} arrives as a QDBusArgument-wrapped variant;
                    // toMap() on it returns empty — demarshal explicitly.
                    const QVariantMap map = qdbus_cast<QVariantMap>(reply.value().variant());
                    slotActiveLayoutsChanged(map);
                    qCInfo(lcEffect) << "Loaded active layouts for" << map.size() << "screens";
                } else {
                    // Not a debug-level trail: a failed fetch leaves the map
                    // unseeded, so every ActiveLayout rule stays out of the
                    // evaluator until a live signal lands — a rule the user
                    // authored silently does nothing, for the session.
                    qCWarning(lcEffect) << "Active layouts: query failed, daemon may not be running:"
                                        << reply.error().message();
                }
            });
}

} // namespace PlasmaZones
