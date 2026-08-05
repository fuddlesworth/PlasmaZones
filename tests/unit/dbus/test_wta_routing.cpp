// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_routing.cpp
 * @brief Unit tests for the WindowTrackingAdaptor open-routing rules
 *        (RouteToScreen / RouteToDesktop), guardedHandoff refusal recovery,
 *        cross-mode move gating, and the snap-to-empty-zone hoist. Split from
 *        test_wta_convenience.cpp (shared fixture: wta_convenience_fixture.h).
 */

#include "wta_convenience_fixture.h"

class TestWtaRouting : public QObject, protected WtaConvenienceFixture
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        initFixture();
    }
    void cleanup()
    {
        cleanupFixture();
    }

    // ── Open routing: RouteToScreen / RouteToDesktop rules ──
    void testApplyOpenRoutingForTiling_routesToAutotileScreenAndDesktop()
    {
        // DP-2 is an AUTOTILE screen; DP-1 (the spawn screen) stays snapping (the
        // registry default).
        PhosphorZones::AssignmentEntry autotile;
        autotile.mode = PhosphorZones::AssignmentEntry::Autotile;
        autotile.tilingAlgorithm = QStringLiteral("dwindle");
        m_layoutManager->setAssignmentEntryDirect(QStringLiteral("DP-2"), 0, QString(), autotile);

        // Register the opening window's metadata so the rule query resolves its appId.
        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        m_wta->setWindowMetadata(QStringLiteral("inst1"), QStringLiteral("routeapp"), QString(), QString(), QString(),
                                 0, 0, QString(), 0, QVariantMap());

        // Rule: route "routeapp" onto DP-2 and onto virtual desktop 2.
        using namespace PhosphorRules;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("routeapp"));
        RuleAction route;
        route.type = QString(ActionType::RouteToScreen);
        route.params.insert(QString(ActionParam::TargetScreenId), QStringLiteral("DP-2"));
        RuleAction desk;
        desk.type = QString(ActionType::RouteToDesktop);
        desk.params.insert(QString(ActionParam::TargetDesktop), 2);
        rule.actions = {route, desk};

        RuleStore store(ConfigDefaults::rulesFilePath(), nullptr); // stack object: no QObject parent
        QVERIFY(store.addRule(rule));
        m_wta->setRuleStore(&store);

        QSignalSpy outputSpy(m_wta, &WindowTrackingAdaptor::windowOutputMoveExpected);
        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);

        const QString routed =
            m_wta->applyOpenRoutingForTiling(QStringLiteral("routeapp|inst1"), QStringLiteral("DP-1"));

        // Redirected to the autotile target, with both the output- and desktop-move
        // signals emitted for the compositor to act on.
        QCOMPARE(routed, QStringLiteral("DP-2"));
        QCOMPARE(outputSpy.count(), 1);
        QCOMPARE(outputSpy.at(0).at(1).toString(), QStringLiteral("DP-2"));
        QCOMPARE(desktopSpy.count(), 1);
        QCOMPARE(desktopSpy.at(0).at(1).toInt(), 2);

        m_wta->setRuleStore(nullptr);
        m_wta->setWindowRegistry(nullptr);
    }

    void testRestorePolicy_scopedExclusionDoesNotCancelLowerPriorityRule()
    {
        // Consumer-level guard for the WTA evaluator's terminal-action scope
        // ({Exclude, ExcludePlacement}, set in ensureRuleEvaluator): a
        // higher-priority decoration-only opt-out must NOT stop the walk
        // before a lower-priority RestorePosition rule resolves. Deleting the
        // setTerminalActionScope call from ensureRuleEvaluator leaves every
        // primitive-level scope test green while this one fails — the
        // exclusion would terminate the walk and the global default (forced
        // FALSE below) would come back instead of the rule's TRUE.
        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        m_wta->setWindowMetadata(QStringLiteral("inst9"), QStringLiteral("deskflow"), QString(), QString(), QString(),
                                 0, 0, QString(), 0, QVariantMap());
        m_settings->setSnappingRestoreFloatedWindowsOnLogin(false);

        using namespace PhosphorRules;
        Rule decoRule;
        decoRule.id = QUuid::createUuid();
        decoRule.enabled = true;
        decoRule.priority = 500;
        decoRule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("deskflow"));
        RuleAction decoAction;
        decoAction.type = QString(ActionType::ExcludeDecorations);
        decoRule.actions = {decoAction};

        Rule restoreRule;
        restoreRule.id = QUuid::createUuid();
        restoreRule.enabled = true;
        restoreRule.priority = 100;
        restoreRule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("deskflow"));
        RuleAction restoreAction;
        restoreAction.type = QString(ActionType::RestorePosition);
        restoreAction.params.insert(QString(ActionParam::Value), true);
        restoreRule.actions = {restoreAction};

        RuleStore store(ConfigDefaults::rulesFilePath(), nullptr); // stack object: no QObject parent
        QVERIFY(store.addRule(decoRule));
        QVERIFY(store.addRule(restoreRule));
        m_wta->setRuleStore(&store);

        QVERIFY(m_wta->shouldRestoreFloatedPosition(QStringLiteral("deskflow|inst9"),
                                                    PhosphorZones::AssignmentEntry::Mode::Snapping));

        m_wta->setRuleStore(nullptr);
        m_wta->setWindowRegistry(nullptr);
        m_settings->setSnappingRestoreFloatedWindowsOnLogin(true);
    }

    void testApplyOpenRoutingForTiling_acceptsScrollingTarget()
    {
        // DP-2 is a SCROLLING screen: org.plasmazones.Tiling serves both
        // tiling-family engines, so a RouteToScreen rule targeting a
        // scrolling monitor must redirect exactly like an autotile target.
        PhosphorZones::AssignmentEntry scrolling;
        scrolling.mode = PhosphorZones::AssignmentEntry::Scrolling;
        m_layoutManager->setAssignmentEntryDirect(QStringLiteral("DP-2"), 0, QString(), scrolling);

        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        m_wta->setWindowMetadata(QStringLiteral("inst3"), QStringLiteral("scrollroute"), QString(), QString(),
                                 QString(), 0, 0, QString(), 0, QVariantMap());

        using namespace PhosphorRules;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("scrollroute"));
        RuleAction route;
        route.type = QString(ActionType::RouteToScreen);
        route.params.insert(QString(ActionParam::TargetScreenId), QStringLiteral("DP-2"));
        rule.actions = {route};

        RuleStore store(ConfigDefaults::rulesFilePath(), nullptr); // stack object: no QObject parent
        QVERIFY(store.addRule(rule));
        m_wta->setRuleStore(&store);

        QSignalSpy outputSpy(m_wta, &WindowTrackingAdaptor::windowOutputMoveExpected);

        const QString routed =
            m_wta->applyOpenRoutingForTiling(QStringLiteral("scrollroute|inst3"), QStringLiteral("DP-1"));

        QCOMPARE(routed, QStringLiteral("DP-2"));
        QCOMPARE(outputSpy.count(), 1);
        QCOMPARE(outputSpy.at(0).at(1).toString(), QStringLiteral("DP-2"));

        m_wta->setRuleStore(nullptr);
        m_wta->setWindowRegistry(nullptr);
    }

    void testApplyOpenRoutingForTiling_declinesSnapModeTarget()
    {
        // DP-2 stays SNAPPING (registry default): autotile must NOT redirect onto a
        // snap-mode monitor (the snap placement path owns those), but RouteToDesktop
        // is engine-neutral and still fires.
        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        m_wta->setWindowMetadata(QStringLiteral("inst2"), QStringLiteral("snaproute"), QString(), QString(), QString(),
                                 0, 0, QString(), 0, QVariantMap());

        using namespace PhosphorRules;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("snaproute"));
        RuleAction route;
        route.type = QString(ActionType::RouteToScreen);
        route.params.insert(QString(ActionParam::TargetScreenId), QStringLiteral("DP-2"));
        RuleAction desk;
        desk.type = QString(ActionType::RouteToDesktop);
        desk.params.insert(QString(ActionParam::TargetDesktop), 3);
        rule.actions = {route, desk};

        RuleStore store(ConfigDefaults::rulesFilePath(), nullptr); // stack object: no QObject parent
        QVERIFY(store.addRule(rule));
        m_wta->setRuleStore(&store);

        QSignalSpy outputSpy(m_wta, &WindowTrackingAdaptor::windowOutputMoveExpected);
        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);

        const QString routed =
            m_wta->applyOpenRoutingForTiling(QStringLiteral("snaproute|inst2"), QStringLiteral("DP-1"));

        QVERIFY2(routed.isEmpty(), "a snap-mode RouteToScreen target must not be an autotile redirect");
        QCOMPARE(outputSpy.count(), 0);
        QCOMPARE(desktopSpy.count(), 1); // desktop routing is engine-neutral
        QCOMPARE(desktopSpy.at(0).at(1).toInt(), 3);

        m_wta->setRuleStore(nullptr);
        m_wta->setWindowRegistry(nullptr);
    }

    // The snap open path's RouteToDesktop emit (applyOpenDesktopRouting, called from
    // SnapAdaptor::resolveWindowRestore) emits windowDesktopMoveRequested when a
    // matched rule pins a desktop, and stays silent otherwise.
    void testApplyOpenDesktopRouting_emitsDesktopMoveForMatch()
    {
        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        m_wta->setWindowMetadata(QStringLiteral("inst3"), QStringLiteral("deskapp"), QString(), QString(), QString(), 0,
                                 0, QString(), 0, QVariantMap());

        using namespace PhosphorRules;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("deskapp"));
        RuleAction desk;
        desk.type = QString(ActionType::RouteToDesktop);
        desk.params.insert(QString(ActionParam::TargetDesktop), 4);
        rule.actions = {desk};

        RuleStore store(ConfigDefaults::rulesFilePath(), nullptr); // stack object: no QObject parent
        QVERIFY(store.addRule(rule));
        m_wta->setRuleStore(&store);

        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        m_wta->applyOpenDesktopRouting(QStringLiteral("deskapp|inst3"), QStringLiteral("DP-1"));
        QCOMPARE(desktopSpy.count(), 1);
        QCOMPARE(desktopSpy.at(0).at(1).toInt(), 4);

        // A window with no matching rule emits nothing.
        m_wta->setWindowMetadata(QStringLiteral("inst4"), QStringLiteral("nomatch"), QString(), QString(), QString(), 0,
                                 0, QString(), 0, QVariantMap());
        QSignalSpy quietSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        m_wta->applyOpenDesktopRouting(QStringLiteral("nomatch|inst4"), QStringLiteral("DP-1"));
        QCOMPARE(quietSpy.count(), 0);

        m_wta->setRuleStore(nullptr);
        m_wta->setWindowRegistry(nullptr);
    }

    // A BARE RouteToScreen rule (no SnapToZone) must move the opening window to the
    // target monitor on the snap open path: applyOpenScreenRouting translates the
    // window's frame onto the target screen's available area and emits the
    // output-move marker plus a FREE applyGeometryRequested (empty zone id). A local
    // WTA wired to a deterministic two-screen ScreenManager is used because the
    // shared fixture has a null ScreenManager.
    void testApplyOpenScreenRouting_movesBareRouteToTargetMonitor()
    {
        PhosphorScreens::FakeScreenProvider fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080), QStringLiteral("DP-1"));
        fake.addScreen(QStringLiteral("DP-2"), QRect(1920, 0, 1920, 1080), QStringLiteral("DP-2"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
        screenMgr.start();

        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        auto* snap = new SnapEngine(m_layoutManager, wta->service(), m_zoneDetector, nullptr, nullptr);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap);
        wta->setEngines(snap, nullptr, nullptr);

        auto* registry = new PhosphorEngine::WindowRegistry(&parent);
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
        rule.actions = {route}; // bare RouteToScreen, NO SnapToZone
        RuleStore store(ConfigDefaults::rulesFilePath(), &parent);
        QVERIFY(store.addRule(rule));
        wta->setRuleStore(&store);

        // Window opened on DP-1 at a known frame.
        const QString w = QStringLiteral("routeapp|inst1");
        wta->setFrameGeometry(w, 100, 100, 800, 600);

        QSignalSpy outputSpy(wta, &WindowTrackingAdaptor::windowOutputMoveExpected);
        QSignalSpy geomSpy(wta, &WindowTrackingAdaptor::applyGeometryRequested);

        wta->applyOpenScreenRouting(w, QStringLiteral("DP-1"));

        // Output-move marker for DP-2.
        QCOMPARE(outputSpy.count(), 1);
        QCOMPARE(outputSpy.at(0).at(1).toString(), QStringLiteral("DP-2"));
        // Free placement on DP-2: applyGeometryRequested(windowId, x, y, w, h, zoneId, screenId, sizeOnly).
        QCOMPARE(geomSpy.count(), 1);
        const auto args = geomSpy.at(0);
        QCOMPARE(args.at(0).toString(), w);
        QVERIFY2(args.at(5).toString().isEmpty(), "a bare route must be a free placement (empty zone id)");
        QCOMPARE(args.at(6).toString(), QStringLiteral("DP-2"));
        const int x = args.at(1).toInt();
        QVERIFY2(x >= 1920 && x < 3840, "the window must land within DP-2's geometry");

        wta->setRuleStore(nullptr);
        wta->setWindowRegistry(nullptr);
        wta->service()->setSnapEngine(nullptr);
        wta->service()->setSnapState(nullptr);
        delete snap;
    }

    // A rule carrying BOTH SnapToZone and RouteToScreen must NOT be moved by
    // applyOpenScreenRouting: the snap placement directive routes AND snaps it on the
    // target screen, so a free move here would double-place the window. The Placement
    // slot guard suppresses it even with everything else set up to move.
    void testApplyOpenScreenRouting_snapToZonePresent_doesNotDoubleMove()
    {
        PhosphorScreens::FakeScreenProvider fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080), QStringLiteral("DP-1"));
        fake.addScreen(QStringLiteral("DP-2"), QRect(1920, 0, 1920, 1080), QStringLiteral("DP-2"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
        screenMgr.start();

        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        auto* snap = new SnapEngine(m_layoutManager, wta->service(), m_zoneDetector, nullptr, nullptr);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap);
        wta->setEngines(snap, nullptr, nullptr);

        auto* registry = new PhosphorEngine::WindowRegistry(&parent);
        wta->setWindowRegistry(registry);
        wta->setWindowMetadata(QStringLiteral("inst2"), QStringLiteral("snaproute"), QString(), QString(), QString(), 0,
                               0, QString(), 0, QVariantMap());

        using namespace PhosphorRules;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("snaproute"));
        RuleAction route;
        route.type = QString(ActionType::RouteToScreen);
        route.params.insert(QString(ActionParam::TargetScreenId), QStringLiteral("DP-2"));
        RuleAction snapTo;
        snapTo.type = QString(ActionType::SnapToZone);
        snapTo.params.insert(QString(ActionParam::Zones), QJsonArray{1});
        rule.actions = {route, snapTo};
        RuleStore store(ConfigDefaults::rulesFilePath(), &parent);
        QVERIFY(store.addRule(rule));
        wta->setRuleStore(&store);

        const QString w = QStringLiteral("snaproute|inst2");
        wta->setFrameGeometry(w, 100, 100, 800, 600);

        QSignalSpy outputSpy(wta, &WindowTrackingAdaptor::windowOutputMoveExpected);
        QSignalSpy geomSpy(wta, &WindowTrackingAdaptor::applyGeometryRequested);

        wta->applyOpenScreenRouting(w, QStringLiteral("DP-1"));

        QVERIFY2(outputSpy.isEmpty(), "a route+snap rule must not free-move via applyOpenScreenRouting");
        QVERIFY2(geomSpy.isEmpty(), "a route+snap rule must not free-move via applyOpenScreenRouting");

        wta->setRuleStore(nullptr);
        wta->setWindowRegistry(nullptr);
        wta->service()->setSnapEngine(nullptr);
        wta->service()->setSnapState(nullptr);
        delete snap;
    }

    void testGuardedHandoff_refusalRehomesIntoSource()
    {
        QObject owner;
        auto* source = new PhosphorScrollEngine::ScrollEngine(nullptr, nullptr, &owner);
        source->setActiveScreens({QStringLiteral("S1")});
        source->windowOpened(QStringLiteral("app|gh"), QStringLiteral("S1"), 0, 0);
        QVERIFY(source->isWindowTracked(QStringLiteral("app|gh")));

        auto* dest = new PhosphorScrollEngine::ScrollEngine(nullptr, nullptr, &owner);
        // dest claims NOTHING → handoffReceive refuses.
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = QStringLiteral("app|gh");
        ctx.toScreenId = QStringLiteral("S2");
        ctx.fromEngineId = source->engineId();
        const bool adopted = WindowTrackingInternal::guardedHandoff(source, dest, ctx, QStringLiteral("S1"));
        QVERIFY(!adopted);
        QVERIFY(!dest->isWindowTracked(QStringLiteral("app|gh")));
        // Re-homed: still managed by the source on the recovery screen.
        QVERIFY(source->isWindowTracked(QStringLiteral("app|gh")));

        // Pre-tracked destination (source == dest): a refused move to an
        // unclaimed screen must report NOT adopted even though
        // isWindowTracked stays true throughout.
        PhosphorEngine::IPlacementEngine::HandoffContext sameCtx;
        sameCtx.windowId = QStringLiteral("app|gh");
        sameCtx.toScreenId = QStringLiteral("S9");
        sameCtx.fromEngineId = source->engineId();
        const bool sameAdopted = WindowTrackingInternal::guardedHandoff(source, source, sameCtx, QStringLiteral("S1"));
        QVERIFY(!sameAdopted);
        QVERIFY(source->isWindowTracked(QStringLiteral("app|gh")));
        QCOMPARE(source->screenForTrackedWindow(QStringLiteral("app|gh")), QStringLiteral("S1"));
    }

    /// Cross-desktop refusal: the recovery must land on the window's SOURCE
    /// desktop, not the recovery screen's current one. A cross-desktop
    /// crossing passes recoverDesktop for exactly this reason — without it
    /// the re-home silently teleports the window onto whatever desktop is
    /// visible, and the user finds it on the wrong desktop after a refusal.
    void testGuardedHandoff_crossDesktopRefusalRecoversSourceDesktop()
    {
        QObject owner;
        auto* source = new PhosphorScrollEngine::ScrollEngine(nullptr, nullptr, &owner);
        source->setActiveScreens({QStringLiteral("S1")});
        source->windowOpened(QStringLiteral("app|xd"), QStringLiteral("S1"), 0, 0);
        QVERIFY(source->isWindowTracked(QStringLiteral("app|xd")));

        auto* dest = new PhosphorScrollEngine::ScrollEngine(nullptr, nullptr, &owner);
        // dest claims nothing → the cross-desktop receive is refused.
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = QStringLiteral("app|xd");
        ctx.toScreenId = QStringLiteral("S2");
        ctx.toDesktop = 4;
        ctx.fromEngineId = source->engineId();
        const bool adopted =
            WindowTrackingInternal::guardedHandoff(source, dest, ctx, QStringLiteral("S1"), /*recoverDesktop=*/7);
        QVERIFY(!adopted);
        QVERIFY(source->isWindowTracked(QStringLiteral("app|xd")));

        const auto recovered = source->capturePlacement(QStringLiteral("app|xd"));
        QVERIFY(recovered.has_value());
        QCOMPARE(recovered->screenId, QStringLiteral("S1"));
        QCOMPARE(recovered->virtualDesktop, 7);
    }

    /// Cross-mode MOVE adoption gating at the adaptor level: when the target
    /// engine refuses the arrival (its screen set does not claim the release
    /// screen), handleCrossModeMove must leave the window managed by the
    /// source and must NOT arm the daemon-owned output-move marker — a marker
    /// for a move that never happens swallows the window's next genuine
    /// outputChanged.
    void testCrossModeMove_refusedByTarget_rehomesAndArmsNoMarker()
    {
        QObject owner;
        auto* scroll = new PhosphorScrollEngine::ScrollEngine(nullptr, nullptr, &owner);
        // The engine claims NOTHING, so its handoffReceive refuses — while the
        // registry still routes DP-2 to it by mode (the mid-flip shape).
        m_wta->setEngines(m_snapEngine, nullptr, scroll);
        PhosphorZones::AssignmentEntry scrolling;
        scrolling.mode = PhosphorZones::AssignmentEntry::Scrolling;
        m_layoutManager->setAssignmentEntryDirect(QStringLiteral("DP-2"), 0, QString(), scrolling);
        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);

        const QString w = QStringLiteral("app|crossmove");
        m_snapEngine->commitSnap(w, m_zoneIds[0], m_screenId);
        QVERIFY(m_snapEngine->isWindowTracked(w));

        QSignalSpy markerSpy(m_wta, &WindowTrackingAdaptor::windowOutputMoveExpected);
        Q_EMIT m_snapEngine->crossModeMoveRequested(w, QStringLiteral("DP-2"), 0, QStringLiteral("right"));

        QVERIFY2(!scroll->isWindowTracked(w), "the refusing target must not end up tracking the window");
        QVERIFY2(m_snapEngine->isWindowTracked(w), "a refused crossing must re-home the window into the source");
        QCOMPARE(m_snapEngine->screenForTrackedWindow(w), m_screenId);
        QVERIFY2(markerSpy.isEmpty(), "no output-move marker may be armed for a refused crossing");

        m_wta->setEngines(m_snapEngine, nullptr, nullptr);
    }

    /// Auto-assign open placement skips OCCUPIED zones: snapToEmptyZone must
    /// hoist the window into a free zone rather than stacking it onto the
    /// first zone in the layout, and must decline once every zone is taken.
    void testSnapToEmptyZone_skipsOccupiedZonesThenDeclines()
    {
        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);
        m_settings->setAutoAssignAllLayouts(true); // the layout's own flag is off by default

        m_snapEngine->commitSnap(QStringLiteral("app1|occupant"), m_zoneIds[0], m_screenId);

        const QString w = QStringLiteral("app2|arrival");
        int x = 0, y = 0, width = 0, height = 0;
        bool shouldSnap = false;
        m_snapAdaptor->snapToEmptyZone(w, m_screenId, false, x, y, width, height, shouldSnap);

        QVERIFY(shouldSnap);
        QVERIFY(width > 0 && height > 0);
        const QString landed = m_wta->getWindowState(w).zoneId;
        QVERIFY2(landed != m_zoneIds[0], "the occupied zone must not be reused while empty zones remain");
        QVERIFY(landed == m_zoneIds[1] || landed == m_zoneIds[2]);

        // Fill the layout: with no empty zone left there is nothing to hoist into.
        m_snapEngine->commitSnap(QStringLiteral("app3|filler-a"), m_zoneIds[1], m_screenId);
        m_snapEngine->commitSnap(QStringLiteral("app4|filler-b"), m_zoneIds[2], m_screenId);

        bool lateSnap = true;
        m_snapAdaptor->snapToEmptyZone(QStringLiteral("app5|late"), m_screenId, false, x, y, width, height, lateSnap);
        QVERIFY2(!lateSnap, "a full layout must decline the empty-zone hoist");
    }
};

QTEST_MAIN(TestWtaRouting)
#include "test_wta_routing.moc"
