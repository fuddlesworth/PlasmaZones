// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The dynamic-workspaces desktop axis: what an identity-based reap of one
// desktop destroys and what it must leave alone, and what a renumber has to
// carry across with the shifted keys.
//
// Split out of test_scrollengine_perscreen.cpp, which had grown past the hard
// size ceiling. The split is by CONCERN, not to hit a number: the per-screen
// suite's every case is a precedence claim inside one resolution cascade
// (rule > per-screen trio > cached global), and none of these is — they are
// lifecycle claims about state destruction, and they share only that suite's
// stub settings and its three-screen fixture.

#include <PhosphorScrollEngine/IScrollSettings.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include "scrollstriptestutils.h"
#include "scrollstubsettings.h"

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
    // screen's tab-strip state.
    engine->setCurrentDesktopForScreen(kS1, 2);
    engine->windowOpened(QStringLiteral("app|tab1"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|tab2"), kS1, 0, 0);
    engine->focusColumnFirst(kS1);
    engine->consumeWindowIntoColumn(kS1);
    engine->toggleColumnTabbed(kS1);

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

    // The tracker now says desktop 2 for kS1; the overrides must have moved
    // with it (a dropped aux-map renumber leaves them stranded at key 3).
    QVERIFY(!engine->perScreenOverrides(kS1).isEmpty());
    QCOMPARE(engine->perScreenOverrides(kS1).value(ScrollPerScreenKeys::centerFocusedColumn()).toBool(), true);
}

QTEST_GUILESS_MAIN(TestScrollEngineDesktopReap)
#include "test_scrollengine_desktopreap.moc"
