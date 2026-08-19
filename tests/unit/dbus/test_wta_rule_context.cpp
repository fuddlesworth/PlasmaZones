// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_rule_context.cpp
 * @brief WindowTrackingAdaptor rule-context coverage: the screen-derived
 *        context fields buildContextualRuleQuery stamps (ScreenId,
 *        ActiveLayout, ScreenOrientation), the consumers that read them
 *        (placementZonesByRule, shouldFloatByRule, the snap engine's exclusion
 *        query provider), and the open-path screen routing that runs when
 *        nothing snapped the window.
 */

#include <QTest>
#include <QJsonArray>
#include <QRect>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <memory>

#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorScreens/Manager.h>
#include "FakeScreenProvider.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleSet.h>
#include <PhosphorRules/RuleStore.h>
#include "config/configdefaults.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestWtaRuleContext : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetector(nullptr);

        // A real VirtualDesktopManager, NOT a null one. buildContextualRuleQuery
        // resolves the screen's current desktop through the daemon's live manager
        // rather than the registry's pushed mirror, so a null manager would answer
        // 0 for every screen and the ActiveLayout stamp would query a desktop no
        // assignment is on. Constructed only — init()/start() are what reach KWin
        // over D-Bus, and nothing here calls them.
        m_desktopManager = new PhosphorWorkspaces::VirtualDesktopManager(nullptr);
        // WTA needs a parent QObject for QDBusAbstractAdaptor.
        m_parent = new QObject(nullptr);
        m_wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, nullptr, m_settings, m_desktopManager,
                                          nullptr, m_parent);

        m_snapEngine = new SnapEngine(m_layoutManager, m_wta->service(), m_zoneDetector, nullptr, nullptr);
        m_snapEngine->setEngineSettings(m_settings);
        m_wta->service()->setSnapState(m_snapEngine->snapState());
        m_wta->service()->setSnapEngine(m_snapEngine);
        // setEngines is what installs the exclusion query provider, the float
        // predicate and the placement resolver onto the snap engine.
        m_wta->setEngines(m_snapEngine, nullptr, nullptr);

        m_testLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_testLayout);
        m_layoutManager->setActiveLayout(m_testLayout);

        m_zoneIds.clear();
        for (PhosphorZones::Zone* z : m_testLayout->zones()) {
            m_zoneIds.append(z->id().toString());
        }

        m_screenId = QStringLiteral("DP-1");
    }

    void cleanup()
    {
        // Detach the borrowed engine from the service BEFORE deleting it so the
        // service never holds a dangling SnapEngine*.
        m_wta->service()->setSnapState(nullptr);
        m_wta->service()->setSnapEngine(nullptr);
        delete m_snapEngine;
        m_snapEngine = nullptr;
        delete m_parent; // owns the WTA
        m_parent = nullptr;
        m_wta = nullptr;
        delete m_desktopManager; // borrowed by the WTA, so it outlives it
        m_desktopManager = nullptr;
        delete m_zoneDetector;
        m_zoneDetector = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        m_testLayout = nullptr;
        m_zoneIds.clear();
        m_guard.reset();
    }

private:
    /// Record a per-output current desktop for the fixture screen that DIFFERS
    /// from the global one, on both the daemon-side manager (which
    /// buildContextualRuleQuery reads) and the registry mirror (which the
    /// registry's own screen-level resolvers read), and return it. Every
    /// ActiveLayout case pins its assignment at this desktop, so a query that
    /// silently fell back to the global desktop would find no assignment.
    int pinPerOutputDesktop()
    {
        const int desktop = m_desktopManager->currentDesktop() + 2;
        m_desktopManager->updateScreenDesktop(m_screenId, desktop);
        // The registry resolves per-screen desktops through an injected
        // provider (one authority, no push-updated mirror to lag) — wire it to
        // the same manager the WTA reads, mirroring the daemon's own wiring.
        m_layoutManager->setCurrentVirtualDesktopProvider([this](const QString& screenId) -> std::optional<int> {
            const int d = m_desktopManager->currentDesktopForScreen(screenId);
            return d >= 1 ? std::optional<int>(d) : std::nullopt;
        });
        return desktop;
    }

private Q_SLOTS:
    // =====================================================================
    // Open-path screen routing (bare RouteToScreen)
    // =====================================================================

    // A BARE RouteToScreen rule (no SnapToZone) must move the opening window to
    // the target monitor on the snap open path: applyOpenScreenRouting translates
    // the window's frame onto the target screen's available area and emits the
    // output-move marker plus a FREE applyGeometryRequested (empty zone id). A
    // local WTA wired to a deterministic two-screen ScreenManager is used because
    // the shared fixture has a null ScreenManager.
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
    }

    // A rule carrying BOTH SnapToZone and RouteToScreen is still moved by
    // applyOpenScreenRouting. Its only caller reaches it exclusively under
    // `if (!result.shouldSnap)`, so by construction the snap already declined and
    // nothing has been placed. calculateSnapToPlacementRule declines for a routed
    // target that is not in snapping mode (typically an autotile monitor), and the
    // autotile routing hook does not pick those up either, because the effect only
    // reports windowOpened for windows already on an autotile screen. Nothing here
    // can double-place, because reaching this function means nothing was placed.
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

        wta->applyOpenScreenRouting(w, QStringLiteral("DP-1"));

        // Exactly one output move: a route+snap rule whose snap declined must
        // still be routed to the target monitor, and only once.
        QCOMPARE(outputSpy.count(), 1);
        QCOMPARE(outputSpy.first().at(1).toString(), QStringLiteral("DP-2"));
        QVERIFY2(!geomSpy.isEmpty(), "the route must carry the translated geometry onto the target monitor");
    }

    // =====================================================================
    // ActiveLayout (discussion #919)
    // =====================================================================

    // A SnapToZone rule matching on ActiveLayout must resolve on the per-window
    // open path. WindowRegistry metadata carries no screen, so ActiveLayout stayed
    // empty on every per-window query and the leaf compared "" against the authored
    // layout id. buildContextualRuleQuery now stamps it from the layout assigned to
    // the screen the window is opening on.
    void testPlacementZonesByRule_activeLayoutMatch_resolvesZones()
    {
        // Pin the PER-OUTPUT desktop, not the global one. If the fixture left the
        // per-output value unrecorded, currentDesktopForScreen would fall back to
        // the same global desktop, and the test would pass just as well against
        // the global accessor — proving nothing about the per-output resolution
        // the stamp depends on. A per-output value that DIFFERS from the global
        // one makes the two distinguishable.
        const int perOutputDesktop = pinPerOutputDesktop();
        QVERIFY2(m_desktopManager->currentDesktopForScreen(m_screenId) != m_desktopManager->currentDesktop(),
                 "fixture must make the per-output and global desktops differ, or this test cannot discriminate");
        m_layoutManager->assignLayout(m_screenId, perOutputDesktop, QString(), m_testLayout);

        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        m_wta->setWindowMetadata(QStringLiteral("inst9"), QStringLiteral("codeapp"), QString(), QString(), QString(), 0,
                                 0, QString(), 0, QVariantMap());

        using namespace PhosphorRules;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::ActiveLayout, Operator::Equals, m_testLayout->id().toString());
        RuleAction snapTo;
        snapTo.type = QString(ActionType::SnapToZone);
        snapTo.params.insert(QString(ActionParam::Zones), QJsonArray{1});
        rule.actions = {snapTo};
        RuleStore store(ConfigDefaults::rulesFilePath(), m_parent);
        QVERIFY(store.addRule(rule));
        m_wta->setRuleStore(&store);
        const auto teardown = qScopeGuard([this] {
            m_wta->setRuleStore(nullptr);
            m_wta->setWindowRegistry(nullptr);
        });

        const PhosphorSnapEngine::PlacementDirective directive =
            m_wta->placementZonesByRule(QStringLiteral("codeapp|inst9"), m_screenId);
        QCOMPARE(directive.zoneOrdinals, QList<int>{1});
    }

    // The zone-NAME form of SnapToZone (discussion #924): the directive carries
    // the names beside the ordinals, trimmed, with blank entries dropped the way
    // out-of-range ordinals are, and an empty ordinal array beside real names is
    // a legitimate "names only" directive.
    void testPlacementZonesByRule_zoneNames_carriedIntoDirective()
    {
        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        m_wta->setWindowMetadata(QStringLiteral("inst10"), QStringLiteral("namedapp"), QString(), QString(), QString(),
                                 0, 0, QString(), 0, QVariantMap());

        using namespace PhosphorRules;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("namedapp"));
        RuleAction snapTo;
        snapTo.type = QString(ActionType::SnapToZone);
        snapTo.params.insert(QString(ActionParam::Zones), QJsonArray{});
        snapTo.params.insert(QString(ActionParam::ZoneNames),
                             QJsonArray{QStringLiteral("  Editor "), QStringLiteral("Terminal")});
        rule.actions = {snapTo};
        RuleStore store(ConfigDefaults::rulesFilePath(), m_parent);
        QVERIFY(store.addRule(rule));
        m_wta->setRuleStore(&store);
        const auto teardown = qScopeGuard([this] {
            m_wta->setRuleStore(nullptr);
            m_wta->setWindowRegistry(nullptr);
        });

        const PhosphorSnapEngine::PlacementDirective directive =
            m_wta->placementZonesByRule(QStringLiteral("namedapp|inst10"), m_screenId);
        QVERIFY(directive.zoneOrdinals.isEmpty());
        QCOMPARE(directive.zoneNames, (QStringList{QStringLiteral("Editor"), QStringLiteral("Terminal")}));
    }

    // The negative half of the pair: the SAME rule against a screen carrying a
    // DIFFERENT layout must not fire. Guards against the stamp degenerating into
    // "any non-empty layout matches".
    void testPlacementZonesByRule_activeLayoutMismatch_resolvesNothing()
    {
        PhosphorZones::Layout* other = createTestLayout(2, m_layoutManager);
        other->setName(QStringLiteral("OtherLayout"));
        m_layoutManager->addLayout(other);
        const int perOutputDesktop = pinPerOutputDesktop();
        QVERIFY2(m_desktopManager->currentDesktopForScreen(m_screenId) != m_desktopManager->currentDesktop(),
                 "fixture must make the per-output and global desktops differ, or this test cannot discriminate");
        m_layoutManager->assignLayout(m_screenId, perOutputDesktop, QString(), other);

        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        m_wta->setWindowMetadata(QStringLiteral("inst10"), QStringLiteral("codeapp"), QString(), QString(), QString(),
                                 0, 0, QString(), 0, QVariantMap());

        using namespace PhosphorRules;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::ActiveLayout, Operator::Equals, m_testLayout->id().toString());
        RuleAction snapTo;
        snapTo.type = QString(ActionType::SnapToZone);
        snapTo.params.insert(QString(ActionParam::Zones), QJsonArray{1});
        rule.actions = {snapTo};
        RuleStore store(ConfigDefaults::rulesFilePath(), m_parent);
        QVERIFY(store.addRule(rule));
        m_wta->setRuleStore(&store);
        const auto teardown = qScopeGuard([this] {
            m_wta->setRuleStore(nullptr);
            m_wta->setWindowRegistry(nullptr);
        });

        const PhosphorSnapEngine::PlacementDirective directive =
            m_wta->placementZonesByRule(QStringLiteral("codeapp|inst10"), m_screenId);
        QVERIFY2(directive.zoneOrdinals.isEmpty(), "a rule keyed on a different layout must not place the window");
    }

    // An autotile screen's active layout is the "autotile:<algorithm>" id, not a
    // layout uuid, and that is the string a rule authored against a tiled monitor
    // carries. The stamp reads assignmentIdForScreen, which is the same producer
    // the context cascade and the daemon's published snapshot use, so the id shape
    // has to match on this path too.
    void testPlacementZonesByRule_activeLayoutAutotileId_resolvesZones()
    {
        const int perOutputDesktop = pinPerOutputDesktop();
        QVERIFY2(m_desktopManager->currentDesktopForScreen(m_screenId) != m_desktopManager->currentDesktop(),
                 "fixture must make the per-output and global desktops differ, or this test cannot discriminate");
        m_layoutManager->assignLayoutById(m_screenId, perOutputDesktop, QString(), QStringLiteral("autotile:bsp"));
        QCOMPARE(m_layoutManager->assignmentIdForScreen(m_screenId, perOutputDesktop, QString()),
                 QStringLiteral("autotile:bsp"));

        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        m_wta->setWindowMetadata(QStringLiteral("inst12"), QStringLiteral("tiledapp"), QString(), QString(), QString(),
                                 0, 0, QString(), 0, QVariantMap());

        using namespace PhosphorRules;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::ActiveLayout, Operator::Equals, QStringLiteral("autotile:bsp"));
        RuleAction snapTo;
        snapTo.type = QString(ActionType::SnapToZone);
        snapTo.params.insert(QString(ActionParam::Zones), QJsonArray{1});
        rule.actions = {snapTo};
        RuleStore store(ConfigDefaults::rulesFilePath(), m_parent);
        QVERIFY(store.addRule(rule));
        m_wta->setRuleStore(&store);
        const auto teardown = qScopeGuard([this] {
            m_wta->setRuleStore(nullptr);
            m_wta->setWindowRegistry(nullptr);
        });

        const PhosphorSnapEngine::PlacementDirective directive =
            m_wta->placementZonesByRule(QStringLiteral("tiledapp|inst12"), m_screenId);
        QCOMPARE(directive.zoneOrdinals, QList<int>{1});
    }

    // The UNHINTED path. The four restore/float resolvers pass no screen hint, and
    // so does the snap engine's exclusion provider when the caller asks about an
    // already-tracked window, so buildContextualRuleQuery resolves the screen
    // itself via resolveScreenForWindow.
    //
    // Scope, stated so nobody reads more into a green run than it earns: this
    // window IS in snap state, which is the case the snap-only service accessor
    // already covered, so the test pins that the unhinted branch stamps at all. It
    // does NOT pin the engine fallbacks resolveScreenForWindow adds for
    // autotile-tracked and not-yet-placed windows. Pinning those needs a fixture
    // with a live autotile engine tracking the window, which this one does not
    // wire.
    void testShouldFloatByRule_activeLayoutMatch_resolvesWithoutScreenHint()
    {
        const int perOutputDesktop = pinPerOutputDesktop();
        QVERIFY2(m_desktopManager->currentDesktopForScreen(m_screenId) != m_desktopManager->currentDesktop(),
                 "fixture must make the per-output and global desktops differ, or this test cannot discriminate");
        m_layoutManager->assignLayout(m_screenId, perOutputDesktop, QString(), m_testLayout);

        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        m_wta->setWindowMetadata(QStringLiteral("inst11"), QStringLiteral("floatapp"), QString(), QString(), QString(),
                                 0, 0, QString(), 0, QVariantMap());

        // Give the service a snap record so the unhinted screen lookup resolves.
        const QString windowId = QStringLiteral("floatapp|inst11");
        // Keyed by the COMPOSITE id, which is what the engine passes to the
        // predicates and therefore what screenForWindow is asked about. Assigning
        // by the bare instance id instead would leave the lookup empty and the
        // test would pass for the wrong reason (or, as here, fail).
        m_wta->service()->assignWindowToZone(windowId, m_zoneIds[0], m_screenId, 1);

        using namespace PhosphorRules;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::ActiveLayout, Operator::Equals, m_testLayout->id().toString());
        RuleAction floatAction;
        floatAction.type = QString(ActionType::Float);
        rule.actions = {floatAction};
        RuleStore store(ConfigDefaults::rulesFilePath(), m_parent);
        QVERIFY(store.addRule(rule));
        m_wta->setRuleStore(&store);
        const auto teardown = qScopeGuard([this] {
            m_wta->setRuleStore(nullptr);
            m_wta->setWindowRegistry(nullptr);
        });

        QVERIFY2(m_wta->shouldFloatByRule(windowId),
                 "an ActiveLayout-keyed Float rule must resolve on the unhinted path too");
    }

    // No screen resolvable at all: no hint, and the window is in no engine's
    // tracker. ScreenId and ActiveLayout are non-optional context fields, so
    // WindowQuery::valueForField would report them ENGAGED-but-empty rather
    // than absent, and a negated leaf would then match every window. The
    // admission tests in rules.cpp (admitNothingStamped) close exactly that
    // inversion: a rule referencing an unstamped screen-derived field is held
    // out of the resolve in BOTH polarities. Pin the negated half so a future
    // change to the stamp's empty-screen branch is a deliberate one.
    void testShouldFloatByRule_unresolvableScreen_negatedLeafHeldOut()
    {
        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        m_wta->setWindowMetadata(QStringLiteral("inst13"), QStringLiteral("nowhereapp"), QString(), QString(),
                                 QString(), 0, 0, QString(), 0, QVariantMap());

        using namespace PhosphorRules;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        // The "is not" shape a user authors: a None (NOT-any) node over an
        // Equals leaf. There is no NotEquals operator.
        rule.match = MatchExpression::makeNone(
            {MatchExpression::makeLeaf(Field::ActiveLayout, Operator::Equals, m_testLayout->id().toString())});
        RuleAction floatAction;
        floatAction.type = QString(ActionType::Float);
        rule.actions = {floatAction};
        RuleStore store(ConfigDefaults::rulesFilePath(), m_parent);
        QVERIFY(store.addRule(rule));
        m_wta->setRuleStore(&store);
        const auto teardown = qScopeGuard([this] {
            m_wta->setRuleStore(nullptr);
            m_wta->setWindowRegistry(nullptr);
        });

        // No assignWindowToZone, no engine tracking, no hint — nothing stamps a
        // screen, so the admission filter refuses the ActiveLayout-referencing
        // rule outright instead of letting the engaged-empty field invert it.
        const QString windowId = QStringLiteral("nowhereapp|inst13");
        QVERIFY2(!m_wta->shouldFloatByRule(windowId),
                 "an unresolvable screen must hold an ActiveLayout-negating rule out of the resolve entirely — "
                 "letting the engaged-empty field through would float every window");
    }

    // =====================================================================
    // ScreenOrientation
    // =====================================================================

    // ScreenOrientation was dead on the window path for the same reason
    // ActiveLayout was: nothing stamped it here, and the effect resolves only
    // appearance actions, so an open-path rule pairing an orientation leaf with a
    // placement action matched nothing. The stamp reads the registry's orientation
    // provider, which the daemon derives from the screen's geometry.
    void testPlacementZonesByRule_screenOrientationMatch_resolvesZones()
    {
        // Portrait geometry for the window's screen, landscape elsewhere — derived
        // the way the daemon derives it, so the token is not hand-written here.
        const QString screenId = m_screenId;
        m_layoutManager->setScreenOrientationProvider([screenId](const QString& queried) -> std::optional<QString> {
            const QRect geom = queried == screenId ? QRect(0, 0, 1080, 1920) : QRect(0, 0, 1920, 1080);
            return geom.height() > geom.width() ? QStringLiteral("portrait") : QStringLiteral("landscape");
        });

        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        m_wta->setWindowMetadata(QStringLiteral("inst14"), QStringLiteral("portraitapp"), QString(), QString(),
                                 QString(), 0, 0, QString(), 0, QVariantMap());

        using namespace PhosphorRules;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::ScreenOrientation, Operator::Equals, QStringLiteral("portrait"));
        RuleAction snapTo;
        snapTo.type = QString(ActionType::SnapToZone);
        snapTo.params.insert(QString(ActionParam::Zones), QJsonArray{2});
        rule.actions = {snapTo};
        RuleStore store(ConfigDefaults::rulesFilePath(), m_parent);
        QVERIFY(store.addRule(rule));
        m_wta->setRuleStore(&store);
        const auto teardown = qScopeGuard([this] {
            m_wta->setRuleStore(nullptr);
            m_wta->setWindowRegistry(nullptr);
        });

        const PhosphorSnapEngine::PlacementDirective directive =
            m_wta->placementZonesByRule(QStringLiteral("portraitapp|inst14"), m_screenId);
        QCOMPARE(directive.zoneOrdinals, QList<int>{2});
    }

    // The landscape half: the same rule against a landscape screen must not fire,
    // so the stamp is not degenerating into "any orientation matches".
    void testPlacementZonesByRule_screenOrientationMismatch_resolvesNothing()
    {
        m_layoutManager->setScreenOrientationProvider([](const QString&) -> std::optional<QString> {
            const QRect geom(0, 0, 1920, 1080);
            return geom.height() > geom.width() ? QStringLiteral("portrait") : QStringLiteral("landscape");
        });

        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        m_wta->setWindowMetadata(QStringLiteral("inst15"), QStringLiteral("portraitapp"), QString(), QString(),
                                 QString(), 0, 0, QString(), 0, QVariantMap());

        using namespace PhosphorRules;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::ScreenOrientation, Operator::Equals, QStringLiteral("portrait"));
        RuleAction snapTo;
        snapTo.type = QString(ActionType::SnapToZone);
        snapTo.params.insert(QString(ActionParam::Zones), QJsonArray{2});
        rule.actions = {snapTo};
        RuleStore store(ConfigDefaults::rulesFilePath(), m_parent);
        QVERIFY(store.addRule(rule));
        m_wta->setRuleStore(&store);
        const auto teardown = qScopeGuard([this] {
            m_wta->setRuleStore(nullptr);
            m_wta->setWindowRegistry(nullptr);
        });

        const PhosphorSnapEngine::PlacementDirective directive =
            m_wta->placementZonesByRule(QStringLiteral("portraitapp|inst15"), m_screenId);
        QVERIFY2(directive.zoneOrdinals.isEmpty(), "a portrait-keyed rule must not fire on a landscape screen");
    }

    // =====================================================================
    // Exclusion query provider (SnapEngine ← WTA)
    // =====================================================================

    // setEngines wires buildContextualRuleQuery in as the snap engine's exclusion
    // query provider, so an Exclude rule keyed on a screen-derived context field
    // resolves for a window the daemon can place a screen on itself. This is the
    // unhinted call shape the navigation actions use.
    void testIsWindowExcluded_activeLayoutRule_resolvesForTrackedWindow()
    {
        const int perOutputDesktop = pinPerOutputDesktop();
        QVERIFY2(m_desktopManager->currentDesktopForScreen(m_screenId) != m_desktopManager->currentDesktop(),
                 "fixture must make the per-output and global desktops differ, or this test cannot discriminate");
        m_layoutManager->assignLayout(m_screenId, perOutputDesktop, QString(), m_testLayout);

        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        m_wta->setWindowMetadata(QStringLiteral("inst16"), QStringLiteral("excludeapp"), QString(), QString(),
                                 QString(), 0, 0, QString(), 0, QVariantMap());

        using namespace PhosphorRules;
        RuleSet excludeSet;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::ActiveLayout, Operator::Equals, m_testLayout->id().toString());
        RuleAction exclude;
        exclude.type = QString(ActionType::Exclude);
        rule.actions = {exclude};
        QVERIFY(excludeSet.addRule(rule));
        m_snapEngine->setExcludeRuleSet(&excludeSet);
        // The borrowed set is a local; drop the borrow before it dies.
        const auto teardown = qScopeGuard([this] {
            m_snapEngine->setExcludeRuleSet(nullptr);
            m_wta->setWindowRegistry(nullptr);
        });

        const QString windowId = QStringLiteral("excludeapp|inst16");
        // Untracked and unhinted: the daemon cannot resolve a screen, so
        // ActiveLayout is empty and the Equals leaf fails.
        QVERIFY2(!m_snapEngine->isWindowExcluded(windowId),
                 "with no screen resolvable the ActiveLayout leaf must not match");

        // Tracked: the service resolves the screen, the provider stamps the
        // layout assigned to it, and the rule fires.
        m_wta->service()->assignWindowToZone(windowId, m_zoneIds[0], m_screenId, 1);
        QVERIFY2(m_snapEngine->isWindowExcluded(windowId),
                 "an ActiveLayout-keyed Exclude rule must resolve through the provider's own screen lookup");
    }

    // The HINTED call shape, which is the one the open path needs: a window being
    // restored is in no SnapState yet, so the daemon's own screen resolution comes
    // back empty and only the caller's hint can carry the screen the window is
    // opening on.
    void testIsWindowExcluded_screenHintResolvesForUntrackedWindow()
    {
        const int perOutputDesktop = pinPerOutputDesktop();
        QVERIFY2(m_desktopManager->currentDesktopForScreen(m_screenId) != m_desktopManager->currentDesktop(),
                 "fixture must make the per-output and global desktops differ, or this test cannot discriminate");
        m_layoutManager->assignLayout(m_screenId, perOutputDesktop, QString(), m_testLayout);

        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        m_wta->setWindowMetadata(QStringLiteral("inst17"), QStringLiteral("openapp"), QString(), QString(), QString(),
                                 0, 0, QString(), 0, QVariantMap());

        using namespace PhosphorRules;
        RuleSet excludeSet;
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = MatchExpression::makeLeaf(Field::ActiveLayout, Operator::Equals, m_testLayout->id().toString());
        RuleAction exclude;
        exclude.type = QString(ActionType::Exclude);
        rule.actions = {exclude};
        QVERIFY(excludeSet.addRule(rule));
        m_snapEngine->setExcludeRuleSet(&excludeSet);
        const auto teardown = qScopeGuard([this] {
            m_snapEngine->setExcludeRuleSet(nullptr);
            m_wta->setWindowRegistry(nullptr);
        });

        const QString windowId = QStringLiteral("openapp|inst17");
        QVERIFY2(!m_snapEngine->isWindowExcluded(windowId),
                 "the same window without a hint has no screen, so the rule must stay inert");
        QVERIFY2(m_snapEngine->isWindowExcluded(windowId, m_screenId),
                 "the caller's screen hint must reach the stamp and resolve the rule");
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    QObject* m_parent = nullptr;
    WindowTrackingAdaptor* m_wta = nullptr;
    PhosphorWorkspaces::VirtualDesktopManager* m_desktopManager = nullptr;
    SnapEngine* m_snapEngine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
    QString m_screenId;
};

QTEST_MAIN(TestWtaRuleContext)
#include "test_wta_rule_context.moc"
