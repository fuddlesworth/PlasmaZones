// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QVariantMap>

#include "../helpers/AutotileTestHelpers.h"
#include "../helpers/ScriptedAlgoTestSetup.h"
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingState.h>

using namespace PlasmaZones;
using namespace PhosphorTileEngine;

// QTest ships no formatter for QJsonObject, so a bare QCOMPARE failure prints
// "Compared values are not the same" with no contents — useless for a suite
// whose whole subject is what is inside a bag.
namespace QTest {
template<>
inline char* toString(const QJsonObject& bag)
{
    return QTest::toString(QJsonDocument(bag).toJson(QJsonDocument::Compact).constData());
}
} // namespace QTest

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

    /// Shaped like aligned-grid's real bag: a grid shape plus normalized
    /// per-column fractions. @p first distinguishes one screen's bag from
    /// another's so a test can tell them apart.
    static QJsonObject sampleBag(double first = 0.7)
    {
        QJsonObject bag;
        bag[QLatin1String("cols")] = 2;
        bag[QLatin1String("rows")] = 2;
        bag[QLatin1String("colFractions")] = QJsonArray{first, 1.0 - first};
        return bag;
    }

    static QVariantMap algorithmOverride(const QString& algorithmId)
    {
        QVariantMap overrides;
        overrides[QString(PhosphorEngine::PerScreenKeys::Algorithm)] = algorithmId;
        return overrides;
    }

    /// Put @p bag on @p screen's current state. Returns false (with the failure
    /// already recorded) when there is no state to write to.
    static bool seedBag(AutotileEngine& engine, const QString& screen, const QJsonObject& bag)
    {
        PhosphorTiles::TilingState* const state = engine.tilingStateForScreen(screen);
        if (!state) {
            return false;
        }
        state->setScriptState(bag);
        return true;
    }

    /// The bag on @p screen's current state. Fails rather than returning an empty
    /// object when there is no state, so an "is empty" assertion cannot pass
    /// because state creation silently fell over.
    static QJsonObject bagOn(AutotileEngine& engine, const QString& screen)
    {
        PhosphorTiles::TilingState* const state = engine.tilingStateForScreen(screen);
        if (!state) {
            QTest::qFail("no TilingState for screen", __FILE__, __LINE__);
            return QJsonObject();
        }
        return state->scriptState();
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
        QVERIFY(seedBag(engine, screen, sampleBag()));

        engine.setAutotileScreens({});
        engine.setAutotileScreens({screen});

        QCOMPARE(bagOn(engine, screen), sampleBag());
    }

    /// A screen pinned to its own algorithm keeps its bag across the toggle too.
    /// Toggle-off drops the resolver's in-memory overrides, which reads to
    /// wipeStateBagsOnEffectiveAlgorithmChange as override -> global, and the
    /// re-enable reads as global -> override. Both are artifacts of the teardown
    /// rather than user-visible switches: the persisted per-screen settings
    /// survive and re-derive the same algorithm, so the bag must still be there.
    void testScriptStateSurvivesToggleOffWithPerScreenAlgorithm()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));
        // Pin the screen to an algorithm OTHER than the global one, so the
        // override drop moves the effective id.
        engine.applyPerScreenConfig(screen, algorithmOverride(QStringLiteral("bsp")));
        QVERIFY(seedBag(engine, screen, sampleBag()));

        engine.setAutotileScreens({});
        // The daemon re-derives per-screen config from the persisted settings and
        // does so BEFORE re-activating screens (see Daemon::updateAutotileScreens).
        engine.applyPerScreenConfig(screen, algorithmOverride(QStringLiteral("bsp")));
        engine.setAutotileScreens({screen});

        QCOMPARE(bagOn(engine, screen), sampleBag());
    }

    /// The state can be created BEFORE the per-screen override is reinstated.
    /// Daemon::updateAutotileScreens seeds window order for every added screen
    /// (autotile.cpp, "Must happen before setActiveScreens()") and that seeding
    /// creates the TilingState, all before the applyPerScreenConfig loop. At that
    /// instant the resolver has no override for the screen, so its effective
    /// algorithm reads as the global one and the tag cannot be adjudicated yet.
    /// A restore attempt in that window must leave the entry alone rather than
    /// treat the mismatch as final.
    void testScriptStateSurvivesStateCreatedBeforeOverrideReinstated()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));
        engine.applyPerScreenConfig(screen, algorithmOverride(QStringLiteral("bsp")));
        QVERIFY(seedBag(engine, screen, sampleBag()));

        engine.setAutotileScreens({});
        // Stands in for seedAutotileOrderForScreen: materialises the state while
        // the resolver still resolves this screen to the global algorithm.
        engine.tilingStateForScreen(screen);
        engine.applyPerScreenConfig(screen, algorithmOverride(QStringLiteral("bsp")));
        engine.setAutotileScreens({screen});

        QCOMPARE(bagOn(engine, screen), sampleBag());
    }

    /// A stashed bag must never overwrite a NEWER live one. Restore does not
    /// consume, so an entry outlives the re-enable that used it, and the
    /// re-enable path restores into whatever state it finds rather than a fresh
    /// one. A toggle-off on another desktop leaves this desktop's state alive
    /// (only the current context is torn down), so coming back to it must not
    /// hand the state its own older bag.
    void testRestoreDoesNotClobberNewerLiveBag()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setCurrentDesktop(2);
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));
        QVERIFY(seedBag(engine, screen, sampleBag(0.7)));

        // Round trip on desktop 2, so an entry for {screen, 2} is left behind.
        engine.setAutotileScreens({});
        engine.setAutotileScreens({screen});
        QCOMPARE(bagOn(engine, screen), sampleBag(0.7));

        // The user adjusts the layout again. The stash still holds the old bag.
        QVERIFY(seedBag(engine, screen, sampleBag(0.4)));

        // Toggle off from desktop 1: only that context is torn down, so
        // desktop 2's state survives holding the newer bag.
        engine.setCurrentDesktop(1);
        engine.setAutotileScreens({});
        engine.setCurrentDesktop(2);
        engine.setAutotileScreens({screen});

        QCOMPARE(bagOn(engine, screen), sampleBag(0.4));
    }

    /// Toggling off tears down only the CURRENT context, so the screen's other
    /// desktops keep their states. Those states' bags must survive too. They used
    /// not to: dropping the in-memory overrides read as override -> global, and
    /// the wipe that fired on it cleared every other context of the screen, so a
    /// toggle rescued one desktop's adjustments and destroyed the rest.
    void testToggleOffKeepsOtherDesktopsLiveBags()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setCurrentDesktop(1);
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));
        engine.applyPerScreenConfig(screen, algorithmOverride(QStringLiteral("bsp")));
        QVERIFY(seedBag(engine, screen, sampleBag(0.7)));

        engine.setCurrentDesktop(2);
        QVERIFY(seedBag(engine, screen, sampleBag(0.4)));

        // Toggle off with desktop 2 current: only that context is torn down.
        engine.setAutotileScreens({});
        engine.applyPerScreenConfig(screen, algorithmOverride(QStringLiteral("bsp")));
        engine.setAutotileScreens({screen});

        // Desktop 2 came back through the stash.
        QCOMPARE(bagOn(engine, screen), sampleBag(0.4));
        // Desktop 1 was never torn down and must simply still be there.
        engine.setCurrentDesktop(1);
        QCOMPARE(bagOn(engine, screen), sampleBag(0.7));
    }

    /// The stash must not route around the "bags never cross algorithms"
    /// invariant: a switch while the state is torn down invalidates the bag.
    void testScriptStateDroppedWhenAlgorithmChangesWhileOff()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));
        QVERIFY(seedBag(engine, screen, sampleBag()));

        engine.setAutotileScreens({});
        engine.setAlgorithm(QLatin1String("bsp"));
        engine.setAutotileScreens({screen});

        QVERIFY(bagOn(engine, screen).isEmpty());
    }

    /// A global algorithm switch wipes the LIVE bag of a screen that follows the
    /// global algorithm, with no toggle involved. This is the wipe the stash
    /// tests lean on elsewhere, pinned directly so it cannot rot unnoticed.
    void testGlobalSwitchWipesLiveBag()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));
        QVERIFY(seedBag(engine, screen, sampleBag()));

        engine.setAlgorithm(QLatin1String("bsp"));

        QVERIFY(bagOn(engine, screen).isEmpty());
    }

    /// A bag a live wipe cleared must stay cleared. Two mechanisms independently
    /// achieve that here and this test pins their disjunction, not either one:
    /// the per-screen wipe drops the stale entry as it clears the live bag, and
    /// the later teardown would erase it anyway by harvesting an EMPTY bag.
    /// Disabling either alone leaves this green. The drop is pinned on its own by
    /// testStashEntryIsDroppedWhenEffectiveAlgorithmMoves, which puts the entry on
    /// a desktop no teardown revisits.
    void testWipedBagIsNotResurrectedByAlgorithmRoundTrip()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));
        engine.applyPerScreenConfig(screen, algorithmOverride(QStringLiteral("bsp")));
        QVERIFY(seedBag(engine, screen, sampleBag()));

        // Round trip through the stash so an entry tagged "bsp" exists.
        engine.setAutotileScreens({});
        engine.applyPerScreenConfig(screen, algorithmOverride(QStringLiteral("bsp")));
        engine.setAutotileScreens({screen});
        QCOMPARE(bagOn(engine, screen), sampleBag());

        // A genuine per-screen switch away wipes the live bag.
        engine.applyPerScreenConfig(screen, algorithmOverride(QStringLiteral("master-stack")));
        QVERIFY(bagOn(engine, screen).isEmpty());

        // Toggling off now harvests an empty bag, which must erase the stale
        // "bsp" entry. Switching back to bsp must not bring the fractions back.
        engine.setAutotileScreens({});
        engine.applyPerScreenConfig(screen, algorithmOverride(QStringLiteral("bsp")));
        engine.setAutotileScreens({screen});

        QVERIFY(bagOn(engine, screen).isEmpty());
    }

    /// A screen pinned to its own algorithm does not follow a GLOBAL switch, so
    /// its stashed bag must survive one. What this pins is the GATE that decides
    /// which screens the switch applies to: the toggle-off has already dropped
    /// the screen's in-memory override, so an override-map reading calls a screen
    /// pinned by persisted settings a follower and destroys a bag whose algorithm
    /// never moved. The resolver's remembered "built under" id is what answers it
    /// correctly, and the per-key tag is the second line of defence.
    ///
    /// The screen needs a state on a second desktop for this test to have any
    /// power. Without one it holds no state at all while toggled off, the switch
    /// visits nothing, and the test passes against a gate that never ran.
    void testStashSurvivesGlobalSwitchWhenScreenIsPinned()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setCurrentDesktop(1);
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));
        // Pinned to a THIRD id, distinct from both the old and the new global
        // algorithm, so every resolution in this test has to consult the override
        // to get the right answer. Pinning to the incoming global id would make
        // the test pass even if per-screen overrides were ignored entirely.
        engine.applyPerScreenConfig(screen, algorithmOverride(QStringLiteral("spiral")));
        QVERIFY(seedBag(engine, screen, sampleBag()));

        // A second context on the same screen, so the toggle-off below leaves a
        // live state behind. Without one the screen has no state at all while it
        // is off, setAlgorithm's wipe loop iterates nothing, and this test cannot
        // observe the by-screen drop it exists to forbid.
        engine.setCurrentDesktop(2);
        QVERIFY(seedBag(engine, screen, sampleBag(0.4)));
        engine.setCurrentDesktop(1);

        engine.setAutotileScreens({});
        // The global algorithm moves while the screen is off. The screen is
        // pinned to spiral, so its effective algorithm never changed.
        engine.setAlgorithm(QLatin1String("bsp"));
        engine.applyPerScreenConfig(screen, algorithmOverride(QStringLiteral("spiral")));
        engine.setAutotileScreens({screen});

        QCOMPARE(bagOn(engine, screen), sampleBag());
    }

    /// The eager drop must actually drop. Every other test here asserts a bag
    /// SURVIVES, so neutering dropStashedScriptStatesForAlgorithmChange makes
    /// them greener rather than redder and the whole mechanism can be deleted
    /// without a failure. This one asserts the opposite direction: once a
    /// screen's effective algorithm genuinely moves, the entry written under the
    /// OLD algorithm is gone for good, not merely refused by the tag. Switching
    /// back is what tells the two apart — a refused entry would reappear.
    void testStashEntryIsDroppedWhenEffectiveAlgorithmMoves()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setCurrentDesktop(1);
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));
        engine.applyPerScreenConfig(screen, algorithmOverride(QStringLiteral("bsp")));

        // The entry under test belongs to desktop 2. It has to be a desktop the
        // rest of the test never tears down again: a teardown harvest rewrites
        // or (on an empty bag) erases the entry, which masks the drop completely.
        engine.setCurrentDesktop(2);
        QVERIFY(seedBag(engine, screen, sampleBag()));
        // Desktop 1 keeps a live state so the screen still has one while the
        // algorithm moves below.
        engine.setCurrentDesktop(1);
        QVERIFY(seedBag(engine, screen, sampleBag(0.4)));

        // Tear down desktop 2 only, leaving a bsp-tagged entry for {screen, 2}.
        engine.setCurrentDesktop(2);
        engine.setAutotileScreens({});

        // The screen's effective algorithm genuinely moves. The eager drop is
        // what must kill the bsp entry here, while desktop 2 has no state to
        // harvest and nothing else can reach it.
        engine.applyPerScreenConfig(screen, algorithmOverride(QStringLiteral("spiral")));
        // Back to bsp, so the tag would MATCH again on the way out. Only an
        // entry that was actually removed stays gone.
        engine.applyPerScreenConfig(screen, algorithmOverride(QStringLiteral("bsp")));

        engine.setAutotileScreens({screen});

        QVERIFY(bagOn(engine, screen).isEmpty());
    }

    /// A screen pinned to the SAME id the global is leaving is still pinned, so
    /// the switch must not touch it. The daemon injects the Algorithm key for
    /// every screen a layout assigns without comparing it to the global, so this
    /// is ordinary configuration rather than a corner. Reading "did this screen
    /// move?" off the remembered id alone answers yes here — the id it was built
    /// under genuinely equals the outgoing global — which is why the live
    /// override map has to be consulted first.
    void testPinnedScreenSurvivesGlobalSwitchAwayFromItsOwnId()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setCurrentDesktop(1);
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));
        // Pinned to the algorithm the global is currently on.
        engine.applyPerScreenConfig(screen, algorithmOverride(QStringLiteral("master-stack")));
        QVERIFY(seedBag(engine, screen, sampleBag()));

        // The global moves away. The screen keeps master-stack via its override.
        engine.setAlgorithm(QLatin1String("bsp"));

        QCOMPARE(bagOn(engine, screen), sampleBag());
    }

    /// The refusal must be READ-ONLY, pinned without depending on the daemon's
    /// seed ordering. A refused restore leaves the entry alone, so putting the
    /// algorithm back makes the bag available again. Under an erase-on-mismatch
    /// restore the entry dies at the refused lookup and never comes back.
    void testRefusedRestoreLeavesTheEntryIntact()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));
        QVERIFY(seedBag(engine, screen, sampleBag()));

        engine.setAutotileScreens({});
        engine.setAlgorithm(QLatin1String("bsp"));
        // A lookup under the wrong algorithm must refuse without consuming.
        QVERIFY(bagOn(engine, screen).isEmpty());
        engine.setAlgorithm(QLatin1String("master-stack"));
        engine.setAutotileScreens({screen});

        QCOMPARE(bagOn(engine, screen), sampleBag());
    }

    /// The stash key carries the desktop, not just the screen. A bag stashed on
    /// one desktop must not surface on another, and must still be there on
    /// return. Without the desktop in the key both assertions invert.
    void testStashIsPerDesktop()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setCurrentDesktop(1);
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));
        QVERIFY(seedBag(engine, screen, sampleBag()));

        engine.setAutotileScreens({});
        engine.setCurrentDesktop(2);
        engine.setAutotileScreens({screen});
        QVERIFY(bagOn(engine, screen).isEmpty());

        engine.setAutotileScreens({});
        engine.setCurrentDesktop(1);
        engine.setAutotileScreens({screen});
        QCOMPARE(bagOn(engine, screen), sampleBag());
    }

    /// Each screen gets its OWN bag back, not a sibling's. Both screens carry a
    /// distinct bag so the assertion fails under any mutation of the stash key,
    /// regardless of which screen is looked up first.
    void testStashIsPerScreen()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen1 = QStringLiteral("eDP-1");
        const QString screen2 = QStringLiteral("HDMI-1");
        engine.setAutotileScreens({screen1, screen2});
        engine.setAlgorithm(QLatin1String("master-stack"));
        QVERIFY(seedBag(engine, screen1, sampleBag(0.7)));
        QVERIFY(seedBag(engine, screen2, sampleBag(0.25)));

        engine.setAutotileScreens({});
        engine.setAutotileScreens({screen1, screen2});

        QCOMPARE(bagOn(engine, screen1), sampleBag(0.7));
        QCOMPARE(bagOn(engine, screen2), sampleBag(0.25));
    }

    /// Unpinning a sticky screen migrates its state to another desktop key. A
    /// stashed bag belongs to that layout, so it has to move with it. Left
    /// behind it would describe windows that now live under a different key,
    /// and restore never consumes, so nothing would ever clear it.
    void testStashMigratesWithStickyScreenUnpin()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        const QString window = QStringLiteral("win-1");
        engine.setCurrentDesktop(1);
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));
        QVERIFY(seedBag(engine, screen, sampleBag()));

        // Leave an entry at {screen, desktop 1}. Restore does not consume, so it
        // is still there after the bag comes back.
        engine.setAutotileScreens({});
        engine.setAutotileScreens({screen});
        QCOMPARE(bagOn(engine, screen), sampleBag());

        // Give the screen an all-sticky window so it pins to desktop 1. The pin
        // has to be established AFTER the toggle, which clears it.
        engine.windowOpened(window, screen, 0, 0);
        QCoreApplication::processEvents(); // windowOpened registers via a queued hop
        engine.updateStickyScreenPins([](const QString&) {
            return true;
        });

        // Move the context on and unpin: the state migrates to desktop 2.
        engine.setCurrentDesktop(2);
        engine.updateStickyScreenPins([](const QString&) {
            return false;
        });

        // The entry must have moved with it. A fresh state back at desktop 1
        // starts clean; without the migration it would be handed the bag that
        // now belongs to the layout living on desktop 2.
        engine.setCurrentDesktop(1);
        QVERIFY(bagOn(engine, screen).isEmpty());
    }

    /// Desktop numbers are reused after a renumber, so a stashed bag for a
    /// removed desktop must not be handed to whatever takes its number.
    void testStashPrunedForRemovedDesktop()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        constexpr int desktop = 3;
        engine.setCurrentDesktop(desktop);
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));
        QVERIFY(seedBag(engine, screen, sampleBag()));

        engine.setAutotileScreens({});
        engine.pruneStatesForDesktop(desktop);
        engine.setAutotileScreens({screen});

        QVERIFY(bagOn(engine, screen).isEmpty());
    }

    /// Same for an activity that no longer exists. Keys with an empty activity
    /// are deliberately never pruned, so the context carries a named one.
    void testStashPrunedForRemovedActivity()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setCurrentActivity(QStringLiteral("activity-a"));
        engine.setAutotileScreens({screen});
        engine.setAlgorithm(QLatin1String("master-stack"));
        QVERIFY(seedBag(engine, screen, sampleBag()));

        engine.setAutotileScreens({});
        engine.pruneStatesForActivities({QStringLiteral("activity-b")});
        engine.setAutotileScreens({screen});

        QVERIFY(bagOn(engine, screen).isEmpty());
    }
};

QTEST_MAIN(TestAutotileScriptStateStash)
#include "test_autotile_script_state_stash.moc"
