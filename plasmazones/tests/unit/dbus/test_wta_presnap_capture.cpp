// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_presnap_capture.cpp
 * @brief The free frame a user snap leaves, recorded through
 *        SnapEngine::recordFreeFrameBeforeUserSnap before the commit puts the
 *        window in a zone: the keyboard sites, the float toggle, snap-all and
 *        the D-Bus moveWindowToZone, for a floating window and for a free one
 *        snap does not hold floating. Plus the helper's own refusals and the
 *        snap intent the snap-all confirmation and a swap record.
 */

#include "wta_convenience_fixture.h"
#include "wta_float_back_fixture.h"

#include <PhosphorEngine/NavigationContext.h>
#include <PhosphorSnapEngine/IZoneAdjacencyResolver.h>

namespace {
const QRect kFree(300, 200, 640, 480);

/// The entry zone a directional key gives a window in no zone: the first
/// zone for left and up, the last for right and down.
struct EdgeZoneAdjacency : PhosphorSnapEngine::IZoneAdjacencyResolver
{
    QString getAdjacentZone(const QString&, const QString&, const QString&) const override
    {
        return {};
    }
    QString getFirstZoneInDirection(const QString& direction, const QString&) const override
    {
        const bool lowEdge = direction == QLatin1String("left") || direction == QLatin1String("up");
        return lowEdge ? first : last;
    }
    QString first;
    QString last;
};

struct PresnapFixture : FloatBackFixture
{
    PresnapFixture()
    {
        adaptor = new SnapAdaptor(snap.get(), wta, &settings, &parent);
        layouts->assignLayout(kLeft, layouts->currentVirtualDesktop(), QString(), layout);
        adjacency.first = zone(0);
        adjacency.last = zone(2);
        snap->setZoneAdjacencyResolver(&adjacency);
    }
    ~PresnapFixture()
    {
        snap->setZoneAdjacencyResolver(nullptr);
        adaptor->clearEngine();
    }
    /// A window of @p appId, registered visible and unmaximized.
    QString registerApp(const QString& instance, const QString& appId)
    {
        PhosphorEngine::WindowMetadata meta;
        meta.appId = appId;
        meta.isMaximized = false;
        meta.isFullscreen = false;
        meta.isMinimized = false;
        registry.upsert(instance, meta);
        const QString windowId = appId + QLatin1Char('|') + instance;
        registry.canonicalizeWindowId(windowId);
        return windowId;
    }
    /// @p windowId snap-floated on the left output, standing at @p frame, with
    /// no float-back on record.
    void floatingAt(const QString& windowId, const QRect& frame)
    {
        snapThenFloat(windowId, kLeft);
        QVERIFY(snap->isFloating(windowId));
        setFrame(windowId, frame);
        wta->service()->clearFreeGeometry(windowId);
    }

    EdgeZoneAdjacency adjacency;
    SnapAdaptor* adaptor = nullptr; // parent-owned
};

/// Snap-all needs a QScreen for the batch, which the synthetic outputs above do
/// not carry, so its row runs on the convenience fixture: no ScreenManager, the
/// primary screen.
struct SnapAllRig : WtaConvenienceFixture
{
    SnapAllRig()
    {
        initFixture();
        m_snapEngine->setNavigationStateProvider(m_wta);
        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);
    }
    ~SnapAllRig()
    {
        m_snapEngine->setNavigationStateProvider(nullptr);
        cleanupFixture();
    }
    using WtaConvenienceFixture::m_screenId;
    using WtaConvenienceFixture::m_snapAdaptor;
    using WtaConvenienceFixture::m_snapEngine;
    using WtaConvenienceFixture::m_wta;
    using WtaConvenienceFixture::m_zoneIds;
};
} // namespace

class TestWtaPresnapCapture : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // A daemon-driven snap of a floating window records its live frame and the
    // snap intent before the commit (F91's intent half). Once in a zone, another
    // snap records nothing, even after the window reported the zone it now
    // occupies (F90).
    void moveWindowToZoneRecordsTheFreeFrameOfAFloatingWindow()
    {
        PresnapFixture f;
        const QString w = f.registerWindow(QStringLiteral("free-1"));
        f.floatingAt(w, kFree);

        f.adaptor->moveWindowToZoneOnScreen(w, f.zone(1), kLeft);

        QCOMPARE(f.floatBack(w, kLeft), kFree);
        QVERIFY(f.wta->service()->userSnappedClasses().contains(QStringLiteral("app")));

        f.setFrame(w, f.zoneRect(1, kLeft));
        f.adaptor->moveWindowToZoneOnScreen(w, f.zone(2), kLeft);
        QCOMPARE(f.floatBack(w, kLeft), kFree);
    }

    // Every user unfloat records the frame it leaves, from the D-Bus float
    // calls as much as from Meta+F, so the next float returns there (F52).
    void dbusUnfloatsRecordTheFreeFrame_data()
    {
        QTest::addColumn<int>("call");
        QTest::newRow("toggleFloatForWindow") << 0;
        QTest::newRow("setWindowFloat") << 1;
        QTest::newRow("setWindowFloatingForScreen") << 2;
    }
    void dbusUnfloatsRecordTheFreeFrame()
    {
        QFETCH(int, call);
        PresnapFixture f;
        const QString w = f.registerWindow(QStringLiteral("unfloat-1"));
        f.floatingAt(w, kFree);
        if (call == 0) {
            f.adaptor->toggleFloatForWindow(w, kLeft);
        } else if (call == 1) {
            f.snap->setWindowFloat(w, false, kLeft);
        } else {
            f.wta->setWindowFloatingForScreen(w, kLeft, false);
        }
        QCOMPARE(f.snap->zoneForWindow(w), f.zone(0));
        QCOMPARE(f.floatBack(w, kLeft), kFree);
    }

    // A window in no zone that snap does not hold floating (a screen change
    // unsnapped it) stands on a free frame too (F78).
    void aFreeWindowThatIsNotFloatingRecordsItsFrame()
    {
        PresnapFixture f;
        const QString w = f.registerWindow(QStringLiteral("f78"));
        f.snap->commitSnap(w, f.zone(0), kLeft);
        f.wta->service()->unassignWindow(w);
        QVERIFY(f.snap->zoneForWindow(w).isEmpty());
        QVERIFY(!f.snap->isFloating(w));
        f.setFrame(w, kFree);

        f.adaptor->moveWindowToZoneOnScreen(w, f.zone(1), kLeft);

        QCOMPARE(f.floatBack(w, kLeft), kFree);
    }

    // A window snap does not track at all records its frame under the screen
    // the snap lands on (F91).
    void moveWindowToZoneOfAnUntrackedWindowRecordsItsFrame()
    {
        PresnapFixture f;
        const QString w = f.registerWindow(QStringLiteral("f91"));
        QVERIFY(f.snap->screenForTrackedWindow(w).isEmpty());
        f.setFrame(w, kFree);

        f.adaptor->moveWindowToZone(w, f.zone(1));

        QCOMPARE(f.snap->zoneForWindow(w), f.zone(1));
        QCOMPARE(f.floatBack(w, kLeft), kFree);
    }

    // Snap-all records each candidate's frame while the shadow still holds it:
    // the effect applies the zones only after the reply (F238).
    void snapAllRecordsEachCandidatesFreeFrame()
    {
        SnapAllRig rig;
        const QString w = QStringLiteral("app|f238");
        rig.m_snapEngine->commitSnap(w, rig.m_zoneIds[0], rig.m_screenId);
        rig.m_snapEngine->setWindowFloat(w, true);
        QVERIFY(rig.m_snapEngine->isFloating(w));
        rig.m_wta->service()->clearFreeGeometry(w);
        rig.m_wta->setFrameGeometry(w, kFree.x(), kFree.y(), kFree.width(), kFree.height());

        const PhosphorProtocol::SnapAllResultList results =
            rig.m_snapAdaptor->calculateSnapAllWindows({w}, rig.m_screenId);

        QCOMPARE(results.size(), 1);
        const auto rec = rig.m_wta->service()->placementStore().peekExact(w);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->freeGeometryByScreen.value(rig.m_screenId), kFree);
    }

    // The snap-all confirmation and a swap are user snaps, so they record the
    // snap intent like every shortcut does (F197).
    void snapAllAndSwapRecordTheSnapIntent()
    {
        PresnapFixture f;
        const QString batched = f.registerApp(QStringLiteral("f197-batch"), QStringLiteral("batchapp"));
        PhosphorProtocol::SnapConfirmationEntry entry;
        entry.windowId = batched;
        entry.zoneId = f.zone(1);
        entry.screenId = kLeft;
        entry.isRestore = false;
        f.adaptor->windowsSnappedBatch({entry});
        QCOMPARE(f.snap->zoneForWindow(batched), f.zone(1));
        QVERIFY(f.wta->service()->userSnappedClasses().contains(QStringLiteral("batchapp")));

        const QString first = f.registerApp(QStringLiteral("f197-a"), QStringLiteral("firstapp"));
        const QString second = f.registerApp(QStringLiteral("f197-b"), QStringLiteral("secondapp"));
        f.snap->commitSnap(first, f.zone(0), kLeft);
        f.snap->commitSnap(second, f.zone(2), kLeft);
        QVERIFY(!f.wta->service()->userSnappedClasses().contains(QStringLiteral("firstapp")));

        f.adaptor->swapWindowsById(first, second);

        QCOMPARE(f.snap->zoneForWindow(first), f.zone(2));
        QVERIFY(f.wta->service()->userSnappedClasses().contains(QStringLiteral("firstapp")));
        QVERIFY(f.wta->service()->userSnappedClasses().contains(QStringLiteral("secondapp")));
    }

    // The float toggle samples through the helper: a second toggle that lands
    // before the floated window's frame report (the shadow still on its zone)
    // keeps the float-back it floated to (F178).
    void secondFloatToggleInsideTheFlushKeepsTheFloatBack()
    {
        PresnapFixture f;
        const QString w = f.registerWindow(QStringLiteral("f178"));
        f.floatingAt(w, kFree);
        f.adaptor->moveWindowToZoneOnScreen(w, f.zone(1), kLeft);
        f.setFrame(w, f.zoneRect(1, kLeft));
        QCOMPARE(f.floatBack(w, kLeft), kFree);
        const PhosphorEngine::NavigationContext ctx{w, kLeft};

        f.snap->toggleFocusedFloat(ctx);
        QVERIFY(f.snap->isFloating(w));
        f.snap->toggleFocusedFloat(ctx);

        QVERIFY(!f.snap->isFloating(w));
        QCOMPARE(f.floatBack(w, kLeft), kFree);
    }

    // The helper's sampling refusals (F45): a maximized window's frame is the
    // output, and a frame on another output is not a spot on this one.
    void helperRefusesAMaximizedWindow()
    {
        PresnapFixture f;
        const QString w = f.registerWindow(QStringLiteral("f45-max"));
        f.floatingAt(w, kFree);
        f.registerWindow(QStringLiteral("f45-max"), true);

        f.snap->recordFreeFrameBeforeUserSnap(w, kLeft);

        QVERIFY(!f.floatBack(w, kLeft).isValid());
    }

    // The frame is filed under the screen it is on, whatever screen the caller
    // passes (F23); a frame from another monitor is never filed under this one.
    void helperFilesTheFrameUnderTheScreenItIsOn()
    {
        PresnapFixture f;
        const QString w = f.registerWindow(QStringLiteral("f45-off"));
        const QRect onRight = kFree.translated(kRightRect.x(), 0);
        f.floatingAt(w, onRight);

        f.snap->recordFreeFrameBeforeUserSnap(w, kLeft);

        QCOMPARE(f.floatBack(w, kRight), onRight);
        QVERIFY(!f.floatBack(w, kLeft).isValid());
    }

    // A bus snap of a window that moved to another monitor with no screen
    // report (snap still tracks it on the first) records the frame it leaves
    // under the monitor it is on (F23).
    void aCrossScreenBusSnapRecordsTheFrameItLeaves()
    {
        PresnapFixture f;
        const QString w = f.registerWindow(QStringLiteral("f23-cross"));
        const QRect onRight = kFree.translated(kRightRect.x(), 0);
        f.floatingAt(w, onRight);
        QCOMPARE(f.snap->screenForTrackedWindow(w), kLeft);

        f.adaptor->moveWindowToZoneOnScreen(w, f.zone(1), kLeft);

        QCOMPARE(f.floatBack(w, kRight), onRight);
    }

    // Each keyboard snap records the free frame it leaves (F45).
    void keyboardSnapsRecordTheFreeFrame_data()
    {
        QTest::addColumn<QString>("site");
        QTest::newRow("move") << QStringLiteral("move");
        QTest::newRow("span") << QStringLiteral("span");
        QTest::newRow("position") << QStringLiteral("position");
        QTest::newRow("push") << QStringLiteral("push");
    }
    void keyboardSnapsRecordTheFreeFrame()
    {
        QFETCH(QString, site);
        PresnapFixture f;
        const QString w = f.registerWindow(QStringLiteral("f45-key"));
        f.floatingAt(w, kFree);
        const PhosphorEngine::NavigationContext ctx{w, kLeft};

        if (site == QLatin1String("move")) {
            f.snap->moveFocusedInDirection(QStringLiteral("left"), ctx);
        } else if (site == QLatin1String("span")) {
            f.snap->spanFocusedInDirection(QStringLiteral("right"), ctx);
        } else if (site == QLatin1String("position")) {
            f.snap->moveFocusedToPosition(2, ctx);
        } else {
            f.snap->pushFocusedToEmptyZone(ctx);
        }

        QVERIFY2(!f.snap->zoneForWindow(w).isEmpty(), "the snap must land, or the row proves nothing");
        QCOMPARE(f.floatBack(w, kLeft), kFree);
    }
};

QTEST_MAIN(TestWtaPresnapCapture)
#include "test_wta_presnap_capture.moc"
