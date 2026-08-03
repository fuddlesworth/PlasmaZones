// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_screen_mode_router.cpp
 * @brief Direct unit tests for ScreenModeRouter — the single source of truth
 *        for "which engine owns screen X".
 *
 * The router's behaviour is exercised indirectly through the daemon, WTA,
 * and VS swapper test suites, but it's the single point of failure for
 * mode dispatch — every window-lifecycle and resnap path funnels through
 * it. These tests pin the contract directly:
 *
 *  - modeFor() prefers the autotile engine's live set over the layout
 *    cascade's assignment (the whole point of the "trust the engine"
 *    stale-guard).
 *  - engineFor() returns the engine that owns the screen.
 *  - isSnapMode / isAutotileMode are consistent with modeFor.
 *  - partitionByMode splits a screen list by mode, preserving input order.
 *  - the scrolling engine's live set is consulted the same way (after
 *    autotile), and a cascade Scrolling/Autotile answer whose engine does
 *    not claim the screen downgrades to Snapping.
 */

#include <QTest>

#include <memory>

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "config/configbackends.h"
#include "core/resolve/screenmoderouter.h"
#include "helpers/AutotileTestHelpers.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
using namespace PhosphorTileEngine;
using namespace PhosphorSnapEngine;

class TestScreenModeRouter : public QObject
{
    Q_OBJECT

private:
    // setAssignmentEntryDirect PERSISTS a rule via RuleStore::save(); without
    // this guard the downgrade test would write DP-7 rules into the shared
    // test-xdg config that every sibling test then loads.
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    SnapEngine* m_snapEngine = nullptr;
    AutotileEngine* m_autotileEngine = nullptr;
    PhosphorScrollEngine::ScrollEngine* m_scrollEngine = nullptr;
    ScreenModeRouter* m_router = nullptr;

private Q_SLOTS:

    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        // PhosphorZones::LayoutRegistry with no backend — every screen hits the default
        // modeForScreen fallback (Snapping unless explicitly assigned).
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));

        // SnapEngine with all-nullptr dependencies is a valid construction:
        // the router only invokes it via the PhosphorEngine::IPlacementEngine interface
        // (which ScreenModeRouter doesn't actually call — it returns the
        // pointer so callers can dispatch). Matches test_snap_engine.cpp's
        // headless stub pattern.
        m_snapEngine = new SnapEngine(nullptr, nullptr, nullptr, nullptr);

        // AutotileEngine with all-nullptr dependencies is explicitly
        // supported for headless tests — see the comment at the top of
        // AutotileEngine::AutotileEngine (src/autotile/AutotileEngine.cpp).
        // The algorithm registry is non-null even in headless mode because
        // the engine resolves an initial algorithm in its constructor;
        // share the test-process registry.
        m_autotileEngine = new AutotileEngine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        // ScrollEngine with null dependencies mirrors the two stubs above —
        // the router only reads isActiveOnScreen from it.
        m_scrollEngine = new PhosphorScrollEngine::ScrollEngine(nullptr, nullptr);

        m_router = new ScreenModeRouter(m_layoutManager, m_snapEngine, m_autotileEngine, m_scrollEngine);
    }

    void cleanup()
    {
        delete m_router;
        m_router = nullptr;
        delete m_scrollEngine;
        m_scrollEngine = nullptr;
        delete m_autotileEngine;
        m_autotileEngine = nullptr;
        delete m_snapEngine;
        m_snapEngine = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        m_guard.reset();
    }

    // ─── modeFor ──────────────────────────────────────────────────────────

    void modeFor_unknownScreen_returnsSnapping()
    {
        // No autotile assignments, no layout assignments — the cascade
        // falls through to the default Snapping branch.
        QCOMPARE(m_router->modeFor(QStringLiteral("DP-1")), PhosphorZones::AssignmentEntry::Snapping);
    }

    void modeFor_autotileScreen_returnsAutotile()
    {
        // Seeding the engine's live set is the fast-path branch: modeFor()
        // returns Autotile without consulting the layout cascade at all.
        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-1")});
        QCOMPARE(m_router->modeFor(QStringLiteral("DP-1")), PhosphorZones::AssignmentEntry::Autotile);
    }

    void modeFor_autotileOnlyTargetsNamedScreen()
    {
        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-1")});
        // A different screen is unaffected — it still reports the default
        // Snapping fallback.
        QCOMPARE(m_router->modeFor(QStringLiteral("DP-2")), PhosphorZones::AssignmentEntry::Snapping);
    }

    void modeFor_emptyScreenId_returnsSnapping()
    {
        // Defensive contract, pinned for real: even when BOTH engines'
        // live sets somehow contain the empty string (a corrupt seed), the
        // router still answers Snapping for an empty id — the guard, not
        // the default.
        QCOMPARE(m_router->modeFor(QString()), PhosphorZones::AssignmentEntry::Snapping);
        m_autotileEngine->setAutotileScreens({QString(), QStringLiteral("DP-1")});
        m_scrollEngine->setActiveScreens({QString()});
        QCOMPARE(m_router->modeFor(QString()), PhosphorZones::AssignmentEntry::Snapping);
        m_autotileEngine->setAutotileScreens({});
        m_scrollEngine->setActiveScreens({});
    }

    // ─── engineFor ────────────────────────────────────────────────────────

    void engineFor_snapScreen_returnsSnapEngine()
    {
        PhosphorEngine::IPlacementEngine* engine = m_router->engineFor(QStringLiteral("DP-1"));
        QVERIFY(engine != nullptr);
        QCOMPARE(static_cast<PhosphorEngine::IPlacementEngine*>(m_snapEngine), engine);
    }

    void engineFor_autotileScreen_returnsAutotileEngine()
    {
        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-1")});
        PhosphorEngine::IPlacementEngine* engine = m_router->engineFor(QStringLiteral("DP-1"));
        QVERIFY(engine != nullptr);
        QCOMPARE(static_cast<PhosphorEngine::IPlacementEngine*>(m_autotileEngine), engine);
    }

    void engineFor_respectsLiveAssignmentUpdates()
    {
        // Flip a screen into autotile, then back out, and verify the router
        // tracks the engine's live state — not a stale snapshot from the
        // first call.
        QCOMPARE(m_router->engineFor(QStringLiteral("DP-1")),
                 static_cast<PhosphorEngine::IPlacementEngine*>(m_snapEngine));

        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-1")});
        QCOMPARE(m_router->engineFor(QStringLiteral("DP-1")),
                 static_cast<PhosphorEngine::IPlacementEngine*>(m_autotileEngine));

        m_autotileEngine->setAutotileScreens({});
        QCOMPARE(m_router->engineFor(QStringLiteral("DP-1")),
                 static_cast<PhosphorEngine::IPlacementEngine*>(m_snapEngine));
    }

    // ─── isSnapMode / isAutotileMode ──────────────────────────────────────

    void modePredicates_consistentWithModeFor()
    {
        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-2")});

        QVERIFY(m_router->isSnapMode(QStringLiteral("DP-1")));
        QVERIFY(!m_router->isAutotileMode(QStringLiteral("DP-1")));

        QVERIFY(m_router->isAutotileMode(QStringLiteral("DP-2")));
        QVERIFY(!m_router->isSnapMode(QStringLiteral("DP-2")));
    }

    void modePredicates_areMutuallyExclusive()
    {
        // For any given screen, EXACTLY ONE of {snapping, autotile,
        // scrolling} must hold — derived from modeFor(), whose switch is
        // exhaustive by construction. The old two-predicate xor became a
        // false guard the moment Scrolling landed (both predicates are
        // false on a scrolling screen), so the loop now seeds all three
        // modes and counts.
        const QStringList screens = {QStringLiteral("DP-1"),      QStringLiteral("DP-2"), QStringLiteral("HDMI-1"),
                                     QStringLiteral("phys/vs:0"), QStringLiteral("DP-9"), QString()};
        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-1"), QStringLiteral("phys/vs:0")});
        m_scrollEngine->setActiveScreens({QStringLiteral("DP-9")});

        for (const QString& sid : screens) {
            const bool isSnap = m_router->isSnapMode(sid);
            const bool isAuto = m_router->isAutotileMode(sid);
            const bool isScroll = m_router->isScrollingMode(sid);
            const int claims = int(isSnap) + int(isAuto) + int(isScroll);
            QCOMPARE_EQ(claims, 1);
            // All THREE predicates must agree with modeFor's verdict.
            QCOMPARE(isSnap, m_router->modeFor(sid) == PhosphorZones::AssignmentEntry::Snapping);
            QCOMPARE(isAuto, m_router->modeFor(sid) == PhosphorZones::AssignmentEntry::Autotile);
            QCOMPARE(isScroll, m_router->modeFor(sid) == PhosphorZones::AssignmentEntry::Scrolling);
        }
    }

    // ─── partitionByMode ──────────────────────────────────────────────────

    void partitionByMode_splitsByLiveAutotileSet()
    {
        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-2"), QStringLiteral("HDMI-1")});

        const QStringList input = {QStringLiteral("DP-1"), QStringLiteral("DP-2"), QStringLiteral("HDMI-1"),
                                   QStringLiteral("DP-3")};
        const auto result = m_router->partitionByMode(input);

        QCOMPARE(result.snap, (QStringList{QStringLiteral("DP-1"), QStringLiteral("DP-3")}));
        QCOMPARE(result.autotile, (QStringList{QStringLiteral("DP-2"), QStringLiteral("HDMI-1")}));
        QVERIFY(result.scrolling.isEmpty());
    }

    void partitionByMode_preservesInputOrderPerBucket()
    {
        // The class contract says "preserves input order within each
        // bucket" — a resnap pipeline iterates deterministically so the
        // order that screens get touched is stable across runs.
        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-2"), QStringLiteral("DP-4")});

        const QStringList input = {QStringLiteral("DP-4"), QStringLiteral("DP-3"), QStringLiteral("DP-2"),
                                   QStringLiteral("DP-1")};
        const auto result = m_router->partitionByMode(input);

        QCOMPARE(result.snap, (QStringList{QStringLiteral("DP-3"), QStringLiteral("DP-1")}));
        QCOMPARE(result.autotile, (QStringList{QStringLiteral("DP-4"), QStringLiteral("DP-2")}));
        QVERIFY(result.scrolling.isEmpty());
    }

    void partitionByMode_emptyInput_returnsEmptyBuckets()
    {
        const auto result = m_router->partitionByMode({});
        QVERIFY(result.snap.isEmpty());
        QVERIFY(result.autotile.isEmpty());
        QVERIFY(result.scrolling.isEmpty());
    }

    void partitionByMode_allSnap_allAutotileIsEmpty()
    {
        const QStringList input = {QStringLiteral("DP-1"), QStringLiteral("DP-2")};
        const auto result = m_router->partitionByMode(input);
        QCOMPARE(result.snap, input);
        QVERIFY(result.autotile.isEmpty());
        QVERIFY(result.scrolling.isEmpty());
    }

    void partitionByMode_allAutotile_allSnapIsEmpty()
    {
        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-1"), QStringLiteral("DP-2")});
        const QStringList input = {QStringLiteral("DP-1"), QStringLiteral("DP-2")};
        const auto result = m_router->partitionByMode(input);
        QVERIFY(result.snap.isEmpty());
        QVERIFY(result.scrolling.isEmpty());
        QCOMPARE(result.autotile, input);
    }

    // ─── Scrolling engine ─────────────────────────────────────────────────

    void modeFor_scrollingScreen_returnsScrolling()
    {
        // Seeding the scroll engine's live set takes the same fast path the
        // autotile set does: modeFor answers from the engine, no cascade.
        m_scrollEngine->setActiveScreens({QStringLiteral("DP-1")});
        QCOMPARE(m_router->modeFor(QStringLiteral("DP-1")), PhosphorZones::AssignmentEntry::Scrolling);
        QCOMPARE(m_router->engineFor(QStringLiteral("DP-1")),
                 static_cast<PhosphorEngine::IPlacementEngine*>(m_scrollEngine));
        QVERIFY(m_router->isScrollingMode(QStringLiteral("DP-1")));
        QVERIFY(!m_router->isSnapMode(QStringLiteral("DP-1")));
        QVERIFY(!m_router->isAutotileMode(QStringLiteral("DP-1")));
    }

    void engineFor_layoutSupport_followsTheLiveEngine()
    {
        // The daemon's layoutSupportForScreen gates read
        // engineFor(screen)->layoutSupport(); pin the router-level capability
        // answer for all three engines, and that a downgraded scrolling
        // cascade answers Placement (the snap engine's), never Templates.
        using LayoutSupport = PhosphorEngine::IPlacementEngine::LayoutSupport;
        QCOMPARE(m_router->engineFor(QStringLiteral("DP-1"))->layoutSupport(), LayoutSupport::Placement);

        m_scrollEngine->setActiveScreens({QStringLiteral("DP-1")});
        QCOMPARE(m_router->engineFor(QStringLiteral("DP-1"))->layoutSupport(), LayoutSupport::Templates);

        m_scrollEngine->setActiveScreens({});
        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-1")});
        QCOMPARE(m_router->engineFor(QStringLiteral("DP-1"))->layoutSupport(), LayoutSupport::Placement);
        m_autotileEngine->setAutotileScreens({});

        // Cascade Scrolling, engine not claiming: the downgrade must reach
        // the capability too, or the picker would apply templates on a
        // screen the snap engine actually owns.
        PhosphorZones::AssignmentEntry entry;
        entry.mode = PhosphorZones::AssignmentEntry::Scrolling;
        m_layoutManager->setAssignmentEntryDirect(QStringLiteral("DP-6"), 0, QString(), entry);
        QCOMPARE(m_router->engineFor(QStringLiteral("DP-6"))->layoutSupport(), LayoutSupport::Placement);
    }

    void modeFor_cascadeScrollingWithoutEngineClaim_downgradesToSnapping()
    {
        // The cascade says Scrolling but the engine's live set does not
        // claim the screen (disabled engine / mid-transition): the router
        // trusts the engine and downgrades to Snapping, symmetric with the
        // stale-Autotile downgrade.
        PhosphorZones::AssignmentEntry entry;
        entry.mode = PhosphorZones::AssignmentEntry::Scrolling;
        m_layoutManager->setAssignmentEntryDirect(QStringLiteral("DP-7"), 0, QString(), entry);
        QCOMPARE(m_router->modeFor(QStringLiteral("DP-7")), PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(m_router->engineFor(QStringLiteral("DP-7")),
                 static_cast<PhosphorEngine::IPlacementEngine*>(m_snapEngine));
    }

    void modeFor_cascadeAutotileWithoutEngineClaim_downgradesToSnapping()
    {
        // The twin of the Scrolling downgrade above, and the one the comment
        // there calls "symmetric": the cascade says Autotile but the autotile
        // engine's live set does not claim the screen, so the router trusts
        // the engine and falls back to Snapping.
        PhosphorZones::AssignmentEntry entry;
        entry.mode = PhosphorZones::AssignmentEntry::Autotile;
        m_layoutManager->setAssignmentEntryDirect(QStringLiteral("DP-9"), 0, QString(), entry);
        QCOMPARE(m_router->modeFor(QStringLiteral("DP-9")), PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(m_router->engineFor(QStringLiteral("DP-9")),
                 static_cast<PhosphorEngine::IPlacementEngine*>(m_snapEngine));
    }

    void modeFor_bothEnginesClaim_autotileWins()
    {
        // Both live sets claiming one screen is a transition artefact; the
        // router's documented order consults autotile first, so it wins
        // deterministically rather than flapping.
        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-8")});
        m_scrollEngine->setActiveScreens({QStringLiteral("DP-8")});
        QCOMPARE(m_router->modeFor(QStringLiteral("DP-8")), PhosphorZones::AssignmentEntry::Autotile);
        QCOMPARE(m_router->engineFor(QStringLiteral("DP-8")),
                 static_cast<PhosphorEngine::IPlacementEngine*>(m_autotileEngine));
    }

    void partitionByMode_scrollingBucket()
    {
        m_scrollEngine->setActiveScreens({QStringLiteral("DP-2")});
        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-3")});
        const QStringList input = {QStringLiteral("DP-1"), QStringLiteral("DP-2"), QStringLiteral("DP-3")};
        const auto result = m_router->partitionByMode(input);
        QCOMPARE(result.snap, (QStringList{QStringLiteral("DP-1")}));
        QCOMPARE(result.scrolling, (QStringList{QStringLiteral("DP-2")}));
        QCOMPARE(result.autotile, (QStringList{QStringLiteral("DP-3")}));
    }
};

QTEST_MAIN(TestScreenModeRouter)
#include "test_screen_mode_router.moc"
