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

#include "autotile/AutotileEngine.h"
#include <PhosphorZones/LayoutRegistry.h>
#include "config/configbackends.h"
#include "core/screenmoderouter.h"
#include "../helpers/AutotileTestHelpers.h"
#include "snap/SnapEngine.h"

using namespace PlasmaZones;

class TestScreenModeRouter : public QObject
{
    Q_OBJECT

private:
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    SnapEngine* m_snapEngine = nullptr;
    AutotileEngine* m_autotileEngine = nullptr;
    ScreenModeRouter* m_router = nullptr;

private Q_SLOTS:

    void init()
    {
        // PhosphorZones::LayoutRegistry with no backend — every screen hits the default
        // modeForScreen fallback (Snapping unless explicitly assigned).
        m_layoutManager = new PhosphorZones::LayoutRegistry(PlasmaZones::createAssignmentsBackend(),
                                                            QStringLiteral("plasmazones/layouts"));

        // SnapEngine with all-nullptr dependencies is a valid construction:
        // the router only invokes it via the PhosphorEngineApi::IPlacementEngine interface
        // (which ScreenModeRouter doesn't actually call — it returns the
        // pointer so callers can dispatch). Matches test_snap_engine.cpp's
        // headless stub pattern.
        m_snapEngine = new SnapEngine(nullptr, nullptr, nullptr, nullptr, nullptr);

        // AutotileEngine with all-nullptr dependencies is explicitly
        // supported for headless tests — see the comment at the top of
        // AutotileEngine::AutotileEngine (src/autotile/AutotileEngine.cpp).
        // The algorithm registry is non-null even in headless mode because
        // the engine resolves an initial algorithm in its constructor;
        // share the test-process registry.
        m_autotileEngine = new AutotileEngine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        m_router = new ScreenModeRouter(m_layoutManager, m_snapEngine, m_autotileEngine);
    }

    void cleanup()
    {
        delete m_router;
        m_router = nullptr;
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
        PhosphorEngineApi::IPlacementEngine* engine = m_router->engineFor(QStringLiteral("DP-1"));
        QVERIFY(engine != nullptr);
        QCOMPARE(static_cast<PhosphorEngineApi::IPlacementEngine*>(m_snapEngine), engine);
    }

    void engineFor_autotileScreen_returnsAutotileEngine()
    {
        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-1")});
        PhosphorEngineApi::IPlacementEngine* engine = m_router->engineFor(QStringLiteral("DP-1"));
        QVERIFY(engine != nullptr);
        QCOMPARE(static_cast<PhosphorEngineApi::IPlacementEngine*>(m_autotileEngine), engine);
    }

    void engineFor_respectsLiveAssignmentUpdates()
    {
        // Flip a screen into autotile, then back out, and verify the router
        // tracks the engine's live state — not a stale snapshot from the
        // first call.
        QCOMPARE(m_router->engineFor(QStringLiteral("DP-1")),
                 static_cast<PhosphorEngineApi::IPlacementEngine*>(m_snapEngine));

        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-1")});
        QCOMPARE(m_router->engineFor(QStringLiteral("DP-1")),
                 static_cast<PhosphorEngineApi::IPlacementEngine*>(m_autotileEngine));

        m_autotileEngine->setAutotileScreens({});
        QCOMPARE(m_router->engineFor(QStringLiteral("DP-1")),
                 static_cast<PhosphorEngineApi::IPlacementEngine*>(m_snapEngine));
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
        // For any given screen, isSnapMode and isAutotileMode must disagree.
        // Today PhosphorZones::AssignmentEntry::Mode is 2-valued so this is tautological;
        // the test pins the invariant so if the enum gains a third state
        // (e.g. a "Disabled" mode) the predicate pair is forced to update
        // in lockstep.
        const QStringList screens = {QStringLiteral("DP-1"), QStringLiteral("DP-2"), QStringLiteral("HDMI-1"),
                                     QStringLiteral("phys/vs:0"), QString()};
        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-1"), QStringLiteral("phys/vs:0")});

        for (const QString& sid : screens) {
            const bool isSnap = m_router->isSnapMode(sid);
            const bool isAuto = m_router->isAutotileMode(sid);
            QVERIFY2(isSnap != isAuto, qPrintable(QStringLiteral("mode predicate collision on %1").arg(sid)));
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
    }

    void partitionByMode_allSnap_allAutotileIsEmpty()
    {
        const QStringList input = {QStringLiteral("DP-1"), QStringLiteral("DP-2")};
        const auto result = m_router->partitionByMode(input);
        QCOMPARE(result.snap, input);
        QVERIFY(result.autotile.isEmpty());
    }

    void partitionByMode_allAutotile_allSnapIsEmpty()
    {
        m_autotileEngine->setAutotileScreens({QStringLiteral("DP-1"), QStringLiteral("DP-2")});
        const QStringList input = {QStringLiteral("DP-1"), QStringLiteral("DP-2")};
        const auto result = m_router->partitionByMode(input);
        QVERIFY(result.snap.isEmpty());
        QCOMPARE(result.autotile, input);
    }
};

QTEST_MAIN(TestScreenModeRouter)
#include "test_screen_mode_router.moc"
