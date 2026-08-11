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
#include <QTimer>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace {
// Bounded retry for the bring-up property fetches: enough attempts to ride
// out a daemon that is still constructing its adaptors, short enough that a
// genuinely absent daemon stops costing round trips. Budgets reset per
// loadSettings run; the live-signal path needs no retry.
constexpr int kBringUpFetchRetryMax = 3;
constexpr int kBringUpFetchRetryDelayMs = 1000;
} // anonymous namespace

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
                   PhosphorProtocol::Service::Interface::Scrolling, QStringLiteral("scrollEffectBehaviourChanged"),
                   this, SLOT(slotScrollEffectBehaviourChanged(QVariantMap)));
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
                PhosphorProtocol::Service::Interface::Scrolling, QStringLiteral("scrollEffectBehaviourChanged"), this,
                SLOT(slotScrollEffectBehaviourChanged(QVariantMap)));

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
                        // The Mode-flip repaint bookend setScrollingScreens
                        // takes for the same discriminator move: opacity is
                        // resolved in the paint path, so a `Mode Equals
                        // "scrolling"` SetOpacity rule that flips verdict here
                        // leaves an undamaged window at its last-painted alpha.
                        // The border sweep above does not cover it — it rebuilds
                        // decorations, not the per-frame alpha of windows that
                        // have none.
                        if (m_effect->m_shaderManager.hasOpacityRules()) {
                            KWin::effects->addRepaintFull();
                        }
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

    // Bring-up fetches for the two pure ruleQuery inputs. Each grants itself
    // a fresh bounded retry budget per loadSettings run: a post-daemonReady
    // Get failure otherwise leaves Mode stamps wrong or ActiveLayout rules
    // held out until the next live signal or a daemon restart.
    m_scrollingScreensFetchRetriesLeft = kBringUpFetchRetryMax;
    m_activeLayoutsFetchRetriesLeft = kBringUpFetchRetryMax;
    m_scrollEffectBehaviourFetchRetriesLeft = kBringUpFetchRetryMax;
    fetchScrollingScreens();
    fetchActiveLayouts();
    fetchScrollEffectBehaviour();
}

// Scrolling screen subset — the Mode-stamp discriminator only, no
// lifecycle transitions to run, so the reply handling is a guarded
// plain assignment. Dispatched from loadSettings; a failed Get re-dispatches
// itself while the retry budget lasts.
void TilingHandler::fetchScrollingScreens()
{
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
                    // Bounded retry. The two generation guards above already
                    // returned for a superseded query or a live-signal write,
                    // so reaching here means this is still the newest attempt
                    // and nothing has seeded the set.
                    if (m_scrollingScreensFetchRetriesLeft > 0) {
                        --m_scrollingScreensFetchRetriesLeft;
                        QTimer::singleShot(kBringUpFetchRetryDelayMs, this, [this] {
                            fetchScrollingScreens();
                        });
                    }
                }
            });
}

// The two scrolling behaviours the compositor owns, published by the daemon
// as ALREADY-RESOLVED screen-id lists (rule ?? config decided daemon-side).
// Bring-up fetch with the same bounded retry as the scrolling-screens query
// above; a failed Get leaves both sets empty, which reads as "off everywhere"
// — the historical behaviour before either was per-screen, and the safe
// direction: focus-follows-mouse stays quiet and no column is cropped until
// the daemon answers.
void TilingHandler::fetchScrollEffectBehaviour()
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << PhosphorProtocol::Service::Interface::Scrolling << QStringLiteral("scrollEffectBehaviour");
    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg, PhosphorProtocol::Service::SyncCallTimeoutMs);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    // Per-dispatch guard only — unlike the scrolling-screens set there is no
    // authoritative local writer to race, so a write generation would have
    // nothing to compare against. A live signal landing first simply wins,
    // and this reply is discarded when a newer query superseded it.
    const quint64 queryGeneration = ++m_scrollEffectBehaviourQueryGeneration;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, queryGeneration](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (queryGeneration != m_scrollEffectBehaviourQueryGeneration) {
            return; // a newer query superseded this one
        }
        QDBusPendingReply<QDBusVariant> reply = *w;
        if (reply.isValid()) {
            applyScrollEffectBehaviour(reply.value().variant().toMap());
            qCInfo(lcEffect) << "Loaded scrolling effect behaviour: ffm=" << m_scrollFocusFollowsMouseScreens
                             << "crop=" << m_scrollCropStraddlerScreens;
        } else {
            qCDebug(lcEffect) << "Scrolling effect behaviour: query failed, daemon may not be running";
            if (m_scrollEffectBehaviourFetchRetriesLeft > 0) {
                --m_scrollEffectBehaviourFetchRetriesLeft;
                QTimer::singleShot(kBringUpFetchRetryDelayMs, this, [this] {
                    fetchScrollEffectBehaviour();
                });
            }
        }
    });
}

// Rules-visible active layout map — a pure ruleQuery input like the
// scrolling subset, so the reply handling is a guarded assignment
// through the setActiveLayouts chokepoint (which owns the rule-cache
// invalidate). This closes the bring-up window where a rule verdict was
// memoised with an empty ActiveLayout stamp before the daemon's first
// push landed. Dispatched from loadSettings; a failed Get re-dispatches
// itself while the retry budget lasts.
void TilingHandler::fetchActiveLayouts()
{
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
                    // evaluator until something seeds it — a rule the user
                    // authored silently does nothing in the meantime.
                    //
                    // Bounded retry below; past the budget, the recovery
                    // paths are the daemon's next live activeLayoutsChanged
                    // (which seeds through the same chokepoint) and a daemon
                    // restart, whose onDaemonReady re-runs this very query.
                    // Seeding on failure is FORBIDDEN — it would admit
                    // ActiveLayout rules against a map that was never
                    // received, and the field resolves to an engaged empty
                    // string, so every negated ActiveLayout leaf would fire
                    // for every window. Held-out rules are the inert failure;
                    // that is the one this path keeps.
                    qCWarning(lcEffect) << "Active layouts: query failed, daemon may not be running:"
                                        << reply.error().message();
                    if (m_activeLayoutsFetchRetriesLeft > 0) {
                        --m_activeLayoutsFetchRetriesLeft;
                        QTimer::singleShot(kBringUpFetchRetryDelayMs, this, [this] {
                            fetchActiveLayouts();
                        });
                    }
                }
            });
}

} // namespace PlasmaZones
