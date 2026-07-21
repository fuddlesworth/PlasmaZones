// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

#include "../helpers/AutotileTestHelpers.h"
#include "../helpers/ScriptedAlgoTestSetup.h"
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingState.h>

using namespace PlasmaZones;
using namespace PhosphorTileEngine;

/**
 * @brief Script-state bags survive an autotile toggle-off/on round trip.
 *
 * Toggling autotile off destroys the current context's TilingState, which used
 * to take the algorithm's opaque script-state bag with it — an aligned grid's
 * manually resized column fractions were silently reset to a uniform layout on
 * re-enable. The engine now rescues the bag into a stash keyed by
 * (screen, desktop, activity) and tagged with the effective algorithm, handing
 * it back only when that algorithm still applies.
 */
class TestAutotileScriptStateStash : public QObject
{
    Q_OBJECT

private:
    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;

    static QJsonObject sampleBag()
    {
        // Shaped like aligned-grid's real bag: a grid shape plus normalized
        // per-column fractions.
        QJsonObject bag;
        bag[QStringLiteral("cols")] = 2;
        bag[QStringLiteral("rows")] = 2;
        bag[QStringLiteral("colFractions")] = QJsonArray{0.7, 0.3};
        return bag;
    }

private Q_SLOTS:

    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(P_SOURCE_DIR)));
    }

    /// The regression: off/on must not reset a manually adjusted layout.
    void testScriptStateSurvivesToggleOff()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));

        PhosphorTiles::TilingState* before = engine.tilingStateForScreen(screen);
        QVERIFY(before);
        before->setScriptState(sampleBag());

        // Toggle autotile off, then back on.
        engine.setAutotileScreens({});
        QCoreApplication::processEvents();
        engine.setAutotileScreens({screen});
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* after = engine.tilingStateForScreen(screen);
        QVERIFY(after);
        QCOMPARE(after->scriptState(), sampleBag());
    }

    /// The stash must not route around the "bags never cross algorithms"
    /// invariant: a switch while the state is torn down invalidates the bag.
    void testScriptStateDroppedWhenAlgorithmChangesWhileOff()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));

        PhosphorTiles::TilingState* before = engine.tilingStateForScreen(screen);
        QVERIFY(before);
        before->setScriptState(sampleBag());

        engine.setAutotileScreens({});
        QCoreApplication::processEvents();
        engine.setAlgorithm(QLatin1String("bsp"));
        engine.setAutotileScreens({screen});
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* after = engine.tilingStateForScreen(screen);
        QVERIFY(after);
        QVERIFY(after->scriptState().isEmpty());
    }

    /// Switching away and back while tiled still wipes: the live wipe sites own
    /// that case, and the stash must not resurrect what they discarded.
    void testScriptStateNotResurrectedByAlgorithmRoundTrip()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen);
        QVERIFY(state);
        state->setScriptState(sampleBag());

        engine.setAlgorithm(QLatin1String("bsp"));
        engine.setAutotileScreens({});
        QCoreApplication::processEvents();
        engine.setAlgorithm(QLatin1String("master-stack"));
        engine.setAutotileScreens({screen});
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* after = engine.tilingStateForScreen(screen);
        QVERIFY(after);
        QVERIFY(after->scriptState().isEmpty());
    }

    /// A bag belongs to one key; a different screen must not inherit it.
    void testStashIsPerScreen()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen1 = QStringLiteral("eDP-1");
        const QString screen2 = QStringLiteral("HDMI-1");
        engine.setAutotileScreens({screen1, screen2});
        engine.setAlgorithm(QLatin1String("master-stack"));

        PhosphorTiles::TilingState* before = engine.tilingStateForScreen(screen1);
        QVERIFY(before);
        before->setScriptState(sampleBag());

        engine.setAutotileScreens({});
        QCoreApplication::processEvents();
        engine.setAutotileScreens({screen1, screen2});
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* restored = engine.tilingStateForScreen(screen1);
        PhosphorTiles::TilingState* other = engine.tilingStateForScreen(screen2);
        QVERIFY(restored && other);
        QCOMPARE(restored->scriptState(), sampleBag());
        QVERIFY(other->scriptState().isEmpty());
    }
};

QTEST_MAIN(TestAutotileScriptStateStash)
#include "test_autotile_script_state_stash.moc"
