// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The dynamic-workspaces desktop axis: what an identity-based reap of one
// desktop destroys and what it must leave alone, and what a renumber has to
// carry across with the shifted keys.
//
// A NEW sibling for a new concern, not a split off test_scrollengine_perscreen:
// nothing moved out of that file, which is at the very top of its size grace
// with no headroom left and so could not have taken these cases anyway. The two
// suites share only the stub settings and the three-screen fixture. The
// per-screen suite's every case is a precedence claim inside one resolution
// cascade (rule > per-screen trio > cached global); none of these is — they are
// lifecycle claims about what a dying or renumbered context destroys, keeps and
// announces.

#include <PhosphorScrollEngine/IScrollSettings.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include "scrollstriptestutils.h"
#include "scrollstubsettings.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QSignalSpy>
#include <QVariantMap>
#include <QtTest>

using namespace PhosphorScrollEngine;

using ScrollTestUtils::makeProviderEngine;
using ScrollTestUtils::StubScrollSettings;

namespace {
const QString kS1 = QStringLiteral("S1");
const QString kS2 = QStringLiteral("S2");
const QString kS3 = QStringLiteral("S3");
} // namespace

class TestScrollEngineDesktopReap : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    void reapDesktopStateDropsOnlyTheDeadDesktop();
    void reapDesktopStateReleasesThePopulatedDesktopsWindows();
    void reapDesktopStateKeepsTheSurvivingSiblingsScreenBookkeeping();
    void renumberDesktopStateShiftsKeysAndKeepsWindows();
    void renumberDesktopStateMovesTheStripStash();
    void renumberDesktopStateMovesPerScreenOverrides();
    void reapDesktopStateASecondTimeIsASilentNoOp();
    void reapDesktopStateLeavesAReusedDesktopIndexEmpty();
    void reapDesktopStateSweepsTheStripStash();
    void reapDesktopStateSweepsTheBurstMarker();
    void reapDesktopStateCancelsALiveDragInsertPreview();
    void activityPruneReleasesItsWindowsAndSweepsTheBurstMarker();
    void activityPruneClearsATrackedDeadActivity();
    void renumberDesktopStateMovesTheBurstMarker();
    void renumberDesktopStateRefusesAPoisonedMapping();
    void focusedColumnWindowsRefusesAnAmbiguousPhysicalOutput();
    void focusedColumnWindowsResolvesASingleSubScreen();

private:
    /// A headless engine active on the three screens, with @p settings
    /// installed and its cached globals refreshed.
    static ScrollEngine* makeEngine(QObject* parent, StubScrollSettings* settings)
    {
        ScrollEngine* engine = makeProviderEngine(parent, {kS1, kS2, kS3});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();
        return engine;
    }

    static bool columnExists(ScrollEngine* engine, const QString& screenId, const QString& windowId)
    {
        auto* state = static_cast<ScrollState*>(engine->stateForScreen(screenId));
        if (!state) {
            return false;
        }
        for (const Column& col : state->strip().columns()) {
            if (col.indexOfWindow(windowId) >= 0) {
                return true;
            }
        }
        return false;
    }
};

void TestScrollEngineDesktopReap::reapDesktopStateDropsOnlyTheDeadDesktop()
{
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    // One window on desktop 1, one on desktop 2 (same screen, distinct
    // per-desktop states).
    engine->windowOpened(QStringLiteral("app|d1"), kS1, 0, 0);
    engine->setCurrentDesktopForScreen(kS1, 2);
    engine->windowOpened(QStringLiteral("app|d2"), kS1, 0, 0);
    QCOMPARE(engine->desktopsWithActiveState(), (QSet<int>{1, 2}));

    // Identity-based reap of desktop 1 leaves desktop 2 untouched.
    engine->reapDesktopState(1);
    QCOMPARE(engine->desktopsWithActiveState(), (QSet<int>{2}));
    QVERIFY(columnExists(engine, kS1, QStringLiteral("app|d2")));
}

void TestScrollEngineDesktopReap::reapDesktopStateReleasesThePopulatedDesktopsWindows()
{
    // A destroyed desktop's windows are ALIVE — KWin relocates them — so the
    // reap must ANNOUNCE them, not drop them silently. Falsifies reverting the
    // desktop prune to a bare bookkeeping drop: without windowsReleased the
    // daemon's tracking service and the effect's float cache keep holding
    // windows this engine no longer manages, and a scroll-floated window never
    // gets its snap-float cleared.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->setCurrentDesktopForScreen(kS1, 2);
    engine->windowOpened(QStringLiteral("app|tiled"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|floated"), kS1, 0, 0);
    engine->setWindowFloat(QStringLiteral("app|floated"), true, kS1);
    QVERIFY(engine->isModeSpecificFloated(QStringLiteral("app|floated")));

    QSignalSpy releasedSpy(engine, &PhosphorEngine::PlacementEngineBase::windowsReleased);
    engine->reapDesktopState(2);
    QCOMPARE(releasedSpy.count(), 1);
    const QStringList released = releasedSpy.first().first().toStringList();
    QVERIFY(released.contains(QStringLiteral("app|tiled")));
    QVERIFY(released.contains(QStringLiteral("app|floated")));

    // The per-window side maps are swept only AFTER the emit, so the float
    // marker the handler reads is still true DURING it — but gone once the
    // call returns, along with the tracking entry.
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|tiled")));
    QVERIFY(!engine->isModeSpecificFloated(QStringLiteral("app|floated")));
}

void TestScrollEngineDesktopReap::reapDesktopStateKeepsTheSurvivingSiblingsScreenBookkeeping()
{
    // The reap releases the dying context with clearScreenBookkeeping=false:
    // the SCREEN survives, so its per-screen maps belong to the sibling
    // contexts that are still live. Falsifies flipping that argument to true —
    // the tab-strip latch would be dropped (and a stale "[]" broadcast while
    // the live desktop still shows a tabbed column) and the pending order seed
    // would be discarded before the transition it was captured for ever
    // arrived.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    // Desktop 2 is the survivor: give it a tabbed column, which latches the
    // screen's tab-strip state. The latch is asserted POSITIVELY below before
    // the reap: without that control the negative assertion further down passes
    // just as happily when toggleColumnTabbed never latched anything at all.
    QSignalSpy latched(engine, &ScrollEngine::tabStripsChanged);
    engine->setCurrentDesktopForScreen(kS1, 2);
    engine->windowOpened(QStringLiteral("app|tab1"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|tab2"), kS1, 0, 0);
    engine->focusColumnFirst(kS1);
    engine->consumeWindowIntoColumn(kS1);
    engine->toggleColumnTabbed(kS1);
    QCoreApplication::processEvents();
    bool sawLatch = false;
    for (const QList<QVariant>& emission : latched) {
        if (emission.at(0).toString() == kS1 && emission.at(1).toString() != QStringLiteral("[]")) {
            sawLatch = true;
        }
    }
    QVERIFY2(sawLatch, "setup must actually latch a tab strip, or the negative below is vacuous");

    // A mode-transition seed for the same screen, naming windows that have
    // not arrived yet (B before A).
    engine->setInitialWindowOrder(kS1, {QStringLiteral("app|B"), QStringLiteral("app|A")});

    // Desktop 1 is the one about to die, populated so the reap takes its full
    // release path. Its window is not in the seed, so the open cannot consume
    // one of the entries under test.
    engine->setCurrentDesktopForScreen(kS1, 1);
    engine->windowOpened(QStringLiteral("app|dying"), kS1, 0, 0);
    engine->setCurrentDesktopForScreen(kS1, 2);

    QSignalSpy strips(engine, &ScrollEngine::tabStripsChanged);
    engine->reapDesktopState(1);
    // The clear broadcast is queued, so give the event loop a turn before
    // concluding it never came.
    QTest::qWait(10);
    for (const QList<QVariant>& emission : strips) {
        QVERIFY2(!(emission.at(0).toString() == kS1 && emission.at(1).toString() == QStringLiteral("[]")),
                 "the surviving sibling context still owns the screen's tab strip");
    }

    // The seed survived too: A arrives first but the seed puts B ahead of it.
    engine->windowOpened(QStringLiteral("app|A"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|B"), kS1, 0, 0);
    const QStringList order = engine->managedWindowOrder(kS1);
    QVERIFY(order.contains(QStringLiteral("app|A")));
    QVERIFY(order.contains(QStringLiteral("app|B")));
    QVERIFY2(order.indexOf(QStringLiteral("app|B")) < order.indexOf(QStringLiteral("app|A")),
             "a surviving order seed must still place the later arrival");
}

void TestScrollEngineDesktopReap::renumberDesktopStateShiftsKeysAndKeepsWindows()
{
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->setCurrentDesktopForScreen(kS1, 3);
    engine->windowOpened(QStringLiteral("app|d3"), kS1, 0, 0);
    engine->setCurrentDesktopForScreen(kS1, 4);
    engine->windowOpened(QStringLiteral("app|d4"), kS1, 0, 0);
    QCOMPARE(engine->desktopsWithActiveState(), (QSet<int>{3, 4}));

    // Desktop 2 died elsewhere: 3→2, 4→3. MUTATION GUARD: after the pass no
    // stale pre-shift int survives, and the windows stay resolvable under
    // the screen's shifted current desktop.
    QHash<int, int> mapping;
    mapping.insert(3, 2);
    mapping.insert(4, 3);
    engine->renumberDesktopState(mapping);
    QCOMPARE(engine->desktopsWithActiveState(), (QSet<int>{2, 3}));
    // The tracker shifted with the states (the screen sat on old-4 = new-3),
    // so the current context still resolves the d4 window's column.
    QVERIFY(columnExists(engine, kS1, QStringLiteral("app|d4")));
    engine->setCurrentDesktopForScreen(kS1, 2);
    QVERIFY(columnExists(engine, kS1, QStringLiteral("app|d3")));
}

void TestScrollEngineDesktopReap::renumberDesktopStateMovesTheStripStash()
{
    // The AUX maps must renumber with the states: a stash left keyed at the
    // old desktop int would restore into the wrong (possibly dead) context.
    // Falsifies dropping the renumberDesktopKeyedHash(m_stripStash, ...)
    // call from engine_context.cpp.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->setCurrentDesktopForScreen(kS1, 3);
    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    // Mode reassignment stashes the strip under S1|3|.
    engine->setActiveScreens({});
    QVERIFY(engine->serializeStripState().contains(QStringLiteral("S1|3|")));

    // Desktop 2 died elsewhere: 3→2.
    QHash<int, int> mapping;
    mapping.insert(3, 2);
    engine->renumberDesktopState(mapping);

    const QJsonObject blob = engine->serializeStripState();
    QVERIFY(blob.contains(QStringLiteral("S1|2|")));
    QVERIFY(!blob.contains(QStringLiteral("S1|3|")));
}

void TestScrollEngineDesktopReap::renumberDesktopStateMovesPerScreenOverrides()
{
    // Same class of guard for m_perScreenOverrides: the override map keys by
    // full context, the tracker shifts on renumber, and the two must move
    // together or the screen silently loses its overrides.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->setCurrentDesktopForScreen(kS1, 3);
    QVariantMap overrides;
    overrides.insert(ScrollPerScreenKeys::centerFocusedColumn(), true);
    engine->applyPerScreenConfig(kS1, overrides);
    QVERIFY(!engine->perScreenOverrides(kS1).isEmpty());

    QHash<int, int> mapping;
    mapping.insert(3, 2);
    engine->renumberDesktopState(mapping);

    // Read through an ABSOLUTE desktop number, not the current one: the tracker
    // shifts with the states, so asking through the current key both before and
    // after would pass with BOTH renumber arms deleted.
    engine->setCurrentDesktopForScreen(kS1, 2);
    QVERIFY(!engine->perScreenOverrides(kS1).isEmpty());
    QCOMPARE(engine->perScreenOverrides(kS1).value(ScrollPerScreenKeys::centerFocusedColumn()).toBool(), true);
    // And nothing is left behind at the old number.
    engine->setCurrentDesktopForScreen(kS1, 3);
    QVERIFY(engine->perScreenOverrides(kS1).isEmpty());
}

void TestScrollEngineDesktopReap::reapDesktopStateASecondTimeIsASilentNoOp()
{
    // KWin can settle a desktop removal in more than one report, and the
    // reconciler drives the reap off each. The second pass has nothing left to
    // release, and must not re-announce the windows the first one handed back:
    // the daemon's restore consumers would re-home an already re-homed window.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("app|d1"), kS1, 0, 0);
    QSignalSpy releasedSpy(engine, &PhosphorEngine::PlacementEngineBase::windowsReleased);
    engine->reapDesktopState(1);
    QCOMPARE(releasedSpy.count(), 1);

    releasedSpy.clear();
    engine->reapDesktopState(1);
    QCOMPARE(releasedSpy.count(), 0);
    QVERIFY(engine->desktopsWithActiveState().isEmpty());
}

void TestScrollEngineDesktopReap::reapDesktopStateLeavesAReusedDesktopIndexEmpty()
{
    // KWin hands the index of a destroyed desktop straight back out, so the
    // reap has to leave the NUMBER clean, not just the state object: anything
    // surviving at the old key resurfaces as the next desktop's opening state.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("app|old"), kS1, 0, 0);
    QVariantMap overrides;
    overrides.insert(ScrollPerScreenKeys::centerFocusedColumn(), true);
    engine->applyPerScreenConfig(kS1, overrides);
    QVERIFY(!engine->perScreenOverrides(kS1).isEmpty());

    engine->reapDesktopState(1);

    // Desktop 1 handed back out. Nothing from its predecessor may answer here.
    QVERIFY(engine->perScreenOverrides(kS1).isEmpty());
    engine->windowOpened(QStringLiteral("app|new"), kS1, 0, 0);
    QCOMPARE(engine->managedWindowOrder(kS1), (QStringList{QStringLiteral("app|new")}));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|old")));
}

void TestScrollEngineDesktopReap::reapDesktopStateSweepsTheStripStash()
{
    // The reap's sweepStripStash arm, which nothing else in the suite reaches:
    // a stash left at the dead key restores into whatever desktop takes the
    // number next. Falsifies deleting that call.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    // The screen leaves scrolling, which stashes the strip under S1|1|.
    engine->setActiveScreens({});
    QVERIFY(engine->serializeStripState().contains(QStringLiteral("S1|1|")));

    engine->reapDesktopState(1);
    QVERIFY(!engine->serializeStripState().contains(QStringLiteral("S1|1|")));
}

void TestScrollEngineDesktopReap::reapDesktopStateSweepsTheBurstMarker()
{
    // The context-keyed burst marker outlives its desktop unless the reap
    // sweeps it, and a marker at a key the screen no longer resolves to takes
    // endArrivalBurst's SKIP arm — which, with no live key for that screen in
    // the same burst, also drops the screen's mode-transition focus seed. So a
    // stale marker eats a seed captured for an unrelated later transition.
    // Falsifies deleting the m_burstPendingApplies loop in pruneStatesForDesktop.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->setCurrentDesktopForScreen(kS1, 2);
    engine->beginArrivalBurst();
    engine->windowOpened(QStringLiteral("app|burst"), kS1, 0, 0); // marker at S1|2|
    // The screen moves off desktop 2, then desktop 2 is destroyed.
    engine->setCurrentDesktopForScreen(kS1, 1);
    engine->reapDesktopState(2);
    // Seeded AFTER the reap: the reap's stateless-screen sweep clears seeds for
    // a screen it left with no state, which is not the arm under test.
    engine->setInitialFocusedWindow(kS1, QStringLiteral("app|x"));
    engine->endArrivalBurst();

    // A later, unrelated burst. app|y arrives last and would own focus on its
    // own; a surviving seed names app|x, so the two outcomes differ.
    engine->beginArrivalBurst();
    engine->windowOpened(QStringLiteral("app|x"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|y"), kS1, 0, 0);
    engine->endArrivalBurst();
    QCOMPARE(engine->managedFocusedWindow(kS1), QStringLiteral("app|x"));
}

void TestScrollEngineDesktopReap::reapDesktopStateCancelsALiveDragInsertPreview()
{
    // A preview captures its keys by VALUE, so a reap that destroyed the state
    // under it would leave the preview pointing at a dead key and its commit
    // would materialise a fresh empty state there.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->setCurrentDesktopForScreen(kS1, 2);
    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("app|b"), kS1));
    QCOMPARE(engine->dragInsertPreviewWindowId(), QStringLiteral("app|b"));

    engine->reapDesktopState(2);
    QVERIFY(engine->dragInsertPreviewWindowId().isEmpty());
}

void TestScrollEngineDesktopReap::activityPruneReleasesItsWindowsAndSweepsTheBurstMarker()
{
    // The activity axis moved to a FULL release in the same pass as the desktop
    // one, and it is the bigger behaviour change of the two: this path used to
    // emit nothing at all. Its burst-marker sweep is the desktop arm's twin and
    // is deletable with the rest of the suite green.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->setCurrentActivity(QStringLiteral("act-old"));
    QSignalSpy releasedSpy(engine, &PhosphorEngine::PlacementEngineBase::windowsReleased);
    engine->beginArrivalBurst();
    engine->windowOpened(QStringLiteral("app|gone"), kS1, 0, 0); // marker at S1|1|act-old
    engine->setCurrentActivity(QStringLiteral("act-new"));
    engine->pruneStatesForActivities({QStringLiteral("act-new")});

    QCOMPARE(releasedSpy.count(), 1);
    QVERIFY(releasedSpy.first().first().toStringList().contains(QStringLiteral("app|gone")));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|gone")));

    // Same seed-eating discriminator as the desktop arm above.
    engine->setInitialFocusedWindow(kS1, QStringLiteral("app|x"));
    engine->endArrivalBurst();
    engine->beginArrivalBurst();
    engine->windowOpened(QStringLiteral("app|x"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|y"), kS1, 0, 0);
    engine->endArrivalBurst();
    QCOMPARE(engine->managedFocusedWindow(kS1), QStringLiteral("app|x"));
}

void TestScrollEngineDesktopReap::activityPruneClearsATrackedDeadActivity()
{
    // The tracker keeps naming a deleted activity until the compositor's
    // currentActivityChanged arrives. Until then currentKeyForScreen answers
    // under the dead name and stateForKey(create) rebuilds state there, undoing
    // the prune one placement at a time. The prune therefore clears the tracked
    // activity back to the empty "no activity context" state.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->setCurrentActivity(QStringLiteral("act-old"));
    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);

    // act-old is both stale AND the tracked current activity.
    engine->pruneStatesForActivities({QStringLiteral("act-other")});
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);

    // The rebuilt strip keys under the EMPTY activity dimension, not the dead
    // name. Key spelling is "screen|desktop|activity".
    const QJsonObject blob = engine->serializeStripState();
    QVERIFY(blob.contains(QStringLiteral("S1|1|")));
    QVERIFY(!blob.contains(QStringLiteral("S1|1|act-old")));
}

void TestScrollEngineDesktopReap::renumberDesktopStateMovesTheBurstMarker()
{
    // The burst marker has to shift WITH the states: left at the old number it
    // no longer matches the screen's (also shifted) current key, so the drain
    // skips it and the deferred apply plus its focus restore are lost.
    // Falsifies dropping renumberDesktopKeyedHash(m_burstPendingApplies, ...).
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->setCurrentDesktopForScreen(kS1, 3);
    // The seed names the FIRST arrival, so a consumed seed and a dropped one
    // give different answers (app|b arrives last and owns focus on its own).
    engine->setInitialFocusedWindow(kS1, QStringLiteral("app|a"));
    engine->beginArrivalBurst();
    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);

    QHash<int, int> mapping;
    mapping.insert(3, 2);
    engine->renumberDesktopState(mapping);
    engine->endArrivalBurst();

    QCOMPARE(engine->managedFocusedWindow(kS1), QStringLiteral("app|a"));
}

void TestScrollEngineDesktopReap::renumberDesktopStateRefusesAPoisonedMapping()
{
    // A target below 1 is refused WHOLE, by every map: KWin desktops are
    // 1-based, and a partial shift would strand one key on a number its
    // siblings just moved onto. The refusal is gated before any side effect, so
    // a live drag-insert preview survives it too — an operation that does
    // nothing must undo nothing.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->setCurrentDesktopForScreen(kS1, 3);
    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    QVariantMap overrides;
    overrides.insert(ScrollPerScreenKeys::centerFocusedColumn(), true);
    engine->applyPerScreenConfig(kS1, overrides);
    QVERIFY(engine->beginDragInsertPreview(QStringLiteral("app|b"), kS1));

    QHash<int, int> poisoned;
    poisoned.insert(3, 0);
    engine->renumberDesktopState(poisoned);

    QCOMPARE(engine->desktopsWithActiveState(), (QSet<int>{3}));
    QCOMPARE(engine->dragInsertPreviewWindowId(), QStringLiteral("app|b"));
    QVERIFY(columnExists(engine, kS1, QStringLiteral("app|a")));
    QVERIFY(!engine->perScreenOverrides(kS1).isEmpty());
}

void TestScrollEngineDesktopReap::focusedColumnWindowsRefusesAnAmbiguousPhysicalOutput()
{
    // focusedColumnWindows is addressed with a PHYSICAL output id (the daemon's
    // per-output desktop map keys physical ids) and resolves it through
    // scrollingScreenForPhysical. When that output is split into two scrolling
    // virtual sub-screens and the user is looking at a THIRD monitor, no
    // tie-break can name the right one, and the caller does not merely report
    // the answer, it relocates that sub-screen's focused column. So the
    // resolution refuses.
    //
    // Reverting the refusal (falling back to the lexicographic minimum) makes
    // the first QVERIFY fail: the getter answers with vs:0's column, and the
    // move-column-to-workspace verb then silently relocates a column the user
    // never addressed.
    const QString vs0 = QStringLiteral("DP-1/vs:0");
    const QString vs1 = QStringLiteral("DP-1/vs:1");
    const QString other = QStringLiteral("DP-2");

    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeProviderEngine(&owner, {vs0, vs1, other});
    engine->setEngineSettings(settings);
    engine->refreshConfigFromSettings();

    engine->windowOpened(QStringLiteral("app|split"), vs0, 0, 0);
    engine->windowOpened(QStringLiteral("app|elsewhere"), other, 0, 0);

    // Active screen on the OTHER monitor: "DP-1" is ambiguous.
    engine->setActiveScreenHint(other);
    QVERIFY(engine->focusedColumnWindows(QStringLiteral("DP-1")).isEmpty());

    // The same address is answerable once the active screen IS one of the
    // sub-screens: the refusal is about ambiguity, not about sub-screens.
    engine->setActiveScreenHint(vs0);
    QCOMPARE(engine->focusedColumnWindows(QStringLiteral("DP-1")), QStringList{QStringLiteral("app|split")});
}

void TestScrollEngineDesktopReap::focusedColumnWindowsResolvesASingleSubScreen()
{
    // The unambiguous half of the same resolution: one scrolling sub-screen on
    // the named output is answered even with the active screen elsewhere, so
    // the refusal above cannot be satisfied by simply never resolving.
    const QString vs0 = QStringLiteral("DP-1/vs:0");
    const QString other = QStringLiteral("DP-2");

    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeProviderEngine(&owner, {vs0, other});
    engine->setEngineSettings(settings);
    engine->refreshConfigFromSettings();

    engine->windowOpened(QStringLiteral("app|only"), vs0, 0, 0);
    engine->windowOpened(QStringLiteral("app|elsewhere"), other, 0, 0);
    engine->setActiveScreenHint(other);

    QCOMPARE(engine->focusedColumnWindows(QStringLiteral("DP-1")), QStringList{QStringLiteral("app|only")});
}

QTEST_GUILESS_MAIN(TestScrollEngineDesktopReap)
#include "test_scrollengine_desktopreap.moc"
