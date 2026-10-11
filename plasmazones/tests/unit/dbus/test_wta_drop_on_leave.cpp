// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_drop_on_leave.cpp
 * @brief WindowTrackingAdaptor::releaseLeftScreens, the one primitive every
 *        cross-screen move runs: what each engine still holds of a window on
 *        a monitor it left is dropped, the arriving engine is left alone, and
 *        a record naming an output that went away stays parked. Plus the
 *        sites that call it on a daemon-driven move (the engine relays and
 *        the cross-mode handoff).
 */

#include "wta_convenience_fixture.h"
#include "helpers/StubPlacementEngine.h"
#include "helpers/WindowPlacementBuilders.h"
#include "helpers/VirtualScreenTestHelpers.h"

#include <PhosphorRules/Rule.h>
#include <QScopeGuard>

/// Two outputs side by side, a local adaptor on a real ScreenManager, a snap
/// engine with per-screen stores, and a bare RouteToScreen rule sending
/// "routeapp" to DP-2 (with a RouteToDesktop when asked). The shared fixture
/// has no ScreenManager, which the route needs.
class RoutedOpenEnv
{
public:
    RoutedOpenEnv(PhosphorZones::LayoutRegistry* layouts, StubZoneDetectorConvenience* detector,
                  StubSettingsConvenience* settings, PhosphorEngine::PlacementEngineBase* tiling, int routeDesktop,
                  PhosphorEngine::PlacementEngineBase* scroll = nullptr)
    {
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080), QStringLiteral("DP-1"));
        fake.addScreen(QStringLiteral("DP-2"), QRect(1920, 0, 1920, 1080), QStringLiteral("DP-2"));
        screenMgr = std::make_unique<PhosphorScreens::ScreenManager>(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr->start();
        wta = new WindowTrackingAdaptor(layouts, detector, screenMgr.get(), settings, nullptr, nullptr, &parent);
        snap = std::make_unique<SnapEngine>(layouts, wta->service(), detector, nullptr, nullptr);
        snap->setEngineSettings(settings);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap.get());
        wta->service()->setSnapStateResolver(PhosphorPlacement::snapStateResolverFor(snap.get()));
        wta->setEngines(snap.get(), tiling, scroll);
        wta->setRoutedOpenDispatcher([this](const PhosphorProtocol::WindowOpenedEntry& entry) {
            dispatched.append(entry);
        });
        registry = new PhosphorEngine::WindowRegistry(&parent);
        wta->setWindowRegistry(registry);
        wta->setWindowMetadata(QStringLiteral("inst1"), QStringLiteral("routeapp"), QString(), QString(), QString(), 0,
                               0, QString(), 0, QVariantMap());
        using namespace PhosphorRules;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("routeapp"));
        RuleAction route;
        route.type = QString(ActionType::RouteToScreen);
        route.params.insert(QString(ActionParam::TargetScreenId), QStringLiteral("DP-2"));
        rule.actions = {route};
        if (routeDesktop >= 1) {
            RuleAction desktop;
            desktop.type = QString(ActionType::RouteToDesktop);
            desktop.params.insert(QString(ActionParam::TargetDesktop), routeDesktop);
            rule.actions.append(desktop);
        }
        store = std::make_unique<RuleStore>(ConfigDefaults::rulesFilePath(), &parent);
        store->addRule(rule);
        wta->setRuleStore(store.get());
        wta->setFrameGeometry(window, 100, 100, 800, 600);
    }
    ~RoutedOpenEnv()
    {
        wta->setRuleStore(nullptr);
        wta->setWindowRegistry(nullptr);
        wta->setEngines(nullptr, nullptr, nullptr);
        wta->service()->setSnapEngine(nullptr);
        wta->service()->setSnapState(nullptr);
    }

    const QString window = QStringLiteral("routeapp|inst1");
    PhosphorScreens::FakePhysicalScreenSource fake;
    std::unique_ptr<PhosphorScreens::ScreenManager> screenMgr;
    QObject parent;
    WindowTrackingAdaptor* wta = nullptr;
    std::unique_ptr<SnapEngine> snap;
    PhosphorEngine::WindowRegistry* registry = nullptr;
    std::unique_ptr<PhosphorRules::RuleStore> store;
    QList<PhosphorProtocol::WindowOpenedEntry> dispatched;
};

class TestWtaDropOnLeave : public QObject, protected WtaConvenienceFixture
{
    Q_OBJECT

private:
    /// @p env's window snapped on DP-1 desktop 1, its placement recorded with
    /// an autotile slot beside the snap one.
    void snapOnDp1WithTileSlot(RoutedOpenEnv& env)
    {
        env.snap->setCurrentDesktopForScreen(QStringLiteral("DP-1"), 1);
        env.snap->setCurrentDesktopForScreen(QStringLiteral("DP-2"), 1);
        env.wta->service()->assignWindowToZone(env.window, m_zoneIds[0], QStringLiteral("DP-1"), 1);
        env.wta->service()->placementStore().record(*env.snap->capturePlacement(env.window));
        env.wta->service()->placementStore().record(PlasmaZones::TestHelpers::makePlacement(
            env.window, QStringLiteral("routeapp"), PhosphorEngine::WindowPlacement::stateTiled(),
            PhosphorEngine::WindowPlacement::autotileEngineId(), QStringLiteral("DP-1")));
    }
    static QString slotState(RoutedOpenEnv& env, const QString& engineId)
    {
        const auto rec = env.wta->service()->placementStore().peekExact(env.window);
        return rec ? rec->slotFor(engineId).state : QString();
    }

    /// The open float terminal's state: floating in snap on DP-1, no zone.
    void floatOnSpawnScreen(RoutedOpenEnv& env)
    {
        env.snap->setCurrentDesktopForScreen(QStringLiteral("DP-1"), 1);
        env.snap->setCurrentDesktopForScreen(QStringLiteral("DP-2"), 1);
        env.wta->service()->assignWindowToZone(env.window, m_zoneIds[0], QStringLiteral("DP-1"), 1);
        env.snap->setWindowFloat(env.window, true, QStringLiteral("DP-1"));
        QVERIFY(env.snap->isFloating(env.window));
        QCOMPARE(env.snap->screenForTrackedWindow(env.window), QStringLiteral("DP-1"));
    }

    /// Snap @p windowId into a zone on desktops 1 and 2 of m_screenId, and
    /// record its placement.
    void snapOnTwoDesktops(const QString& windowId, const QString& screenId)
    {
        m_snapEngine->setCurrentDesktopForScreen(screenId, 1);
        m_wta->service()->assignWindowToZone(windowId, m_zoneIds[0], screenId, 1);
        m_snapEngine->setCurrentDesktopForScreen(screenId, 2);
        m_snapEngine->stateForWindowOnScreen(windowId, screenId, 2)
            ->assignWindowToZone(windowId, m_zoneIds[1], screenId, 2);
        m_snapEngine->setCurrentDesktopForScreen(screenId, 1);
        m_wta->service()->placementStore().record(*m_snapEngine->capturePlacement(windowId));
    }

    QString snapSlotState(const QString& windowId) const
    {
        const auto rec = m_wta->service()->placementStore().peekExact(windowId);
        return rec ? rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).state : QString();
    }

private Q_SLOTS:
    void init()
    {
        initFixture();
        installPerScreenResolver();
    }
    void cleanup()
    {
        m_wta->setEngines(m_snapEngine, nullptr, nullptr);
        m_wta->service()->setSnapState(m_snapEngine->snapState());
        cleanupFixture();
    }

    // Every snap membership on the monitor left goes, on every desktop, with
    // the record's snap slot, and silently: no zone change is announced for
    // memory on a screen the window left. The other tiling engine is asked to
    // let go off the kept screen; the arriving one is not.
    void releaseLeftScreens_dropsOffMonitorSnapMembershipsSilently()
    {
        StubPlacementEngine arriving;
        StubPlacementEngine other;
        m_wta->setEngines(m_snapEngine, &arriving, &other);
        const QString w = QStringLiteral("app|leaves");
        const QString dest = QStringLiteral("DP-2");
        snapOnTwoDesktops(w, m_screenId);
        QCOMPARE(snapSlotState(w), QString(PhosphorEngine::WindowPlacement::stateSnapped()));

        QSignalSpy zoneSpy(m_wta->service(), &PhosphorPlacement::WindowTrackingService::windowZoneChanged);
        m_wta->releaseLeftScreens(w, dest, &arriving);

        QVERIFY2(!m_snapEngine->isWindowTracked(w), "no snap membership may stay on the monitor left");
        QCOMPARE(zoneSpy.count(), 0);
        QCOMPARE(snapSlotState(w), QString(PhosphorEngine::WindowPlacement::stateReleased()));
        QCOMPARE(other.releasedOffScreen.size(), 1);
        QCOMPARE(other.releasedOffScreen.first(), qMakePair(w, dest));
        QVERIFY2(arriving.releasedOffScreen.isEmpty(), "the arriving engine may still be moving the window");
    }

    // Snap arriving on the new screen keeps the slot: the arrival is the
    // placement it now describes. The memberships on the monitor left go all
    // the same.
    void releaseLeftScreens_snapArrivalKeepsTheSlot()
    {
        const QString w = QStringLiteral("app|snap-arrives");
        snapOnTwoDesktops(w, m_screenId);

        m_wta->releaseLeftScreens(w, QStringLiteral("DP-2"), m_snapEngine);

        QVERIFY(!m_snapEngine->isWindowTracked(w));
        QCOMPARE(snapSlotState(w), QString(PhosphorEngine::WindowPlacement::stateSnapped()));
    }

    // A crossing between virtual screens of one monitor is a move like any
    // other: the membership on the virtual screen left goes, and the record
    // forgets the zone too.
    void releaseLeftScreens_virtualScreenCrossingReleasesTheSlot()
    {
        const QString w = QStringLiteral("app|vs-leave");
        const QString vs0 = QStringLiteral("DP-1/vs:0");
        const QString vs1 = QStringLiteral("DP-1/vs:1");
        m_snapEngine->setCurrentDesktopForScreen(vs0, 1);
        m_snapEngine->setCurrentDesktopForScreen(vs1, 1);
        m_wta->service()->assignWindowToZone(w, m_zoneIds[0], vs0, 1);
        m_wta->service()->placementStore().record(*m_snapEngine->capturePlacement(w));

        m_wta->releaseLeftScreens(w, vs1, nullptr);

        QVERIFY(!m_snapEngine->isWindowTracked(w));
        QCOMPARE(snapSlotState(w), QString(PhosphorEngine::WindowPlacement::stateReleased()));
    }

    // Kept on the virtual screen its record names, nothing is left behind.
    void releaseLeftScreens_sameScreenKeepsTheSlot()
    {
        const QString w = QStringLiteral("app|vs-stay");
        const QString vs0 = QStringLiteral("DP-1/vs:0");
        m_snapEngine->setCurrentDesktopForScreen(vs0, 1);
        m_wta->service()->assignWindowToZone(w, m_zoneIds[0], vs0, 1);
        m_wta->service()->placementStore().record(*m_snapEngine->capturePlacement(w));

        m_wta->releaseLeftScreens(w, vs0, nullptr);

        QCOMPARE(snapSlotState(w), QString(PhosphorEngine::WindowPlacement::stateSnapped()));
    }

    // A float bit dropped by the release is closed out on the float channel
    // once, where it was broadcast: a second release has nothing to say.
    void releaseLeftScreens_closesOutABroadcastFloatOnce()
    {
        const QString w = QStringLiteral("app|float-close-out");
        QVERIFY(m_wta->relayWindowFloatingChanged(w, true, m_screenId));
        QVERIFY(!m_wta->service()->isWindowFloating(w));

        QSignalSpy floatSpy(m_wta, &WindowTrackingAdaptor::windowFloatingChanged);
        m_wta->releaseLeftScreens(w, QStringLiteral("DP-2"), nullptr);
        QCOMPARE(floatSpy.count(), 1);
        QCOMPARE(floatSpy.first().at(1).toBool(), false);
        QCOMPARE(floatSpy.first().at(2).toString(), QStringLiteral("DP-2"));

        m_wta->releaseLeftScreens(w, QStringLiteral("DP-2"), nullptr);
        QCOMPARE(floatSpy.count(), 1);
    }

    // The daemon hears of every release with the screen kept, for its own
    // per-screen memory (the engine orders a mode round trip seeds from,
    // F695).
    void releaseLeftScreens_tellsTheDaemonTheScreenKept()
    {
        QList<QPair<QString, QString>> told;
        m_wta->setWindowLeftScreenHook([&told](const QString& windowId, const QString& keepScreenId) {
            told.append({windowId, keepScreenId});
        });
        const auto unhook = qScopeGuard([this] {
            m_wta->setWindowLeftScreenHook({});
        });
        const QString w = QStringLiteral("app|left-hook");

        m_wta->releaseLeftScreens(w, QStringLiteral("DP-2"), nullptr);

        QCOMPARE(told.size(), 1);
        QCOMPARE(told.first(), (QPair<QString, QString>{w, QStringLiteral("DP-2")}));
    }

    // A record naming an output that is no longer connected is the evacuee
    // park's: a move on the remaining output leaves its slot alone. The same
    // record on a connected output is released.
    void releaseLeftScreens_recordOnAnUnpluggedOutputStaysParked()
    {
        const QString dp1 = QStringLiteral("DP-1");
        const QString dp2 = QStringLiteral("DP-2");
        const QString gone = QStringLiteral("HDMI-9");
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(dp1, QRect(0, 0, 1920, 1080), dp1);
        fake.addScreen(dp2, QRect(1920, 0, 1920, 1080), dp2);
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();
        QVERIFY(screenMgr.physicalScreenFor(dp1).isValid());
        QVERIFY(!screenMgr.physicalScreenFor(gone).isValid());
        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        QCOMPARE(wta->service()->screenManager(), &screenMgr);
        const auto recordOn = [wta](const QString& windowId, const QString& screenId) {
            wta->service()->placementStore().record(PlasmaZones::TestHelpers::makePlacement(
                windowId, QStringLiteral("app"), PhosphorEngine::WindowPlacement::stateSnapped(),
                PhosphorEngine::WindowPlacement::snapEngineId(), screenId));
        };
        const auto slotState = [wta](const QString& windowId) {
            return wta->service()
                ->placementStore()
                .peekExact(windowId)
                ->slotFor(PhosphorEngine::WindowPlacement::snapEngineId())
                .state;
        };
        const QString parked = QStringLiteral("app|parked-record");
        const QString left = QStringLiteral("app|left-record");
        recordOn(parked, gone);
        recordOn(left, dp1);

        wta->releaseLeftScreens(parked, dp2, nullptr);
        wta->releaseLeftScreens(left, dp2, nullptr);

        QCOMPARE(slotState(parked), QString(PhosphorEngine::WindowPlacement::stateSnapped()));
        QCOMPARE(slotState(left), QString(PhosphorEngine::WindowPlacement::stateReleased()));
    }

    // An engine relays its move marker BEFORE it re-keys the window: the
    // screens left are released first, and the relaying engine is never asked
    // to let go of the window it is moving.
    void engineRelay_leavesBeforeTheMarker()
    {
        StubPlacementEngine autotile;
        m_wta->setEngines(m_snapEngine, &autotile, nullptr);
        const QString w = QStringLiteral("app|relayed");
        const QString dest = QStringLiteral("DP-2");
        snapOnTwoDesktops(w, m_screenId);

        QSignalSpy markerSpy(m_wta, &WindowTrackingAdaptor::windowOutputMoveExpected);
        Q_EMIT autotile.windowOutputMoveExpected(w, dest);

        QCOMPARE(markerSpy.count(), 1);
        QCOMPARE(markerSpy.first().at(0).toString(), w);
        QCOMPARE(markerSpy.first().at(1).toString(), dest);
        QVERIFY(!m_snapEngine->isWindowTracked(w));
        QCOMPARE(snapSlotState(w), QString(PhosphorEngine::WindowPlacement::stateReleased()));
        QVERIFY2(autotile.releasedOffScreen.isEmpty(), "the relaying engine must not release its own move");
    }

    // A keyboard move from snapping onto a tiling monitor leaves nothing on
    // the monitor left: the snap release keeps another desktop's membership
    // whose context is still snapping, which on the SAME monitor is right,
    // but across monitors would drag the window back on that desktop (F60).
    void crossModeMove_snapToTilingLeavesNothingOnTheSourceMonitor()
    {
        StubPlacementEngine scroll;
        m_wta->setEngines(m_snapEngine, nullptr, &scroll);
        const QString dest = QStringLiteral("DP-2");
        PhosphorZones::AssignmentEntry scrolling;
        scrolling.mode = PhosphorZones::AssignmentEntry::Scrolling;
        m_layoutManager->setAssignmentEntryDirect(dest, 0, QString(), scrolling);
        m_layoutManager->assignLayout(m_screenId, 0, QString(), m_testLayout);
        const QString w = QStringLiteral("app|crossmode-leave");
        snapOnTwoDesktops(w, m_screenId);

        QSignalSpy markerSpy(m_wta, &WindowTrackingAdaptor::windowOutputMoveExpected);
        // The window leaves its own fullscreen BEFORE the tiling engine
        // receives it: its tile batch would bail on a fullscreen surface (F526).
        int receivedAtHandBack = -1;
        const QMetaObject::Connection handBack =
            connect(m_wta, &WindowTrackingAdaptor::fullscreenHandBackRequested, this, [&](const QString& id) {
                if (id == w) {
                    receivedAtHandBack = scroll.received.size();
                }
            });
        Q_EMIT m_snapEngine->crossModeMoveRequested(w, dest, 0, QStringLiteral("right"));
        disconnect(handBack);

        QCOMPARE(receivedAtHandBack, 0);
        QCOMPARE(scroll.received.size(), 1);
        QCOMPARE(markerSpy.count(), 1);
        QVERIFY2(!m_snapEngine->isWindowTracked(w), "the other desktop's zone on the monitor left must go too");
        QCOMPARE(snapSlotState(w), QString(PhosphorEngine::WindowPlacement::stateReleased()));
        QVERIFY2(scroll.releasedOffScreen.isEmpty(), "the arriving engine must not be released");
    }

    // A rule routing a window onto a snapping monitor takes its open float
    // with it: Meta+F and the next snap key act there, not on the monitor it
    // opened on (F411, F245).
    void routeToSnappingTarget_rehomesTheOpenFloat()
    {
        RoutedOpenEnv env(m_layoutManager, m_zoneDetector, m_settings, nullptr, 0);
        floatOnSpawnScreen(env);

        QVERIFY(env.wta->applyOpenScreenRouting(env.window, QStringLiteral("DP-1")));

        QVERIFY(env.snap->isFloating(env.window));
        QCOMPARE(env.snap->screenForTrackedWindow(env.window), QStringLiteral("DP-2"));
        QVERIFY2(env.wta->service()->preFloatZones(env.window).isEmpty(), "the home on DP-1 must be forgotten");
        QVERIFY(env.dispatched.isEmpty());
    }

    // Onto a tiling monitor the snap float on the spawn monitor goes and the
    // target's engine gets the window as a genuine open (F448, F472).
    void routeToTilingTarget_dropsTheSnapFloatAndDispatchesAFocusableOpen()
    {
        StubPlacementEngine autotile;
        autotile.activeScreens.insert(QStringLiteral("DP-2"));
        RoutedOpenEnv env(m_layoutManager, m_zoneDetector, m_settings, &autotile, 0);
        floatOnSpawnScreen(env);
        QSignalSpy markerSpy(env.wta, &WindowTrackingAdaptor::windowOutputMoveExpected);

        QVERIFY(env.wta->applyOpenScreenRouting(env.window, QStringLiteral("DP-1")));

        QVERIFY2(!env.snap->isWindowTracked(env.window), "no snap float may stay on the monitor it opened on");
        QCOMPARE(markerSpy.count(), 1);
        QCOMPARE(env.dispatched.size(), 1);
        QCOMPARE(env.dispatched.first().windowId, env.window);
        QCOMPARE(env.dispatched.first().screenId, QStringLiteral("DP-2"));
        QVERIFY(env.dispatched.first().focusEligible);
    }

    // A sweep re-places a window that was already open: no focus (F510).
    void routeToTilingTarget_sweepDispatchesWithoutFocus()
    {
        StubPlacementEngine autotile;
        autotile.activeScreens.insert(QStringLiteral("DP-2"));
        RoutedOpenEnv env(m_layoutManager, m_zoneDetector, m_settings, &autotile, 0);

        env.wta->applyOpenScreenRouting(env.window, QStringLiteral("DP-1"),
                                        PhosphorEngine::RestoreReason::PendingSweep);

        QCOMPARE(env.dispatched.size(), 1);
        QVERIFY(!env.dispatched.first().focusEligible);
    }

    // The same rule also sent the window to a desktop not in view on the
    // target: its arrival places it there, so nothing is dispatched now.
    void routeToTilingTarget_offViewDesktopRouteDoesNotDispatch()
    {
        StubPlacementEngine autotile;
        autotile.activeScreens.insert(QStringLiteral("DP-2"));
        RoutedOpenEnv env(m_layoutManager, m_zoneDetector, m_settings, &autotile, 2);

        QVERIFY(env.wta->applyOpenScreenRouting(env.window, QStringLiteral("DP-1")));

        QVERIFY(env.dispatched.isEmpty());
    }

    // ── Hook 2: a KWin move across screens a tiling engine runs ─────────────

    // A tile KWin moved onto a snapping monitor: the zone it kept frozen on
    // the monitor left goes, with every slot of the record naming that monitor,
    // and the effect hears it is no longer snapped (F124, F673).
    void crossedScreens_tileMovedToASnappingMonitorDropsItsFrozenZone()
    {
        RoutedOpenEnv env(m_layoutManager, m_zoneDetector, m_settings, nullptr, 0);
        snapOnDp1WithTileSlot(env);
        QSignalSpy stateSpy(env.wta, &WindowTrackingAdaptor::windowStateChanged);

        env.wta->windowCrossedScreens(env.window, QStringLiteral("DP-1"), QStringLiteral("DP-2"));

        QVERIFY(!env.snap->isWindowTracked(env.window));
        QCOMPARE(slotState(env, PhosphorEngine::WindowPlacement::snapEngineId()),
                 QString(PhosphorEngine::WindowPlacement::stateReleased()));
        QCOMPARE(slotState(env, PhosphorEngine::WindowPlacement::autotileEngineId()),
                 QString(PhosphorEngine::WindowPlacement::stateReleased()));
        QCOMPARE(stateSpy.count(), 1);
        const auto entry = stateSpy.first().at(1).value<PhosphorProtocol::WindowStateEntry>();
        QCOMPARE(entry.changeType, QStringLiteral("screen_changed"));
        QCOMPARE(entry.screenId, QStringLiteral("DP-2"));
    }

    // The engine that took the window onto the new screen keeps its hold and
    // its slot; the snap memory on the monitor left goes.
    void crossedScreens_adoptingEngineKeepsItsHoldAndSlot()
    {
        StubPlacementEngine autotile;
        autotile.id = QString(PhosphorEngine::WindowPlacement::autotileEngineId());
        RoutedOpenEnv env(m_layoutManager, m_zoneDetector, m_settings, &autotile, 0);
        snapOnDp1WithTileSlot(env);
        autotile.heldScreen.insert(env.window, QStringLiteral("DP-2"));

        env.wta->windowCrossedScreens(env.window, QStringLiteral("DP-1"), QStringLiteral("DP-2"));

        QVERIFY(autotile.releasedOffScreen.isEmpty());
        QCOMPARE(slotState(env, PhosphorEngine::WindowPlacement::autotileEngineId()),
                 QString(PhosphorEngine::WindowPlacement::stateTiled()));
        QCOMPARE(slotState(env, PhosphorEngine::WindowPlacement::snapEngineId()),
                 QString(PhosphorEngine::WindowPlacement::stateReleased()));
        QVERIFY(!env.snap->isWindowTracked(env.window));
    }

    // The other tiling engine's hold (a background desktop's column of the
    // same window) is released off the new screen (F470, F447).
    void crossedScreens_backgroundTilingHoldIsReleased()
    {
        StubPlacementEngine autotile;
        StubPlacementEngine scroll;
        RoutedOpenEnv env(m_layoutManager, m_zoneDetector, m_settings, &autotile, 0, &scroll);
        autotile.heldScreen.insert(env.window, QStringLiteral("DP-2"));

        env.wta->windowCrossedScreens(env.window, QStringLiteral("DP-1"), QStringLiteral("DP-2"));

        QVERIFY(scroll.releasedOffScreen.contains(qMakePair(env.window, QStringLiteral("DP-2"))));
        QVERIFY(autotile.releasedOffScreen.isEmpty());
    }

    // A crossing between virtual screens of one monitor drops the snap
    // membership and the record's snap slot, as a monitor change does.
    void crossedScreens_virtualScreenCrossingReleasesTheRecord()
    {
        RoutedOpenEnv env(m_layoutManager, m_zoneDetector, m_settings, nullptr, 0);
        // The split the crossing names, so its ids are screens the daemon knows.
        QVERIFY(env.screenMgr->setVirtualScreenConfig(QStringLiteral("DP-1"),
                                                      TestHelpers::makeSplitConfig(QStringLiteral("DP-1"))));
        const QString vs0 = QStringLiteral("DP-1/vs:0");
        const QString vs1 = QStringLiteral("DP-1/vs:1");
        env.snap->setCurrentDesktopForScreen(vs0, 1);
        env.snap->setCurrentDesktopForScreen(vs1, 1);
        env.wta->service()->assignWindowToZone(env.window, m_zoneIds[0], vs0, 1);
        env.wta->service()->placementStore().record(*env.snap->capturePlacement(env.window));

        env.wta->windowCrossedScreens(env.window, vs0, vs1);

        QVERIFY(!env.snap->isWindowTracked(env.window));
        QCOMPARE(slotState(env, PhosphorEngine::WindowPlacement::snapEngineId()),
                 QString(PhosphorEngine::WindowPlacement::stateReleased()));
    }

    // A record naming an output that went away is the evacuee park's.
    void crossedScreens_recordOnAnUnpluggedOutputStaysParked()
    {
        RoutedOpenEnv env(m_layoutManager, m_zoneDetector, m_settings, nullptr, 0);
        env.wta->service()->placementStore().record(PlasmaZones::TestHelpers::makePlacement(
            env.window, QStringLiteral("routeapp"), PhosphorEngine::WindowPlacement::stateTiled(),
            PhosphorEngine::WindowPlacement::autotileEngineId(), QStringLiteral("HDMI-9")));

        env.wta->windowCrossedScreens(env.window, QStringLiteral("DP-1"), QStringLiteral("DP-2"));

        QCOMPARE(slotState(env, PhosphorEngine::WindowPlacement::autotileEngineId()),
                 QString(PhosphorEngine::WindowPlacement::stateTiled()));
    }

    // A notice naming no screen is refused before anything is released.
    void crossedScreens_emptyScreenIsRefused()
    {
        RoutedOpenEnv env(m_layoutManager, m_zoneDetector, m_settings, nullptr, 0);
        snapOnDp1WithTileSlot(env);

        env.wta->windowCrossedScreens(env.window, QString(), QStringLiteral("DP-2"));
        env.wta->windowCrossedScreens(env.window, QStringLiteral("DP-1"), QString());

        QVERIFY(env.snap->isWindowTracked(env.window));
        QCOMPARE(slotState(env, PhosphorEngine::WindowPlacement::snapEngineId()),
                 QString(PhosphorEngine::WindowPlacement::stateSnapped()));
    }
};

QTEST_MAIN(TestWtaDropOnLeave)
#include "test_wta_drop_on_leave.moc"
