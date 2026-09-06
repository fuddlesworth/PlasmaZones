// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The compositor focus-report protocol (engine_focus.cpp): which reports the
// self-activation echo filter swallows and which it adopts. Its own file
// rather than more of the verbs suite, which is past the size ceiling and
// whose subject is the verbs themselves, not the report ordering underneath
// them.
//
// Every case here runs on the BARE fixture (no auto-echo): the whole subject
// is the ORDER in which the engine's own activation echo and the compositor's
// genuine reports land, and makeProviderEngine's synchronous echo collapses
// that ordering before an assertion can observe it.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>

#include "scrollstriptestutils.h"

#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;

namespace {

const QString kS1 = QStringLiteral("S1");

} // namespace

class TestScrollEngineFocusReport : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    void closeEchoAfterTheCompositorsOwnPickWinsTheStrip();
    void staleEchoWithALaterActivationQueuedIsStillSwallowed();

private:
    /// a | b | c as three columns on S1, focus on c (arrival order), with REAL
    /// geometry providers (applyLayout bails at its work-area guard without
    /// one, long before the activation this suite observes) but WITHOUT
    /// makeProviderEngine's auto-echo.
    static ScrollEngine* threeWindows(QObject* parent)
    {
        auto* engine = new ScrollEngine(nullptr, nullptr, parent);
        const ScrollTestUtils::GeometryFn geometry = [](const QString&) {
            return ScrollTestUtils::defaultScreenRect();
        };
        engine->setScreenGeometryProviders(geometry, geometry);
        engine->setActiveScreens({kS1});
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
        // Each open activated its arrival and queued the echo. Drain them in
        // order, as the compositor would, so the queue is empty when a case
        // starts and the reports it hand-delivers land against known state.
        engine->windowFocused(QStringLiteral("app|a"), kS1);
        engine->windowFocused(QStringLiteral("app|b"), kS1);
        engine->windowFocused(QStringLiteral("app|c"), kS1);
        return engine;
    }

    static ScrollState* stateFor(ScrollEngine* engine)
    {
        return static_cast<ScrollState*>(engine->stateForScreen(kS1));
    }
};

void TestScrollEngineFocusReport::closeEchoAfterTheCompositorsOwnPickWinsTheStrip()
{
    // The close of the middle column races two activations: the compositor
    // picks its own successor and reports it, and the engine asks for the
    // right neighbour. The compositor's report is in flight first (it is
    // queued before the daemon even hears the close), so the strip moves onto
    // its pick; the engine's activation then really takes focus, and its echo
    // is the last word. The echo filter must let that final echo through.
    // Swallowing it left the strip centred on the compositor's pick while the
    // focus ring sat on the engine's (the "right one has focus, left one is
    // centred" report).
    QObject owner;
    ScrollEngine* engine = threeWindows(&owner);
    ScrollState* state = stateFor(engine);
    QVERIFY(state);
    engine->windowFocused(QStringLiteral("app|b"), kS1);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|b"));

    QSignalSpy activate(engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested);
    engine->windowClosed(QStringLiteral("app|b"));
    QCOMPARE(activate.count(), 1);
    QCOMPARE(activate.last().at(0).toString(), QStringLiteral("app|c"));
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|c"));

    // The compositor's own successor pick lands first and is genuine focus.
    engine->windowFocused(QStringLiteral("app|a"), kS1);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));

    // The engine's activation echo: nothing later is queued and the strip
    // disagrees with it, so it is adopted rather than swallowed.
    engine->windowFocused(QStringLiteral("app|c"), kS1);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|c"));
}

void TestScrollEngineFocusReport::staleEchoWithALaterActivationQueuedIsStillSwallowed()
{
    // The swallow the filter exists for: a rapid focus scroll queues a second
    // activation before the first echo lands, and that stale first echo must
    // NOT rewind the strip (the next step would then advance from the rewound
    // column and skip one). The adopt arm above is gated on the queue's tail
    // being EMPTY precisely so this case keeps its swallow.
    QObject owner;
    ScrollEngine* engine = threeWindows(&owner);
    ScrollState* state = stateFor(engine);
    QVERIFY(state);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|c"));

    QSignalSpy activate(engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested);
    engine->focusColumnPlain(-1, kS1); // c -> b
    engine->focusColumnPlain(-1, kS1); // b -> a
    QCOMPARE(activate.count(), 2);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));

    // Stale echo of the first step, with the second's still due: swallowed.
    engine->windowFocused(QStringLiteral("app|b"), kS1);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));
    // Final echo agrees with the strip: nothing to do either way.
    engine->windowFocused(QStringLiteral("app|a"), kS1);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));
}

QTEST_GUILESS_MAIN(TestScrollEngineFocusReport)
#include "test_scrollengine_focusreport.moc"
