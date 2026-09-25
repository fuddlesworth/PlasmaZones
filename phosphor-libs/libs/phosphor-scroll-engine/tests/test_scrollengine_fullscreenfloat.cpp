// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The compositor's fullscreen hold (ScrollEngine::setWindowFullscreenFloat):
// a strip tile entering its OWN fullscreen leaves the strip with a user
// float's slot memory but a PASSIVE announcement, comes back to the same slot
// at the same width, never takes over or undoes a user float, keeps answering
// the hold through a close until the daemon's capture has read it, and is
// returned by a re-announce when the effect has lost its record (a restart).
// Its own file because the maximize suite that holds the other fullscreen
// arms is near the size ceiling.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include <PhosphorEngine/WindowPlacement.h>

#include "scrollstriptestutils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;

using ScrollTestUtils::makeProviderEngine;

namespace {

const QString kScreen = QStringLiteral("S1");
const QString kA = QStringLiteral("app|a");
const QString kB = QStringLiteral("app|b");
const QString kC = QStringLiteral("app|c");

ScrollState* stateFor(ScrollEngine* engine)
{
    return static_cast<ScrollState*>(engine->stateForScreen(kScreen));
}

/// Three single-tile columns a, b, c with b focused.
ScrollEngine* threeColumns(QObject* owner)
{
    ScrollEngine* engine = makeProviderEngine(owner, {kScreen});
    for (const QString& id : {kA, kB, kC}) {
        engine->windowOpened(id, kScreen, 0, 0);
    }
    engine->windowFocused(kB, kScreen);
    QCoreApplication::processEvents();
    return engine;
}

/// The window ids the latest windowsTiled batch carried.
QSet<QString> windowsInLatestBatch(const QSignalSpy& tiled)
{
    QSet<QString> out;
    if (tiled.isEmpty()) {
        return out;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(tiled.last().at(0).toString().toUtf8());
    for (const QJsonValue& entry : doc.array()) {
        out.insert(entry.toObject().value(QLatin1String("windowId")).toString());
    }
    return out;
}

} // namespace

class TestScrollEngineFullscreenFloat : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Proves the vertical arm really is transposed, so a lost ENVIRONMENT
    /// property cannot leave it silently re-running the horizontal suite.
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    void theHoldLeavesTheStripPassivelyAndReturnsToTheSlot();
    void aSecondHoldAndReturnCycleLandsInTheSameSlot();
    void theHoldIsAlreadySetWhenThePassiveAnnouncementFires();
    void aUserFloatIsNeitherTakenOverNorUndone();
    void aUserUnfloatDuringTheHoldEndsIt();
    void aReAnnounceDuringTheHoldReturnsTheWindow();
    void anUntrackedWindowIsRefused();
    void aReturnWhoseStackAnchorHasClosedOpensAFreshColumn();
    void aHoldOfTheOnlyWindowEmptiesTheStripAndTheReturnRefillsIt();
    void aHeldTabReturnsToItsTabGroup();
    void aHeldWindowHasNoPlacementToCapture();
    void aCloseDuringTheHoldKeepsTheHoldAnswerUntilThePrune();
    void aReopenOfAClosedHeldWindowConsumesTheHoldAnswer();
    void aModeReleaseAnnouncesTheHoldClearBeforeWindowsReleased();
};

void TestScrollEngineFullscreenFloat::theHoldLeavesTheStripPassivelyAndReturnsToTheSlot()
{
    QObject owner;
    ScrollEngine* engine = threeColumns(&owner);
    ScrollState* state = stateFor(engine);
    QVERIFY(state);
    QCOMPARE(state->strip().columnCount(), 3);
    QCOMPARE(state->strip().columnOfWindow(kB), 1);
    // A width the context default cannot produce, so the return proves the
    // slot memory rather than a rebuild at the default.
    engine->setColumnWidth(ColumnWidth::makeFixed(377), kScreen);
    QCoreApplication::processEvents();

    QSignalSpy passive(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced);
    QSignalSpy active(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);

    // A foreign screen hint: the engine acts on, and announces, the window's
    // own tracked screen.
    QVERIFY(engine->setWindowFullscreenFloat(kB, true, QStringLiteral("S9")));
    QVERIFY(state->isFloating(kB));
    QVERIFY(engine->isFullscreenFloated(kB));
    QCOMPARE(state->strip().columnCount(), 2);
    QCoreApplication::processEvents();
    // The neighbours close up on the wire: the batch omits the held window.
    QVERIFY(!windowsInLatestBatch(tiled).contains(kB));
    QVERIFY(windowsInLatestBatch(tiled).contains(kA));
    QVERIFY(windowsInLatestBatch(tiled).contains(kC));
    // Passive channel only: no float OSD, no free-geometry restore.
    QCOMPARE(passive.count(), 1);
    QCOMPARE(passive.last().at(0).toString(), kB);
    QCOMPARE(passive.last().at(1).toBool(), true);
    QCOMPARE(passive.last().at(2).toString(), kScreen);
    QCOMPARE(active.count(), 0);
    // A re-hold of OUR hold is idempotent and answers true: the effect
    // re-enters after a lost return and must not read "refused" as "not ours".
    QVERIFY(engine->setWindowFullscreenFloat(kB, true, kScreen));
    QVERIFY(engine->isFullscreenFloated(kB));
    QCOMPARE(state->strip().columnCount(), 2);
    QCOMPARE(passive.count(), 1);

    QVERIFY(engine->setWindowFullscreenFloat(kB, false, kScreen));
    QVERIFY(!state->isFloating(kB));
    QVERIFY(!engine->isFullscreenFloated(kB));
    QCOMPARE(state->strip().columnCount(), 3);
    QCOMPARE(state->strip().columnOfWindow(kB), 1);
    QCOMPARE(state->strip().columns().at(1).width, ColumnWidth::makeFixed(377));
    QCOMPARE(passive.count(), 2);
    QCOMPARE(passive.last().at(1).toBool(), false);
    QCOMPARE(passive.last().at(2).toString(), kScreen);
    QCOMPARE(active.count(), 0);
    // The return consumed the hold; a second return has nothing to undo.
    QVERIFY(!engine->setWindowFullscreenFloat(kB, false, kScreen));
}

void TestScrollEngineFullscreenFloat::aSecondHoldAndReturnCycleLandsInTheSameSlot()
{
    QObject owner;
    ScrollEngine* engine = threeColumns(&owner);
    ScrollState* state = stateFor(engine);
    QVERIFY(state);
    QSignalSpy passive(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced);
    QSignalSpy active(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);

    for (int cycle = 0; cycle < 2; ++cycle) {
        QVERIFY2(engine->setWindowFullscreenFloat(kB, true, kScreen),
                 qPrintable(QStringLiteral("hold, cycle %1").arg(cycle)));
        QCOMPARE(state->strip().columnCount(), 2);
        QVERIFY(engine->isFullscreenFloated(kB));
        QVERIFY2(engine->setWindowFullscreenFloat(kB, false, kScreen),
                 qPrintable(QStringLiteral("return, cycle %1").arg(cycle)));
        QCoreApplication::processEvents();
        QCOMPARE(state->strip().columnCount(), 3);
        QCOMPARE(state->strip().columnOfWindow(kB), 1);
        QVERIFY(!state->isFloating(kB));
        QVERIFY(!engine->isFullscreenFloated(kB));
    }
    // Two announcements per cycle, all passive.
    QCOMPARE(passive.count(), 4);
    QCOMPARE(active.count(), 0);
    // A THIRD return has nothing of ours left to undo.
    QVERIFY(!engine->setWindowFullscreenFloat(kB, false, kScreen));
}

void TestScrollEngineFullscreenFloat::theHoldIsAlreadySetWhenThePassiveAnnouncementFires()
{
    QObject owner;
    ScrollEngine* engine = threeColumns(&owner);
    int seen = 0;
    bool heldInsideHandler = false;
    QObject::connect(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced, engine,
                     [&](const QString& windowId, bool floating, const QString&) {
                         if (windowId == kB && floating) {
                             ++seen;
                             // The daemon's capture runs synchronously inside
                             // this announcement and classifies the float by
                             // this probe.
                             heldInsideHandler = engine->isFullscreenFloated(kB);
                         }
                     });
    QVERIFY(engine->setWindowFullscreenFloat(kB, true, kScreen));
    QCOMPARE(seen, 1);
    QVERIFY2(
        heldInsideHandler,
        "the hold must be on the slot before the passive announcement, or the daemon captures it as a plain float");
}

void TestScrollEngineFullscreenFloat::aUserFloatIsNeitherTakenOverNorUndone()
{
    QObject owner;
    ScrollEngine* engine = threeColumns(&owner);
    ScrollState* state = stateFor(engine);
    QVERIFY(state);

    engine->setWindowFloat(kB, true, kScreen);
    QVERIFY(state->isFloating(kB));
    QVERIFY(!engine->setWindowFullscreenFloat(kB, true, kScreen));
    QVERIFY(!engine->isFullscreenFloated(kB));
    QVERIFY(!engine->setWindowFullscreenFloat(kB, false, kScreen));
    QVERIFY(state->isFloating(kB));
}

void TestScrollEngineFullscreenFloat::aUserUnfloatDuringTheHoldEndsIt()
{
    QObject owner;
    ScrollEngine* engine = threeColumns(&owner);
    ScrollState* state = stateFor(engine);
    QVERIFY(state);

    QVERIFY(engine->setWindowFullscreenFloat(kB, true, kScreen));
    engine->setWindowFloat(kB, false, kScreen);
    QVERIFY(!state->isFloating(kB));
    QVERIFY(!engine->isFullscreenFloated(kB));
    // The user's unfloat lands in the same slot the hold remembered.
    QCOMPARE(state->strip().columnCount(), 3);
    QCOMPARE(state->strip().columnOfWindow(kB), 1);
    // The compositor's later return finds nothing of its own to undo.
    QVERIFY(!engine->setWindowFullscreenFloat(kB, false, kScreen));
    QVERIFY(!state->isFloating(kB));
}

void TestScrollEngineFullscreenFloat::aReAnnounceDuringTheHoldReturnsTheWindow()
{
    QObject owner;
    ScrollEngine* engine = threeColumns(&owner);
    ScrollState* state = stateFor(engine);
    QVERIFY(state);

    QVERIFY(engine->setWindowFullscreenFloat(kB, true, kScreen));
    QCOMPARE(state->strip().columnCount(), 2);
    // An effect that lost its record of the hold (a restart) announces the
    // window when it leaves fullscreen; the engine treats that as the return.
    engine->windowOpened(kB, kScreen, 0, 0);
    QCoreApplication::processEvents();
    QVERIFY(!state->isFloating(kB));
    QVERIFY(!engine->isFullscreenFloated(kB));
    QCOMPARE(state->strip().columnCount(), 3);
    QCOMPARE(state->strip().columnOfWindow(kB), 1);
}

void TestScrollEngineFullscreenFloat::anUntrackedWindowIsRefused()
{
    QObject owner;
    ScrollEngine* engine = threeColumns(&owner);
    QVERIFY(!engine->setWindowFullscreenFloat(QStringLiteral("app|zzz"), true, kScreen));
    QVERIFY(!engine->setWindowFullscreenFloat(QStringLiteral("app|zzz"), false, kScreen));
    QVERIFY(!engine->isFullscreenFloated(QStringLiteral("app|zzz")));
}

void TestScrollEngineFullscreenFloat::aReturnWhoseStackAnchorHasClosedOpensAFreshColumn()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {kScreen});
    engine->windowOpened(kA, kScreen, 0, 0);
    engine->windowOpened(kB, kScreen, 0, 0);
    engine->windowOpened(kC, kScreen, 0, 0);
    // Fold b into a's column so the hold records a as b's stack anchor.
    engine->windowFocused(kA, kScreen);
    engine->consumeWindowIntoColumn(kScreen);
    engine->windowFocused(kB, kScreen);
    QCoreApplication::processEvents();
    ScrollState* state = stateFor(engine);
    QVERIFY(state);
    QCOMPARE(state->strip().columnCount(), 2);
    QCOMPARE(state->strip().columnOfWindow(kA), state->strip().columnOfWindow(kB));

    QVERIFY(engine->setWindowFullscreenFloat(kB, true, kScreen));
    engine->windowClosed(kA);
    QCoreApplication::processEvents();
    QCOMPARE(state->strip().columnCount(), 1);

    QVERIFY(engine->setWindowFullscreenFloat(kB, false, kScreen));
    QCoreApplication::processEvents();
    QVERIFY(!state->isFloating(kB));
    QVERIFY(!engine->isFullscreenFloated(kB));
    QCOMPARE(state->strip().columnCount(), 2);
    QVERIFY(state->strip().columnOfWindow(kB) >= 0);
    QVERIFY(state->strip().columnOfWindow(kB) != state->strip().columnOfWindow(kC));
}

void TestScrollEngineFullscreenFloat::aHoldOfTheOnlyWindowEmptiesTheStripAndTheReturnRefillsIt()
{
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {kScreen});
    engine->windowOpened(kA, kScreen, 0, 0);
    QCoreApplication::processEvents();
    ScrollState* state = stateFor(engine);
    QVERIFY(state);
    QCOMPARE(state->strip().columnCount(), 1);

    QVERIFY(engine->setWindowFullscreenFloat(kA, true, kScreen));
    QCOMPARE(state->strip().columnCount(), 0);
    QVERIFY(engine->isFullscreenFloated(kA));

    QVERIFY(engine->setWindowFullscreenFloat(kA, false, kScreen));
    QCoreApplication::processEvents();
    QCOMPARE(state->strip().columnCount(), 1);
    QCOMPARE(state->strip().columnOfWindow(kA), 0);
    QVERIFY(!state->isFloating(kA));
}

void TestScrollEngineFullscreenFloat::aHeldTabReturnsToItsTabGroup()
{
    // The launcher-plus-game shape: the game is a TAB of its launcher's
    // column when it goes fullscreen, and comes back as that tab.
    QObject owner;
    ScrollEngine* engine = makeProviderEngine(&owner, {kScreen});
    engine->windowOpened(kA, kScreen, 0, 0);
    engine->windowOpened(kB, kScreen, 0, 0);
    engine->windowFocused(kA, kScreen);
    engine->consumeWindowIntoColumn(kScreen);
    engine->toggleColumnTabbed(kScreen);
    engine->windowFocused(kB, kScreen);
    QCoreApplication::processEvents();
    ScrollState* state = stateFor(engine);
    QVERIFY(state);
    QCOMPARE(state->strip().columnCount(), 1);
    QCOMPARE(state->strip().columns().first().display, ColumnDisplay::Tabbed);

    QVERIFY(engine->setWindowFullscreenFloat(kB, true, kScreen));
    QCOMPARE(state->strip().columnCount(), 1);
    QCOMPARE(state->strip().columns().first().display, ColumnDisplay::Tabbed);
    QVERIFY(!state->strip().containsWindow(kB));

    QVERIFY(engine->setWindowFullscreenFloat(kB, false, kScreen));
    QCoreApplication::processEvents();
    QCOMPARE(state->strip().columnCount(), 1);
    QCOMPARE(state->strip().columnOfWindow(kB), state->strip().columnOfWindow(kA));
    QCOMPARE(state->strip().columns().first().display, ColumnDisplay::Tabbed);
    QCOMPARE(state->strip().activeWindowId(), kB);
}

void TestScrollEngineFullscreenFloat::aHeldWindowHasNoPlacementToCapture()
{
    QObject owner;
    ScrollEngine* engine = threeColumns(&owner);
    // Tiled: captured as a tiled slot, the ordinary answer.
    const auto before = engine->capturePlacement(kB);
    QVERIFY(before.has_value());
    QCOMPARE(before->engines.value(engine->engineId()).state, QString(PhosphorEngine::WindowPlacement::stateTiled()));

    QVERIFY(engine->setWindowFullscreenFloat(kB, true, kScreen));
    // A hold is not placement intent: recording it as a float slot would
    // re-float the window, hold-less, on the next reopen or mode round trip.
    QVERIFY(!engine->capturePlacement(kB).has_value());

    QVERIFY(engine->setWindowFullscreenFloat(kB, false, kScreen));
    const auto after = engine->capturePlacement(kB);
    QVERIFY(after.has_value());
    QCOMPARE(after->engines.value(engine->engineId()).state, QString(PhosphorEngine::WindowPlacement::stateTiled()));
}

void TestScrollEngineFullscreenFloat::aCloseDuringTheHoldKeepsTheHoldAnswerUntilThePrune()
{
    QObject owner;
    ScrollEngine* engine = threeColumns(&owner);
    ScrollState* state = stateFor(engine);
    QVERIFY(state);
    QVERIFY(engine->setWindowFullscreenFloat(kB, true, kScreen));

    engine->windowClosed(kB);
    QCoreApplication::processEvents();
    QCOMPARE(state->strip().columnCount(), 2);
    QVERIFY(!engine->isWindowTracked(kB));
    // The daemon's close capture runs AFTER Tiling.windowClosed and asks
    // this, so the answer has to outlive the slot.
    QVERIFY(engine->isFullscreenFloated(kB));
    // The compositor's later return has no slot to undo.
    QVERIFY(!engine->setWindowFullscreenFloat(kB, false, kScreen));
    QCOMPARE(state->strip().columnCount(), 2);
    // Spent by the capture; reaped on aliveness as the belt.
    engine->forgetClosedFullscreenHold(kB);
    QVERIFY(!engine->isFullscreenFloated(kB));
    QVERIFY(engine->setWindowFullscreenFloat(kA, true, kScreen));
    engine->windowClosed(kA);
    QVERIFY(engine->isFullscreenFloated(kA));
    engine->pruneStaleWindows({kC});
    QCoreApplication::processEvents();
    QVERIFY(!engine->isFullscreenFloated(kA));
    QCOMPARE(state->strip().columnCount(), 1);
}

void TestScrollEngineFullscreenFloat::aReopenOfAClosedHeldWindowConsumesTheHoldAnswer()
{
    QObject owner;
    ScrollEngine* engine = threeColumns(&owner);
    ScrollState* state = stateFor(engine);
    QVERIFY(state);
    QVERIFY(engine->setWindowFullscreenFloat(kB, true, kScreen));
    engine->windowClosed(kB);
    QVERIFY(engine->isFullscreenFloated(kB));
    engine->windowOpened(kB, kScreen, 0, 0);
    QCoreApplication::processEvents();
    QVERIFY(engine->isWindowTracked(kB));
    QVERIFY(!engine->isFullscreenFloated(kB));
    QVERIFY(!state->isFloating(kB));
    QCOMPARE(state->strip().columnCount(), 3);
}

void TestScrollEngineFullscreenFloat::aModeReleaseAnnouncesTheHoldClearBeforeWindowsReleased()
{
    QObject owner;
    ScrollEngine* engine = threeColumns(&owner);
    QVERIFY(engine->setWindowFullscreenFloat(kB, true, kScreen));
    QVERIFY(engine->isModeSpecificFloated(kB));

    int order = 0;
    int passiveAt = -1;
    int releasedAt = -1;
    bool trackedAtAnnounce = true;
    bool markerVisibleInRelease = false;
    QObject::connect(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced, engine,
                     [&](const QString& windowId, bool floating, const QString&) {
                         if (windowId == kB && !floating) {
                             passiveAt = order++;
                             // The daemon's else-arm clears the marker only
                             // for a TRACKED window; a released one must read
                             // untracked here.
                             trackedAtAnnounce = engine->isWindowTracked(kB);
                         }
                     });
    QObject::connect(engine, &PhosphorEngine::PlacementEngineBase::windowsReleased, engine,
                     [&](const QStringList& ids, const QSet<QString>&) {
                         if (ids.contains(kB)) {
                             releasedAt = order++;
                             markerVisibleInRelease = engine->isModeSpecificFloated(kB);
                         }
                     });

    engine->setActiveScreens({});
    QVERIFY2(passiveAt >= 0, "the release must announce the hold clear");
    QVERIFY2(releasedAt >= 0, "the release must emit windowsReleased for b");
    QVERIFY2(passiveAt < releasedAt, "the passive clear must precede windowsReleased");
    QVERIFY(!trackedAtAnnounce);
    QVERIFY2(markerVisibleInRelease,
             "the mode-specific float marker is an INPUT to the windowsReleased handler and must survive the announce");
    QVERIFY(!engine->isFullscreenFloated(kB));
}

QTEST_GUILESS_MAIN(TestScrollEngineFullscreenFloat)
#include "test_scrollengine_fullscreenfloat.moc"
