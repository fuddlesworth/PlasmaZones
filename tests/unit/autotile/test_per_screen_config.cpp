// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>

#include "autotile/AutotileEngine.h"
#include "autotile/AutotileConfig.h"
#include "autotile/AlgorithmRegistry.h"
#include "autotile/TilingAlgorithm.h"
#include "autotile/TilingState.h"
#include "core/constants.h"

#include "../helpers/ScriptedAlgoTestSetup.h"

using namespace PlasmaZones;

/**
 * @brief Unit tests for PerScreenConfigResolver
 *
 * Tests cover:
 * - Effective maxWindows with per-screen algo overrides
 * - Per-screen split ratio and master count overrides
 * - Algorithm overrides resetting split ratio to new default
 * - Clearing overrides restoring global defaults
 * - Per-side outer gap overrides
 * - Empty overrides and empty screen name edge cases
 * - removeOverridesForScreen
 */
class TestPerScreenConfig : public QObject
{
    Q_OBJECT

private:
    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;

private Q_SLOTS:

    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // effectiveMaxWindows
    // ═══════════════════════════════════════════════════════════════════════════

    void testPerScreen_effectiveMaxWindows_perScreenAlgoDiffersFromGlobal()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QStringLiteral("HDMI-1");
        engine.setAutotileScreens({screen});

        // Global algorithm is master-stack
        engine.setAlgorithm(QLatin1String("master-stack"));

        // Override screen to use BSP algorithm (which has a different default maxWindows)
        QVariantMap overrides;
        overrides[QStringLiteral("Algorithm")] = QLatin1String("bsp");
        engine.applyPerScreenConfig(screen, overrides);

        // When the per-screen algo differs and global maxWindows is at the global
        // algo's default, effectiveMaxWindows should return the per-screen algo's default
        auto* bspAlgo = AlgorithmRegistry::instance()->algorithm(QLatin1String("bsp"));
        auto* msAlgo = AlgorithmRegistry::instance()->algorithm(QLatin1String("master-stack"));
        QVERIFY(bspAlgo);
        QVERIFY(msAlgo);

        // Precondition: the two algorithms must have different default maxWindows
        // for this test to be meaningful
        QVERIFY(bspAlgo->defaultMaxWindows() != msAlgo->defaultMaxWindows());

        // Set global maxWindows to the master-stack default
        engine.config()->maxWindows = msAlgo->defaultMaxWindows();

        int effective = engine.effectiveMaxWindows(screen);
        QCOMPARE(effective, bspAlgo->defaultMaxWindows());
    }

    void testPerScreen_effectiveMaxWindows_userCustomizedGlobalHonored()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QStringLiteral("HDMI-1");
        engine.setAutotileScreens({screen});

        engine.setAlgorithm(QLatin1String("master-stack"));

        // Override screen to use BSP
        QVariantMap overrides;
        overrides[QStringLiteral("Algorithm")] = QLatin1String("bsp");
        engine.applyPerScreenConfig(screen, overrides);

        // User has explicitly customized global maxWindows away from algo default
        auto* msAlgo = AlgorithmRegistry::instance()->algorithm(QLatin1String("master-stack"));
        QVERIFY(msAlgo);
        engine.config()->maxWindows = msAlgo->defaultMaxWindows() + 2;

        // Should honor the user-customized global, not the per-screen algo default
        int effective = engine.effectiveMaxWindows(screen);
        QCOMPARE(effective, engine.config()->maxWindows);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Unlimited overflow mode returns the named sentinel so std::min clamps
    // become idempotent and onWindowAdded's gate is wide open. The constant
    // lives in AutotileDefaults so any caller (resolver, tests, future
    // headroom math) shares one definition.
    // ─────────────────────────────────────────────────────────────────────
    void testPerScreen_effectiveMaxWindows_unlimitedReturnsSentinel()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));

        engine.config()->overflowBehavior = AutotileOverflowBehavior::Unlimited;

        const int effective = engine.effectiveMaxWindows(screen);
        QCOMPARE(effective, AutotileDefaults::UnlimitedMaxWindowsSentinel);
    }

    // ─────────────────────────────────────────────────────────────────────
    // A per-screen MaxWindows override wins even when global overflow is
    // Unlimited — users must be able to clamp individual screens while
    // running unlimited elsewhere. The override path is checked BEFORE the
    // Unlimited short-circuit in PerScreenConfigResolver::effectiveMaxWindows.
    // ─────────────────────────────────────────────────────────────────────
    void testPerScreen_effectiveMaxWindows_unlimitedRespectsPerScreenOverride()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screenA = QStringLiteral("eDP-1");
        const QString screenB = QStringLiteral("HDMI-1");
        engine.setAutotileScreens({screenA, screenB});
        engine.setAlgorithm(QLatin1String("master-stack"));

        engine.config()->overflowBehavior = AutotileOverflowBehavior::Unlimited;

        // Screen A clamps to 3 via per-screen override; screen B inherits Unlimited.
        QVariantMap overrides;
        overrides[QStringLiteral("MaxWindows")] = 3;
        engine.applyPerScreenConfig(screenA, overrides);

        QCOMPARE(engine.effectiveMaxWindows(screenA), 3);
        QCOMPARE(engine.effectiveMaxWindows(screenB), AutotileDefaults::UnlimitedMaxWindowsSentinel);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Symmetric coverage: Float (default) global with a per-screen override
    // — the per-screen override path runs ahead of the Unlimited short
    // circuit, so this exercises the same priority ordering from the
    // opposite branch and pins it against future refactors that might move
    // the override below either short circuit.
    // ─────────────────────────────────────────────────────────────────────
    void testPerScreen_effectiveMaxWindows_floatRespectsPerScreenOverride()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screenA = QStringLiteral("eDP-1");
        const QString screenB = QStringLiteral("HDMI-1");
        engine.setAutotileScreens({screenA, screenB});
        engine.setAlgorithm(QLatin1String("master-stack"));

        // Float global, screen A clamps to 2, screen B falls through to global default.
        engine.config()->overflowBehavior = AutotileOverflowBehavior::Float;
        QVariantMap overrides;
        overrides[QStringLiteral("MaxWindows")] = 2;
        engine.applyPerScreenConfig(screenA, overrides);

        QCOMPARE(engine.effectiveMaxWindows(screenA), 2);
        QCOMPARE(engine.effectiveMaxWindows(screenB), engine.config()->maxWindows);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // applyOverrides
    // ═══════════════════════════════════════════════════════════════════════════

    void testPerScreen_applyOverrides_splitRatioSet()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        QVariantMap overrides;
        overrides[QStringLiteral("SplitRatio")] = 0.75;
        engine.applyPerScreenConfig(screen, overrides);

        TilingState* state = engine.stateForScreen(screen);
        QVERIFY(state);
        QVERIFY(qFuzzyCompare(state->splitRatio(), 0.75));

        // Verify the override is stored
        QVERIFY(engine.hasPerScreenOverride(screen, QStringLiteral("SplitRatio")));
    }

    void testPerScreen_applyOverrides_masterCountSet()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        // Add windows so master count can be verified
        TilingState* state = engine.stateForScreen(screen);
        state->addWindow(QStringLiteral("win1"));
        state->addWindow(QStringLiteral("win2"));
        state->addWindow(QStringLiteral("win3"));

        QVariantMap overrides;
        overrides[QStringLiteral("MasterCount")] = 2;
        engine.applyPerScreenConfig(screen, overrides);

        QCOMPARE(state->masterCount(), 2);
        QVERIFY(engine.hasPerScreenOverride(screen, QStringLiteral("MasterCount")));
    }

    void testPerScreen_applyOverrides_algorithmResetsSplitRatio()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        TilingState* state = engine.stateForScreen(screen);
        state->setSplitRatio(0.75);

        // Override with a new algorithm but NO explicit SplitRatio override.
        // The algorithm override should reset the split ratio to the new algo's default.
        QVariantMap overrides;
        overrides[QStringLiteral("Algorithm")] = QLatin1String("bsp");
        engine.applyPerScreenConfig(screen, overrides);

        auto* bspAlgo = AlgorithmRegistry::instance()->algorithm(QLatin1String("bsp"));
        QVERIFY(bspAlgo);
        QVERIFY(qFuzzyCompare(state->splitRatio(), bspAlgo->defaultSplitRatio()));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // clearOverrides
    // ═══════════════════════════════════════════════════════════════════════════

    void testPerScreen_clearOverrides_restoresGlobalDefaults()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        // Set global defaults
        engine.config()->splitRatio = 0.6;
        engine.config()->masterCount = 1;

        TilingState* state = engine.stateForScreen(screen);
        state->addWindow(QStringLiteral("win1"));
        state->addWindow(QStringLiteral("win2"));

        // Apply per-screen overrides
        QVariantMap overrides;
        overrides[QStringLiteral("SplitRatio")] = 0.8;
        overrides[QStringLiteral("MasterCount")] = 2;
        engine.applyPerScreenConfig(screen, overrides);

        QVERIFY(qFuzzyCompare(state->splitRatio(), 0.8));
        QCOMPARE(state->masterCount(), 2);

        // Clear overrides — should restore global defaults
        engine.clearPerScreenConfig(screen);

        QVERIFY(qFuzzyCompare(state->splitRatio(), 0.6));
        QCOMPARE(state->masterCount(), 1);
        QVERIFY(!engine.hasPerScreenOverride(screen, QStringLiteral("SplitRatio")));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // effectiveOuterGaps — per-side overrides
    // ═══════════════════════════════════════════════════════════════════════════

    void testPerScreen_effectiveOuterGaps_perSideOverrides()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QStringLiteral("HDMI-1");
        engine.setAutotileScreens({screen});

        // Set a uniform outer gap globally
        engine.config()->outerGap = 10;
        engine.config()->usePerSideOuterGap = false;

        // Override per-screen with per-side gaps
        QVariantMap overrides;
        overrides[QStringLiteral("OuterGapTop")] = 20;
        overrides[QStringLiteral("OuterGapBottom")] = 5;
        engine.applyPerScreenConfig(screen, overrides);

        ::PhosphorLayout::EdgeGaps gaps = engine.effectiveOuterGaps(screen);
        QCOMPARE(gaps.top, 20);
        QCOMPARE(gaps.bottom, 5);
        // Left and right fall back to the global uniform outer gap (10),
        // since no per-screen OuterGap or OuterGapLeft/Right override was set.
        // Using the literal value instead of engine.effectiveOuterGap(screen) to
        // avoid a circular assertion that always passes.
        QCOMPARE(gaps.left, 10);
        QCOMPARE(gaps.right, 10);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Edge cases
    // ═══════════════════════════════════════════════════════════════════════════

    void testPerScreen_emptyOverridesCallsClear()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        // Pre-condition: no overrides should exist yet
        QVERIFY2(!engine.hasPerScreenOverride(screen, QStringLiteral("SplitRatio")),
                 "Pre-condition failed: SplitRatio override should not exist before applying");

        // First apply real overrides
        QVariantMap overrides;
        overrides[QStringLiteral("SplitRatio")] = 0.8;
        engine.applyPerScreenConfig(screen, overrides);

        // Verify overrides were actually set (pre-condition for the clear test)
        QVERIFY2(engine.hasPerScreenOverride(screen, QStringLiteral("SplitRatio")),
                 "Pre-condition failed: SplitRatio override was not set by applyPerScreenConfig");

        // Passing empty overrides should clear them
        engine.applyPerScreenConfig(screen, QVariantMap());

        QVERIFY(!engine.hasPerScreenOverride(screen, QStringLiteral("SplitRatio")));
    }

    void testPerScreen_emptyScreenNameIgnored()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);

        // Should not crash or create state for empty screen name
        QVariantMap overrides;
        overrides[QStringLiteral("SplitRatio")] = 0.8;
        engine.applyPerScreenConfig(QString(), overrides);

        // No per-screen overrides should exist for empty name
        QVERIFY(engine.perScreenOverrides(QString()).isEmpty());
    }

    void testPerScreen_removeOverridesForScreen()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QStringLiteral("DP-1");
        engine.setAutotileScreens({screen});

        QVariantMap overrides;
        overrides[QStringLiteral("InnerGap")] = 20;
        overrides[QStringLiteral("OuterGap")] = 15;
        engine.applyPerScreenConfig(screen, overrides);

        QVERIFY(engine.hasPerScreenOverride(screen, QStringLiteral("InnerGap")));
        QVERIFY(engine.hasPerScreenOverride(screen, QStringLiteral("OuterGap")));

        // Remove all overrides for the screen (used during screen removal)
        // clearPerScreenConfig should remove the overrides
        engine.clearPerScreenConfig(screen);

        QVERIFY(!engine.hasPerScreenOverride(screen, QStringLiteral("InnerGap")));
        QVERIFY(!engine.hasPerScreenOverride(screen, QStringLiteral("OuterGap")));
    }
};

QTEST_MAIN(TestPerScreenConfig)
#include "test_per_screen_config.moc"
