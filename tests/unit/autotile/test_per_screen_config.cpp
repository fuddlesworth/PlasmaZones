// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>

#include <PhosphorTileEngine/AutotileEngine.h>
#include "../helpers/AutotileTestHelpers.h"
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingState.h>
#include "core/constants.h"

#include "../helpers/ScriptedAlgoTestSetup.h"

using namespace PlasmaZones;
using namespace PhosphorTileEngine;

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
        QVERIFY(m_scriptSetup.init(QStringLiteral(P_SOURCE_DIR)));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // effectiveMaxWindows
    // ═══════════════════════════════════════════════════════════════════════════

    void testPerScreen_effectiveMaxWindows_perScreenAlgoDiffersFromGlobal()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
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
        auto* bspAlgo = m_scriptSetup.registry()->algorithm(QLatin1String("bsp"));
        auto* msAlgo = m_scriptSetup.registry()->algorithm(QLatin1String("master-stack"));
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
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("HDMI-1");
        engine.setAutotileScreens({screen});

        engine.setAlgorithm(QLatin1String("master-stack"));

        // Override screen to use BSP
        QVariantMap overrides;
        overrides[QStringLiteral("Algorithm")] = QLatin1String("bsp");
        engine.applyPerScreenConfig(screen, overrides);

        // User has explicitly customized global maxWindows away from algo default
        auto* msAlgo = m_scriptSetup.registry()->algorithm(QLatin1String("master-stack"));
        QVERIFY(msAlgo);
        engine.config()->maxWindows = msAlgo->defaultMaxWindows() + 2;

        // Should honor the user-customized global, not the per-screen algo default
        int effective = engine.effectiveMaxWindows(screen);
        QCOMPARE(effective, engine.config()->maxWindows);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Unlimited overflow mode returns the named sentinel so std::min clamps
    // become idempotent and onWindowAdded's gate is wide open. The constant
    // lives in PhosphorTiles::AutotileDefaults so any caller (resolver, tests, future
    // headroom math) shares one definition.
    // ─────────────────────────────────────────────────────────────────────
    void testPerScreen_effectiveMaxWindows_unlimitedReturnsSentinel()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));

        engine.config()->overflowBehavior = PhosphorTiles::AutotileOverflowBehavior::Unlimited;

        const int effective = engine.effectiveMaxWindows(screen);
        QCOMPARE(effective, PhosphorTiles::AutotileDefaults::UnlimitedMaxWindowsSentinel);
    }

    // ─────────────────────────────────────────────────────────────────────
    // A per-screen MaxWindows override wins even when global overflow is
    // Unlimited — users must be able to clamp individual screens while
    // running unlimited elsewhere. The override path is checked BEFORE the
    // Unlimited short-circuit in PerScreenConfigResolver::effectiveMaxWindows.
    // ─────────────────────────────────────────────────────────────────────
    void testPerScreen_effectiveMaxWindows_unlimitedRespectsPerScreenOverride()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenA = QStringLiteral("eDP-1");
        const QString screenB = QStringLiteral("HDMI-1");
        engine.setAutotileScreens({screenA, screenB});
        engine.setAlgorithm(QLatin1String("master-stack"));

        engine.config()->overflowBehavior = PhosphorTiles::AutotileOverflowBehavior::Unlimited;

        // Screen A clamps to 3 via per-screen override; screen B inherits Unlimited.
        QVariantMap overrides;
        overrides[QStringLiteral("MaxWindows")] = 3;
        engine.applyPerScreenConfig(screenA, overrides);

        QCOMPARE(engine.effectiveMaxWindows(screenA), 3);
        QCOMPARE(engine.effectiveMaxWindows(screenB), PhosphorTiles::AutotileDefaults::UnlimitedMaxWindowsSentinel);
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
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenA = QStringLiteral("eDP-1");
        const QString screenB = QStringLiteral("HDMI-1");
        engine.setAutotileScreens({screenA, screenB});
        engine.setAlgorithm(QLatin1String("master-stack"));

        // Float global, screen A clamps to 2, screen B falls through to global default.
        engine.config()->overflowBehavior = PhosphorTiles::AutotileOverflowBehavior::Float;
        QVariantMap overrides;
        overrides[QStringLiteral("MaxWindows")] = 2;
        engine.applyPerScreenConfig(screenA, overrides);

        QCOMPARE(engine.effectiveMaxWindows(screenA), 2);
        QCOMPARE(engine.effectiveMaxWindows(screenB), engine.config()->maxWindows);
    }

    // A per-screen InsertPosition override wins over the global config value, and an
    // out-of-range stored value is clamped. Exercises the resolver's per-screen
    // override + qBound branch (effectiveInsertPosition), the same override-then-clamp
    // shape effectiveOverflowBehavior uses, which the effectiveMaxWindows tests above
    // don't reach.
    void testPerScreen_effectiveInsertPosition_perScreenOverrideAndClamp()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenA = QStringLiteral("eDP-1");
        const QString screenB = QStringLiteral("HDMI-1");
        engine.setAutotileScreens({screenA, screenB});
        engine.setAlgorithm(QLatin1String("master-stack"));

        engine.config()->insertPosition = PhosphorTiles::AutotileInsertPosition::End;

        // Screen A overrides to AsMaster; screen B inherits the global End.
        QVariantMap overrides;
        overrides[QStringLiteral("InsertPosition")] = static_cast<int>(PhosphorTiles::AutotileInsertPosition::AsMaster);
        engine.applyPerScreenConfig(screenA, overrides);
        QCOMPARE(engine.effectiveInsertPosition(screenA), PhosphorTiles::AutotileInsertPosition::AsMaster);
        QCOMPARE(engine.effectiveInsertPosition(screenB), PhosphorTiles::AutotileInsertPosition::End);

        // An out-of-range stored int clamps into the valid enum range rather than
        // producing an invalid enumerator.
        QVariantMap bogus;
        bogus[QStringLiteral("InsertPosition")] = 999;
        engine.applyPerScreenConfig(screenA, bogus);
        const int clamped = static_cast<int>(engine.effectiveInsertPosition(screenA));
        QVERIFY(clamped >= PhosphorTiles::AutotileDefaults::MinInsertPosition);
        QVERIFY(clamped <= PhosphorTiles::AutotileDefaults::MaxInsertPosition);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // applyOverrides
    // ═══════════════════════════════════════════════════════════════════════════

    void testPerScreen_applyOverrides_splitRatioSet()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        QVariantMap overrides;
        overrides[QStringLiteral("SplitRatio")] = 0.75;
        engine.applyPerScreenConfig(screen, overrides);

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        QVERIFY(qFuzzyCompare(state->splitRatio(), 0.75));

        // Verify the override is stored
        QVERIFY(engine.hasPerScreenOverride(screen, QStringLiteral("SplitRatio")));
    }

    void testPerScreen_applyOverrides_masterCountSet()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        // Add windows so master count can be verified
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
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
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        state->setSplitRatio(0.75);

        // Override with an algorithm DIFFERENT from the global default ("bsp")
        // and NO explicit SplitRatio override. A genuine effective switch
        // resets the split ratio to the new algorithm's default.
        QVariantMap overrides;
        overrides[QStringLiteral("Algorithm")] = QLatin1String("master-stack");
        engine.applyPerScreenConfig(screen, overrides);

        auto* msAlgo = m_scriptSetup.registry()->algorithm(QLatin1String("master-stack"));
        QVERIFY(msAlgo);
        QVERIFY(qFuzzyCompare(state->splitRatio(), msAlgo->defaultSplitRatio()));
    }

    void testPerScreen_applyOverrides_sameAlgorithmAsGlobalKeepsTunedRatio()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        state->setSplitRatio(0.75);

        // Pinning an Algorithm override that matches the algorithm the screen
        // already follows globally ("bsp") is not an effective change, so the
        // user's live-tuned split ratio must survive.
        QVariantMap overrides;
        overrides[QStringLiteral("Algorithm")] = QLatin1String("bsp");
        engine.applyPerScreenConfig(screen, overrides);

        QVERIFY(qFuzzyCompare(state->splitRatio(), 0.75));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // clearOverrides
    // ═══════════════════════════════════════════════════════════════════════════

    void testPerScreen_clearOverrides_restoresGlobalDefaults()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        // Set global defaults
        engine.config()->splitRatio = 0.6;
        engine.config()->masterCount = 1;

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
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

    // A SetSplitRatio / SetMasterCount rule removed while the override map stays
    // NON-EMPTY (another override survives) routes through applyPerScreenConfig,
    // not clearPerScreenConfig. The stateful splitRatio / masterCount must revert
    // to the config baseline rather than keep the stale removed-rule value.
    void testPerScreen_removeSplitRatioAndMasterCount_whileMapNonEmpty_revertsToBaseline()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        engine.config()->splitRatio = 0.6;
        engine.config()->masterCount = 1;

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        state->addWindow(QStringLiteral("win1"));
        state->addWindow(QStringLiteral("win2"));

        // Include the (unchanged) Algorithm key — concrete-algorithm autotile screens
        // ALWAYS carry it (the daemon injects it), which is the shape that exposes the
        // SplitRatio revert bug if it were gated on Algorithm-key presence.
        QVariantMap overrides;
        overrides[QStringLiteral("Algorithm")] = QLatin1String("bsp");
        overrides[QStringLiteral("SplitRatio")] = 0.8;
        overrides[QStringLiteral("MasterCount")] = 2;
        overrides[QStringLiteral("MaxWindows")] = 4;
        engine.applyPerScreenConfig(screen, overrides);
        QVERIFY(qFuzzyCompare(state->splitRatio(), 0.8));
        QCOMPARE(state->masterCount(), 2);

        // Drop SplitRatio + MasterCount but keep the (unchanged) Algorithm + MaxWindows
        // → still non-empty, and the algorithm did NOT change, so both stateful values
        // must revert to the config baseline (not stay stuck at the removed values).
        QVariantMap reduced;
        reduced[QStringLiteral("Algorithm")] = QLatin1String("bsp");
        reduced[QStringLiteral("MaxWindows")] = 4;
        engine.applyPerScreenConfig(screen, reduced);

        QVERIFY(qFuzzyCompare(state->splitRatio(), 0.6));
        QCOMPARE(state->masterCount(), 1);
    }

    // The revert-on-removal path must NOT clobber a value the user live-adjusted
    // (drag-to-resize) when that key was NEVER an override: an unrelated override
    // apply with no prior SplitRatio key leaves the live-tuned split ratio intact.
    void testPerScreen_liveAdjustedSplitRatio_notClobberedByUnrelatedOverride()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.config()->splitRatio = 0.6;

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        state->addWindow(QStringLiteral("win1"));
        state->addWindow(QStringLiteral("win2"));
        state->setSplitRatio(0.42); // live drag-adjust, no override key involved

        // Apply an unrelated override (MaxWindows only). No prior SplitRatio override
        // existed, so the guard must leave the live-adjusted value untouched.
        QVariantMap overrides;
        overrides[QStringLiteral("MaxWindows")] = 4;
        engine.applyPerScreenConfig(screen, overrides);

        QVERIFY(qFuzzyCompare(state->splitRatio(), 0.42));
    }

    // Concrete-algorithm screens ALWAYS carry the injected Algorithm key. An
    // unrelated tiling-param rule edit re-applies the overrides map with that
    // (unchanged) key, which must NOT re-fire the algorithm split-ratio reset and
    // clobber a live drag-tuned ratio — the reset fires only on a genuine algorithm
    // switch (compared against the previous override map's algorithm).
    void testPerScreen_liveAdjustedSplitRatio_survivesUnrelatedOverride_concreteAlgorithm()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        state->addWindow(QStringLiteral("win1"));
        state->addWindow(QStringLiteral("win2"));

        // Non-vacuity precondition: the tuned value must differ from bsp's default,
        // else "survives" would pass even if the reset wrongly fired.
        auto* bspAlgo = m_scriptSetup.registry()->algorithm(QLatin1String("bsp"));
        QVERIFY(bspAlgo);
        QVERIFY(!qFuzzyCompare(bspAlgo->defaultSplitRatio(), 0.42));

        // Establish the concrete algorithm. The first apply legitimately resets to
        // the algo default (previous map had no algorithm → genuine switch).
        QVariantMap first;
        first[QStringLiteral("Algorithm")] = QLatin1String("bsp");
        engine.applyPerScreenConfig(screen, first);

        // User live-adjusts the split ratio (drag-resize) — no SplitRatio override.
        state->setSplitRatio(0.42);

        // Unrelated rule edit re-applies with the SAME (unchanged) Algorithm key.
        QVariantMap second;
        second[QStringLiteral("Algorithm")] = QLatin1String("bsp");
        second[QStringLiteral("MaxWindows")] = 4;
        engine.applyPerScreenConfig(screen, second);

        QVERIFY(qFuzzyCompare(state->splitRatio(), 0.42));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // effectiveOuterGaps — per-side overrides
    // ═══════════════════════════════════════════════════════════════════════════

    void testPerScreen_effectiveOuterGaps_perSideOverrides()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
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
    // Context gap provider (per-context window-rule gap overrides) — highest
    // precedence, matching the snapping gap pipeline.
    // ═══════════════════════════════════════════════════════════════════════════

    void testPerScreen_contextGapProvider_beatsPerScreenAndGlobal()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("DP-1");
        engine.setAutotileScreens({screen});

        // Global + per-screen gaps (the lower-precedence layers).
        engine.config()->innerGap = 4;
        engine.config()->outerGap = 6;
        QVariantMap overrides;
        overrides[QStringLiteral("InnerGap")] = 8;
        overrides[QStringLiteral("OuterGap")] = 10;
        engine.applyPerScreenConfig(screen, overrides);
        QCOMPARE(engine.effectiveInnerGap(screen), 8); // per-screen beats global
        QCOMPARE(engine.effectiveOuterGap(screen), 10);

        // A context gap override for this screen must win over both.
        engine.setContextGapProvider([screen](const QString& s) -> QVariantMap {
            if (s != screen) {
                return {};
            }
            QVariantMap m;
            m.insert(QStringLiteral("InnerGap"), 14);
            m.insert(QStringLiteral("OuterGap"), 18);
            return m;
        });
        QCOMPARE(engine.effectiveInnerGap(screen), 14);
        QCOMPARE(engine.effectiveOuterGap(screen), 18);

        // A different screen with no context override still resolves normally.
        const QString other = QStringLiteral("DP-2");
        engine.setAutotileScreens({screen, other});
        QCOMPARE(engine.effectiveInnerGap(other), 4); // global (no per-screen, no context)
        QCOMPARE(engine.effectiveOuterGap(other), 6);
    }

    void testPerScreen_contextGapProvider_emptyMapFallsThrough()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("DP-1");
        engine.setAutotileScreens({screen});
        engine.config()->innerGap = 5;

        // Provider returns an empty map → no context override → global value.
        engine.setContextGapProvider([](const QString&) -> QVariantMap {
            return {};
        });
        QCOMPARE(engine.effectiveInnerGap(screen), 5);
    }

    // Context per-side outer gaps are honoured (and beat a per-screen per-side
    // override) only when the rule set UsePerSideOuterGap — mirroring snapping.
    void testPerScreen_contextGapProvider_perSideHonoredWhenUsePerSideSet()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("DP-1");
        engine.setAutotileScreens({screen});
        engine.config()->outerGap = 6;

        // A per-screen per-side override sits below the context layer.
        QVariantMap overrides;
        overrides[QStringLiteral("OuterGapTop")] = 10;
        engine.applyPerScreenConfig(screen, overrides);

        engine.setContextGapProvider([screen](const QString& s) -> QVariantMap {
            if (s != screen) {
                return {};
            }
            QVariantMap m;
            m.insert(QStringLiteral("UsePerSideOuterGap"), true);
            m.insert(QStringLiteral("OuterGapTop"), 20);
            m.insert(QStringLiteral("OuterGapBottom"), 22);
            return m;
        });

        const ::PhosphorLayout::EdgeGaps gaps = engine.effectiveOuterGaps(screen);
        QCOMPARE(gaps.top, 20); // context per-side beats per-screen per-side (10)
        QCOMPARE(gaps.bottom, 22);
        // Unset sides fall back to the global outer gap (no context uniform).
        QCOMPARE(gaps.left, 6);
        QCOMPARE(gaps.right, 6);
    }

    // Context per-side outer gaps WITHOUT UsePerSideOuterGap are ignored — a
    // stale per-side entry must not apply (it would diverge from snapping).
    void testPerScreen_contextGapProvider_perSideIgnoredWhenUsePerSideUnset()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("DP-1");
        engine.setAutotileScreens({screen});
        engine.config()->outerGap = 6;

        engine.setContextGapProvider([screen](const QString& s) -> QVariantMap {
            if (s != screen) {
                return {};
            }
            QVariantMap m;
            m.insert(QStringLiteral("OuterGapTop"), 30); // no UsePerSideOuterGap flag
            return m;
        });

        const ::PhosphorLayout::EdgeGaps gaps = engine.effectiveOuterGaps(screen);
        // The context layer yields nothing (per-side gated off, no uniform), so
        // resolution falls through to the global uniform outer gap.
        QCOMPARE(gaps.top, 6);
        QCOMPARE(gaps.bottom, 6);
        QCOMPARE(gaps.left, 6);
        QCOMPARE(gaps.right, 6);
    }

    // A context uniform OuterGap wins as one atomic layer: it is NOT blended
    // per-key with a static per-screen per-side override below it.
    void testPerScreen_contextGapProvider_uniformWinsAtomicallyOverPerScreenPerSide()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("DP-1");
        engine.setAutotileScreens({screen});
        engine.config()->outerGap = 6;

        QVariantMap overrides;
        overrides[QStringLiteral("OuterGapTop")] = 40; // per-screen per-side
        engine.applyPerScreenConfig(screen, overrides);

        engine.setContextGapProvider([screen](const QString& s) -> QVariantMap {
            if (s != screen) {
                return {};
            }
            QVariantMap m;
            m.insert(QStringLiteral("OuterGap"), 12); // context uniform, no per-side
            return m;
        });

        const ::PhosphorLayout::EdgeGaps gaps = engine.effectiveOuterGaps(screen);
        // All sides take the context uniform; the per-screen top=40 does NOT bleed in.
        QCOMPARE(gaps.top, 12);
        QCOMPARE(gaps.bottom, 12);
        QCOMPARE(gaps.left, 12);
        QCOMPARE(gaps.right, 12);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Edge cases
    // ═══════════════════════════════════════════════════════════════════════════

    void testPerScreen_emptyOverridesCallsClear()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
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
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        // Should not crash or create state for empty screen name
        QVariantMap overrides;
        overrides[QStringLiteral("SplitRatio")] = 0.8;
        engine.applyPerScreenConfig(QString(), overrides);

        // No per-screen overrides should exist for empty name
        QVERIFY(engine.perScreenOverrides(QString()).isEmpty());
    }

    void testPerScreen_removeOverridesForScreen()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
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
