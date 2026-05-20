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
 */

#include <QTest>

#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "config/configbackends.h"
#include "core/screenmoderouter.h"
#include "../helpers/AutotileTestHelpers.h"
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorScrollEngine/ScrollEngine.h>

using namespace PlasmaZones;
using namespace PhosphorTileEngine;
using namespace PhosphorSnapEngine;

class TestScreenModeRouter : public QObject
{
    Q_OBJECT

private:
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    SnapEngine* m_snapEngine = nullptr;
    AutotileEngine* m_autotileEngine = nullptr;
    PhosphorScrollEngine::ScrollEngine* m_scrollEngine = nullptr;
    ScreenModeRouter* m_router = nullptr;

private Q_SLOTS:

    void init()
    {
        // PhosphorZones::LayoutRegistry with no backend — every screen hits the default
        // modeForScreen fallback (Snapping unless explicitly assigned).
        m_layoutManager = new PhosphorZones::LayoutRegistry(PlasmaZones::createAssignmentsBackend(),
                                                            QStringLiteral("plasmazones/layouts"));

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

        // ScrollEngine has no constructor dependencies.
        m_scrollEngine = new PhosphorScrollEngine::ScrollEngine(nullptr);

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
        // Defensive: an empty screen id should never trigger autotile mode.
        // The engine's set does not contain the empty string by default.
        QCOMPARE(m_router->modeFor(QString()), PhosphorZones::AssignmentEntry::Snapping);
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
        // For any given screen exactly one of isSnapMode / isAutotileMode /
        // isScrollMode is true — the predicate trio must stay exhaustive and
        // mutually exclusive as PhosphorZones::AssignmentEntry::Mode grows.
        const QStringList screens = {QStringLiteral("DP-1"), QStringLiteral("DP-2"), QStringLiteral("DP-3"),
                                     QStringLiteral("phys/vs:0"), QString()};
        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-1"), QStringLiteral("phys/vs:0")});
        m_scrollEngine->setActiveScreens({QStringLiteral("DP-2")});

        for (const QString& sid : screens) {
            const int trueCount = (m_router->isSnapMode(sid) ? 1 : 0) + (m_router->isAutotileMode(sid) ? 1 : 0)
                + (m_router->isScrollMode(sid) ? 1 : 0);
            QVERIFY2(trueCount == 1,
                     qPrintable(QStringLiteral("mode predicates not exhaustive/exclusive on %1").arg(sid)));
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
    }

    void partitionByMode_emptyInput_returnsEmptyBuckets()
    {
        const auto result = m_router->partitionByMode({});
        QVERIFY(result.snap.isEmpty());
        QVERIFY(result.autotile.isEmpty());
        // Scroll bucket must also be empty — a regression that left dirty
        // state in the scroll bucket would otherwise pass these tests.
        QVERIFY(result.scroll.isEmpty());
    }

    void partitionByMode_allSnap_allAutotileIsEmpty()
    {
        const QStringList input = {QStringLiteral("DP-1"), QStringLiteral("DP-2")};
        const auto result = m_router->partitionByMode(input);
        QCOMPARE(result.snap, input);
        QVERIFY(result.autotile.isEmpty());
        QVERIFY(result.scroll.isEmpty());
    }

    void partitionByMode_allAutotile_allSnapIsEmpty()
    {
        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-1"), QStringLiteral("DP-2")});
        const QStringList input = {QStringLiteral("DP-1"), QStringLiteral("DP-2")};
        const auto result = m_router->partitionByMode(input);
        QVERIFY(result.snap.isEmpty());
        QCOMPARE(result.autotile, input);
        QVERIFY(result.scroll.isEmpty());
    }

    // ─── scroll mode ──────────────────────────────────────────────────────

    void engineFor_scrollScreen_returnsScrollEngine()
    {
        m_scrollEngine->setActiveScreens({QStringLiteral("DP-1")});
        PhosphorEngine::IPlacementEngine* engine = m_router->engineFor(QStringLiteral("DP-1"));
        QVERIFY(engine != nullptr);
        QCOMPARE(static_cast<PhosphorEngine::IPlacementEngine*>(m_scrollEngine), engine);
    }

    void partitionByMode_includesScrollBucket()
    {
        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-2")});
        m_scrollEngine->setActiveScreens({QStringLiteral("DP-3")});
        const QStringList input{QStringLiteral("DP-1"), QStringLiteral("DP-2"), QStringLiteral("DP-3")};
        const auto result = m_router->partitionByMode(input);
        QCOMPARE(result.snap, QStringList{QStringLiteral("DP-1")});
        QCOMPARE(result.autotile, QStringList{QStringLiteral("DP-2")});
        QCOMPARE(result.scroll, QStringList{QStringLiteral("DP-3")});
    }
};

QTEST_MAIN(TestScreenModeRouter)
#include "test_screen_mode_router.moc"
