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
#include "compositor/effectlogging.h"

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
                   PhosphorProtocol::Service::Interface::Scrolling, QStringLiteral("stripContextChanged"), this,
                   SLOT(slotStripContextChanged(QString, QString, QString)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Scrolling, QStringLiteral("scrollEffectBehaviourChanged"),
                   this, SLOT(slotScrollEffectBehaviourChanged(QVariantMap)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Scrolling,
                   QStringLiteral("scrollFocusScrollBlockedWindowsChanged"), this,
                   SLOT(slotScrollFocusScrollBlockedWindowsChanged(QStringList)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("activeLayoutsChanged"), this,
                   SLOT(slotActiveLayoutsChanged(QVariantMap)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("scrollTabStripsChanged"), this,
                   SLOT(slotScrollTabStripsChanged(QString, QString)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("scrollTabColorsChanged"), this,
                   SLOT(slotScrollTabColorsChanged(QString, QVariantMap)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("scrollTabPaintOverridesChanged"), this,
                   SLOT(slotScrollTabPaintOverridesChanged(QString, QVariantMap)));

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
                PhosphorProtocol::Service::Interface::Scrolling, QStringLiteral("stripContextChanged"), this,
                SLOT(slotStripContextChanged(QString, QString, QString)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Scrolling, QStringLiteral("scrollEffectBehaviourChanged"), this,
                SLOT(slotScrollEffectBehaviourChanged(QVariantMap)));

    // The scroll cap's blocked-window list, on its own signal because it fires
    // on every relayout that moves the answer while the map above fires when
    // settings or rules change.
    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Scrolling,
                QStringLiteral("scrollFocusScrollBlockedWindowsChanged"), this,
                SLOT(slotScrollFocusScrollBlockedWindowsChanged(QStringList)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("activeLayoutsChanged"), this,
                SLOT(slotActiveLayoutsChanged(QVariantMap)));

    // Compositor-drawn tab indicators: the engine's structural payload and
    // the per-window colour verdict broadcast.
    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("scrollTabStripsChanged"), this,
                SLOT(slotScrollTabStripsChanged(QString, QString)));
    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("scrollTabColorsChanged"), this,
                SLOT(slotScrollTabColorsChanged(QString, QVariantMap)));
    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("scrollTabPaintOverridesChanged"), this,
                SLOT(slotScrollTabPaintOverridesChanged(QString, QVariantMap)));

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
                // Compositor teardown: a D-Bus reply can land after KWin::effects
                // is gone (the daemon-loss handler documents the same case), and
                // the stacking-order walk below would deref the null.
                if (!KWin::effects) {
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
                    // drainDeadSessionState's clears (and, when the daemon exits
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
                    // going through setScrollingScreens' invalidation. The
                    // union itself is a discriminator input as well
                    // (keepFloatingAboveDefault reads isManagedScreen), and
                    // this reply races the float re-seed too: a float resolved
                    // before it landed read every screen as snapping. So the
                    // gate keys on any managed-set change, as slotScreensChanged's
                    // does.
                    const QSet<QString> scrollingBefore = scrollingScreenIntersection();
                    const bool managedChanged = m_managedScreens != published;
                    m_managedScreens = published;
                    if (managedChanged || scrollingScreenIntersection() != scrollingBefore) {
                        m_effect->invalidateAllRuleCaches();
                        m_effect->scheduleBorderSweep();
                        // The Mode-flip repaint bookend setScrollingScreens
                        // takes for the same discriminator move: opacity is
                        // resolved in the paint path, so a `Mode Equals
                        // "scrolling"` SetOpacity rule that flips verdict here
                        // leaves an undamaged window at its last-painted alpha.
                        // The border sweep above does not cover it — it rebuilds
                        // decorations, not the per-frame alpha of windows that
                        // have none. KWin::effects is non-null here: the reply
                        // handler returns at its top when the compositor is gone.
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
    m_scrollFocusScrollBlockedFetchRetriesLeft = kBringUpFetchRetryMax;
    m_scrollTabStripsFetchRetriesLeft = kBringUpFetchRetryMax;
    m_scrollTabOverridesFetchRetriesLeft = kBringUpFetchRetryMax;
    fetchScrollingScreens();
    fetchActiveLayouts();
    fetchScrollEffectBehaviour();
    fetchScrollFocusScrollBlockedWindows();
    // Overrides BEFORE strips: both replies travel the same connection in
    // dispatch order, so the first rebuild already layers the overrides.
    fetchScrollTabPaintOverrides();
    fetchScrollTabStrips();
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
                        // Bail if the generation moved while the retry was
                        // armed. Only a newer DISPATCH moves this counter (unlike
                        // the two tab fetches, which voidInFlightScrollTabFetches
                        // also bumps on daemon loss), so the case this catches is a
                        // retry armed before a daemon restart: the new session's
                        // loadSettings dispatches its own fetch, and without this
                        // the stale retry would land afterwards and discard that
                        // fresh reply in favour of its own.
                        QTimer::singleShot(kBringUpFetchRetryDelayMs, this, [this, scrollQueryGeneration] {
                            if (scrollQueryGeneration == m_scrollingScreensQueryGeneration) {
                                fetchScrollingScreens();
                            }
                        });
                    }
                }
            });
}

// The three scrolling behaviours the compositor owns, published by the daemon
// as ALREADY-RESOLVED screen-id lists (rule ?? config decided daemon-side):
// focus-follows-mouse, straddler crop, and the vertical-strip axis. The
// scroll cap's blocked-window list is fetched separately, below.
// Bring-up fetch with the same bounded retry as the scrolling-screens query
// above; a failed Get leaves all three sets empty, which reads as "off
// everywhere" — the historical behaviour before any of them was per-screen,
// and the safe direction at bring-up: focus-follows-mouse stays quiet, no
// column is cropped, and every strip runs horizontally until the daemon
// answers. (An empty axis set is only safe HERE, before any strip has been
// laid out. Once a session is running, a malformed axis value keeps the
// current membership instead — see applyScrollEffectBehaviour.)
void TilingHandler::fetchScrollEffectBehaviour()
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << PhosphorProtocol::Service::Interface::Scrolling << QStringLiteral("scrollEffectBehaviour");
    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg, PhosphorProtocol::Service::SyncCallTimeoutMs);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    // Both guards the scrolling-screens fetch carries. The write generation
    // voids this reply when a live scrollEffectBehaviourChanged signal
    // applied between dispatch and landing — the daemon's signal writes
    // locally through applyScrollEffectBehaviour, so "no local writer" never
    // held here, and without the check a stale reply reverted the live axis
    // membership for the rest of the session. The query generation handles
    // the write-free race (two loadSettings runs across a daemon restart).
    const quint64 generationAtDispatch = m_scrollEffectBehaviourGeneration;
    const quint64 queryGeneration = ++m_scrollEffectBehaviourQueryGeneration;
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generationAtDispatch, queryGeneration](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                if (queryGeneration != m_scrollEffectBehaviourQueryGeneration) {
                    return; // a newer query superseded this one
                }
                if (m_scrollEffectBehaviourGeneration != generationAtDispatch) {
                    return; // a live signal carried a newer map
                }
                QDBusPendingReply<QDBusVariant> reply = *w;
                if (reply.isValid()) {
                    // a{sv} arrives as a QDBusArgument-wrapped variant; toMap() on it
                    // returns EMPTY — demarshal explicitly, exactly as fetchActiveLayouts
                    // does. reply.isValid() is true either way, so the silent-empty
                    // failure had no retry and logged two empty sets as a success:
                    // per-screen focus-follows-mouse and straddler cropping never
                    // populated at bring-up.
                    applyScrollEffectBehaviour(qdbus_cast<QVariantMap>(reply.value().variant()));
                    // The axis set is logged too: a wrong crop or focus-follows-mouse
                    // membership at least announces itself as a behaviour that does
                    // not happen, but a wrong axis is silent on screen — the strip
                    // just shears along the wrong component for the length of every
                    // leg — so bring-up is the one place it can be confirmed.
                    qCInfo(lcEffect) << "Loaded scrolling effect behaviour: ffm=" << m_scrollFocusFollowsMouseScreens
                                     << "crop=" << m_scrollCropStraddlerScreens
                                     << "verticalAxis=" << m_scrollVerticalAxisScreens;
                } else {
                    qCDebug(lcEffect) << "Scrolling effect behaviour: query failed, daemon may not be running";
                    if (m_scrollEffectBehaviourFetchRetriesLeft > 0) {
                        --m_scrollEffectBehaviourFetchRetriesLeft;
                        // Bail if the generation moved while the retry was
                        // armed. Only a newer DISPATCH moves this counter (unlike
                        // the two tab fetches, which voidInFlightScrollTabFetches
                        // also bumps on daemon loss), so the case this catches is a
                        // retry armed before a daemon restart: the new session's
                        // loadSettings dispatches its own fetch, and without this
                        // the stale retry would land afterwards and discard that
                        // fresh reply in favour of its own.
                        QTimer::singleShot(kBringUpFetchRetryDelayMs, this, [this, queryGeneration] {
                            if (queryGeneration == m_scrollEffectBehaviourQueryGeneration) {
                                fetchScrollEffectBehaviour();
                            }
                        });
                    }
                }
            });
}

// The focus-follows-mouse scroll cap's blocked-window list. Its own property
// beside the behaviour map, because the daemon re-derives it on every relayout
// of a capped screen while that map answers settings and rules — carrying both
// on one property made a strip that merely scrolled re-publish three screen
// lists that had not moved, and this side re-parse them to find out.
//
// A failed Get leaves the set empty, which reads as "refuse nothing": focus
// follows the pointer the way it did before the cap existed. That is the safe
// direction for the same reason the parse fails open — the caller turns
// membership into a REFUSED focus, so a wire hiccup that refused everything
// would look like focus-follows-mouse being broken rather than uncapped.
void TilingHandler::fetchScrollFocusScrollBlockedWindows()
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << PhosphorProtocol::Service::Interface::Scrolling << QStringLiteral("scrollFocusScrollBlockedWindows");
    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg, PhosphorProtocol::Service::SyncCallTimeoutMs);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    // Both guards its sibling carries, for the same two races: the write
    // generation voids this reply when a live signal applied between dispatch
    // and landing, and the query generation handles two loadSettings runs
    // across a daemon restart.
    const quint64 generationAtDispatch = m_scrollFocusScrollBlockedGeneration;
    const quint64 queryGeneration = ++m_scrollFocusScrollBlockedQueryGeneration;
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generationAtDispatch, queryGeneration](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                if (queryGeneration != m_scrollFocusScrollBlockedQueryGeneration
                    || generationAtDispatch != m_scrollFocusScrollBlockedGeneration) {
                    return;
                }
                QDBusPendingReply<QDBusVariant> reply = *w;
                if (reply.isValid()) {
                    applyScrollFocusScrollBlockedWindows(reply.value().variant().toStringList());
                    // Logged on its OWN reply rather than beside the behaviour
                    // map's: the two are separate Gets now, and the map's reply
                    // lands first, so printing this there would have described
                    // a set that reply did not load.
                    qCInfo(lcEffect) << "Loaded focus scroll blocks:" << m_scrollFocusScrollBlockedWindows;
                } else {
                    qCDebug(lcEffect) << "Focus scroll blocks: query failed, daemon may not be running";
                    if (m_scrollFocusScrollBlockedFetchRetriesLeft > 0) {
                        --m_scrollFocusScrollBlockedFetchRetriesLeft;
                        // Bail if the generation moved while the retry was
                        // armed. Only a newer DISPATCH moves this counter, so
                        // the case this catches is a retry armed before a
                        // daemon restart: the new session's loadSettings
                        // dispatches its own fetch, and without this the stale
                        // retry would land afterwards and discard that fresh
                        // reply in favour of its own.
                        QTimer::singleShot(kBringUpFetchRetryDelayMs, this, [this, queryGeneration] {
                            if (queryGeneration == m_scrollFocusScrollBlockedQueryGeneration) {
                                fetchScrollFocusScrollBlockedWindows();
                            }
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
                        // Bail if the generation moved while the retry was
                        // armed. Only a newer DISPATCH moves this counter (unlike
                        // the two tab fetches, which voidInFlightScrollTabFetches
                        // also bumps on daemon loss), so the case this catches is a
                        // retry armed before a daemon restart: the new session's
                        // loadSettings dispatches its own fetch, and without this
                        // the stale retry would land afterwards and discard that
                        // fresh reply in favour of its own.
                        QTimer::singleShot(kBringUpFetchRetryDelayMs, this, [this, layoutsQueryGeneration] {
                            if (layoutsQueryGeneration == m_activeLayoutsQueryGeneration) {
                                fetchActiveLayouts();
                            }
                        });
                    }
                }
            });
}

// Bring-up replay of the per-screen context-rule paint overrides. Fetched
// BEFORE the strips so the first rebuild already layers them; the slot is
// last-writer per screen, so a live signal landing mid-flight simply wins.
//
// Both tab fetches carry the same two guards as their three siblings above.
// The per-dispatch GENERATION guard is not optional here even though the slot
// is per-key: across a daemon restart two loadSettings runs put two Gets in
// flight, and a late reply from the DEAD session would re-install a payload
// for a screen the new daemon never names — and never clears, because the
// engine's "[]" retraction is latched on its own membership set, which a fresh
// session starts empty. Only the newest dispatch may apply. The bounded RETRY
// covers the post-daemonReady Get failure that would otherwise leave every
// pill blank until the next relayout (which, for a screen whose strip does
// not change, never comes).
void TilingHandler::fetchScrollTabPaintOverrides()
{
    const quint64 generation = ++m_scrollTabOverridesQueryGeneration;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("scrollTabPaintOverrides"));
    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg, PhosphorProtocol::Service::SyncCallTimeoutMs);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (generation != m_scrollTabOverridesQueryGeneration) {
            return; // superseded by a newer dispatch
        }
        QDBusPendingReply<QVariantMap> reply = *w;
        if (!reply.isValid()) {
            if (m_scrollTabOverridesFetchRetriesLeft > 0) {
                --m_scrollTabOverridesFetchRetriesLeft;
                // Bail if the generation moved while the retry was armed: a
                // daemon loss voids in-flight fetches by bumping it, and a
                // retry into the dead service would only re-bump and warn.
                QTimer::singleShot(kBringUpFetchRetryDelayMs, this, [this, generation] {
                    if (generation == m_scrollTabOverridesQueryGeneration) {
                        fetchScrollTabPaintOverrides();
                    }
                });
                return;
            }
            qCWarning(lcEffect) << "scrollTabPaintOverrides: query failed, daemon may not be running:"
                                << reply.error().message();
            return;
        }
        const QVariantMap byScreen = reply.value();
        for (auto it = byScreen.constBegin(); it != byScreen.constEnd(); ++it) {
            // A nested a{sv} arrives QDBusArgument-wrapped; toMap() on it is
            // empty, so demarshal explicitly (same trap fetchScrollEffectBehaviour
            // documents).
            slotScrollTabPaintOverridesChanged(it.key(), qdbus_cast<QVariantMap>(it.value()));
        }
    });
}

// Bring-up replay of the compositor-drawn tab indicators. The engine's
// scrollTabStripsChanged is change-gated and the payloads it emitted before
// this effect instance existed are gone, so a freshly loaded effect (login,
// KCM toggle, crash recovery) pulls the daemon's per-screen cache once.
// Routed through the ordinary slot so the two paths cannot diverge. A live
// signal that lands while this is in flight simply wins: the slot is
// last-writer per screen and the cache the method answers from is the same
// one the signal updates, so the reply can only be equal or older per key —
// and an older payload for one screen is overwritten by that screen's next
// relayout. The generation guard and retry are explained on the overrides
// fetch above; the strips need them for the same reasons.
void TilingHandler::fetchScrollTabStrips()
{
    const quint64 generation = ++m_scrollTabStripsQueryGeneration;
    QDBusMessage msg =
        QDBusMessage::createMethodCall(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                       PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("scrollTabStrips"));
    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg, PhosphorProtocol::Service::SyncCallTimeoutMs);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (generation != m_scrollTabStripsQueryGeneration) {
            return; // superseded by a newer dispatch
        }
        QDBusPendingReply<QVariantMap> reply = *w;
        if (!reply.isValid()) {
            if (m_scrollTabStripsFetchRetriesLeft > 0) {
                --m_scrollTabStripsFetchRetriesLeft;
                // Same generation bail as the overrides retry above.
                QTimer::singleShot(kBringUpFetchRetryDelayMs, this, [this, generation] {
                    if (generation == m_scrollTabStripsQueryGeneration) {
                        fetchScrollTabStrips();
                    }
                });
                return;
            }
            qCWarning(lcEffect) << "scrollTabStrips: query failed, daemon may not be running:"
                                << reply.error().message();
            return;
        }
        const QVariantMap strips = reply.value();
        for (auto it = strips.constBegin(); it != strips.constEnd(); ++it) {
            slotScrollTabStripsChanged(it.key(), it.value().toString());
        }
    });
}

} // namespace PlasmaZones
