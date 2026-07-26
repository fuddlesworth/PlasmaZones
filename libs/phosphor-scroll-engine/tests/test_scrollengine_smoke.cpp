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
    void operationScreenFallbackIsDeterministic();
    void minSizeSeedsAndCarries();

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

    engine->setActiveScreens({QStringLiteral("S1")});
    QVERIFY(engine->isEnabled());
    QVERIFY(engine->isActiveOnScreen(QStringLiteral("S1")));
    QCOMPARE(screensSpy.count(), 1);

    // Identical non-empty set re-emits with isDesktopSwitch=true (the
    // effect catch-scan wakeup contract).
    engine->setActiveScreens({QStringLiteral("S1")});
    QCOMPARE(screensSpy.count(), 2);
    QCOMPARE(screensSpy.last().at(1).toBool(), true);

    engine->setActiveScreens({});
    QVERIFY(!engine->isEnabled());
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
}

QTEST_APPLESS_MAIN(TestScrollEngineSmoke)
#include "test_scrollengine_smoke.moc"
