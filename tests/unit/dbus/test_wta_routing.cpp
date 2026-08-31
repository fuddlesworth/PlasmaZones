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

#include <QScopeGuard>

class TestWtaRouting : public QObject, protected WtaConvenienceFixture
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        initFixture();
        // Owned by m_parent, which initFixture replaces per test — drop the
        // stale pointer or seedLiveWindow would push metadata into a registry
        // the previous test's teardown already destroyed.
        m_windowRegistry = nullptr;
    }
    void cleanup()
    {
        cleanupFixture();
    }

    // ── Focus relay: windowActivated arms the snap layer-focus memory ──
    void testWindowActivated_armsSnapLayerFocusMemory()
    {
        // windowActivated is the PRODUCTION entry point for
        // SnapEngine::windowFocused (snap is deliberately not in the
        // TilingAdaptor lifecycle vector), so this pins the wiring the
        // engine-level switch suites bypass by calling windowFocused
        // directly — the layer-switch candidate memories are dead in a live
        // session if this relay is dropped.
        const QString windowId = QStringLiteral("app|w1");
        const QString screen = QStringLiteral("DP-1");
        SnapState* state = m_snapEngine->stateForWindowOnScreen(windowId, screen);
        state->assignWindowToZone(windowId, QStringLiteral("zone-1"), screen, 1);
        QVERIFY(state->lastSnappedFocus().isEmpty());

        m_wta->windowActivated(windowId, screen);
        QCOMPARE(state->lastSnappedFocus(), windowId);
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
        // Detach on every exit, including a mid-test QVERIFY abort: the stack
        // store is destroyed before the adaptor that borrows it.
        const auto teardown = qScopeGuard([this] {
            m_wta->setRuleStore(nullptr);
            m_wta->setWindowRegistry(nullptr);
        });

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
        // Detach on every exit, including a mid-test QVERIFY abort: the stack
        // store is destroyed before the adaptor that borrows it.
        const auto teardown = qScopeGuard([this] {
            m_wta->setRuleStore(nullptr);
            m_wta->setWindowRegistry(nullptr);
            m_settings->setSnappingRestoreFloatedWindowsOnLogin(true);
        });

        QVERIFY(m_wta->shouldRestoreFloatedPosition(QStringLiteral("deskflow|inst9"),
                                                    PhosphorZones::AssignmentEntry::Mode::Snapping));
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
        // Detach on every exit, including a mid-test QVERIFY abort: the stack
        // store is destroyed before the adaptor that borrows it.
        const auto teardown = qScopeGuard([this] {
            m_wta->setRuleStore(nullptr);
            m_wta->setWindowRegistry(nullptr);
        });

        QSignalSpy outputSpy(m_wta, &WindowTrackingAdaptor::windowOutputMoveExpected);

        const QString routed =
            m_wta->applyOpenRoutingForTiling(QStringLiteral("scrollroute|inst3"), QStringLiteral("DP-1"));

        QCOMPARE(routed, QStringLiteral("DP-2"));
        QCOMPARE(outputSpy.count(), 1);
        QCOMPARE(outputSpy.at(0).at(1).toString(), QStringLiteral("DP-2"));
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
        // Detach on every exit, including a mid-test QVERIFY abort: the stack
        // store is destroyed before the adaptor that borrows it.
        const auto teardown = qScopeGuard([this] {
            m_wta->setRuleStore(nullptr);
            m_wta->setWindowRegistry(nullptr);
        });

        QSignalSpy outputSpy(m_wta, &WindowTrackingAdaptor::windowOutputMoveExpected);
        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);

        const QString routed =
            m_wta->applyOpenRoutingForTiling(QStringLiteral("snaproute|inst2"), QStringLiteral("DP-1"));

        QVERIFY2(routed.isEmpty(), "a snap-mode RouteToScreen target must not be an autotile redirect");
        QCOMPARE(outputSpy.count(), 0);
        QCOMPARE(desktopSpy.count(), 1); // desktop routing is engine-neutral
        QCOMPARE(desktopSpy.at(0).at(1).toInt(), 3);
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
        // Detach on every exit, including a mid-test QVERIFY abort: the stack
        // store is destroyed before the adaptor that borrows it.
        const auto teardown = qScopeGuard([this] {
            m_wta->setRuleStore(nullptr);
            m_wta->setWindowRegistry(nullptr);
        });

        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        // The RETURN is load-bearing, not just the signal: both open channels
        // use it to suppress the cross-desktop session restore, so a rule that
        // names a desktop is not fought by a remembered one. Asserting only the
        // spy would leave that half untested.
        QVERIFY2(m_wta->applyOpenDesktopRouting(QStringLiteral("deskapp|inst3"), QStringLiteral("DP-1")),
                 "a matched RouteToDesktop must report the match to its caller");
        QCOMPARE(desktopSpy.count(), 1);
        QCOMPARE(desktopSpy.at(0).at(1).toInt(), 4);

        // A window with no matching rule emits nothing and reports no match.
        m_wta->setWindowMetadata(QStringLiteral("inst4"), QStringLiteral("nomatch"), QString(), QString(), QString(), 0,
                                 0, QString(), 0, QVariantMap());
        QSignalSpy quietSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        QVERIFY(!m_wta->applyOpenDesktopRouting(QStringLiteral("nomatch|inst4"), QStringLiteral("DP-1")));
        QCOMPARE(quietSpy.count(), 0);
    }

    void testApplyOpenDesktopRouting_reportsNoMatchWithoutARuleStore()
    {
        // The defensive guard. false is the permissive answer here, so an
        // inverted sense would silently suppress every persisted restore on
        // both channels rather than failing loudly.
        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        m_wta->setWindowMetadata(QStringLiteral("inst10"), QStringLiteral("deskapp"), QString(), QString(), QString(),
                                 0, 0, QString(), 0, QVariantMap());
        const auto teardown = qScopeGuard([this] {
            m_wta->setWindowRegistry(nullptr);
        });

        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        QVERIFY(!m_wta->applyOpenDesktopRouting(QStringLiteral("deskapp|inst10"), QStringLiteral("DP-1")));
        QCOMPARE(desktopSpy.count(), 0);
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
        // Detach on every exit, including a mid-test QVERIFY abort: the stack
        // store is destroyed before the adaptor that borrows it.
        const auto teardown = qScopeGuard([wta, snap] {
            wta->setRuleStore(nullptr);
            wta->setWindowRegistry(nullptr);
            wta->service()->setSnapEngine(nullptr);
            wta->service()->setSnapState(nullptr);
            delete snap;
        });

        // Window opened on DP-1 at a known frame.
        const QString w = QStringLiteral("routeapp|inst1");
        wta->setFrameGeometry(w, 100, 100, 800, 600);

        QSignalSpy outputSpy(wta, &WindowTrackingAdaptor::windowOutputMoveExpected);
        QSignalSpy geomSpy(wta, &WindowTrackingAdaptor::applyGeometryRequested);

        QVERIFY2(wta->applyOpenScreenRouting(w, QStringLiteral("DP-1")),
                 "a matched bare route must report the directive as matched");

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
    }

    // A rule carrying BOTH SnapToZone and RouteToScreen is still moved by
    // applyOpenScreenRouting. Its only caller reaches it exclusively under
    // !shouldSnap — the placement directive already declined (a non-snapping
    // target, no layout, no surviving ordinal) — so suppressing the move on the
    // Placement slot's mere presence left the window neither snapped nor routed
    // (the #921 fix). The verdict is still "matched": the rule owns the
    // window's monitor either way.
    void testApplyOpenScreenRouting_snapToZonePresent_stillHonoursRoute()
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
        // Detach on every exit, including a mid-test QVERIFY abort: the stack
        // store is destroyed before the adaptor that borrows it.
        const auto teardown = qScopeGuard([wta, snap] {
            wta->setRuleStore(nullptr);
            wta->setWindowRegistry(nullptr);
            wta->service()->setSnapEngine(nullptr);
            wta->service()->setSnapState(nullptr);
            delete snap;
        });

        const QString w = QStringLiteral("snaproute|inst2");
        wta->setFrameGeometry(w, 100, 100, 800, 600);

        QSignalSpy outputSpy(wta, &WindowTrackingAdaptor::windowOutputMoveExpected);
        QSignalSpy geomSpy(wta, &WindowTrackingAdaptor::applyGeometryRequested);

        QVERIFY2(wta->applyOpenScreenRouting(w, QStringLiteral("DP-1")),
                 "a matched route+snap rule must report the directive as matched");

        // The route is honoured even though the Placement slot rode along:
        // reaching this call at all means the snap did not happen.
        QCOMPARE(outputSpy.count(), 1);
        QCOMPARE(outputSpy.at(0).at(1).toString(), QStringLiteral("DP-2"));
        QCOMPARE(geomSpy.count(), 1);
        QCOMPARE(geomSpy.at(0).at(6).toString(), QStringLiteral("DP-2"));
    }

    // The bool verdict of applyOpenScreenRouting and the directiveMatched
    // out-param of applyOpenRoutingForTiling gate the cross-screen reclaim
    // on their respective channels. Both must report "a directive matched"
    // for the cases where a rule OWNS the window's monitor but no move is
    // needed or possible — reading those as "no rule" is what let the two
    // channels apply opposite precedence to the same rule.
    void testRoutingVerdicts_matchedButNoMove_reportMatchedOnBothChannels()
    {
        PhosphorScreens::FakeScreenProvider fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
        screenMgr.start();

        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        auto* registry = new PhosphorEngine::WindowRegistry(&parent);
        wta->setWindowRegistry(registry);
        wta->setWindowMetadata(QStringLiteral("inst3"), QStringLiteral("pinapp"), QString(), QString(), QString(), 0, 0,
                               QString(), 0, QVariantMap());

        using namespace PhosphorRules;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("pinapp"));
        RuleAction route;
        route.type = QString(ActionType::RouteToScreen);
        route.params.insert(QString(ActionParam::TargetScreenId), QStringLiteral("DP-1"));
        rule.actions = {route};
        RuleStore store(ConfigDefaults::rulesFilePath(), &parent);
        QVERIFY(store.addRule(rule));
        wta->setRuleStore(&store);
        // Detach on every exit, including a mid-test QVERIFY abort: the stack
        // store is destroyed before the adaptor that borrows it.
        const auto teardown = qScopeGuard([wta] {
            wta->setRuleStore(nullptr);
            wta->setWindowRegistry(nullptr);
        });

        const QString w = QStringLiteral("pinapp|inst3");
        wta->setFrameGeometry(w, 100, 100, 800, 600);

        // ALREADY ON TARGET. The snap channel returns true...
        QVERIFY2(wta->applyOpenScreenRouting(w, QStringLiteral("DP-1")),
                 "a rule pinning the window to the screen it already occupies still owns that monitor");
        // ...and the tiling channel must agree, with an EMPTY redirect (no
        // move needed) but directiveMatched set — the two answers are
        // deliberately separate.
        bool matched = false;
        const QString routed = wta->applyOpenRoutingForTiling(w, QStringLiteral("DP-1"), &matched);
        QVERIFY2(routed.isEmpty(), "already on target: no redirect");
        QVERIFY2(matched, "…but the directive matched, so the reclaim must be vetoed on this channel too");

        // NO RULE AT ALL: both channels report unmatched, so the reclaim runs.
        wta->setWindowMetadata(QStringLiteral("inst4"), QStringLiteral("otherapp"), QString(), QString(), QString(), 0,
                               0, QString(), 0, QVariantMap());
        const QString other = QStringLiteral("otherapp|inst4");
        wta->setFrameGeometry(other, 100, 100, 800, 600);
        QVERIFY2(!wta->applyOpenScreenRouting(other, QStringLiteral("DP-1")), "no rule → no directive");
        bool otherMatched = true;
        QVERIFY(wta->applyOpenRoutingForTiling(other, QStringLiteral("DP-1"), &otherMatched).isEmpty());
        QVERIFY2(!otherMatched, "no rule → no directive on the tiling channel either");
    }

    // The ownership verdict both channels share (hasValidPlacementTarget) is a
    // PAYLOAD-SHAPE judgement over the SnapToZone targets, and the zone-NAME
    // form (discussion #924) must count: a names-only rule owns the window's
    // target on both channels even though no zone in the test layout carries
    // that name (no engine is wired here; in the daemon the engine would then
    // decline the snap and the remembered-placement fallback stays suppressed,
    // as for an ordinal the layout lacks). The control is a window no rule
    // matches, which both channels must report as unowned. (A payload with no
    // valid target cannot be built through the store: Rule::isValid runs the
    // descriptor validator, which needs at least one valid entry across the
    // two lists.)
    void testRoutingVerdicts_namesOnlyPlacement_ownsOnBothChannels()
    {
        PhosphorScreens::FakeScreenProvider fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
        screenMgr.start();

        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        auto* registry = new PhosphorEngine::WindowRegistry(&parent);
        wta->setWindowRegistry(registry);
        wta->setWindowMetadata(QStringLiteral("inst5"), QStringLiteral("namedapp"), QString(), QString(), QString(), 0,
                               0, QString(), 0, QVariantMap());
        wta->setWindowMetadata(QStringLiteral("inst6"), QStringLiteral("otherapp"), QString(), QString(), QString(), 0,
                               0, QString(), 0, QVariantMap());

        using namespace PhosphorRules;
        Rule named;
        named.id = QUuid::createUuid();
        named.enabled = true;
        named.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("namedapp"));
        RuleAction snapByName;
        snapByName.type = QString(ActionType::SnapToZone);
        snapByName.params.insert(QString(ActionParam::ZoneNames), QJsonArray{QStringLiteral("Editor")});
        named.actions = {snapByName};
        RuleStore store(ConfigDefaults::rulesFilePath(), nullptr); // stack object: no QObject parent
        QVERIFY(store.addRule(named));
        wta->setRuleStore(&store);
        // Detach on every exit: `store` is declared after `parent`, so on a
        // mid-test QVERIFY abort it dies first while the WTA still holds it.
        const auto teardown = qScopeGuard([wta] {
            wta->setRuleStore(nullptr);
            wta->setWindowRegistry(nullptr);
        });

        const QString w = QStringLiteral("namedapp|inst5");
        wta->setFrameGeometry(w, 100, 100, 800, 600);
        QVERIFY2(wta->applyOpenScreenRouting(w, QStringLiteral("DP-1")),
                 "a names-only SnapToZone rule owns the window's target on the snap channel");
        bool matched = false;
        QVERIFY(wta->applyOpenRoutingForTiling(w, QStringLiteral("DP-1"), &matched).isEmpty());
        QVERIFY2(matched, "…and on the tiling channel");

        const QString other = QStringLiteral("otherapp|inst6");
        wta->setFrameGeometry(other, 100, 100, 800, 600);
        QVERIFY2(!wta->applyOpenScreenRouting(other, QStringLiteral("DP-1")), "no rule → no ownership");
        bool otherMatched = true;
        QVERIFY(wta->applyOpenRoutingForTiling(other, QStringLiteral("DP-1"), &otherMatched).isEmpty());
        QVERIFY2(!otherMatched, "no rule → no ownership on the tiling channel either");
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

    // ── Cross-desktop session restore (applyPersistedDesktopRestore) ──
    //
    // A Wayland session restores no virtual-desktop membership, so every window
    // reopens on whichever desktop is current at login. These pin the daemon's
    // compensation: the persisted record's desktop is re-applied through
    // windowDesktopMoveRequested before any engine places the window.

    void testPersistedDesktopRestore_sendsWindowBackToItsRecordedDesktop()
    {
        seedPersistedDesktopRecord(QStringLiteral("deskapp"), QStringLiteral("old-uuid"), /*recordedDesktop=*/2);
        // The window reopened with a FRESH uuid (a real logout: the client is a
        // new Wayland surface) on desktop 1 — so the store's same-instance
        // branch cannot match and the appId FIFO has to carry the record.
        seedLiveWindow(QStringLiteral("newinst"), QStringLiteral("deskapp"), /*liveDesktop=*/1);

        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        QVERIFY2(m_wta->applyPersistedDesktopRestore(QStringLiteral("deskapp|newinst")),
                 "a record naming another desktop must claim the open");
        QCOMPARE(desktopSpy.count(), 1);
        QCOMPARE(desktopSpy.at(0).at(0).toString(), QStringLiteral("deskapp|newinst"));
        QCOMPARE(desktopSpy.at(0).at(1).toInt(), 2);
    }

    void testPersistedDesktopRestore_leavesTheRecordForTheEngineRestore()
    {
        // Non-consuming by contract: the window is only on its way to the
        // recorded desktop, and the engine restore that runs when that desktop
        // is next shown needs the same record. A take() here would strand it.
        seedPersistedDesktopRecord(QStringLiteral("deskapp"), QStringLiteral("old-uuid"), 2);
        seedLiveWindow(QStringLiteral("newinst"), QStringLiteral("deskapp"), 1);

        QVERIFY(m_wta->applyPersistedDesktopRestore(QStringLiteral("deskapp|newinst")));

        const auto still = m_wta->service()->placementStore().peek(QString(), QStringLiteral("deskapp"),
                                                                   [](const PhosphorEngine::WindowPlacement& p) {
                                                                       return p.virtualDesktop == 2;
                                                                   });
        QVERIFY2(still.has_value(), "the placement record must survive the desktop move");
    }

    void testPersistedDesktopRestore_declinesWhenAlreadyOnTheRecordedDesktop()
    {
        // The commonest outcome at login — every window that was on the desktop
        // showing when the session came up.
        seedPersistedDesktopRecord(QStringLiteral("deskapp"), QStringLiteral("old-uuid"), 2);
        seedLiveWindow(QStringLiteral("newinst"), QStringLiteral("deskapp"), /*liveDesktop=*/2);

        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        QVERIFY2(!m_wta->applyPersistedDesktopRestore(QStringLiteral("deskapp|newinst")),
                 "a window already on its recorded desktop must fall through to normal placement");
        QCOMPARE(desktopSpy.count(), 0);
    }

    void testPersistedDesktopRestore_honoursTheSettingBeingOff()
    {
        seedPersistedDesktopRecord(QStringLiteral("deskapp"), QStringLiteral("old-uuid"), 2);
        seedLiveWindow(QStringLiteral("newinst"), QStringLiteral("deskapp"), 1);
        m_settings->setRestoreWindowsToDesktopOnLogin(false);

        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        QVERIFY(!m_wta->applyPersistedDesktopRestore(QStringLiteral("deskapp|newinst")));
        QCOMPARE(desktopSpy.count(), 0);
    }

    void testPersistedDesktopRestore_ignoresAnInSessionRecord()
    {
        // THE login-only guard. This record was captured live in this session
        // (record(), not deserialize()), so it carries no fromPersistedSession
        // flag and must not relocate anything — otherwise closing a window on
        // desktop 2 and reopening it from desktop 1 would teleport it, which is
        // a mid-session behaviour the setting does not promise.
        PhosphorEngine::WindowPlacement live;
        live.windowId = QStringLiteral("deskapp|old-uuid");
        live.appId = QStringLiteral("deskapp");
        live.screenId = m_screenId;
        live.virtualDesktop = 2;
        live.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(),
                            {PhosphorEngine::WindowPlacement::stateSnapped(), {}, -1});
        m_wta->service()->placementStore().record(live);

        seedLiveWindow(QStringLiteral("newinst"), QStringLiteral("deskapp"), 1);

        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        QVERIFY2(!m_wta->applyPersistedDesktopRestore(QStringLiteral("deskapp|newinst")),
                 "an in-session record must not drive a cross-desktop move");
        QCOMPARE(desktopSpy.count(), 0);
    }

    void testPersistedDesktopRestore_firesAtMostOncePerRecord()
    {
        // The one-shot property, and the reason the restore spends the flag
        // itself instead of leaving it to the engine restore: after a logout the
        // record's windowId carries the OLD uuid while the live window has a new
        // one, so the store's same-instance merge never fires and nothing else
        // would ever disarm it. A record left armed teleports the app across
        // desktops on any later mid-session reopen — which is exactly the
        // behaviour the login-only contract rules out.
        seedPersistedDesktopRecord(QStringLiteral("deskapp"), QStringLiteral("old-uuid"), 2);
        seedLiveWindow(QStringLiteral("newinst"), QStringLiteral("deskapp"), 1);
        QVERIFY(m_wta->applyPersistedDesktopRestore(QStringLiteral("deskapp|newinst")));

        // Same window announced again from desktop 1 — the engine restore
        // declined, so the record was never consumed and is still in the store.
        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        QVERIFY2(!m_wta->applyPersistedDesktopRestore(QStringLiteral("deskapp|newinst")),
                 "a spent record must not re-arm the cross-desktop restore");
        QCOMPARE(desktopSpy.count(), 0);

        // A DIFFERENT window of the same app must not inherit the spent record
        // either — the flag is per record, not per call.
        seedLiveWindow(QStringLiteral("otherinst"), QStringLiteral("deskapp"), 1);
        QVERIFY(!m_wta->applyPersistedDesktopRestore(QStringLiteral("deskapp|otherinst")));
        QCOMPARE(desktopSpy.count(), 0);
    }

    void testPersistedDesktopRestore_liveCaptureDisarmsTheDaemonRestartShape()
    {
        // The other disarm path, covering a daemon restart rather than a logout:
        // the uuid survives, so the engine's live capture MERGES into the
        // persisted record and the merge's engine-capture branch clears the flag
        // there. Without that branch a restart would re-arm the move every time
        // the daemon came up.
        seedPersistedDesktopRecord(QStringLiteral("deskapp"), QStringLiteral("sameinst"), 3);

        PhosphorEngine::WindowPlacement captured;
        captured.windowId = QStringLiteral("deskapp|sameinst");
        captured.appId = QStringLiteral("deskapp");
        captured.screenId = m_screenId;
        // Three distinct desktops on purpose. The capture must differ from the
        // seeded record (3) so the merge is a real change, and the merged value
        // (2) must differ from the live desktop (1) so the "already home" exit
        // does NOT fire — otherwise this case would decline for that reason and
        // pass even with the merge-clear deleted, proving nothing. Keeping all
        // three apart leaves the cleared flag as the only thing that can produce
        // the decline.
        captured.virtualDesktop = 2;
        captured.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(),
                                {PhosphorEngine::WindowPlacement::stateSnapped(), {}, -1});
        QVERIFY(m_wta->service()->placementStore().record(captured));

        seedLiveWindow(QStringLiteral("sameinst"), QStringLiteral("deskapp"), 1);
        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        QVERIFY2(!m_wta->applyPersistedDesktopRestore(QStringLiteral("deskapp|sameinst")),
                 "a live engine capture supersedes the persisted context");
        QCOMPARE(desktopSpy.count(), 0);
    }

    void testPersistedDesktopRestore_sameInstanceRecordStillMoves()
    {
        // The daemon-restart shape: the uuid survives, so peek matches on its
        // same-instance branch rather than falling through to the appId FIFO.
        // Every other case here exercises the FIFO branch, so without this one
        // the same-instance branch has no test that expects a MOVE — only the
        // merge-clear case, which expects a decline.
        seedPersistedDesktopRecord(QStringLiteral("deskapp"), QStringLiteral("sameinst"), 2);
        seedLiveWindow(QStringLiteral("sameinst"), QStringLiteral("deskapp"), 1);

        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        QVERIFY(m_wta->applyPersistedDesktopRestore(QStringLiteral("deskapp|sameinst")));
        QCOMPARE(desktopSpy.count(), 1);
        QCOMPARE(desktopSpy.at(0).at(0).toString(), QStringLiteral("deskapp|sameinst"));
        QCOMPARE(desktopSpy.at(0).at(1).toInt(), 2);
    }

    void testPersistedDesktopRestore_ignoresNonPositiveRecordedDesktop()
    {
        // 0 means on-all-desktops or unknown on the RECORD side too. The live
        // side of this is covered above; this pins the peek predicate's
        // virtualDesktop > 0 term, which nothing else exercises.
        seedPersistedDesktopRecord(QStringLiteral("deskapp"), QStringLiteral("old-uuid"), 0);
        seedLiveWindow(QStringLiteral("newinst"), QStringLiteral("deskapp"), 1);

        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        QVERIFY(!m_wta->applyPersistedDesktopRestore(QStringLiteral("deskapp|newinst")));
        QCOMPARE(desktopSpy.count(), 0);
    }

    void testPersistedDesktopRestore_ignoresNegativeRecordedDesktop()
    {
        seedPersistedDesktopRecord(QStringLiteral("deskapp"), QStringLiteral("old-uuid"), -1);
        seedLiveWindow(QStringLiteral("newinst"), QStringLiteral("deskapp"), 1);

        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        QVERIFY(!m_wta->applyPersistedDesktopRestore(QStringLiteral("deskapp|newinst")));
        QCOMPARE(desktopSpy.count(), 0);
    }

    void testPersistedDesktopRestore_geometryOnlyWriteLeavesTheRecordArmed()
    {
        // The defensive half of the merge-clear. Disarming is scoped to a real
        // engine capture, because a geometry-only write (recordFreeGeometry and
        // the bringup frame-geometry seed both take that path) happens for every
        // window before any engine places it. If that scope were ever widened,
        // the seed would disarm every record before the first window is placed
        // and the whole feature would silently stop working — with no other test
        // failing.
        seedPersistedDesktopRecord(QStringLiteral("deskapp"), QStringLiteral("old-uuid"), 2);
        seedLiveWindow(QStringLiteral("newinst"), QStringLiteral("deskapp"), 1);

        // A valid, non-empty rect: recordFreeGeometry early-returns on an
        // invalid one, so a default QRect would make this assert nothing.
        m_wta->service()->recordFreeGeometry(QStringLiteral("deskapp|newinst"), m_screenId, QRect(10, 20, 300, 200),
                                             /*overwrite=*/true);

        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        QVERIFY2(m_wta->applyPersistedDesktopRestore(QStringLiteral("deskapp|newinst")),
                 "a geometry-only write must not disarm the persisted restore");
        QCOMPARE(desktopSpy.count(), 1);
        QCOMPARE(desktopSpy.at(0).at(1).toInt(), 2);
    }

    void testPersistedDesktopRestore_declinesWithoutLiveMetadata()
    {
        // The effect pushes metadata ahead of every open, but the daemon must
        // not assume it has arrived. Without this case, deleting the !meta guard
        // crashes rather than failing a test.
        seedPersistedDesktopRecord(QStringLiteral("deskapp"), QStringLiteral("old-uuid"), 2);
        seedLiveWindow(QStringLiteral("someoneelse"), QStringLiteral("deskapp"), 1);

        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        QVERIFY(!m_wta->applyPersistedDesktopRestore(QStringLiteral("deskapp|nometadata")));
        QCOMPARE(desktopSpy.count(), 0);
    }

    void testPersistedDesktopRestore_declinesWithoutAWindowRegistry()
    {
        // No seedLiveWindow at all, so the registry is never created and the
        // null-registry guard is the one under test.
        seedPersistedDesktopRecord(QStringLiteral("deskapp"), QStringLiteral("old-uuid"), 2);

        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        QVERIFY(!m_wta->applyPersistedDesktopRestore(QStringLiteral("deskapp|newinst")));
        QCOMPARE(desktopSpy.count(), 0);
    }

    void testPersistedDesktopRestore_usesTheRegistryAppIdNotTheEmbeddedOne()
    {
        // An Electron/CEF app that re-broadcasts its WM_CLASS mid-session has
        // its records filed under the CURRENT class, while a window id minted
        // earlier still carries the OLD one. The lookup therefore has to ask the
        // registry rather than split the id. Without this case, swapping
        // currentAppIdFor for WindowId::extractAppId passes every other test.
        // The instance ids must DIFFER, or peek's same-instance branch matches
        // across buckets and the appId is never consulted at all — which is
        // exactly what made a first draft of this case pass either way.
        seedPersistedDesktopRecord(QStringLiteral("newclass"), QStringLiteral("old-uuid"), 2);
        seedLiveWindow(QStringLiteral("newinst"), QStringLiteral("newclass"), 1);

        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        QVERIFY2(m_wta->applyPersistedDesktopRestore(QStringLiteral("oldclass|newinst")),
                 "the record is filed under the registry's current appId, not the id's embedded one");
        QCOMPARE(desktopSpy.count(), 1);
        QCOMPARE(desktopSpy.at(0).at(1).toInt(), 2);
    }

    void testResolveWindowRestore_sendsTheWindowBackAndDeclinesToSnapIt()
    {
        // The SNAP production arm, end to end. Every other case in this block
        // calls applyPersistedDesktopRestore directly, so the wiring in
        // SnapAdaptor::resolveWindowRestore — the isOpenPath gate, and the early
        // return that must skip the engine resolve — had no coverage at all.
        //
        // Declining to snap is the load-bearing half: the window is on its way
        // to a desktop this screen is not showing, so snapping it into the
        // CURRENT desktop's layout would place it where the user cannot see it
        // and record a zone against the wrong desktop.
        seedPersistedDesktopRecord(QStringLiteral("deskapp"), QStringLiteral("old-uuid"), 2);
        seedLiveWindow(QStringLiteral("newinst"), QStringLiteral("deskapp"), 1);

        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        int x = 0, y = 0, width = 0, height = 0;
        bool shouldSnap = true;
        m_snapAdaptor->resolveWindowRestore(QStringLiteral("deskapp|newinst"), m_screenId, /*sticky=*/false,
                                            /*windowKind=*/0, /*isOpenPath=*/true, /*minWidth=*/0, /*minHeight=*/0, x,
                                            y, width, height, shouldSnap);
        QCOMPARE(desktopSpy.count(), 1);
        QCOMPARE(desktopSpy.at(0).at(1).toInt(), 2);
        QVERIFY2(!shouldSnap, "the window must not be snapped into the desktop it is leaving");

        // And the record is still there for the engine restore that runs when
        // the window actually lands.
        const auto still =
            m_wta->service()->placementStore().peek(QStringLiteral("deskapp|newinst"), QStringLiteral("deskapp"));
        QVERIFY2(still.has_value(), "the desktop move must not consume the placement record");
        QCOMPARE(still->virtualDesktop, 2);
    }

    void testResolveWindowRestore_leavesTheDesktopAloneOffTheOpenPath()
    {
        // The arrival re-drive comes back through this same slot with
        // isOpenPath=false, and must NOT re-emit the move — that would bounce
        // the window straight off the desktop it just reached.
        seedPersistedDesktopRecord(QStringLiteral("deskapp"), QStringLiteral("old-uuid"), 2);
        seedLiveWindow(QStringLiteral("newinst"), QStringLiteral("deskapp"), 1);

        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        int x = 0, y = 0, width = 0, height = 0;
        bool shouldSnap = true;
        m_snapAdaptor->resolveWindowRestore(QStringLiteral("deskapp|newinst"), m_screenId, /*sticky=*/false,
                                            /*windowKind=*/0, /*isOpenPath=*/false, /*minWidth=*/0, /*minHeight=*/0, x,
                                            y, width, height, shouldSnap);
        QCOMPARE(desktopSpy.count(), 0);
    }

    void testPersistedDesktopRestore_leavesStickyAndUnknownWindowsAlone()
    {
        // virtualDesktop 0 on the LIVE side means on-all-desktops or unknown.
        // A sticky window is already everywhere so there is nowhere to send it,
        // and an unknown one gives nothing to compare — moving on a guess is
        // worse than leaving KWin's choice standing.
        seedPersistedDesktopRecord(QStringLiteral("deskapp"), QStringLiteral("old-uuid"), 2);
        seedLiveWindow(QStringLiteral("newinst"), QStringLiteral("deskapp"), /*liveDesktop=*/0);

        QSignalSpy desktopSpy(m_wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        QVERIFY(!m_wta->applyPersistedDesktopRestore(QStringLiteral("deskapp|newinst")));
        QCOMPARE(desktopSpy.count(), 0);
    }

private:
    /// Seed a placement record as if it had been read back from disk at startup.
    /// Routed through deserialize() rather than record() deliberately: fromJson
    /// is the only producer of WindowPlacement::fromPersistedSession, so a
    /// hand-built record would test a state the daemon cannot actually reach.
    void seedPersistedDesktopRecord(const QString& appId, const QString& instanceId, int recordedDesktop)
    {
        QJsonObject slot;
        slot[QLatin1String("state")] = QString(PhosphorEngine::WindowPlacement::stateSnapped());
        QJsonObject engines;
        engines[QString(PhosphorEngine::WindowPlacement::snapEngineId())] = slot;

        QJsonObject rec;
        rec[QLatin1String("windowId")] = QString(appId + QLatin1Char('|') + instanceId);
        rec[QLatin1String("screen")] = m_screenId;
        rec[QLatin1String("desktop")] = recordedDesktop;
        rec[QLatin1String("engines")] = engines;

        QJsonArray bucket;
        bucket.append(rec);
        QJsonObject root;
        root[appId] = bucket;
        m_wta->service()->placementStore().deserialize(root);
    }

    /// Register live metadata for a reopened window, as the effect's
    /// setWindowMetadata push does ahead of every open.
    void seedLiveWindow(const QString& instanceId, const QString& appId, int liveDesktop)
    {
        if (!m_windowRegistry) {
            m_windowRegistry = new PhosphorEngine::WindowRegistry(m_parent);
            m_wta->setWindowRegistry(m_windowRegistry);
        }
        m_wta->setWindowMetadata(instanceId, appId, QString(), QString(), QString(), /*pid=*/0, liveDesktop, QString(),
                                 /*windowType=*/0, QVariantMap());
    }

    PhosphorEngine::WindowRegistry* m_windowRegistry = nullptr;
};

QTEST_MAIN(TestWtaRouting)
#include "test_wta_routing.moc"
