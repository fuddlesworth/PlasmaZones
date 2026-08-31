// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Strip IDENTITY across the daemon/compositor seam
// (docs/strip-identity-seam-plan.md): which strip a screen is currently
// showing, how that answer is announced, and the forced batch that rides the
// same transitions.
//
// Its own file rather than more of the smoke suite, per that file's stated
// rule that a new concern takes a sibling. This is one: it has its own signal
// (stripContextChanged), its own engine state (the announced-epoch memo and
// the force-emit arm), and a consumer that is not the geometry pipeline.
//
// The two halves are tested together because they fail together. A switch onto
// a strip nobody touched moves no rect, so the geometry batch is suppressed by
// the emit-on-change gate and the compositor is never told the strip under it
// was replaced. The announcement exists to say so, and the force-emit exists so
// the batch that repopulates the state actually arrives.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include "scrollstriptestutils.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;
using ScrollTestUtils::defaultScreenRect;
using ScrollTestUtils::GeometryFn;
using ScrollTestUtils::makeProviderEngine;

class TestScrollEngineStripContext : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    void desktopSwitchBackEmitsEvenWhenNoRectMoved();
    void backgroundFocusReportForcesTheReturnBatch();
    void identicalSetRePushWithoutASwitchStaysSuppressed();
    void stripContextIsAnnouncedOnDesktopSwitch();
    void changedSetSwitchStillAnnouncesTheStayingScreen();
    void stripContextIsReAnnouncedAfterAScreenLeavesTheSet();
    void stripContextEpochIsStableAcrossARePush();
};

void TestScrollEngineStripContext::desktopSwitchBackEmitsEvenWhenNoRectMoved()
{
    // Returning to a desktop whose strip nobody touched emitted NOTHING: every
    // resolved rect equalled the applied baseline, so applyLayout's
    // emit-on-change gate suppressed the batch. But that baseline belongs to
    // the strip that was current BEFORE the switch, so "nothing moved" was
    // never evidence about what is on screen now — and the compositor, whose
    // per-window strip state is not keyed by desktop, went on describing the
    // strip it had been showing. The first scroll verb after the return then
    // appeared to do nothing.
    QObject owner;
    // A provider engine, not makeEngine: applyLayout bails at its work-area
    // guard long before the emit gate without one, so a bare fixture asserts
    // nothing about emitting and passes or fails for the wrong reason.
    const GeometryFn geometry = [](const QString&) {
        return defaultScreenRect();
    };
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")}, geometry, geometry);
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    QCoreApplication::processEvents();

    // Away and back with the strip untouched in between. Nothing here moves a
    // rect on desktop 1, which is the whole point: the pre-fix engine had
    // nothing to say and said nothing.
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 2);
    engine->setActiveScreens({QStringLiteral("S1")});
    QCoreApplication::processEvents();

    QSignalSpy tiledSpy(engine, &ScrollEngine::windowsTiled);
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->setActiveScreens({QStringLiteral("S1")});
    QCoreApplication::processEvents();

    QVERIFY2(tiledSpy.count() > 0, "switching back to an untouched strip must still emit a batch");
    // The batch has to carry the strip, not merely be non-empty: an emission
    // naming no window would satisfy a count check while telling the
    // compositor nothing about which columns it is now showing.
    bool sawWindow = false;
    for (const auto& emission : std::as_const(tiledSpy)) {
        const QJsonArray batch = QJsonDocument::fromJson(emission.at(0).toString().toUtf8()).array();
        for (const QJsonValue& v : batch) {
            const QString id = v.toObject().value(QLatin1String("windowId")).toString();
            if (id == QStringLiteral("app|a") || id == QStringLiteral("app|b")) {
                sawWindow = true;
            }
        }
    }
    QVERIFY2(sawWindow, "the switch-back batch must name the strip's windows");
}
void TestScrollEngineStripContext::backgroundFocusReportForcesTheReturnBatch()
{
    // Activating a window that lives on another desktop (a taskbar click)
    // can land the focus report while the engine's context for the screen
    // still resolves the desktop being LEFT. The strip's focus and anchor
    // move in the background state, but no geometry batch may be emitted for
    // a context that is not on screen — so the report's centering has to
    // ride the desktop return. The screen-keyed force flag is not a safe
    // carrier for that (any interleaved pass can spend it); the pending
    // focus emit is keyed to the context and is consumed only by a pass that
    // runs with that context current.
    QObject owner;
    const GeometryFn geometry = [](const QString&) {
        return defaultScreenRect();
    };
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")}, geometry, geometry);
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    QCoreApplication::processEvents();

    // Away, with the destination populated so the return leg cannot lean on
    // the empty-resolve bail resetting the baseline (same reasoning as the
    // changed-set test below).
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 2);
    engine->setActiveScreens({QStringLiteral("S1")});
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);
    QCoreApplication::processEvents();

    // The activation report for desktop 1's window arrives BEFORE the
    // engine hears about the switch back — the ordering a taskbar click
    // produces when KWin activates first and switches second. app|a is the
    // window the strip already calls active, so the report is refused by
    // every focus test; the pending emit is what it must still arm.
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    QCoreApplication::processEvents();

    QSignalSpy tiledSpy(engine, &ScrollEngine::windowsTiled);
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->setActiveScreens({QStringLiteral("S1")});
    QCoreApplication::processEvents();

    QVERIFY2(tiledSpy.count() > 0, "the return after a background focus report must emit a batch");
    bool sawWindow = false;
    for (const auto& emission : std::as_const(tiledSpy)) {
        const QJsonArray batch = QJsonDocument::fromJson(emission.at(0).toString().toUtf8()).array();
        for (const QJsonValue& v : batch) {
            const QString id = v.toObject().value(QLatin1String("windowId")).toString();
            if (id == QStringLiteral("app|a")) {
                sawWindow = true;
            }
        }
    }
    QVERIFY2(sawWindow, "the return batch must re-assert the focused window's geometry");

    // The pending arm must be spent by that return, not linger: an identical
    // re-push with nothing new to say stays suppressed, exactly like the
    // negative-control test below.
    QSignalSpy afterSpy(engine, &ScrollEngine::windowsTiled);
    engine->setActiveScreens({QStringLiteral("S1")});
    QCoreApplication::processEvents();
    QCOMPARE(afterSpy.count(), 0);
}
void TestScrollEngineStripContext::identicalSetRePushWithoutASwitchStaysSuppressed()
{
    // The negative control for the test above, and the reason the force is
    // armed from the desktop-switch flag rather than from setActiveScreens
    // itself. updateEngineScreens re-derives and re-pushes the same set
    // routinely; if THAT forced an emit, the emit-on-change gate would be
    // effectively disabled and every redundant re-push would re-feed the
    // compositor a full batch. A fix for the switch case that also passed this
    // by emitting unconditionally would be a performance regression wearing a
    // green test.
    QObject owner;
    const GeometryFn geometry = [](const QString&) {
        return defaultScreenRect();
    };
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")}, geometry, geometry);
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    QCoreApplication::processEvents();

    QSignalSpy tiledSpy(engine, &ScrollEngine::windowsTiled);
    engine->setActiveScreens({QStringLiteral("S1")}); // same set, no context change
    QCoreApplication::processEvents();
    QCOMPARE(tiledSpy.count(), 0);
}
void TestScrollEngineStripContext::stripContextIsAnnouncedOnDesktopSwitch()
{
    // Strip identity has to reach the compositor on a channel of its own.
    // Carried as a field on the geometry batch it would be silent in exactly
    // the case that matters, because applyLayout emits on change only and a
    // switch onto an untouched strip moves no rect.
    QObject owner;
    const GeometryFn geometry = [](const QString&) {
        return defaultScreenRect();
    };
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")}, geometry, geometry);
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    QCoreApplication::processEvents();

    QSignalSpy ctxSpy(engine, &ScrollEngine::stripContextChanged);
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 2);
    engine->setActiveScreens({QStringLiteral("S1")});
    QCoreApplication::processEvents();
    QCOMPARE(ctxSpy.count(), 1);
    QCOMPARE(ctxSpy.at(0).at(0).toString(), QStringLiteral("S1"));
    const QString epochD2 = ctxSpy.at(0).at(1).toString();
    QVERIFY(!epochD2.isEmpty());

    // Back to desktop 1: a DIFFERENT strip, so a different epoch. Comparing
    // the two values is the only thing a consumer may do with them, so it is
    // the only thing asserted here.
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->setActiveScreens({QStringLiteral("S1")});
    QCoreApplication::processEvents();
    QCOMPARE(ctxSpy.count(), 2);
    QVERIFY2(ctxSpy.at(1).at(1).toString() != epochD2, "each desktop's strip must carry its own epoch");
}
void TestScrollEngineStripContext::changedSetSwitchStillAnnouncesTheStayingScreen()
{
    // The shape per-context modes actually take. A desktop switch does not
    // only move contexts, it can change WHICH screens are scrolling: one
    // monitor's new desktop is tiling, so it leaves the set, and the push that
    // carries the switch is a set CHANGE rather than an identical-set re-push.
    //
    // The screen that stayed scrolling has still had its strip replaced, and
    // it is the one nothing was watching: the changed-set path announced only
    // for screens being ADDED, so a stayer got no announcement, and it armed
    // no force-emit either, so its batch was suppressed by the emit-on-change
    // gate comparing against the outgoing strip's baseline. Both halves are
    // asserted here, because either alone leaves the compositor painting the
    // previous strip's state.
    QObject owner;
    const GeometryFn geometry = [](const QString&) {
        return defaultScreenRect();
    };
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1"), QStringLiteral("S2")}, geometry, geometry);
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->setCurrentDesktopForScreen(QStringLiteral("S2"), 1);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    QCoreApplication::processEvents();

    // Away to desktop 2, which has a window of its OWN. That matters: an
    // empty destination strip takes applyLayout's empty-resolve bail, which
    // clears the view baseline as a side effect and would leave something
    // changed on the way back, masking whether the force did any work. With
    // desktop 2 populated the return leg is a genuine no-rect-moved case.
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 2);
    engine->setActiveScreens({QStringLiteral("S1"), QStringLiteral("S2")});
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    QCoreApplication::processEvents();

    QSignalSpy ctxSpy(engine, &ScrollEngine::stripContextChanged);
    QSignalSpy tiledSpy(engine, &ScrollEngine::windowsTiled);

    // The return, as a set CHANGE: S1 goes back to desktop 1 and stays
    // scrolling while S2 drops out of the set in the same push. That
    // combination routes through the changed-set branch rather than the
    // identical-set one, and S1 is a stayer rather than an addition.
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->setActiveScreens({QStringLiteral("S1")});
    QCoreApplication::processEvents();

    bool announcedS1 = false;
    for (int i = 0; i < ctxSpy.count(); ++i) {
        if (ctxSpy.at(i).at(0).toString() == QStringLiteral("S1")) {
            announcedS1 = true;
        }
    }
    QVERIFY2(announcedS1, "a screen that stays scrolling across a set-changing switch must be announced");

    // And the batch must actually be emitted. app|a is back on the strip it
    // was last laid out on, so every rect the retile resolves equals the
    // baseline and the emit-on-change gate suppresses the batch unless the
    // switch armed the force. Without the arm the compositor is never told the
    // strip came back, and goes on painting desktop 2's state.
    QVERIFY2(tiledSpy.count() >= 1, "the staying screen's switch must force a batch even though no rect moved");
}
void TestScrollEngineStripContext::stripContextIsReAnnouncedAfterAScreenLeavesTheSet()
{
    // A screen leaving the scrolling set drops its announced epoch, so
    // re-entering announces again rather than being suppressed by an epoch it
    // was told about under a previous stint. That drop had no coverage at all:
    // deleting it broke nothing, while it is what a mode round trip depends on.
    //
    // The re-announced epoch is the SAME value, because identity tracks the
    // context and the context did not move. What changed is that the engine
    // forgot it had said so.
    QObject owner;
    const GeometryFn geometry = [](const QString&) {
        return defaultScreenRect();
    };
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")}, geometry, geometry);
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    QCoreApplication::processEvents();

    QSignalSpy ctxSpy(engine, &ScrollEngine::stripContextChanged);
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 2);
    engine->setActiveScreens({QStringLiteral("S1")});
    QCoreApplication::processEvents();
    QCOMPARE(ctxSpy.count(), 1);
    const QString epoch = ctxSpy.at(0).at(1).toString();

    // Out of the set (the screen's desktop flipped to another mode), then back
    // on the same context.
    engine->setActiveScreens({});
    engine->setActiveScreens({QStringLiteral("S1")});
    QCoreApplication::processEvents();

    QCOMPARE(ctxSpy.count(), 2);
    QCOMPARE(ctxSpy.at(1).at(0).toString(), QStringLiteral("S1"));
    QCOMPARE(ctxSpy.at(1).at(1).toString(), epoch);
}
void TestScrollEngineStripContext::stripContextEpochIsStableAcrossARePush()
{
    // The negative control. An epoch that changed on every push would make the
    // consumer retire its strip state constantly — throwing away exactly the
    // parked-column relocations the identity exists to protect, and turning a
    // correctness fix into a permanent visual regression. Identity must track
    // the CONTEXT, not the number of times it was asked.
    QObject owner;
    const GeometryFn geometry = [](const QString&) {
        return defaultScreenRect();
    };
    ScrollEngine* engine = makeProviderEngine(&owner, {QStringLiteral("S1")}, geometry, geometry);
    engine->setCurrentDesktopForScreen(QStringLiteral("S1"), 1);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    QCoreApplication::processEvents();

    QSignalSpy ctxSpy(engine, &ScrollEngine::stripContextChanged);
    engine->setActiveScreens({QStringLiteral("S1")});
    engine->setActiveScreens({QStringLiteral("S1")});
    QCoreApplication::processEvents();
    QCOMPARE(ctxSpy.count(), 0);
}

QTEST_GUILESS_MAIN(TestScrollEngineStripContext)
#include "test_scrollengine_stripcontext.moc"
