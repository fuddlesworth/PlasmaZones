// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The compositor's fullscreen hold (ScrollEngine::setWindowFullscreenFloat):
// a strip tile entering its OWN fullscreen leaves the strip with a user
// float's slot memory but a PASSIVE announcement, comes back to the same slot,
// never takes over or undoes a user float, and is returned by a re-announce
// after an effect restart. Its own file because the maximize suite that holds
// the other fullscreen arms is near the size ceiling.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include "scrollstriptestutils.h"

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

} // namespace

class TestScrollEngineFullscreenFloat : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theHoldLeavesTheStripPassivelyAndReturnsToTheSlot();
    void aUserFloatIsNeitherTakenOverNorUndone();
    void aUserUnfloatDuringTheHoldEndsIt();
    void aReAnnounceDuringTheHoldReturnsTheWindow();
    void anUntrackedWindowIsRefused();
};

void TestScrollEngineFullscreenFloat::theHoldLeavesTheStripPassivelyAndReturnsToTheSlot()
{
    QObject owner;
    ScrollEngine* engine = threeColumns(&owner);
    ScrollState* state = stateFor(engine);
    QVERIFY(state);
    QCOMPARE(state->strip().columnCount(), 3);
    QCOMPARE(state->strip().columnOfWindow(kB), 1);

    QSignalSpy passive(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced);
    QSignalSpy active(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);

    QVERIFY(engine->setWindowFullscreenFloat(kB, true, kScreen));
    QVERIFY(state->isFloating(kB));
    QVERIFY(engine->isFullscreenFloated(kB));
    QCOMPARE(state->strip().columnCount(), 2);
    // Passive channel only: no float OSD, no free-geometry restore.
    QCOMPARE(passive.count(), 1);
    QCOMPARE(passive.last().at(0).toString(), kB);
    QCOMPARE(passive.last().at(1).toBool(), true);
    QCOMPARE(active.count(), 0);
    // A second hold of the same window changes nothing.
    QVERIFY(!engine->setWindowFullscreenFloat(kB, true, kScreen));
    QCOMPARE(passive.count(), 1);

    QVERIFY(engine->setWindowFullscreenFloat(kB, false, kScreen));
    QVERIFY(!state->isFloating(kB));
    QVERIFY(!engine->isFullscreenFloated(kB));
    QCOMPARE(state->strip().columnCount(), 3);
    QCOMPARE(state->strip().columnOfWindow(kB), 1);
    QCOMPARE(passive.count(), 2);
    QCOMPARE(passive.last().at(1).toBool(), false);
    QCOMPARE(active.count(), 0);
    // The return consumed the hold; a second return has nothing to undo.
    QVERIFY(!engine->setWindowFullscreenFloat(kB, false, kScreen));
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
    // An effect restart has no record of the hold and announces the window
    // when it leaves fullscreen; the engine treats that as the return.
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

QTEST_GUILESS_MAIN(TestScrollEngineFullscreenFloat)
#include "test_scrollengine_fullscreenfloat.moc"
