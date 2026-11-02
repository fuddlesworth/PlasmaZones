// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_rule_context.cpp
 * @brief WindowTrackingAdaptor rule-context coverage: the screen-derived
 *        context fields buildContextualRuleQuery stamps (ScreenId,
 *        ActiveLayout, and the screen shape: orientation, width, height,
 *        aspect ratio), the refusal of a rule negating a shape field the
 *        screen cannot answer yet, and the consumers that read them
 *        (placementZonesByRule, shouldFloatByRule, the snap engine's exclusion
 *        query provider). The open-path screen routing that runs when nothing
 *        snapped the window is covered by test_wta_routing.cpp.
 */

#include <QTest>
#include <QJsonArray>
#include <QRect>
#include <QScopeGuard>
#include <QSize>
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
        // Detach the rule store and registry installRule borrowed into the WTA
        // before either dies (the store is a member, the registry a child of
        // m_parent).
        m_wta->setRuleStore(nullptr);
        m_wta->setWindowRegistry(nullptr);
        m_store.reset();
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
    /// Register @p appId under @p instanceId, install ONE enabled rule matching
    /// @p match with @p action into a fresh store, and point the WTA at it.
    /// Returns false when the store refuses the rule so the caller can QVERIFY
    /// it and stop the slot (a QVERIFY inside a helper returns only from the
    /// helper). The store is a member so it outlives the slot body; cleanup()
    /// detaches it from the WTA before destroying either.
    bool installRule(const QString& instanceId, const QString& appId, const PhosphorRules::MatchExpression& match,
                     const PhosphorRules::RuleAction& action)
    {
        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        m_wta->setWindowMetadata(instanceId, appId, QString(), QString(), QString(), 0, 0, QString(), 0, QVariantMap());
        PhosphorRules::Rule rule;
        rule.id = QUuid::createUuid();
        rule.enabled = true;
        rule.match = match;
        rule.actions = {action};
        m_store = std::make_unique<PhosphorRules::RuleStore>(ConfigDefaults::rulesFilePath(), nullptr);
        if (!m_store->addRule(rule)) {
            return false;
        }
        m_wta->setRuleStore(m_store.get());
        return true;
    }

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

    /// Give the fixture screen a size in device pixels through the registry's
    /// size provider, the hook the daemon wires to the compositor's report.
    /// Every other screen stays unknown.
    void setPixelSize(const QSize& size)
    {
        const QString screenId = m_screenId;
        m_layoutManager->setScreenPixelSizeProvider([screenId, size](const QString& queried) -> std::optional<QSize> {
            return queried == screenId ? std::optional<QSize>(size) : std::nullopt;
        });
    }

    /// A SnapToZone action targeting the 1-based zone @p ordinal.
    static PhosphorRules::RuleAction snapTo(int ordinal)
    {
        PhosphorRules::RuleAction action;
        action.type = QString(PhosphorRules::ActionType::SnapToZone);
        action.params.insert(QString(PhosphorRules::ActionParam::Zones), QJsonArray{ordinal});
        return action;
    }

private Q_SLOTS:
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

        using namespace PhosphorRules;
        RuleAction snapTo;
        snapTo.type = QString(ActionType::SnapToZone);
        snapTo.params.insert(QString(ActionParam::Zones), QJsonArray{1});
        QVERIFY(installRule(
            QStringLiteral("inst9"), QStringLiteral("codeapp"),
            MatchExpression::makeLeaf(Field::ActiveLayout, Operator::Equals, m_testLayout->id().toString()), snapTo));

        const PhosphorSnapEngine::PlacementDirective directive =
            m_wta->placementZonesByRule(QStringLiteral("codeapp|inst9"), m_screenId);
        QCOMPARE(directive.zoneOrdinals, QList<int>{1});
    }

    // The zone-NAME form of SnapToZone (discussion #924): the directive carries
    // the names beside the ordinals, trimmed, and an empty ordinal array beside
    // real names is a legitimate "names only" directive. (A blank name never
    // reaches this path through a store: Rule::isValid runs the descriptor
    // validator, which refuses it; the reader's own blank-drop is a defensive
    // re-validation against a future loader change, not a reachable branch.)
    void testPlacementZonesByRule_zoneNames_carriedIntoDirective()
    {
        using namespace PhosphorRules;
        RuleAction snapTo;
        snapTo.type = QString(ActionType::SnapToZone);
        snapTo.params.insert(QString(ActionParam::Zones), QJsonArray{});
        snapTo.params.insert(QString(ActionParam::ZoneNames),
                             QJsonArray{QStringLiteral("  Editor "), QStringLiteral("Terminal")});
        QVERIFY(installRule(QStringLiteral("inst10"), QStringLiteral("namedapp"),
                            MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("namedapp")),
                            snapTo));

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

        using namespace PhosphorRules;
        RuleAction snapTo;
        snapTo.type = QString(ActionType::SnapToZone);
        snapTo.params.insert(QString(ActionParam::Zones), QJsonArray{1});
        QVERIFY(installRule(
            QStringLiteral("inst10"), QStringLiteral("codeapp"),
            MatchExpression::makeLeaf(Field::ActiveLayout, Operator::Equals, m_testLayout->id().toString()), snapTo));

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

        using namespace PhosphorRules;
        RuleAction snapTo;
        snapTo.type = QString(ActionType::SnapToZone);
        snapTo.params.insert(QString(ActionParam::Zones), QJsonArray{1});
        QVERIFY(installRule(
            QStringLiteral("inst12"), QStringLiteral("tiledapp"),
            MatchExpression::makeLeaf(Field::ActiveLayout, Operator::Equals, QStringLiteral("autotile:bsp")), snapTo));

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

        using namespace PhosphorRules;
        RuleAction floatAction;
        floatAction.type = QString(ActionType::Float);
        QVERIFY(
            installRule(QStringLiteral("inst11"), QStringLiteral("floatapp"),
                        MatchExpression::makeLeaf(Field::ActiveLayout, Operator::Equals, m_testLayout->id().toString()),
                        floatAction));

        // Give the service a snap record so the unhinted screen lookup resolves.
        const QString windowId = QStringLiteral("floatapp|inst11");
        // Keyed by the COMPOSITE id, which is what the engine passes to the
        // predicates and therefore what screenForWindow is asked about. Assigning
        // by the bare instance id instead would leave the lookup empty and the
        // test would pass for the wrong reason (or, as here, fail).
        m_wta->service()->assignWindowToZone(windowId, m_zoneIds[0], m_screenId, 1);

        QVERIFY2(m_wta->shouldFloatByRule(windowId),
                 "an ActiveLayout-keyed Float rule must resolve on the unhinted path too");
    }

    // No screen resolvable at all: no hint, and the window is in no engine's
    // tracker. ScreenId and ActiveLayout are non-optional context fields, so
    // WindowQuery::valueForField would report them ENGAGED-but-empty rather
    // than absent, and a negated leaf would then match every window. The
    // admission tests in rules_admission.h (admitNothingStamped) close exactly that
    // inversion: a rule referencing an unstamped screen-derived field is held
    // out of the resolve in BOTH polarities. Pin the negated half so a future
    // change to the stamp's empty-screen branch is a deliberate one.
    void testShouldFloatByRule_unresolvableScreen_negatedLeafHeldOut()
    {
        using namespace PhosphorRules;
        RuleAction floatAction;
        floatAction.type = QString(ActionType::Float);
        // The "is not" shape a user authors: a None (NOT-any) node over an
        // Equals leaf. There is no NotEquals operator.
        QVERIFY(installRule(QStringLiteral("inst13"), QStringLiteral("nowhereapp"),
                            MatchExpression::makeNone({MatchExpression::makeLeaf(Field::ActiveLayout, Operator::Equals,
                                                                                 m_testLayout->id().toString())}),
                            floatAction));

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

        using namespace PhosphorRules;
        RuleAction snapTo;
        snapTo.type = QString(ActionType::SnapToZone);
        snapTo.params.insert(QString(ActionParam::Zones), QJsonArray{2});
        QVERIFY(installRule(
            QStringLiteral("inst14"), QStringLiteral("portraitapp"),
            MatchExpression::makeLeaf(Field::ScreenOrientation, Operator::Equals, QStringLiteral("portrait")), snapTo));

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

        using namespace PhosphorRules;
        RuleAction snapTo;
        snapTo.type = QString(ActionType::SnapToZone);
        snapTo.params.insert(QString(ActionParam::Zones), QJsonArray{2});
        QVERIFY(installRule(
            QStringLiteral("inst15"), QStringLiteral("portraitapp"),
            MatchExpression::makeLeaf(Field::ScreenOrientation, Operator::Equals, QStringLiteral("portrait")), snapTo));

        const PhosphorSnapEngine::PlacementDirective directive =
            m_wta->placementZonesByRule(QStringLiteral("portraitapp|inst15"), m_screenId);
        QVERIFY2(directive.zoneOrdinals.isEmpty(), "a portrait-keyed rule must not fire on a landscape screen");
    }

    // =====================================================================
    // Screen size and aspect ratio
    // =====================================================================

    // The size fields ride the same stamp as the orientation: the registry's
    // pixel-size provider, which the daemon wires to the compositor's report.
    // A super-ultrawide here, 5120x1440 on the window's screen only.
    void testPlacementZonesByRule_screenAspectRatioMatch_resolvesZones()
    {
        setPixelSize(QSize(5120, 1440));

        using namespace PhosphorRules;
        QVERIFY(installRule(
            QStringLiteral("inst20"), QStringLiteral("wideapp"),
            MatchExpression::makeLeaf(Field::ScreenAspectRatio, Operator::Equals, QStringLiteral("32:9")), snapTo(3)));

        const PhosphorSnapEngine::PlacementDirective directive =
            m_wta->placementZonesByRule(QStringLiteral("wideapp|inst20"), m_screenId);
        QCOMPARE(directive.zoneOrdinals, QList<int>{3});
    }

    // The same rule on a 16:9 screen: the token is derived from the size, not
    // matched by anything engaged.
    void testPlacementZonesByRule_screenAspectRatioMismatch_resolvesNothing()
    {
        setPixelSize(QSize(2560, 1440));

        using namespace PhosphorRules;
        QVERIFY(installRule(
            QStringLiteral("inst21"), QStringLiteral("wideapp"),
            MatchExpression::makeLeaf(Field::ScreenAspectRatio, Operator::Equals, QStringLiteral("32:9")), snapTo(3)));

        const PhosphorSnapEngine::PlacementDirective directive =
            m_wta->placementZonesByRule(QStringLiteral("wideapp|inst21"), m_screenId);
        QVERIFY2(directive.zoneOrdinals.isEmpty(), "a 32:9-keyed rule must not fire on a 16:9 screen");
    }

    void testPlacementZonesByRule_screenWidthMatch_resolvesZones()
    {
        setPixelSize(QSize(5120, 1440));

        using namespace PhosphorRules;
        QVERIFY(installRule(QStringLiteral("inst22"), QStringLiteral("wideapp"),
                            MatchExpression::makeLeaf(Field::ScreenWidth, Operator::GreaterThan, 3000), snapTo(2)));

        const PhosphorSnapEngine::PlacementDirective directive =
            m_wta->placementZonesByRule(QStringLiteral("wideapp|inst22"), m_screenId);
        QCOMPARE(directive.zoneOrdinals, QList<int>{2});
    }

    void testPlacementZonesByRule_screenWidthMismatch_resolvesNothing()
    {
        // The same monitor in its half-width split mode.
        setPixelSize(QSize(2560, 1440));

        using namespace PhosphorRules;
        QVERIFY(installRule(QStringLiteral("inst23"), QStringLiteral("wideapp"),
                            MatchExpression::makeLeaf(Field::ScreenWidth, Operator::GreaterThan, 3000), snapTo(2)));

        const PhosphorSnapEngine::PlacementDirective directive =
            m_wta->placementZonesByRule(QStringLiteral("wideapp|inst23"), m_screenId);
        QVERIFY2(directive.zoneOrdinals.isEmpty(), "a width-keyed rule must not fire on a narrower screen");
    }

    // A stamped screen whose size the daemon has not learned yet (no report,
    // no last-session record) leaves the size fields unanswered. A positive
    // leaf is then false, but `None{ScreenWidth Equals 5120}` would match every
    // window, so the admission refuses a rule that negates an unanswered shape
    // field. The screen IS stamped here (the hint), which is what the
    // screen-derived exclusion of admitNothingStamped does not cover.
    void testShouldFloatByRule_unknownShape_negatedSizeLeafHeldOut()
    {
        using namespace PhosphorRules;
        RuleAction floatAction;
        floatAction.type = QString(ActionType::Float);
        QVERIFY(installRule(
            QStringLiteral("inst24"), QStringLiteral("sizeapp"),
            MatchExpression::makeNone({MatchExpression::makeLeaf(Field::ScreenWidth, Operator::Equals, 5120)}),
            floatAction));
        const QString windowId = QStringLiteral("sizeapp|inst24");

        QVERIFY2(!m_wta->shouldFloatByRule(windowId, m_screenId),
                 "a rule negating the width of a screen whose size is unknown must be refused, not match every window");

        // Discriminator: once the size is known the same rule is legitimate,
        // and a 2560-wide screen is indeed not 5120 wide.
        setPixelSize(QSize(2560, 1440));
        QVERIFY2(m_wta->shouldFloatByRule(windowId, m_screenId),
                 "with the size known, a negated width rule is admitted and judged on the real width");
    }

    // The same refusal on the aspect token, whose unknown form is an EMPTY
    // string rather than a disengaged value.
    void testShouldFloatByRule_unknownShape_negatedAspectLeafHeldOut()
    {
        using namespace PhosphorRules;
        RuleAction floatAction;
        floatAction.type = QString(ActionType::Float);
        QVERIFY(installRule(QStringLiteral("inst25"), QStringLiteral("aspectapp"),
                            MatchExpression::makeNone({MatchExpression::makeLeaf(
                                Field::ScreenAspectRatio, Operator::Equals, QStringLiteral("32:9"))}),
                            floatAction));
        const QString windowId = QStringLiteral("aspectapp|inst25");

        QVERIFY(!m_wta->shouldFloatByRule(windowId, m_screenId));
        setPixelSize(QSize(2560, 1440));
        QVERIFY(m_wta->shouldFloatByRule(windowId, m_screenId));
    }

    // The placement resolver runs mid-session as well as at open (the Meta+F
    // unfloat's rule tier, Snap.snapToAppRule). The shared evaluator memo holds
    // the OPEN-time verdict, so a later call must resolve fresh: after the
    // monitor rotates, a portrait-keyed SnapToZone rule must stop answering.
    void testPlacementZonesByRule_midSession_resolvesTheCurrentShape()
    {
        setPixelSize(QSize(1440, 2560));

        using namespace PhosphorRules;
        QVERIFY(installRule(
            QStringLiteral("inst26"), QStringLiteral("rotateapp"),
            MatchExpression::makeLeaf(Field::ScreenOrientation, Operator::Equals, QStringLiteral("portrait")),
            snapTo(2)));
        const QString windowId = QStringLiteral("rotateapp|inst26");

        // The open: seeds the memo with the portrait verdict.
        QCOMPARE(m_wta->placementZonesByRule(windowId, m_screenId).zoneOrdinals, QList<int>{2});

        // The monitor rotates back to landscape. The memo still holds the
        // portrait verdict (the rule revision did not move), so the later
        // call has to look past it.
        setPixelSize(QSize(2560, 1440));
        QVERIFY2(m_wta->placementZonesByRule(windowId, m_screenId).zoneOrdinals.isEmpty(),
                 "a mid-session placement resolve must read the screen's current shape, not the open-time verdict");
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

    // The effect's seed for a returning output reaches the desktop manager as
    // a seed, never as a switch, and invalid input is refused (F700), a desktop
    // past the last one included (F81; this manager has one desktop).
    void testSeedScreenDesktop_forwardsAsSeed()
    {
        QSignalSpy switched(m_desktopManager, &PhosphorWorkspaces::VirtualDesktopManager::screenDesktopChanged);
        QSignalSpy seeded(m_desktopManager, &PhosphorWorkspaces::VirtualDesktopManager::screenDesktopSeeded);
        m_wta->seedScreenDesktop(m_screenId, 1);
        QCOMPARE(seeded.count(), 1);
        QCOMPARE(switched.count(), 0);
        QCOMPARE(m_desktopManager->currentDesktopForScreen(m_screenId), 1);
        m_wta->seedScreenDesktop(QString(), 1);
        m_wta->seedScreenDesktop(m_screenId, 0);
        m_wta->seedScreenDesktop(m_screenId, 2);
        QCOMPARE(seeded.count(), 1);
    }

    // A screen switch to a desktop past the last one is refused, and a
    // metadata push naming one reads as unknown (F81; one desktop here).
    void testDesktopPastTheLastIsRefused()
    {
        QSignalSpy switched(m_desktopManager, &PhosphorWorkspaces::VirtualDesktopManager::screenDesktopChanged);
        m_wta->screenDesktopChanged(m_screenId, 2);
        QCOMPARE(switched.count(), 0);

        auto* registry = new PhosphorEngine::WindowRegistry(m_parent);
        m_wta->setWindowRegistry(registry);
        const auto teardown = qScopeGuard([this] {
            m_wta->setWindowRegistry(nullptr);
        });
        m_wta->setWindowMetadata(QStringLiteral("past-1"), QStringLiteral("app"), QString(), QString(), QString(), 0, 2,
                                 QString(), 0, QVariantMap{{QStringLiteral("width"), 640}});
        QCOMPARE(registry->metadata(QStringLiteral("past-1"))->virtualDesktop, 0);
    }

    // A screen's desktop report within the count reaches the desktop manager
    // (F857). A manager that was never started answers a count of one (its
    // KWin read is off), so the report names desktop 1, which the screen had
    // no entry for yet.
    void testScreenDesktopChanged_forwardsToTheDesktopManager()
    {
        QSignalSpy switched(m_desktopManager, &PhosphorWorkspaces::VirtualDesktopManager::screenDesktopChanged);
        m_wta->screenDesktopChanged(m_screenId, 1);
        QCOMPARE(switched.count(), 1);
        QCOMPARE(switched.first().at(0).toString(), m_screenId);
        QCOMPARE(switched.first().at(1).toInt(), 1);
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
    std::unique_ptr<PhosphorRules::RuleStore> m_store;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
    QString m_screenId;
};

QTEST_MAIN(TestWtaRuleContext)
#include "test_wta_rule_context.moc"
