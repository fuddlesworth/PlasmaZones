// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_snap_float_statement.cpp
 * @brief The float state the snap engine states, through the WindowTracking
 *        relay the effect hears: a batch restore floats the window where it
 *        lands, on its own screen (F107, F664, F838); a commit states the
 *        float of the desktop in view, not of a hidden one (F725, F741, F795);
 *        and entering a desktop states the float or free state a window on
 *        several desktops has there, the per-window reconcile touching no
 *        other window (F742, F777, F778). The float-back goes back on for a
 *        frame management left the window at, a size-increment client's
 *        settled frame short of its zone included (F293), and never for one
 *        the user dragged it to. A float re-stated on a return from a tiling
 *        desktop is heard after that desktop's own float edge.
 */

#include "wta_snap_nav_fixture.h"
#include "dbus/snapadaptor/snapadaptor.h"

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/GeometryUtils.h>
#include <PhosphorProtocol/WindowTypes.h>

#include <QSignalSpy>

#include <algorithm>

namespace {
const QRect kFreeLeft(300, 200, 640, 480);
const QRect kFreeRight(2200, 200, 640, 480);

/// The relay to the effect, through the SnapAdaptor as the daemon wires it,
/// and the routed float verdict and writer the daemon gives the tracking
/// service on snapping screens.
struct StatementFixture : SnapNavFixture
{
    StatementFixture()
    {
        adaptor = new SnapAdaptor(snap.get(), wta, &settings, &parent);
        wta->service()->setEngineFloatResolver([this](const QString& windowId) {
            return snap->isFloating(windowId);
        });
        wta->service()->setEngineFloatWriter([this](const QString& windowId, bool floating) {
            snap->setFloating(windowId, floating);
        });
    }
    ~StatementFixture()
    {
        wta->service()->setEngineFloatResolver({});
        wta->service()->setEngineFloatWriter({});
        adaptor->clearEngine();
    }
    /// A batch restore of @p windowId to @p rect, naming @p screen.
    static PhosphorEngine::ZoneAssignmentEntry restoreFor(const QString& windowId, const QRect& rect,
                                                          const QString& screen)
    {
        PhosphorEngine::ZoneAssignmentEntry entry;
        entry.windowId = windowId;
        entry.targetZoneId = PhosphorEngine::RestoreSentinel;
        entry.targetGeometry = rect;
        entry.targetScreenId = screen;
        return entry;
    }
    /// @p windowId on desktops 1 and 2 of the left output, snapped in zone 0
    /// on desktop 1 and floated on desktop 2 at @p floatBack; desktop 1 in
    /// view and re-entered, so the effect last heard "not floating".
    void snappedOnOneFloatedOnTwo(const QString& windowId, const QRect& floatBack)
    {
        showDesktop(kLeft, 1);
        snapOn(windowId, {zone(0)}, kLeft, 1);
        showDesktop(kLeft, 2);
        snap->reconcileDesktopMemberships(kLeft, onOneAndTwo());
        snap->setWindowFloat(windowId, true, kLeft);
        wta->service()->recordFreeGeometry(windowId, kLeft, floatBack, true);
        showDesktop(kLeft, 1);
        snap->reconcileDesktopMemberships(kLeft, onOneAndTwo());
    }
    static PhosphorEngine::DesktopSpanQuery onOneAndTwo()
    {
        return [](const QString&) {
            PhosphorEngine::DesktopSpan span;
            span.known = true;
            span.desktops = {1, 2};
            return span;
        };
    }

    SnapAdaptor* adaptor = nullptr; // parent-owned
};
} // namespace

class TestSnapFloatStatement : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ── The batch restore arm ──────────────────────────────────────────────

    // Each geometry names the screen its commit resolved; a restore's is
    // empty, the effect's float marker (F838).
    void aBatchNamesEachEntrysScreen()
    {
        StatementFixture f;
        const QString a = f.registerWindow(QStringLiteral("s1-a"));
        const QString b = f.registerWindow(QStringLiteral("s1-b"));
        PhosphorEngine::ZoneAssignmentEntry snapEntry;
        snapEntry.windowId = a;
        snapEntry.targetZoneId = f.zone(0);
        snapEntry.targetGeometry = f.zoneRect(0, kRight);
        snapEntry.targetScreenId = kRight;

        const PhosphorProtocol::WindowGeometryList geometries =
            f.snap->applyBatchAssignments({snapEntry, StatementFixture::restoreFor(b, kFreeLeft, kLeft)});

        QCOMPARE(geometries.size(), 2);
        QCOMPARE(geometries.at(0).screenId, kRight);
        QVERIFY2(geometries.at(1).screenId.isEmpty(), "a restore is applied with no screen");
    }

    // A restore consumes the float-back of the screen it lands on and keeps
    // the one remembered for the other monitor.
    void aRestoreClearsOnlyItsScreensFloatBack()
    {
        StatementFixture f;
        const QString w = f.registerWindow(QStringLiteral("s1-clear"));
        f.wta->service()->recordFreeGeometry(w, kLeft, kFreeLeft, true);
        f.wta->service()->recordFreeGeometry(w, kRight, kFreeRight, true);
        QCOMPARE(f.floatBack(w, kRight), kFreeRight);

        f.snap->applyBatchAssignments({StatementFixture::restoreFor(w, kFreeLeft, kLeft)});

        QVERIFY(!f.floatBack(w, kLeft).isValid());
        QCOMPARE(f.floatBack(w, kRight), kFreeRight);
    }

    // A restore naming no screen, whose rect lies on none, clears nothing:
    // the all-screens clear wiped the spot remembered on every monitor (F664).
    void aRestoreWithNoScreenClearsNothing()
    {
        StatementFixture f;
        const QString w = f.registerWindow(QStringLiteral("s1-none"));
        f.wta->service()->recordFreeGeometry(w, kLeft, kFreeLeft, true);
        f.wta->service()->recordFreeGeometry(w, kRight, kFreeRight, true);

        f.snap->applyBatchAssignments({StatementFixture::restoreFor(w, QRect(3700, 1000, 600, 600), QString())});

        QCOMPARE(f.floatBack(w, kLeft), kFreeLeft);
        QCOMPARE(f.floatBack(w, kRight), kFreeRight);
    }

    // A snapped window put back to its free spot floats there, tracked, and
    // its record says so (F107).
    void aRestoreLeavesTheWindowFloating()
    {
        StatementFixture f;
        const QString w = f.registerWindow(QStringLiteral("s1-float"));
        f.snap->commitSnap(w, f.zone(0), kLeft);
        QSignalSpy floats(f.snap.get(), &PhosphorSnapEngine::SnapEngine::windowFloatingChanged);

        f.snap->applyBatchAssignments({StatementFixture::restoreFor(w, kFreeLeft, kLeft)});

        QVERIFY(f.snap->isFloating(w));
        QVERIFY(f.snap->isWindowTracked(w));
        const auto rec = f.wta->service()->placementStore().peekExact(w);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->slotFor(WindowPlacement::snapEngineId()).state, WindowPlacement::stateFloating());
        QCOMPARE(floats.count(), 1);
        QCOMPARE(floats.first().at(0).toString(), w);
        QCOMPARE(floats.first().at(1).toBool(), true);
        QCOMPARE(floats.first().at(2).toString(), kLeft);
    }

    // A span entry commits every zone it names.
    void aMultiZoneEntryCommitsTheWholeSpan()
    {
        StatementFixture f;
        const QString w = f.registerWindow(QStringLiteral("s1-span"));
        PhosphorEngine::ZoneAssignmentEntry entry;
        entry.windowId = w;
        entry.targetZoneId = f.zone(0);
        entry.targetZoneIds = {f.zone(0), f.zone(1)};
        entry.targetGeometry = f.wta->service()->resolveZoneGeometry(entry.targetZoneIds, kLeft);
        entry.targetScreenId = kLeft;

        f.snap->applyBatchAssignments({entry});

        QCOMPARE(f.wta->service()->zonesForWindow(w), entry.targetZoneIds);
    }

    // The layout switch's restore of an excess window names the screen its
    // rect was read for (F664).
    void aResnapRestoreNamesItsScreen()
    {
        StatementFixture f;
        const QString w = f.registerWindow(QStringLiteral("s1-resnap"));
        f.wta->service()->recordFreeGeometry(w, kLeft, kFreeLeft, true);
        f.snap->commitSnap(w, f.zone(2), kLeft);
        PhosphorZones::Layout* two = createTestLayout(2, f.layouts);
        f.layouts->addLayout(two);
        f.layouts->assignLayout(kLeft, 1, QString(), two);
        f.wta->service()->populateResnapBufferForAllScreens({}, {kLeft});

        const QVector<PhosphorEngine::ZoneAssignmentEntry> entries = f.snap->calculateResnapFromPreviousLayout();

        const auto it = std::find_if(entries.cbegin(), entries.cend(), [&w](const auto& entry) {
            return entry.windowId == w;
        });
        QVERIFY(it != entries.cend());
        QCOMPARE(it->targetZoneId, QString(PhosphorEngine::RestoreSentinel));
        QCOMPARE(it->targetScreenId, kLeft);
    }

    // ── A commit's float statement ─────────────────────────────────────────

    // A commit of a floating window ends the float once, and the next float
    // is heard (F795).
    void aCommitEndsAFloatOnce()
    {
        StatementFixture f;
        const QString w = f.registerWindow(QStringLiteral("s2-once"));
        f.snapThenFloat(w, kLeft);
        QSignalSpy cleared(f.snap.get(), &PhosphorSnapEngine::SnapEngine::windowFloatingClearedForSnap);
        QSignalSpy relayed(f.wta, &WindowTrackingAdaptor::windowFloatingChanged);

        f.snapOn(w, {f.zone(1)}, kLeft);
        QCOMPARE(cleared.count(), 1);
        QCOMPARE(relayed.count(), 1);
        QCOMPARE(relayed.first().at(1).toBool(), false);

        f.snap->setWindowFloat(w, true, kLeft);
        QCOMPARE(relayed.count(), 2);
        QCOMPARE(relayed.last().at(1).toBool(), true);
    }

    // The routed verdict answering another engine does not hide snap's own
    // float bit: the commit still ends it and drops its pre-float zone.
    void aForeignVerdictStillEndsTheOwnBit()
    {
        StatementFixture f;
        const QString w = f.registerWindow(QStringLiteral("s2-foreign"));
        f.snapThenFloat(w, kLeft);
        f.wta->service()->setEngineFloatResolver([](const QString&) {
            return false;
        });
        QSignalSpy cleared(f.snap.get(), &PhosphorSnapEngine::SnapEngine::windowFloatingClearedForSnap);

        f.snapOn(w, {f.zone(1)}, kLeft);

        QCOMPARE(cleared.count(), 1);
        QVERIFY(f.wta->service()->preFloatZones(w).isEmpty());
    }

    // A commit of a window that floats nowhere says nothing about floating.
    void aNonFloatingCommitSaysNothing()
    {
        StatementFixture f;
        const QString w = f.registerWindow(QStringLiteral("s2-plain"));
        QSignalSpy cleared(f.snap.get(), &PhosphorSnapEngine::SnapEngine::windowFloatingClearedForSnap);

        f.snapOn(w, {f.zone(0)}, kLeft);

        QCOMPARE(cleared.count(), 0);
    }

    // A commit pinned to a hidden desktop leaves the float the desktop in
    // view shows (F725).
    void aHiddenPinnedCommitLeavesTheFloatInView()
    {
        StatementFixture f;
        const QString w = f.registerWindow(QStringLiteral("s2-hidden"));
        f.showDesktop(kLeft, 1);
        f.snapOn(w, {f.zone(0)}, kLeft, 2);
        f.snap->stateForWindowOnScreen(w, kLeft, 1);
        f.snap->setWindowFloat(w, true, kLeft);
        QSignalSpy cleared(f.snap.get(), &PhosphorSnapEngine::SnapEngine::windowFloatingClearedForSnap);
        QSignalSpy relayed(f.wta, &WindowTrackingAdaptor::windowFloatingChanged);

        f.snapOn(w, {f.zone(2)}, kLeft, 2);

        QCOMPARE(cleared.count(), 0);
        QCOMPARE(relayed.count(), 0);
        QVERIFY(static_cast<PhosphorSnapEngine::SnapState*>(f.snap->stateForScreen(kLeft))->isFloating(w));
    }

    // A commit on the desktop in view ends a float the window has on another
    // of its desktops, which is what the effect last heard, so the next
    // float there is heard too (F741).
    void aCommitInViewEndsAFloatFromAnotherDesktop()
    {
        StatementFixture f;
        const QString w = f.registerWindow(QStringLiteral("s2-elsewhere"));
        f.showDesktop(kLeft, 1);
        f.snapOn(w, {f.zone(0)}, kLeft, 1);
        f.snap->stateForWindowOnScreen(w, kLeft, 2);
        f.showDesktop(kLeft, 2);
        f.snap->setWindowFloat(w, true, kLeft);
        f.showDesktop(kLeft, 1);
        QSignalSpy relayed(f.wta, &WindowTrackingAdaptor::windowFloatingChanged);

        f.snapOn(w, {f.zone(0)}, kLeft, 1);
        QCOMPARE(relayed.count(), 1);
        QCOMPARE(relayed.first().at(1).toBool(), false);

        f.snap->setWindowFloat(w, true, kLeft);
        QCOMPARE(relayed.count(), 2);
        QCOMPARE(relayed.last().at(1).toBool(), true);
    }

    // ── Entering a desktop ─────────────────────────────────────────────────

    // Entering the desktop where the window floats says so, and puts it back
    // on its float-back when it stands on another desktop's zone (F742).
    void enteringADesktopWhereItFloatsSaysFloating()
    {
        StatementFixture f;
        const QString w = f.registerWindow(QStringLiteral("s4-float"));
        f.snappedOnOneFloatedOnTwo(w, kFreeLeft);
        f.setFrame(w, f.zoneRect(0, kLeft));
        QSignalSpy relayed(f.wta, &WindowTrackingAdaptor::windowFloatingChanged);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);

        f.showDesktop(kLeft, 2);
        f.snap->reconcileDesktopMemberships(kLeft, StatementFixture::onOneAndTwo());

        QCOMPARE(relayed.count(), 1);
        QCOMPARE(relayed.first().at(1).toBool(), true);
        QCOMPARE(applies.count(), 1);
        QCOMPARE(applies.first().at(0).toString(), w);
        const QRect applied(applies.first().at(1).toInt(), applies.first().at(2).toInt(), applies.first().at(3).toInt(),
                            applies.first().at(4).toInt());
        QCOMPARE(applied, kFreeLeft);
        QVERIFY(applies.first().at(5).toString().isEmpty());
        QCOMPARE(applies.first().at(8).toInt(), static_cast<int>(PhosphorProtocol::PlacementPurpose::Restatement));
    }

    // A size-increment client settles short of its zone, so its frame is not
    // the zone rect, yet management put it there. Entering the desktop where
    // it floats puts it back on its float-back as for an exact zone frame
    // (F293). The exact zone-rect compare alone missed it.
    void aFloatSettledShortOfItsZoneIsPutBack()
    {
        StatementFixture f;
        const QString w = f.registerWindow(QStringLiteral("s4-short"));
        f.snappedOnOneFloatedOnTwo(w, kFreeLeft);
        const QRect settled = f.zoneRect(0, kLeft).adjusted(0, 0, -7, -9);
        f.setFrame(w, settled);
        QVERIFY2(f.wta->isSettledManagedFrame(w, settled), "precondition: reported while it held its zone in view");
        QSignalSpy relayed(f.wta, &WindowTrackingAdaptor::windowFloatingChanged);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);

        f.showDesktop(kLeft, 2);
        f.snap->reconcileDesktopMemberships(kLeft, StatementFixture::onOneAndTwo());

        QCOMPARE(relayed.count(), 1);
        QCOMPARE(applies.count(), 1);
        const QRect applied(applies.first().at(1).toInt(), applies.first().at(2).toInt(), applies.first().at(3).toInt(),
                            applies.first().at(4).toInt());
        QCOMPARE(applied, kFreeLeft);
    }

    // A float standing anywhere but where management left it is the user's,
    // and is not moved (F777). The frame comes from a drag, which the adaptor
    // never takes for a settled managed frame.
    void aFloatAwayFromAnyZoneIsNotMoved()
    {
        StatementFixture f;
        const QString w = f.registerWindow(QStringLiteral("s4-free"));
        f.snappedOnOneFloatedOnTwo(w, kFreeLeft);
        const QRect moved(900, 300, 500, 400);
        f.wta->setInteractiveDragWindow(w);
        f.setFrame(w, moved);
        f.wta->setInteractiveDragWindow(QString());
        QVERIFY(!f.wta->isSettledManagedFrame(w, moved));
        QSignalSpy relayed(f.wta, &WindowTrackingAdaptor::windowFloatingChanged);
        QSignalSpy applies(f.wta, &WindowTrackingAdaptor::applyGeometryRequested);

        f.showDesktop(kLeft, 2);
        f.snap->reconcileDesktopMemberships(kLeft, StatementFixture::onOneAndTwo());

        QCOMPARE(relayed.count(), 1);
        QCOMPARE(applies.count(), 0);
    }

    // A per-window reconcile re-applies that window only: the others on the
    // screen did not move (F777).
    void aPerWindowReconcileLeavesTheOtherWindows()
    {
        StatementFixture f;
        const QString w1 = f.registerWindow(QStringLiteral("s4-one"));
        const QString w2 = f.registerWindow(QStringLiteral("s4-two"));
        f.showDesktop(kLeft, 1);
        f.snapOn(w1, {f.zone(0)}, kLeft, 1);
        f.snapOn(w2, {f.zone(1)}, kLeft, 1);
        f.snapOn(w1, {f.zone(1)}, kLeft, 2);
        f.snapOn(w2, {f.zone(2)}, kLeft, 2);
        QSignalSpy resnaps(f.snap.get(), &PhosphorSnapEngine::SnapEngine::resnapToNewLayoutRequested);

        f.snap->reconcileWindowMemberships(w2, StatementFixture::onOneAndTwo());

        QStringList reapplied;
        for (const QList<QVariant>& emission : std::as_const(resnaps)) {
            for (const auto& entry :
                 PhosphorEngine::GeometryUtils::deserializeZoneAssignments(emission.at(0).toString(), nullptr)) {
                reapplied.append(entry.windowId);
            }
        }
        QVERIFY2(reapplied.contains(w2), "control: the reconciled window is re-applied");
        QVERIFY2(!reapplied.contains(w1), "another window on the screen is left alone");
    }

    // Entering a desktop where the window holds nothing, while it is snapped
    // on another, says it is unsnapped here (F778).
    void enteringADesktopWhereItHoldsNothingSaysUnsnapped()
    {
        StatementFixture f;
        const QString w = f.registerWindow(QStringLiteral("s4-empty"));
        f.showDesktop(kLeft, 1);
        f.snapOn(w, {f.zone(0)}, kLeft, 1);
        f.snap->stateForWindowOnScreen(w, kLeft, 2);
        QSignalSpy states(f.snap.get(), &PhosphorSnapEngine::SnapEngine::windowSnapStateChanged);

        f.showDesktop(kLeft, 2);
        f.snap->reconcileDesktopMemberships(kLeft, StatementFixture::onOneAndTwo());

        int unsnapped = 0;
        for (const QList<QVariant>& emission : std::as_const(states)) {
            const auto entry = emission.at(1).value<PhosphorProtocol::WindowStateEntry>();
            if (entry.windowId == w && entry.changeType == QLatin1String("unsnapped")) {
                ++unsnapped;
            }
        }
        QCOMPARE(unsnapped, 1);
    }

    // A sticky window floating on desktop 1, shown on desktop 2 while a
    // tiling mode runs there. The float edge the tiling engine sends from
    // there goes through the same relay as snapping's, so the relay's
    // last-broadcast memory reads "not floating" by the return, and the float
    // re-stated on desktop 1 is heard rather than taken for a repeat. With no
    // edge from there, the memory and the effect both still read "floating".
    void aFloatReStatedOnAReturnFromTilingIsHeard()
    {
        StatementFixture f;
        const QString w = f.registerWindow(QStringLiteral("s4-tiling"));
        f.showDesktop(kLeft, 1);
        f.snapThenFloat(w, kLeft);
        const auto* inView = static_cast<PhosphorSnapEngine::SnapState*>(f.snap->stateForScreen(kLeft));
        QVERIFY2(inView && inView->isFloating(w) && inView->zonesForWindow(w).isEmpty(),
                 "precondition: floating on desktop 1, with no zone there");
        const PhosphorEngine::DesktopSpanQuery sticky = [](const QString&) {
            PhosphorEngine::DesktopSpan span;
            span.known = true;
            span.sticky = true;
            return span;
        };
        // Captures the engine, not the fixture: the fixture's type is in an
        // anonymous namespace, which a capture would carry into this member's
        // closure type (-Wsubobject-linkage).
        auto* const snap = f.snap.get();
        const auto runs = [snap](PhosphorZones::AssignmentEntry::Mode mode) {
            snap->setLiveModeResolver([mode](const QString&) {
                return mode;
            });
        };
        QSignalSpy relayed(f.wta, &WindowTrackingAdaptor::windowFloatingChanged);

        runs(PhosphorZones::AssignmentEntry::Mode::Autotile);
        f.showDesktop(kLeft, 2);
        f.snap->reconcileDesktopMemberships(kLeft, sticky);
        // The tiling engine's edge, routed as the daemon's passive sync routes it.
        f.wta->relayWindowFloatingChanged(w, false, kLeft);
        QCOMPARE(relayed.count(), 1);
        runs(PhosphorZones::AssignmentEntry::Mode::Snapping);
        f.showDesktop(kLeft, 1);
        f.snap->reconcileDesktopMemberships(kLeft, sticky);
        f.snap->setLiveModeResolver({});

        QCOMPARE(relayed.count(), 2);
        QCOMPARE(relayed.last().at(0).toString(), w);
        QCOMPARE(relayed.last().at(1).toBool(), true);
    }
};

QTEST_MAIN(TestSnapFloatStatement)
#include "test_snap_float_statement.moc"
