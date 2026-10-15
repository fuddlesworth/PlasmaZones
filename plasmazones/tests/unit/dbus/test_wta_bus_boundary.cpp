// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_bus_boundary.cpp
 * @brief What WindowTrackingAdaptor takes from the effect's reports: a screen
 *        the daemon does not know is held until it arrives, a split monitor's
 *        physical id lands in the right virtual screen, a metadata push is
 *        normalised to WindowMetadata's invariants, the focused-window, frame
 *        and resize reports are keyed and routed as the adaptor documents, the
 *        stale-window prune drops what died, and a close on a removed virtual
 *        screen files where the frame is (F81, F852, F48, F853 to F858, F251).
 */

#include "wta_float_back_fixture.h"
#include "helpers/VirtualScreenTestHelpers.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowTypeEnum.h>

#include <QRegularExpression>
#include <QScopeGuard>
#include <QSignalSpy>

namespace Key = PhosphorProtocol::Service::WindowMetadataKey;

class TestWtaBusBoundary : public QObject
{
    Q_OBJECT

    static void push(WindowTrackingAdaptor* wta, const QString& instance, const QString& appId, int desktop,
                     const QVariantMap& extended,
                     int windowType = static_cast<int>(PhosphorProtocol::WindowType::Normal))
    {
        wta->setWindowMetadata(instance, appId, QString(), QStringLiteral("t"), QString(), 100, desktop, QString(),
                               windowType, extended);
    }

private Q_SLOTS:
    // KWin's output can reach the effect before the daemon's QScreen. The
    // report is held, not stored, and lands when the screen does.
    void cursorReportOnAnUnknownScreenIsHeldUntilItArrives()
    {
        FloatBackFixture f;
        QCoreApplication::processEvents(); // the adaptor wires its screen hooks on the first loop turn
        f.wta->cursorScreenChanged(kLeft);
        QCOMPARE(f.wta->lastCursorScreenName(), kLeft);
        f.wta->cursorScreenChanged(QStringLiteral("DP-9"));
        QCOMPARE(f.wta->lastCursorScreenName(), kLeft);

        f.fake.addScreen(QStringLiteral("DP-9"), QRect(3840, 0, 1920, 1080), QStringLiteral("DP-9"));
        QTRY_COMPARE(f.wta->lastCursorScreenName(), QStringLiteral("DP-9"));
    }

    // A later report supersedes a held one.
    void aLaterCursorReportDropsTheHeldOne()
    {
        FloatBackFixture f;
        QCoreApplication::processEvents();
        f.wta->cursorScreenChanged(QStringLiteral("DP-9"));
        f.wta->cursorScreenChanged(kRight);
        f.fake.addScreen(QStringLiteral("DP-9"), QRect(3840, 0, 1920, 1080), QStringLiteral("DP-9"));
        QTest::qWait(20);
        QCOMPARE(f.wta->lastCursorScreenName(), kRight);
    }

    // The effect can send a split monitor's physical id; the focused-window
    // record takes the virtual screen the window's frame is in.
    void activationOnAPhysicalIdOfASplitMonitorTakesTheFramesVirtualScreen()
    {
        FloatBackFixture f;
        QVERIFY(f.screenMgr->setVirtualScreenConfig(kRight, TestHelpers::makeSplitConfig(kRight)));
        const QString windowId = f.registerWindow(QStringLiteral("split-1"));
        f.setFrame(windowId, QRect(3000, 100, 400, 300)); // the right half of DP-2
        f.wta->windowActivated(windowId, kRight);
        QCOMPARE(f.wta->lastActiveScreenName(), QStringLiteral("DP-2/vs:1"));
    }

    // The focused window's move report names the physical id of a split
    // monitor: the virtual screen the window is tracked on answers, not the
    // monitor's first (F48). Snap is then taken off vs:1, so the read answers
    // from the screen the report records rather than from snap's tracking.
    void activeWindowScreenOnAPhysicalIdOfASplitMonitorTakesTheTrackedVirtualScreen()
    {
        FloatBackFixture f;
        QVERIFY(f.screenMgr->setVirtualScreenConfig(kRight, TestHelpers::makeSplitConfig(kRight)));
        const QString vs1 = QStringLiteral("DP-2/vs:1");
        f.layouts->assignLayout(vs1, 1, QString(), f.layout);
        f.snap->setCurrentDesktopForScreen(vs1, 1);
        const QString windowId = f.registerWindow(QStringLiteral("split-2"));
        f.snap->commitSnap(windowId, f.zone(0), vs1);
        f.wta->windowActivated(windowId, vs1);
        f.snap->setLiveModeResolver([vs1](const QString& screenId) {
            return screenId == vs1 ? PhosphorZones::AssignmentEntry::Mode::Autotile
                                   : PhosphorZones::AssignmentEntry::Mode::Snapping;
        });
        QCOMPARE(f.wta->service()->screenForWindow(windowId), vs1);
        f.wta->activeWindowScreenChanged(windowId, kLeft);
        QCOMPARE(f.wta->lastActiveScreenName(), kLeft);
        f.wta->activeWindowScreenChanged(windowId, kRight);
        QCOMPARE(f.wta->lastActiveScreenName(), vs1);
    }

    // The report is matched to the focused window by its canonical id, so a
    // class-renamed composite still moves the shortcut screen (F48).
    void activeWindowScreenChangedMatchesTheFocusedWindowByCanonicalId()
    {
        FloatBackFixture f;
        f.wta->windowActivated(f.registerWindow(QStringLiteral("i1")), kLeft);
        f.wta->activeWindowScreenChanged(QStringLiteral("renamed|i1"), kRight);
        QCOMPARE(f.wta->lastActiveScreenName(), kRight);
    }

    // With nothing focused, a report naming no window moves nothing (F48).
    void activeWindowScreenChangedIgnoresAnEmptyWindowId()
    {
        FloatBackFixture f;
        f.wta->activeWindowScreenChanged(QString(), kRight);
        QVERIFY(f.wta->lastActiveScreenName().isEmpty());
    }

    // A split monitor's physical id in a cursor report lands in the virtual
    // screen the focused window's frame is in, else the monitor's first; an
    // unsplit monitor's id stands as it is (F856).
    void cursorReportOnASplitMonitorTakesTheFocusedWindowsVirtualScreen()
    {
        FloatBackFixture f;
        QVERIFY(f.screenMgr->setVirtualScreenConfig(kRight, TestHelpers::makeSplitConfig(kRight)));
        f.wta->cursorScreenChanged(kRight);
        QCOMPARE(f.wta->lastCursorScreenName(), QStringLiteral("DP-2/vs:0"));
        f.wta->cursorScreenChanged(kLeft);
        QCOMPARE(f.wta->lastCursorScreenName(), kLeft);

        const QString windowId = f.registerWindow(QStringLiteral("cursor-1"));
        f.setFrame(windowId, QRect(3000, 100, 400, 300)); // the right half of DP-2
        f.wta->windowActivated(windowId, kRight);
        f.wta->cursorScreenChanged(kRight);
        QCOMPARE(f.wta->lastCursorScreenName(), QStringLiteral("DP-2/vs:1"));
    }

    // The frame shadow is keyed on the canonical id, and a non-positive size
    // is refused (F853).
    void frameReportIsKeyedCanonicallyAndRefusesAnEmptySize()
    {
        FloatBackFixture f;
        const QString windowId = f.registerWindow(QStringLiteral("frame-1"));
        const QRect frame(100, 100, 400, 300);
        f.wta->setFrameGeometry(QStringLiteral("renamed|frame-1"), frame.x(), frame.y(), frame.width(), frame.height());
        QCOMPARE(f.wta->frameGeometry(windowId), frame);
        f.wta->setFrameGeometry(windowId, 0, 0, 0, 300);
        f.wta->setFrameGeometry(windowId, 0, 0, 400, -1);
        QCOMPARE(f.wta->frameGeometry(windowId), frame);
    }

    // A finished interactive resize goes to the engine holding the window in
    // view, a scroll strip ahead of the tiles, and keeps the frame shadow; a
    // non-positive old frame reaches neither and changes nothing (F858).
    void resizeReachesTheEngineHoldingTheWindow()
    {
        FloatBackFixture f;
        StubPlacementEngine scroll;
        f.wta->setEngines(f.snap.get(), &f.tiling, &scroll);
        const auto unwire = qScopeGuard([&f] {
            f.wta->setEngines(f.snap.get(), &f.tiling, nullptr);
        });
        const QString windowId = f.registerWindow(QStringLiteral("resize-1"));
        scroll.heldScreen.insert(windowId, kLeft);
        f.tiling.heldScreen.insert(windowId, kLeft);
        f.setFrame(windowId, QRect(100, 100, 400, 300));

        f.wta->notifyWindowResized(windowId, 100, 100, 0, 0, 100, 100, 600, 300);
        QVERIFY(scroll.resized.isEmpty());
        QVERIFY(f.tiling.resized.isEmpty());
        QCOMPARE(f.wta->frameGeometry(windowId), QRect(100, 100, 400, 300));

        f.wta->notifyWindowResized(windowId, 100, 100, 400, 300, 100, 100, 600, 300);
        QCOMPARE(scroll.resized.size(), 1);
        QCOMPARE(scroll.resized.first().first, windowId);
        QCOMPARE(scroll.resized.first().second, kLeft);
        QVERIFY2(f.tiling.resized.isEmpty(), "the strip holding the window takes the resize, not the tiles");
        QCOMPARE(f.wta->frameGeometry(windowId), QRect(100, 100, 600, 300));
    }

    // A report with no desktop manager to forward to is taken and dropped.
    void screenDesktopReportWithNoDesktopManagerChangesNothing()
    {
        FloatBackFixture f;
        f.wta->screenDesktopChanged(kLeft, 2);
        QCOMPARE(f.wta->currentDesktopForScreen(kLeft), 0);
    }

    // A move onto a screen nobody knows changes nothing yet: the snap leave
    // would run against an id nothing can place on.
    void screenChangeOnAnUnknownScreenMovesNothing()
    {
        FloatBackFixture f;
        const QString windowId = f.registerWindow(QStringLiteral("snapped-1"));
        f.snap->commitSnap(windowId, f.zone(0), kLeft);
        QCOMPARE(f.snap->screenForTrackedWindow(windowId), kLeft);
        QSignalSpy states(f.wta, &WindowTrackingAdaptor::windowStateChanged);

        f.wta->windowScreenChanged(windowId, QStringLiteral("DP-9"));

        QCOMPARE(f.snap->screenForTrackedWindow(windowId), kLeft);
        QCOMPARE(f.snap->zoneForWindow(windowId), f.zone(0));
        QCOMPARE(states.count(), 0);
    }

    // WindowMetadata's invariant: a span lists two or more desktops and
    // starts with the scalar desktop.
    void metadataOneEntrySpanIsCleared()
    {
        FloatBackFixture f;
        QVariantMap ext{{QString(Key::Width), 640}, {QString(Key::VirtualDesktops), QVariantList{2}}};
        push(f.wta, QStringLiteral("span-1"), QStringLiteral("app"), 2, ext);
        QVERIFY(f.registry.metadata(QStringLiteral("span-1"))->virtualDesktops.isEmpty());

        ext.insert(QString(Key::VirtualDesktops), QVariantList{3, 2});
        push(f.wta, QStringLiteral("span-1"), QStringLiteral("app"), 2, ext);
        QVERIFY2(f.registry.metadata(QStringLiteral("span-1"))->virtualDesktops.isEmpty(),
                 "a span not led by the scalar desktop is no span");

        ext.insert(QString(Key::VirtualDesktops), QVariantList{2, 3});
        push(f.wta, QStringLiteral("span-1"), QStringLiteral("app"), 2, ext);
        QCOMPARE(f.registry.metadata(QStringLiteral("span-1"))->virtualDesktops, (QList<int>{2, 3}));
    }

    // The activity list follows the same invariant: two or more, led by the
    // scalar activity. A caption-only refresh that moves the scalar drops a
    // list from before the move (F426).
    void metadataActivityListFollowsTheSpanInvariant()
    {
        FloatBackFixture f;
        const QString a = QStringLiteral("act-a");
        const QString b = QStringLiteral("act-b");
        const auto pushActivities = [&f](const QString& activity, const QVariantMap& extended) {
            f.wta->setWindowMetadata(QStringLiteral("acts-1"), QStringLiteral("app"), QString(), QStringLiteral("t"),
                                     QString(), 100, 1, activity,
                                     static_cast<int>(PhosphorProtocol::WindowType::Normal), extended);
        };
        pushActivities(a, {{QString(Key::Width), 640}, {QString(Key::Activities), QVariantList{a, b}}});
        QCOMPARE(f.registry.metadata(QStringLiteral("acts-1"))->activities, (QStringList{a, b}));

        pushActivities(a, {{QString(Key::Width), 640}, {QString(Key::Activities), QVariantList{b, a}}});
        QVERIFY2(f.registry.metadata(QStringLiteral("acts-1"))->activities.isEmpty(),
                 "a list not led by the scalar activity is no list");

        pushActivities(a, {{QString(Key::Width), 640}, {QString(Key::Activities), QVariantList{a, b}}});
        pushActivities(b, {});
        QVERIFY2(f.registry.metadata(QStringLiteral("acts-1"))->activities.isEmpty(),
                 "a caption refresh that moved the scalar drops the stale list");
    }

    // A non-positive size is unknown, not one the min-size gate compares.
    void metadataNonPositiveSizeIsUnknown()
    {
        FloatBackFixture f;
        push(f.wta, QStringLiteral("size-1"), QStringLiteral("app"), 1,
             {{QString(Key::Width), 0}, {QString(Key::Height), -5}});
        const auto meta = f.registry.metadata(QStringLiteral("size-1"));
        QVERIFY(meta);
        QVERIFY(!meta->width.has_value());
        QVERIFY(!meta->height.has_value());
    }

    // F852 pins: the first-seen composite is frozen as the canonical id, and
    // the negative-desktop and window-type clamps hold.
    void metadataSeedsTheCanonicalId()
    {
        FloatBackFixture f;
        push(f.wta, QStringLiteral("seed-1"), QStringLiteral("old"), 1, {{QString(Key::Width), 640}});
        push(f.wta, QStringLiteral("seed-1"), QStringLiteral("new"), 1, {{QString(Key::Width), 640}});
        QCOMPARE(f.registry.canonicalizeForLookup(QStringLiteral("new|seed-1")), QStringLiteral("old|seed-1"));
    }

    void metadataNegativeDesktopReadsUnknown()
    {
        FloatBackFixture f;
        push(f.wta, QStringLiteral("neg-1"), QStringLiteral("app"), -3, {{QString(Key::Width), 640}});
        QCOMPARE(f.registry.metadata(QStringLiteral("neg-1"))->virtualDesktop, 0);
    }

    void metadataOutOfRangeTypeReadsUnknown()
    {
        FloatBackFixture f;
        push(f.wta, QStringLiteral("type-1"), QStringLiteral("app"), 1, {{QString(Key::Width), 640}}, 9999);
        QCOMPARE(f.registry.metadata(QStringLiteral("type-1"))->windowType, PhosphorProtocol::WindowType::Unknown);
    }

    // The stale-window prune (F855). An empty alive set is refused outright:
    // it can arrive before session-restored apps have mapped.
    void pruneRefusesAnEmptyAliveSet()
    {
        FloatBackFixture f;
        const QString windowId = f.registerWindow(QStringLiteral("kept-1"));
        f.setFrame(windowId, QRect(100, 100, 400, 300));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("refusing empty alive set")));
        f.wta->pruneStaleWindows({});
        QCOMPARE(f.wta->frameGeometry(windowId), QRect(100, 100, 400, 300));
    }

    // The engines are pruned with each alive window's canonical id, so a
    // class-renamed window is not dropped from their maps.
    void pruneHandsTheEnginesCanonicalIds()
    {
        FloatBackFixture f;
        const QString windowId = f.registerWindow(QStringLiteral("prune-1"));
        f.wta->pruneStaleWindows({QStringLiteral("renamed|prune-1")});
        QCOMPARE(f.tiling.pruned.size(), 1);
        QCOMPARE(f.tiling.pruned.first(), QSet<QString>{windowId});
    }

    // A window that died with no close signal loses its float-broadcast dedup
    // entry and its registry record, and the live one keeps its record.
    void pruneDropsADeadWindowsBroadcastAndRegistryRecord()
    {
        FloatBackFixture f;
        const QString dead = f.registerWindow(QStringLiteral("gone-1"));
        QVERIFY(f.wta->relayWindowFloatingChanged(dead, true, kLeft));
        QVERIFY2(!f.wta->relayWindowFloatingChanged(dead, true, kLeft), "control: an unchanged state is deduped");
        f.wta->pruneStaleWindows({f.registerWindow(QStringLiteral("alive-1"))});
        QVERIFY2(f.wta->relayWindowFloatingChanged(dead, true, kLeft), "the prune dropped the dead window's entry");
        QVERIFY(!f.registry.contains(QStringLiteral("gone-1")));
        QVERIFY(f.registry.contains(QStringLiteral("alive-1")));
    }

    // A dead window's last snap placement is captured before the prune wipes
    // the store that answers it. The service write records nothing itself.
    void pruneCapturesADeadWindowBeforeItsSnapStateGoes()
    {
        FloatBackFixture f;
        const QString dead = f.registerWindow(QStringLiteral("gone-2"));
        f.wta->service()->assignWindowToZone(dead, f.zone(1), kLeft, 1);
        f.setFrame(dead, QRect(100, 100, 400, 300));
        QVERIFY(!f.wta->service()->placementStore().peekExact(dead));
        f.wta->pruneStaleWindows({f.registerWindow(QStringLiteral("alive-2"))});
        const auto rec = f.wta->service()->placementStore().peekExact(dead);
        QVERIFY2(rec, "the dead window's placement is recorded before its snap state goes");
        QCOMPARE(rec->slotFor(WindowPlacement::snapEngineId()).zoneIds, QStringList{f.zone(1)});
    }

    // A close reported on a virtual screen the daemon no longer has (its
    // physical monitor is known, its subdivision is gone) files the float-back
    // under the screen the frame is on, not under the dead id (F251, F851).
    void closeOnARemovedVirtualScreenFilesTheFloatBackWhereTheFrameIs()
    {
        FloatBackFixture f;
        QVERIFY(f.screenMgr->setVirtualScreenConfig(kRight, TestHelpers::makeSplitConfig(kRight)));
        const QString vs1 = QStringLiteral("DP-2/vs:1");
        const QString dead = QStringLiteral("DP-2/vs:5");
        const QString windowId = f.registerWindow(QStringLiteral("close-vs-1"));
        const QRect frame(3000, 100, 400, 300); // the right half of DP-2
        f.setFrame(windowId, frame);
        QVERIFY2(!f.snap->isWindowTracked(windowId) && !f.tiling.isWindowTracked(windowId),
                 "control: no engine captures, so the close falls back to the reported screen");

        f.wta->windowClosed(windowId, 0, dead);

        const auto rec = f.wta->service()->placementStore().peekExact(windowId);
        QVERIFY(rec);
        QCOMPARE(rec->screenId, vs1);
        QCOMPARE(rec->freeGeometryByScreen.value(vs1), frame);
        QVERIFY(!rec->freeGeometryByScreen.contains(dead));
    }
};

QTEST_MAIN(TestWtaBusBoundary)
#include "test_wta_bus_boundary.moc"
