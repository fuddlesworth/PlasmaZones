// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Headless ScrollEngine smoke test: tracking, ordering, float state,
// capture, and handoff semantics with null daemon dependencies (the engine
// tolerates a missing screen manager / tracking service; geometry emission
// is covered by the strip-model tests, not here).

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;

class TestScrollEngineSmoke : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void screensSetLifecycle();
    void openTrackAndOrder();
    void floatPullsOutAndRestoresSlot();
    void capturePlacementReportsSlot();
    void handoffReleaseIsTrackingOnly();
    void contextKeysSeparateDesktops();
    void floatRestoresDisplayIntent();
    void pruneDropsWindowBookkeeping();
    void pruneRemovedScreenAndActivitiesSweep();
    void operationScreenFallbackIsDeterministic();
    void minSizeSeedsAndCarries();
    void minSizeSurvivesFloatRoundTrip();
    void handoffReleaseClearsFloatMarker();
    void applyPathEmitsOnChangeOnly();

private:
    static ScrollEngine* makeEngine(QObject* parent)
    {
        auto* engine = new ScrollEngine(nullptr, nullptr, parent);
        engine->setActiveScreens({QStringLiteral("S1"), QStringLiteral("S2")});
        return engine;
    }
};

void TestScrollEngineSmoke::screensSetLifecycle()
{
    QObject owner;
    auto* engine = new ScrollEngine(nullptr, nullptr, &owner);
    QVERIFY(!engine->isEnabled());
    QSignalSpy screensSpy(engine, &ScrollEngine::scrollingScreensChanged);

    QSignalSpy enabledSpy(engine, &ScrollEngine::enabledChanged);
    engine->setActiveScreens({QStringLiteral("S2"), QStringLiteral("S1")});
    QVERIFY(engine->isEnabled());
    QVERIFY(engine->isActiveOnScreen(QStringLiteral("S1")));
    QCOMPARE(screensSpy.count(), 1);
    // The wire payload is SORTED (consumers compare successive payloads).
    QCOMPARE(screensSpy.last().at(0).toStringList(), (QStringList{QStringLiteral("S1"), QStringLiteral("S2")}));
    QCOMPARE(enabledSpy.count(), 1);
    engine->setActiveScreens({QStringLiteral("S1")}); // genuine shrink → emit 2

    // Identical non-empty set re-emits with isDesktopSwitch=true (the
    // effect catch-scan wakeup contract).
    engine->setActiveScreens({QStringLiteral("S1")});
    QCOMPARE(screensSpy.count(), 3);
    QCOMPARE(screensSpy.last().at(1).toBool(), true);

    engine->setActiveScreens({});
    QVERIFY(!engine->isEnabled());
    // Empty-identical: a second empty set has nothing to catch-scan for,
    // so no re-emit.
    const int afterEmpty = screensSpy.count();
    engine->setActiveScreens({});
    QCOMPARE(screensSpy.count(), afterEmpty);
}

void TestScrollEngineSmoke::openTrackAndOrder()
{
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);

    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S2"), 0, 0);

    QVERIFY(engine->isWindowTracked(QStringLiteral("app|a")));
    QVERIFY(engine->isWindowTiled(QStringLiteral("app|b")));
    QCOMPARE(engine->screenForTrackedWindow(QStringLiteral("app|b")), QStringLiteral("S1"));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("app|a"), QStringLiteral("app|b")}));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S2")), (QStringList{QStringLiteral("app|c")}));

    // Opens on a non-scrolling screen are ignored.
    engine->windowOpened(QStringLiteral("app|d"), QStringLiteral("OTHER"), 0, 0);
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|d")));

    engine->windowClosed(QStringLiteral("app|a"));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|a")));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), (QStringList{QStringLiteral("app|b")}));
}

void TestScrollEngineSmoke::floatPullsOutAndRestoresSlot()
{
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("S1"), 0, 0);

    QSignalSpy floatSpy(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
    engine->setWindowFloat(QStringLiteral("app|b"), true, QStringLiteral("S1"));
    QVERIFY(engine->isWindowFloatingInScroll(QStringLiteral("app|b")));
    QVERIFY(engine->isModeSpecificFloated(QStringLiteral("app|b")));
    QVERIFY(!engine->isWindowTiled(QStringLiteral("app|b")));
    QVERIFY(engine->isWindowTracked(QStringLiteral("app|b")));
    QCOMPARE(floatSpy.count(), 1);
    QCOMPARE(engine->allFloatingWindows(), (QStringList{QStringLiteral("app|b")}));

    // Unfloat restores the remembered column slot: b returns between a and c.
    engine->setWindowFloat(QStringLiteral("app|b"), false, QStringLiteral("S1"));
    QVERIFY(!engine->isWindowFloatingInScroll(QStringLiteral("app|b")));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")),
             (QStringList{QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")}));
}

void TestScrollEngineSmoke::capturePlacementReportsSlot()
{
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);

    const auto placement = engine->capturePlacement(QStringLiteral("app|b"));
    QVERIFY(placement.has_value());
    QCOMPARE(placement->screenId, QStringLiteral("S1"));
    const PhosphorEngine::EngineSlot slot = placement->slotFor(QStringLiteral("scrolling"));
    QCOMPARE(slot.state, QString(PhosphorEngine::WindowPlacement::stateTiled()));
    QCOMPARE(slot.order, 1);

    engine->setWindowFloat(QStringLiteral("app|b"), true, QStringLiteral("S1"));
    const auto floated = engine->capturePlacement(QStringLiteral("app|b"));
    QVERIFY(floated.has_value());
    QCOMPARE(floated->slotFor(QStringLiteral("scrolling")).state,
             QString(PhosphorEngine::WindowPlacement::stateFloating()));

    QVERIFY(!engine->capturePlacement(QStringLiteral("app|nope")).has_value());

    // Stacked column: slot.order is the COLUMN index (a restore feeds it to
    // insertWindowAt as a column position), NOT the window index — the two
    // diverge once a column holds more than one tile. Unfloat b (the float
    // arm above left it floating), then consume it into a's column: both
    // windows now report column 0.
    engine->setWindowFloat(QStringLiteral("app|b"), false, QStringLiteral("S1"));
    engine->windowFocused(QStringLiteral("app|b"), QStringLiteral("S1"));
    engine->consumeOrExpelWindow(-1, QStringLiteral("S1"));
    const auto stackedA = engine->capturePlacement(QStringLiteral("app|a"));
    const auto stackedB = engine->capturePlacement(QStringLiteral("app|b"));
    QVERIFY(stackedA.has_value() && stackedB.has_value());
    QCOMPARE(stackedA->slotFor(QStringLiteral("scrolling")).order, 0);
    QCOMPARE(stackedB->slotFor(QStringLiteral("scrolling")).order, 0);
}

void TestScrollEngineSmoke::handoffReleaseIsTrackingOnly()
{
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);

    engine->handoffRelease(QStringLiteral("app|a"));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|a")));
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), (QStringList{QStringLiteral("app|b")}));

    // Receive adopts onto the target screen's strip and focuses it.
    PhosphorEngine::IPlacementEngine::HandoffContext ctx;
    ctx.windowId = QStringLiteral("app|a");
    ctx.toScreenId = QStringLiteral("S2");
    ctx.fromEngineId = QStringLiteral("snap");
    engine->handoffReceive(ctx);
    QVERIFY(engine->isWindowTracked(QStringLiteral("app|a")));
    QCOMPARE(engine->screenForTrackedWindow(QStringLiteral("app|a")), QStringLiteral("S2"));
}

void TestScrollEngineSmoke::contextKeysSeparateDesktops()
{
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->setCurrentDesktop(1);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);

    // Desktop 2 has its own strip: the desktop-1 window keeps its state but
    // the current-context order for S1 is empty.
    engine->setCurrentDesktop(2);
    QVERIFY(engine->managedWindowOrder(QStringLiteral("S1")).isEmpty());
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), (QStringList{QStringLiteral("app|b")}));

    engine->setCurrentDesktop(1);
    QCOMPARE(engine->managedWindowOrder(QStringLiteral("S1")), (QStringList{QStringLiteral("app|a")}));
    QCOMPARE(engine->desktopsWithActiveState(), (QSet<int>{1, 2}));

    engine->pruneStatesForDesktop(2);
    QCOMPARE(engine->desktopsWithActiveState(), (QSet<int>{1}));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|b")));
}

void TestScrollEngineSmoke::floatRestoresDisplayIntent()
{
    // FloatRestore regression (pass-2 fix): the intent a floated column
    // held must survive the round trip — replacing restore.display with
    // the engine default in unfloatWindowInternal fails this.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S1"), 0, 0);
    engine->windowFocused(QStringLiteral("app|b"), QStringLiteral("S1"));
    engine->toggleColumnTabbed(QStringLiteral("S1"));

    auto columnDisplayOf = [engine](const QString& windowId) {
        auto* state = static_cast<ScrollState*>(engine->stateForScreen(QStringLiteral("S1")));
        const int col = state->strip().columnOfWindow(windowId);
        return col >= 0 ? state->strip().columns().at(col).display : ColumnDisplay::Normal;
    };
    QCOMPARE(columnDisplayOf(QStringLiteral("app|b")), ColumnDisplay::Tabbed);

    engine->setWindowFloat(QStringLiteral("app|b"), true, QStringLiteral("S1"));
    engine->setWindowFloat(QStringLiteral("app|b"), false, QStringLiteral("S1"));
    QCOMPARE(columnDisplayOf(QStringLiteral("app|b")), ColumnDisplay::Tabbed);
}

void TestScrollEngineSmoke::pruneDropsWindowBookkeeping()
{
    // dropWindowBookkeeping regression: pruning a desktop's state must
    // sweep the per-window side maps — the float marker is the observable
    // one headless (m_lastAppliedRect never populates without a screen
    // manager).
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->setCurrentDesktop(2);
    engine->windowOpened(QStringLiteral("app|d2"), QStringLiteral("S1"), 0, 0);
    engine->setWindowFloat(QStringLiteral("app|d2"), true, QStringLiteral("S1"));
    QVERIFY(engine->isModeSpecificFloated(QStringLiteral("app|d2")));

    engine->pruneStatesForDesktop(2);
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|d2")));
    QVERIFY(!engine->isModeSpecificFloated(QStringLiteral("app|d2")));
}

void TestScrollEngineSmoke::pruneRemovedScreenAndActivitiesSweep()
{
    // The other two prune entry points, same bookkeeping contract as the
    // desktop prune above. Removed-screen: the physical id AND its virtual
    // sub-screens ("S1#0") are swept, but an id that merely shares the
    // prefix ("S10") is not. Activities: only states whose key names an
    // activity absent from the valid list go; activity-less states stay.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    // The default fixture only activates S1/S2; the sub-screen and the
    // prefix-sharing sibling must be MANAGED or the sweep asserts below
    // pass vacuously.
    engine->setActiveScreens(
        {QStringLiteral("S1"), QStringLiteral("S1#0"), QStringLiteral("S10"), QStringLiteral("S2")});
    engine->windowOpened(QStringLiteral("app|p"), QStringLiteral("S1"), 0, 0);
    engine->windowOpened(QStringLiteral("app|v"), QStringLiteral("S1#0"), 0, 0);
    engine->windowOpened(QStringLiteral("app|s10"), QStringLiteral("S10"), 0, 0);
    engine->setWindowFloat(QStringLiteral("app|p"), true, QStringLiteral("S1"));
    QVERIFY(engine->isWindowTracked(QStringLiteral("app|v")));
    QVERIFY(engine->isWindowTracked(QStringLiteral("app|s10")));

    engine->pruneStatesForRemovedScreen(QStringLiteral("S1"));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|p")));
    QVERIFY(!engine->isModeSpecificFloated(QStringLiteral("app|p")));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|v")));
    QVERIFY(engine->isWindowTracked(QStringLiteral("app|s10")));

    engine->setCurrentActivity(QStringLiteral("actB"));
    engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("S2"), 0, 0);
    engine->setWindowFloat(QStringLiteral("app|b"), true, QStringLiteral("S2"));
    engine->pruneStatesForActivities({QStringLiteral("actA")});
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|b")));
    QVERIFY(!engine->isModeSpecificFloated(QStringLiteral("app|b")));
    // The activity-less S10 state predates the activity context and stays.
    QVERIFY(engine->isWindowTracked(QStringLiteral("app|s10")));
}

void TestScrollEngineSmoke::operationScreenFallbackIsDeterministic()
{
    // With no active screen, a screen-less operation must land on the
    // lexicographic minimum of the active set — QSet iteration order is
    // unspecified, so reverting the deterministic pick is invisible to a
    // casual repro but not to this assertion.
    QObject owner;
    auto* engine = new ScrollEngine(nullptr, nullptr, &owner);
    engine->setActiveScreens({QStringLiteral("S2"), QStringLiteral("S1")});
    engine->setWindowFloat(QStringLiteral("app|f"), false, QString());
    QCOMPARE(engine->screenForTrackedWindow(QStringLiteral("app|f")), QStringLiteral("S1"));
}

void TestScrollEngineSmoke::minSizeSeedsAndCarries()
{
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);

    // windowOpened's min args land in the strip and surface through the
    // IPlacementEngine::windowMinimumSize override.
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 500, 400);
    QCOMPARE(engine->windowMinimumSize(QStringLiteral("app|a")), QSize(500, 400));
    QCOMPARE(engine->windowMinimumSize(QStringLiteral("app|nope")), QSize());

    // handoffReceive seeds ctx.minSize so the first relayout clamps
    // without a refuse/re-discover round trip.
    PhosphorEngine::IPlacementEngine::HandoffContext ctx;
    ctx.windowId = QStringLiteral("app|x");
    ctx.toScreenId = QStringLiteral("S2");
    ctx.fromEngineId = QStringLiteral("autotile");
    ctx.minSize = QSize(640, 360);
    engine->handoffReceive(ctx);
    QCOMPARE(engine->windowMinimumSize(QStringLiteral("app|x")), QSize(640, 360));

    // Partial min (height only) still seeds — the guard is an OR, and a
    // client can legitimately constrain a single axis.
    PhosphorEngine::IPlacementEngine::HandoffContext partial;
    partial.windowId = QStringLiteral("app|y");
    partial.toScreenId = QStringLiteral("S2");
    partial.fromEngineId = QStringLiteral("autotile");
    partial.minSize = QSize(0, 360);
    engine->handoffReceive(partial);
    QCOMPARE(engine->windowMinimumSize(QStringLiteral("app|y")), QSize(0, 360));
}

void TestScrollEngineSmoke::minSizeSurvivesFloatRoundTrip()
{
    // I25 regression: FloatRestore carries the min size, so a float→unfloat
    // round trip keeps the relayout clamps without waiting for the
    // compositor to re-report.
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 500, 400);
    QCOMPARE(engine->windowMinimumSize(QStringLiteral("app|a")), QSize(500, 400));
    engine->setWindowFloat(QStringLiteral("app|a"), true, QStringLiteral("S1"));
    engine->setWindowFloat(QStringLiteral("app|a"), false, QStringLiteral("S1"));
    QCOMPARE(engine->windowMinimumSize(QStringLiteral("app|a")), QSize(500, 400));
}

void TestScrollEngineSmoke::handoffReleaseClearsFloatMarker()
{
    // The mode-transition float marker must not outlive this engine's
    // tracking (the daemon's release handler reads it).
    QObject owner;
    ScrollEngine* engine = makeEngine(&owner);
    engine->windowOpened(QStringLiteral("app|f"), QStringLiteral("S1"), 0, 0);
    engine->setWindowFloat(QStringLiteral("app|f"), true, QStringLiteral("S1"));
    QVERIFY(engine->isModeSpecificFloated(QStringLiteral("app|f")));
    engine->handoffRelease(QStringLiteral("app|f"));
    QVERIFY(!engine->isWindowTracked(QStringLiteral("app|f")));
    QVERIFY(!engine->isModeSpecificFloated(QStringLiteral("app|f")));
}

void TestScrollEngineSmoke::applyPathEmitsOnChangeOnly()
{
    // The apply path, driven headless through the geometry-provider seam:
    // windowsTiled fires when rects genuinely move and stays silent on a
    // no-op re-apply; the tab-strip payload is change-gated with the empty
    // latch broadcasting exactly one "[]".
    QObject owner;
    auto* engine = new ScrollEngine(nullptr, nullptr, &owner);
    engine->setScreenGeometryProviders(
        [](const QString&) {
            return QRect(0, 0, 1200, 800);
        },
        [](const QString&) {
            return QRect(0, 0, 1200, 800);
        });
    engine->setActiveScreens({QStringLiteral("S1")});

    QSignalSpy tiledSpy(engine, &ScrollEngine::windowsTiled);
    QSignalSpy stripSpy(engine, &ScrollEngine::tabStripsChanged);
    engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("S1"), 0, 0);
    QVERIFY(tiledSpy.count() >= 1);
    const int afterOpen = tiledSpy.count();

    // Same-state re-apply: nothing moved, nothing emitted.
    engine->retile(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    QCOMPARE(tiledSpy.count(), afterOpen);

    // Tabbed column: the strip payload appears once and does not re-emit
    // byte-identically on a plain retile.
    engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("S1"));
    engine->toggleColumnTabbed(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    QVERIFY(stripSpy.count() >= 1);
    const int afterTab = stripSpy.count();
    engine->retile(QStringLiteral("S1"));
    QCoreApplication::processEvents();
    QCOMPARE(stripSpy.count(), afterTab);

    // Closing the last window latches ONE empty broadcast.
    engine->windowClosed(QStringLiteral("app|a"));
    QCoreApplication::processEvents();
    QVERIFY(stripSpy.count() > afterTab);
    QCOMPARE(stripSpy.last().at(1).toString(), QStringLiteral("[]"));
}

QTEST_APPLESS_MAIN(TestScrollEngineSmoke)
#include "test_scrollengine_smoke.moc"
