// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The close-settle reflow hold (engine_closehold.cpp): a configured
// scrollingCloseReflowDelayMs makes windowClosed start a per-screen hold
// instead of reflowing immediately, so the closing window's disappear
// animation plays over an unchanged strip. These cases assert the observable
// contract through the windowsTiled batch signal: no reflow batch until the
// hold expires, a second close pushing the one deadline, and the zero-delay
// default keeping the historical immediate reflow.
//
// Timing: the holds here are wall-clock (QTimer-driven), so the delays are
// sized with wide margins — the mid-hold probes sit at less than half the
// deadline and the expiry waits use QTRY with a multiple of it.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include "scrollstriptestutils.h"
#include "scrollstubsettings.h"

#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;

using ScrollTestUtils::makeProviderEngine;
using ScrollTestUtils::StubScrollSettings;

namespace {
const QString kS1 = QStringLiteral("S1");
} // namespace

class TestScrollEngineCloseHold : public QObject
{
    Q_OBJECT

    static ScrollEngine* makeEngine(QObject* parent, StubScrollSettings* settings)
    {
        ScrollEngine* engine = makeProviderEngine(parent, {kS1});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();
        return engine;
    }

private Q_SLOTS:
    void zeroDelayReflowsImmediately();
    void holdDefersReflowUntilExpiry();
    void secondClosePushesTheDeadline();
    void identicalSetRetileCannotBreakTheHold();
};

void TestScrollEngineCloseHold::zeroDelayReflowsImmediately()
{
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);
    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);

    // Drain the opens' own queued retiles first: scheduleRetileForScreen posts
    // its applyLayout to the event loop, so without a turn here those land
    // inside the hold below and would be mistaken for a hold leak.
    QTest::qWait(50);
    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    engine->windowClosed(QStringLiteral("app|a"));
    // The historical contract: the reflow batch goes out inside the close
    // handler itself, no event loop needed.
    QVERIFY(tiled.count() >= 1);
}

void TestScrollEngineCloseHold::holdDefersReflowUntilExpiry()
{
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->closeReflowDelayMs = 250;
    ScrollEngine* engine = makeEngine(&owner, settings);
    engine->refreshConfigFromSettings();
    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);

    // Drain the opens' own queued retiles first: scheduleRetileForScreen posts
    // its applyLayout to the event loop, so without a turn here those land
    // inside the hold below and would be mistaken for a hold leak.
    QTest::qWait(50);
    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    engine->windowClosed(QStringLiteral("app|a"));
    // Inside the hold: the strip model already dropped the window, but no
    // reflow batch may go out (that is the hold's whole point — the corpse
    // keeps its slot on screen while its close animation plays).
    QCOMPARE(tiled.count(), 0);
    QTest::qWait(100);
    QCOMPARE(tiled.count(), 0);
    // Expiry: the scheduled flush runs the one deferred applyLayout.
    QTRY_VERIFY_WITH_TIMEOUT(tiled.count() >= 1, 1000);
}

void TestScrollEngineCloseHold::secondClosePushesTheDeadline()
{
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->closeReflowDelayMs = 400;
    ScrollEngine* engine = makeEngine(&owner, settings);
    engine->refreshConfigFromSettings();
    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);

    // Drain the opens' own queued retiles first: scheduleRetileForScreen posts
    // its applyLayout to the event loop, so without a turn here those land
    // inside the hold below and would be mistaken for a hold leak.
    QTest::qWait(50);
    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    engine->windowClosed(QStringLiteral("app|a"));
    QTest::qWait(150);
    // Latest close wins: this second close pushes the shared deadline to
    // now+400, so the FIRST close's expiry (at 400 from its own start, 250
    // from here) must not flush.
    engine->windowClosed(QStringLiteral("app|b"));
    QTest::qWait(300);
    QCOMPARE(tiled.count(), 0);
    // One flush serves both closes once the pushed deadline passes.
    QTRY_VERIFY_WITH_TIMEOUT(tiled.count() >= 1, 1000);
}

// Regression: the hold's two original arms both defer, and the strip reflowed
// anyway, because windowClosed emits placementChanged from its own last line
// and the DAEMON wires that signal synchronously back into setActiveScreens.
// The set is unchanged by a close, and that branch retiles every screen
// unconditionally through scheduleRetileForScreen — one queued turn later, well
// inside the hold. Reproduced here by wiring the same fan-out the daemon does;
// the other cases in this file run on a bare engine and so never saw it.
void TestScrollEngineCloseHold::identicalSetRetileCannotBreakTheHold()
{
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->closeReflowDelayMs = 250;
    ScrollEngine* engine = makeEngine(&owner, settings);
    engine->refreshConfigFromSettings();

    // The daemon's tiled-count gate: any placement change re-derives the engine
    // screen set and pushes it back. A close always moves the count, so the
    // gate never suppresses it, and the pushed set is identical.
    QObject::connect(engine, &PhosphorEngine::PlacementEngineBase::placementChanged, engine, [engine](const QString&) {
        engine->setActiveScreens({kS1});
    });

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    QTest::qWait(50);

    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    engine->windowClosed(QStringLiteral("app|a"));
    // The re-push's queued retile lands on the next turn. It must be swallowed
    // by the hold, not applied — this is the assertion the bug failed.
    QTest::qWait(100);
    QCOMPARE(tiled.count(), 0);
    // And the flush still delivers the one reflow once the hold expires.
    QTRY_VERIFY_WITH_TIMEOUT(tiled.count() >= 1, 1000);
}

QTEST_GUILESS_MAIN(TestScrollEngineCloseHold)
#include "test_scrollengine_closehold.moc"
