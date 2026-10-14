// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_snap_desktop_carry.cpp
 * @brief The snap carry of a window KWin moves to another desktop or activity
 *        (ledger-pass37 EJ): a destination keeps the zone or float the window
 *        already has there, a floating window arrives floating, a minimized
 *        one carries its slot into the layout it arrives in, a destination
 *        with no slot leaves the other desktops' zones alone, a span keeps its
 *        zones on a shared layout, every destination is given its slot with
 *        one apply, a float kept on a hidden desktop is given its float-back
 *        when that desktop shows, and an arrival in view takes focus. Plus
 *        the activity arms: a move into the activity in view is carried like
 *        a desktop move, and a move to one not in view lands when its desktop
 *        shows there (F550, ledger-pass26 DJ).
 */

#include <QHash>
#include <QRect>
#include <QSet>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>
#include <memory>

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"
#include <PhosphorEngine/GeometryUtils.h>
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorSnapEngine/INavigationStateProvider.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorZones/Zone.h>

using namespace PlasmaZones;
using PhosphorEngine::WindowPlacement;
using PhosphorSnapEngine::SnapEngine;
using PhosphorSnapEngine::SnapState;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {
const QString kScreen = QStringLiteral("DP-1");
const QString kWindow = QStringLiteral("app|cccccccc-0000-0000-0000-000000000001");
const QString kSecond = QStringLiteral("app|cccccccc-0000-0000-0000-000000000002");
const QString kActX = QStringLiteral("act-x");
const QString kActY = QStringLiteral("act-y");
const QRect kFree(100, 100, 600, 400);

PhosphorEngine::DesktopSpan on(QSet<int> desktops, const QString& activity = QString())
{
    PhosphorEngine::DesktopSpan span;
    span.known = true;
    span.desktops = std::move(desktops);
    span.activity = activity;
    return span;
}

PhosphorEngine::DesktopSpanQuery spanOf(const PhosphorEngine::DesktopSpan& windowSpan)
{
    return [windowSpan](const QString& windowId) {
        return windowId == kWindow ? windowSpan : on({1});
    };
}
} // namespace

/// The compositor's frames, as the effect reports them. Outside the anonymous
/// namespace: the test class holds one as a member, and a member of an
/// internal-linkage type is -Wsubobject-linkage.
class FrameNavState : public PhosphorSnapEngine::INavigationStateProvider
{
public:
    QString lastCursorScreenName() const override
    {
        return kScreen;
    }
    QString lastActiveScreenName() const override
    {
        return kScreen;
    }
    QString lastActiveWindowId() const override
    {
        return {};
    }
    QRect frameGeometry(const QString& windowId) const override
    {
        return m_frames.value(windowId);
    }

    QHash<QString, QRect> m_frames;
};

class TestSnapDesktopCarry : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetector(nullptr);
        m_service = new PhosphorPlacement::WindowTrackingService(m_layoutManager, nullptr, nullptr);
        m_engine = new SnapEngine(m_layoutManager, m_service, m_zoneDetector, nullptr, nullptr);
        m_engine->setEngineSettings(m_settings);
        m_service->setSnapEngine(m_engine);
        m_service->setSnapStateResolver(PhosphorPlacement::snapStateResolverFor(m_engine));

        m_testLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_testLayout);
        m_layoutManager->setActiveLayout(m_testLayout);
        m_zoneIds = sortedZoneIds(m_testLayout);
        QObject::connect(m_engine, &SnapEngine::resnapToNewLayoutRequested, m_engine, [this](const QString& payload) {
            const auto entries = PhosphorEngine::GeometryUtils::deserializeZoneAssignments(payload, nullptr);
            m_batch += entries;
            m_engine->applyBatchAssignments(entries);
        });
    }

    void cleanup()
    {
        m_engine->setNavigationStateProvider(nullptr);
        m_nav.m_frames.clear();
        m_service->setSnapState(nullptr);
        m_service->setSnapEngine(nullptr);
        delete m_engine;
        m_engine = nullptr;
        delete m_service;
        m_service = nullptr;
        delete m_zoneDetector;
        m_zoneDetector = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        m_batch.clear();
        m_guard.reset();
    }

    // A window on desktops 1 and 2 with its own zone on each, moved off 1
    // while 1 is in view: it is put into the zone it has on 2, and the
    // release of 1 is not reported away (F754).
    void aKeptZoneOnAHiddenDesktopIsReStated()
    {
        const QStringList destination = assignLayout(2, 3);
        snapOn(1, m_zoneIds[0]);
        snapOn(2, destination[1]);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);

        const auto result = m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));

        QVERIFY2(result.released.isEmpty(), "a window keeping a zone on this screen is not released");
        QCOMPARE(m_batch.size(), 1);
        QCOMPARE(m_batch.first().targetZoneId, destination[1]);
        QCOMPARE(m_batch.first().virtualDesktop, 2);
        QCOMPARE(m_batch.first().targetGeometry, m_service->resolveZoneGeometry({destination[1]}, kScreen));
        QCOMPARE(zonesOn(2), QStringList{destination[1]});
        QVERIFY(zonesOn(1).isEmpty());
    }

    // A destination where the window floats keeps the float: no zone is
    // written there, and the float is stated (F619).
    void aKeptFloatIsNotOverwritten()
    {
        assignLayout(2, 3);
        snapOn(1, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(on({1, 2})));
        m_engine->setWindowFloat(kWindow, true, kScreen);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_batch.clear();
        QSignalSpy floats(m_engine, &SnapEngine::windowFloatingChanged);

        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));

        QVERIFY2(m_batch.isEmpty(), "a kept float is not overwritten by the carry");
        QCOMPARE(floats.count(), 1);
        QCOMPARE(floats.first().at(1).toBool(), true);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        const SnapState* there = static_cast<SnapState*>(m_engine->stateForScreen(kScreen));
        QVERIFY(there->isFloating(kWindow));
        QVERIFY(there->zonesForWindow(kWindow).isEmpty());
    }

    // A snapped window moved onto a hidden desktop where it floats keeps the
    // float with no apply now, since its client is suspended there and drops
    // a grow. The first time that desktop shows, its float-back is re-stated,
    // once (F780).
    void aKeptFloatOnAHiddenDesktopGetsItsFloatBackWhenShown()
    {
        QSignalSpy restated(m_engine, &SnapEngine::restatementGeometryRequested);
        moveSnappedOntoAHiddenFloat();
        QVERIFY2(m_batch.isEmpty(), "no apply reaches the suspended client");
        QCOMPARE(restated.count(), 0);

        showDesktop(2);
        QCOMPARE(restated.count(), 1);
        const QList<QVariant> args = restated.first();
        QCOMPARE(args.at(0).toString(), kWindow);
        QCOMPARE(QRect(args.at(1).toInt(), args.at(2).toInt(), args.at(3).toInt(), args.at(4).toInt()), kFree);
        QCOMPARE(args.at(6).toString(), kScreen);

        showDesktop(1);
        showDesktop(2);
        QCOMPARE(restated.count(), 1);
    }

    // A window moved onto a hidden desktop with no slot for it floats at its
    // pre-snap rect there, and that apply reaches a suspended client, which
    // drops a grow. Its float-back is re-stated when the desktop first
    // shows, like a kept float's (F1019).
    void aNoSlotFloatOnAHiddenDesktopGetsItsFloatBackWhenShown()
    {
        assignLayout(2, 2);
        recordFreeGeometry(kFree);
        snapOn(1, m_zoneIds[2]);
        m_nav.m_frames.insert(kWindow, m_service->resolveZoneGeometry({m_zoneIds[2]}, kScreen));
        m_engine->setNavigationStateProvider(&m_nav);
        QSignalSpy restated(m_engine, &SnapEngine::restatementGeometryRequested);

        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));
        QCOMPARE(m_batch.size(), 1);
        QCOMPARE(m_batch.first().targetZoneId, QString(PhosphorEngine::RestoreSentinel));
        QCOMPARE(restated.count(), 0);

        showDesktop(2);
        QCOMPARE(restated.count(), 1);
        const QList<QVariant> args = restated.first();
        QCOMPARE(QRect(args.at(1).toInt(), args.at(2).toInt(), args.at(3).toInt(), args.at(4).toInt()), kFree);
    }

    // A carry into the desktop in view is a re-statement even into a slot
    // the window did not hold, not the user snapping it: an auto-snapped
    // window keeps its mark (F1018). The mark sits on desktop 3, which the
    // move keeps, since the release of the desktop left drops that store's
    // copy either way. Read through the mark, not the last-used zone: this
    // fixture wires no desktop manager, so currentVirtualDesktopForScreen
    // answers 0 and a commit pinned to desktop 2 never passes the last-used
    // gate whatever its intent. Mutation: commit `intent` in place of
    // AutoReplaced for a tagged entry and the commit clears the mark.
    void aCarryIntoViewKeepsTheAutoSnappedMark()
    {
        snapOn(3, m_zoneIds[0]);
        snapOn(1, m_zoneIds[1]);
        m_engine->setCurrentDesktopForScreen(kScreen, 3);
        m_service->markAsAutoSnapped(kWindow);
        QVERIFY(m_service->isAutoSnapped(kWindow));
        m_engine->setCurrentDesktopForScreen(kScreen, 2);

        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2, 3})));

        QCOMPARE(m_batch.size(), 1);
        QCOMPARE(m_batch.first().virtualDesktop, 2);
        QVERIFY(m_batch.first().restatement);
        QCOMPARE(zonesOn(2), QStringList{m_zoneIds[1]});
        QCOMPARE(zonesOn(3), QStringList{m_zoneIds[0]});
        QVERIFY2(m_service->isAutoSnapped(kWindow), "a carry is not the user's snap");
    }

    // A frame that moved while the desktop was hidden is the user's: the
    // owed float-back is dropped, not applied over it (F780).
    void aMovedFrameKeepsItsPlaceWhenShown()
    {
        QSignalSpy restated(m_engine, &SnapEngine::restatementGeometryRequested);
        moveSnappedOntoAHiddenFloat();
        m_nav.m_frames.insert(kWindow, QRect(40, 40, 300, 200));

        showDesktop(2);
        QCOMPARE(restated.count(), 0);
    }

    // A floating window arrives floating where it goes, tracked there with
    // its residence and no zone to unfloat into (float_home_cross_desktop,
    // F608).
    void aFloatingWindowArrivesFloating()
    {
        assignLayout(2, 3);
        snapOn(1, m_zoneIds[1]);
        m_engine->setWindowFloat(kWindow, true, kScreen);

        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));

        QVERIFY(m_batch.isEmpty());
        QVERIFY(m_engine->isWindowTracked(kWindow));
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        const SnapState* there = static_cast<SnapState*>(m_engine->stateForScreen(kScreen));
        QVERIFY(there->isFloating(kWindow));
        QCOMPARE(there->desktopForWindow(kWindow), 2);
        QVERIFY(there->preFloatZones(kWindow).isEmpty());
        const auto rec = m_service->placementStore().peekExact(kWindow);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->slotFor(WindowPlacement::snapEngineId()).state, WindowPlacement::stateFloating());
        QCOMPARE(rec->virtualDesktop, 2);
    }

    // A minimized snapped window moved to a desktop running another layout
    // carries its slot as the pre-float zone there, so the unminimize puts it
    // into the matching zone of that layout (F953, l18_minimized_layout_switch).
    void aMinimizedSnappedWindowCarriesItsSlot()
    {
        const QStringList destination = assignLayout(2, 3);
        snapOn(1, m_zoneIds[1]);
        m_engine->setWindowFloat(kWindow, true, kScreen);
        m_service->markSuspensionFloat(kWindow);

        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));

        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        const SnapState* there = static_cast<SnapState*>(m_engine->stateForScreen(kScreen));
        QVERIFY(there->isFloating(kWindow));
        QCOMPARE(there->preFloatZones(kWindow), QStringList{destination[1]});
        m_engine->setWindowFloat(kWindow, false, kScreen);
        QCOMPARE(zonesOn(2), QStringList{destination[1]});
    }

    // A window on desktops 1 and 3, moved off 1 onto 2 and 3, where 2 has no
    // slot in its position: desktop 3 keeps its zone, desktop 2 takes nothing
    // and nothing moves the window (F477).
    void aNoSlotDestinationLeavesTheOtherDesktopsZone()
    {
        holdOnOneAndThree();
        m_engine->setCurrentDesktopForScreen(kScreen, 3);

        const auto result = m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2, 3})));

        QVERIFY(result.released.isEmpty());
        QVERIFY2(m_batch.isEmpty(), "the window shows desktop 3's zone already");
        QCOMPARE(zonesOn(3), QStringList{m_zoneIds[0]});
        QVERIFY(zonesOn(2).isEmpty());
        QVERIFY(persistedZonesByDesktop().contains(3));
        QVERIFY(!persistedZonesByDesktop().contains(1));
        const auto rec = m_service->placementStore().peekExact(kWindow);
        QVERIFY2(rec && rec->freeGeometryByScreen.value(kScreen).isValid(), "the float-back is not consumed");
    }

    // The same move with desktop 2 in view: the window floats at its pre-snap
    // rect there, and desktop 3's zone is left alone (F779).
    void aNoSlotDesktopInViewFloatsTheWindowThere()
    {
        holdOnOneAndThree();
        m_engine->setCurrentDesktopForScreen(kScreen, 2);

        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2, 3})));

        QCOMPARE(m_batch.size(), 1);
        QCOMPARE(m_batch.first().targetZoneId, QString(PhosphorEngine::RestoreSentinel));
        QCOMPARE(m_batch.first().virtualDesktop, 2);
        QCOMPARE(m_batch.first().targetGeometry, kFree);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        QVERIFY(static_cast<SnapState*>(m_engine->stateForScreen(kScreen))->isFloating(kWindow));
        QCOMPARE(zonesOn(3), QStringList{m_zoneIds[0]});
    }

    // A window on one desktop moved to one with no slot floats at its
    // pre-snap rect there and stays tracked, so its record says floating on
    // the desktop it moved to (F535).
    void aSingleDesktopNoSlotMoveStaysTracked()
    {
        assignLayout(2, 2);
        recordFreeGeometry(kFree);
        snapOn(1, m_zoneIds[2]);

        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));

        QCOMPARE(m_batch.size(), 1);
        QCOMPARE(m_batch.first().virtualDesktop, 2);
        QVERIFY(m_engine->isWindowTracked(kWindow));
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        QVERIFY(static_cast<SnapState*>(m_engine->stateForScreen(kScreen))->isFloating(kWindow));
        const auto rec = m_service->placementStore().peekExact(kWindow);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->slotFor(WindowPlacement::snapEngineId()).state, WindowPlacement::stateFloating());
        QCOMPARE(rec->virtualDesktop, 2);
    }

    // A span keeps its zones on a desktop running the same layout
    // (span_carry_kwin_desktop_move, F959)...
    void aSpanKeepsItsZonesOnASharedLayout()
    {
        const QStringList span{m_zoneIds[0], m_zoneIds[1]};
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_service->assignWindowToZones(kWindow, span, kScreen, 1);

        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));

        QCOMPARE(m_batch.size(), 1);
        QCOMPARE(m_batch.first().targetZoneId, m_zoneIds[0]);
        QCOMPARE(m_batch.first().targetZoneIds, span);
        QCOMPARE(m_batch.first().targetGeometry, m_service->resolveZoneGeometry(span, kScreen));
        QCOMPARE(zonesOn(2), span);
    }

    // ...and takes its first zone's slot on another.
    void aSpanTakesItsFirstSlotOnAnotherLayout()
    {
        const QStringList destination = assignLayout(2, 3);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_service->assignWindowToZones(kWindow, {m_zoneIds[0], m_zoneIds[1]}, kScreen, 1);

        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));

        QCOMPARE(m_batch.size(), 1);
        QCOMPARE(m_batch.first().targetZoneId, destination[0]);
        QVERIFY(m_batch.first().targetZoneIds.isEmpty());
        QCOMPARE(zonesOn(2), QStringList{destination[0]});
    }

    // A window carried onto the desktop in view arrives there and takes focus
    // when "Focus new windows" is on; off, or onto a desktop not in view, it
    // does not (focus_new_windows_rule, F564).
    void aCarryIntoTheDesktopInViewFocuses_data()
    {
        QTest::addColumn<bool>("focusNewWindows");
        QTest::addColumn<int>("inView");
        QTest::addColumn<int>("expected");
        QTest::newRow("in view") << true << 2 << 1;
        QTest::newRow("setting off") << false << 2 << 0;
        QTest::newRow("not in view") << true << 3 << 0;
    }

    void aCarryIntoTheDesktopInViewFocuses()
    {
        QFETCH(bool, focusNewWindows);
        QFETCH(int, inView);
        QFETCH(int, expected);
        m_settings->setSnappingFocusNewWindows(focusNewWindows);
        snapOn(1, m_zoneIds[1]);
        m_engine->setCurrentDesktopForScreen(kScreen, inView);
        QSignalSpy activations(m_engine, &SnapEngine::activateWindowRequested);

        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));

        QCOMPARE(m_batch.size(), 1);
        QCOMPARE(activations.count(), expected);
    }

    // A window moved onto two desktops is given its slot on each, with one
    // apply: the other desktop's zone is written into its store without one.
    void everyDestinationGetsItsSlot()
    {
        snapOn(1, m_zoneIds[1]);

        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2, 3})));

        QCOMPARE(m_batch.size(), 1);
        QCOMPARE(m_batch.first().virtualDesktop, 2);
        QCOMPARE(zonesOn(2), QStringList{m_zoneIds[1]});
        QCOMPARE(zonesOn(3), QStringList{m_zoneIds[1]});
    }

    // A window floating on the desktop it leaves and snapped on the one it
    // keeps is re-stated in that zone, and told it no longer floats: the
    // release ended the float before any commit could see it (F776).
    void aFloatSourceKeptZoneSaysNotFloating()
    {
        snapOn(2, m_zoneIds[1]);
        snapOn(1, m_zoneIds[0]);
        m_engine->setWindowFloat(kWindow, true, kScreen);
        m_batch.clear();
        QSignalSpy floats(m_engine, &SnapEngine::windowFloatingChanged);

        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));

        QCOMPARE(m_batch.size(), 1);
        QCOMPARE(m_batch.first().targetZoneId, m_zoneIds[1]);
        QCOMPARE(m_batch.first().virtualDesktop, 2);
        QCOMPARE(floats.count(), 1);
        QCOMPARE(floats.first().at(1).toBool(), false);
    }

    // A snapped window moved onto the same desktop of the activity in view is
    // carried like a desktop move into the layout that activity runs (F550).
    void anActivityMoveIntoViewCarriesTheSnap()
    {
        const QStringList other = activityLayout();
        showActivity(kActY);
        snapOn(1, m_zoneIds[1]);
        showActivity(kActX);

        const auto result = m_engine->reconcileWindowMemberships(kWindow, spanOf(on({1}, kActX)));

        QVERIFY(result.released.isEmpty());
        QCOMPARE(m_batch.size(), 1);
        QCOMPARE(m_batch.first().virtualDesktop, 1);
        QCOMPARE(m_batch.first().targetZoneId, other[1]);
        QCOMPARE(zonesOn(1), QStringList{other[1]});
    }

    // ...and keeps the zone it already holds in that activity.
    void anActivityMoveIntoViewKeepsItsZoneThere()
    {
        const QStringList other = activityLayout();
        showActivity(kActY);
        snapOn(1, m_zoneIds[1]);
        showActivity(kActX);
        snapOn(1, other[0]);
        m_batch.clear();

        const auto result = m_engine->reconcileWindowMemberships(kWindow, spanOf(on({1}, kActX)));

        QVERIFY(result.released.isEmpty());
        QVERIFY(m_batch.isEmpty());
        QCOMPARE(zonesOn(1), QStringList{other[0]});
    }

    // A move to an activity not in view places nothing now: the snap is
    // released here and waits for that activity.
    void anActivityMoveToAHiddenActivityPlacesNothingNow()
    {
        activityLayout();
        showActivity(kActY);
        snapOn(1, m_zoneIds[1]);

        const auto result = m_engine->reconcileWindowMemberships(kWindow, spanOf(on({1}, kActX)));

        QCOMPARE(result.released.size(), 1);
        QVERIFY(m_batch.isEmpty());
        QVERIFY(!m_engine->isWindowTracked(kWindow));
    }

    // It lands in the matching zone of that activity's layout the first time
    // its desktop shows there (ledger-pass26 DJ).
    void aMoveToAHiddenActivityLandsOnArrival()
    {
        const QStringList other = activityLayout();
        showActivity(kActY);
        snapOn(1, m_zoneIds[1]);
        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({1}, kActX)));
        QVERIFY(m_batch.isEmpty());

        showActivity(kActX);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(on({1}, kActX)));

        QCOMPARE(m_batch.size(), 1);
        QCOMPARE(m_batch.first().targetZoneId, other[1]);
        QCOMPARE(m_batch.first().virtualDesktop, 1);
        QVERIFY(m_batch.first().restatement);
        QCOMPARE(zonesOn(1), QStringList{other[1]});
    }

    // Moved back before it arrived, it returns to the zone it had.
    void aMoveBackBeforeArrivalRestoresTheZone()
    {
        activityLayout();
        showActivity(kActY);
        snapOn(1, m_zoneIds[1]);
        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({1}, kActX)));

        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({1}, kActY)));

        QCOMPARE(m_batch.size(), 1);
        QCOMPARE(m_batch.first().targetZoneId, m_zoneIds[1]);
        QVERIFY(m_engine->isWindowTracked(kWindow));
        QCOMPARE(zonesOn(1), QStringList{m_zoneIds[1]});
    }

    // A close drops the waiting carry, and so does a later snap of the window.
    void aPendingCarryDropsOnCloseAndOnASnap()
    {
        activityLayout();
        showActivity(kActY);
        snapOn(1, m_zoneIds[1]);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_service->assignWindowToZone(kSecond, m_zoneIds[2], kScreen, 1);
        const auto movedAway = [](const QString& windowId) {
            return on({1}, windowId == kWindow || windowId == kSecond ? kActX : kActY);
        };
        m_engine->reconcileWindowMemberships(kWindow, movedAway);
        m_engine->reconcileWindowMemberships(kSecond, movedAway);
        QVERIFY(m_batch.isEmpty());

        m_engine->forgetWindow(kWindow);
        m_engine->commitSnap(kSecond, m_zoneIds[0], kScreen);
        showActivity(kActX);
        m_engine->reconcileDesktopMemberships(kScreen, [](const QString& windowId) {
            return on({1}, windowId == kWindow ? kActX : kActY);
        });

        QVERIFY2(m_batch.isEmpty(), "neither carry is waiting any more");
    }

    // A waiting carry lands only when its own desktop shows in that activity.
    void aPendingCarryWaitsForItsDesktop()
    {
        const QStringList other = activityLayout();
        showActivity(kActY);
        snapOn(1, m_zoneIds[1]);
        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({1}, kActX)));

        showActivity(kActX);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(on({1}, kActX)));
        QVERIFY2(m_batch.isEmpty(), "desktop 1 is not in view");

        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(on({1}, kActX)));
        QCOMPARE(m_batch.size(), 1);
        QCOMPARE(m_batch.first().targetZoneId, other[1]);
    }

    // A waiting carry from a desktop removed meanwhile (the prune of a shrunk
    // desktop count, which runs no renumber) lands nowhere: a desktop added
    // later under that number gets nothing (F1020).
    void aPendingCarryGoesWithItsDesktop()
    {
        activityLayout();
        showActivity(kActY);
        snapOn(2, m_zoneIds[1]);
        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2}, kActX)));
        QVERIFY(m_batch.isEmpty());

        m_engine->pruneStatesForDesktop(2);
        showActivity(kActX);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(on({2}, kActX)));

        QVERIFY2(m_batch.isEmpty(), "the carry went with its desktop");
    }

private:
    /// A layout of @p zoneCount zones assigned to @p desktop of kScreen, its
    /// zone ids by position.
    QStringList assignLayout(int desktop, int zoneCount)
    {
        PhosphorZones::Layout* layout = createTestLayout(zoneCount, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->assignLayout(kScreen, desktop, QString(), layout);
        return sortedZoneIds(layout);
    }

    /// A three-zone layout assigned to desktop 1 of act-x, its zone ids by
    /// position; act-y runs the default one.
    QStringList activityLayout()
    {
        PhosphorZones::Layout* layout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->assignLayout(kScreen, 1, kActX, layout);
        return sortedZoneIds(layout);
    }

    void showActivity(const QString& activity)
    {
        m_layoutManager->setCurrentActivity(activity);
        m_engine->setCurrentActivity(activity);
    }

    static QStringList sortedZoneIds(PhosphorZones::Layout* layout)
    {
        QVector<PhosphorZones::Zone*> zones = layout->zones();
        PhosphorZones::LayoutUtils::sortZonesByNumber(zones);
        QStringList ids;
        for (PhosphorZones::Zone* zone : std::as_const(zones)) {
            ids.append(zone->id().toString());
        }
        return ids;
    }

    /// kWindow in position 3 on desktop 1 and position 1 on desktop 3, a
    /// two-zone layout on desktop 2, its float-back and record on file.
    void holdOnOneAndThree()
    {
        assignLayout(2, 2);
        recordFreeGeometry(kFree);
        snapOn(1, m_zoneIds[2]);
        snapOn(3, m_zoneIds[0]);
        if (const auto rec = m_engine->capturePlacement(kWindow)) {
            m_service->placementStore().record(*rec);
        }
        QVERIFY(persistedZonesByDesktop().contains(1));
    }

    /// kWindow snapped in zone 1 of desktop 1 and floating on desktop 2, its
    /// frame on the zone, moved off 1 onto 2 while 1 is in view.
    void moveSnappedOntoAHiddenFloat()
    {
        assignLayout(2, 3);
        snapOn(1, m_zoneIds[0]);
        m_engine->stateForWindowOnScreen(kWindow, kScreen, 2)->setFloatingOnScreen(kWindow, kScreen, 2);
        recordFreeGeometry(kFree);
        m_nav.m_frames.insert(kWindow, m_service->resolveZoneGeometry({m_zoneIds[0]}, kScreen));
        m_engine->setNavigationStateProvider(&m_nav);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));
    }

    /// Switch kScreen to @p desktop and run the pass a desktop switch runs.
    void showDesktop(int desktop)
    {
        m_engine->setCurrentDesktopForScreen(kScreen, desktop);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(on({2})));
    }

    void snapOn(int desktop, const QString& zoneId)
    {
        m_engine->setCurrentDesktopForScreen(kScreen, desktop);
        m_service->assignWindowToZone(kWindow, zoneId, kScreen, desktop);
    }

    QStringList zonesOn(int desktop)
    {
        m_engine->setCurrentDesktopForScreen(kScreen, desktop);
        const SnapState* state = static_cast<SnapState*>(m_engine->stateForScreen(kScreen));
        return state ? state->zonesForWindow(kWindow) : QStringList{};
    }

    QHash<int, QStringList> persistedZonesByDesktop() const
    {
        const auto rec = m_service->placementStore().peekExact(kWindow);
        return rec ? rec->slotFor(WindowPlacement::snapEngineId()).zonesByDesktop : QHash<int, QStringList>{};
    }

    void recordFreeGeometry(const QRect& rect)
    {
        WindowPlacement rec;
        rec.windowId = kWindow;
        rec.appId = QStringLiteral("app");
        rec.screenId = kScreen;
        rec.virtualDesktop = 1;
        rec.freeGeometryByScreen.insert(kScreen, rect);
        m_service->placementStore().record(rec);
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    SnapEngine* m_engine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
    QVector<PhosphorEngine::ZoneAssignmentEntry> m_batch;
    FrameNavState m_nav;
};

QTEST_MAIN(TestSnapDesktopCarry)
#include "test_snap_desktop_carry.moc"
