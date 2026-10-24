// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_snap_desktop_membership.cpp
 *
 * The snap engine's per-desktop membership arm. Adoption grants a window a
 * store of its own on the desktop in view, and what that buys is that
 * snapping it there lands in ITS OWN assignment instead of overwriting the
 * zone it holds on another desktop. The rest of the suite pins the
 * bookkeeping around that (the persisted per-desktop zone map, the close and
 * release paths, the restore that seeds other desktops), the carry of a snap
 * into another desktop's layout, the reapply on a switch, and the renumber
 * after a desktop removal.
 *
 * The reapply covers the return from a tiling or scrolling desktop or
 * activity to a window shown in both, its zone put back and its float
 * re-stated, and leaves alone a window whose frame still stands on its zone
 * and one shown in no other context. An activity switch puts back the zone of
 * the activity entered. Beside it: a layout-switch resnap keeps a span only
 * in the layout it was made in, a declined managed restore forgets every
 * desktop's remembered zone, a split re-files an adopted membership with the
 * window's others, and a resnap or a rotation reads only the stores the
 * window is a member of, from the desktop in view.
 */

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

#include <PhosphorEngine/GeometryUtils.h>
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorSnapEngine/INavigationStateProvider.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorZones/Zone.h>

#include <QDebug>
#include <QGuiApplication>
#include <QHash>
#include <QRect>
#include <QScopeGuard>
#include <QSet>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QUuid>
#include <memory>
#include <utility>

using namespace PlasmaZones;
using PhosphorEngine::WindowPlacement;
using PhosphorSnapEngine::SnapEngine;
using PhosphorSnapEngine::SnapState;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {
const QString kScreen = QStringLiteral("DP-1");
const QString kScreen2 = QStringLiteral("DP-2");
const QString kWindow = QStringLiteral("app|11111111-2222-3333-4444-555555555555");
const QString kOther = QStringLiteral("app|66666666-7777-8888-9999-000000000000");
/// What a missing held key reads as: desktop 0, which no membership has.
const PhosphorEngine::PlacementStateKey kNoKey{QString(), 0, QString()};

PhosphorEngine::DesktopSpan sticky()
{
    PhosphorEngine::DesktopSpan span;
    span.known = true;
    span.sticky = true;
    return span;
}

PhosphorEngine::DesktopSpan on(QSet<int> desktops)
{
    PhosphorEngine::DesktopSpan span;
    span.known = true;
    span.desktops = std::move(desktops);
    return span;
}

PhosphorEngine::DesktopSpanQuery spanOf(const PhosphorEngine::DesktopSpan& windowSpan)
{
    return [windowSpan](const QString& windowId) {
        return windowId == kWindow ? windowSpan : on({1});
    };
}
} // namespace

/// The compositor's frames as the effect reports them, and which of them a
/// window settled at under a zone or a tile (the adaptor's settled managed
/// frame). Outside the anonymous namespace: the test class holds one as a
/// member, and a member of an internal-linkage type is -Wsubobject-linkage.
class SettledFrameNav : public PhosphorSnapEngine::INavigationStateProvider
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
    bool isSettledManagedFrame(const QString& windowId, const QRect& frame) const override
    {
        return m_settled.contains(windowId) && m_frames.value(windowId) == frame;
    }

    QHash<QString, QRect> m_frames;
    QSet<QString> m_settled;
};

class TestSnapDesktopMembership : public QObject
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
        installFullResolver();

        m_testLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_testLayout);
        m_layoutManager->setActiveLayout(m_testLayout);
        m_zoneIds.clear();
        for (PhosphorZones::Zone* z : m_testLayout->zones()) {
            m_zoneIds.append(z->id().toString());
        }
    }

    void cleanup()
    {
        m_engine->setNavigationStateProvider(nullptr);
        m_nav.m_frames.clear();
        m_nav.m_settled.clear();
        m_engine->setLiveModeResolver({});
        m_engine->setContextModeResolver({});
        m_engine->setManagedRestorePredicate({});
        m_engine->setWindowRegistry(nullptr);
        m_registry.reset();
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
        m_testLayout = nullptr;
        m_zoneIds.clear();
        m_guard.reset();
        // Checked after the teardown, so a failure cannot skip it.
        const QStringList batchErrors = std::exchange(m_batchErrors, {});
        QVERIFY2(batchErrors.isEmpty(), qPrintable(batchErrors.join(QStringLiteral("; "))));
    }

    // The defect the whole change exists for: a sticky window snapped on
    // desktop 1 and then on desktop 2 shared ONE zone across both. Adoption
    // gives desktop 2 its own store, and the second snap lands there.
    void adoptedDesktopGetsItsOwnZoneAssignment()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        const auto result = m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        QCOMPARE(result.adopted.size(), 1);
        QCOMPARE(result.adopted.first().first, kWindow);
        // Membership only: nothing is snapped on desktop 2 until the user
        // snaps it there.
        QVERIFY(!m_engine->stateForWindow(kWindow)->isWindowSnapped(kWindow));
        // The held key still answers, from the store that genuinely holds
        // the window, so isWindowTracked and Meta+F's managed test never
        // read an adopted-but-unsnapped window as untracked.
        QVERIFY(m_engine->heldKeyForWindow(kWindow).has_value());
        QCOMPARE(m_engine->heldKeyForWindow(kWindow)->desktop, 1);
        QVERIFY2(m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky())).isEmpty(),
                 "a second pass over the adopted desktop adopts nothing");

        snapOn(2, kWindow, m_zoneIds[1]);
        QCOMPARE(zonesOn(2, kWindow), QStringList{m_zoneIds[1]});
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
        // The primary membership follows the desktop in view.
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        QCOMPARE(m_engine->heldKeyForWindow(kWindow).value_or(kNoKey).desktop, 2);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QCOMPARE(m_engine->heldKeyForWindow(kWindow).value_or(kNoKey).desktop, 1);
        // And a pass on the desktop it was already snapped on adopts nothing.
        QVERIFY(m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky())).isEmpty());
    }

    // The capture that feeds the persisted record names every desktop's zone,
    // and does so whether the window is snapped or floating on the desktop
    // in view.
    void captureNamesEveryDesktopsZone()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);

        auto rec = m_engine->capturePlacement(kWindow);
        QVERIFY(rec.has_value());
        PhosphorEngine::EngineSlot slot = rec->slotFor(WindowPlacement::snapEngineId());
        QCOMPARE(slot.state, WindowPlacement::stateSnapped());
        QCOMPARE(slot.zonesByDesktop.value(1), QStringList{m_zoneIds[0]});
        QCOMPARE(slot.zonesByDesktop.value(2), QStringList{m_zoneIds[1]});

        // Floated on desktop 2, still snapped on desktop 1: the record must
        // keep saying so, or a restart brings the window back floating on
        // every desktop.
        m_engine->setWindowFloat(kWindow, true, kScreen);
        rec = m_engine->capturePlacement(kWindow);
        QVERIFY(rec.has_value());
        slot = rec->slotFor(WindowPlacement::snapEngineId());
        QCOMPARE(slot.state, WindowPlacement::stateFloating());
        QCOMPARE(slot.zonesByDesktop.value(1), QStringList{m_zoneIds[0]});
    }

    // Adopted into the desktop in view but not snapped there, the window is
    // still this engine's (a background store holds it), and its capture
    // says so: floating here, with the other desktops' zones kept, on the
    // screen the member store names.
    void adoptedButUnsnappedDesktopIsTrackedAndCapturesItsOtherZones()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        QVERIFY(m_engine->isWindowTracked(kWindow));

        const auto rec = m_engine->capturePlacement(kWindow);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->screenId, kScreen);
        const PhosphorEngine::EngineSlot slot = rec->slotFor(WindowPlacement::snapEngineId());
        QCOMPARE(slot.state, WindowPlacement::stateFloating());
        QCOMPARE(slot.zonesByDesktop.value(1), QStringList{m_zoneIds[0]});
        QVERIFY(!slot.zonesByDesktop.contains(2));
    }

    // A span that stops covering a desktop releases that desktop's zone AND
    // forgets its persisted entry through the tracker, because the store
    // merges the map and a capture's silence would leave the zone on disk.
    void releaseClearsTheZoneAndForgetsThePersistedEntry()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);
        QVERIFY(recordCapture(kWindow));
        QVERIFY(persistedZonesByDesktop(kWindow).contains(1));

        // Un-stuck onto desktop 2 while desktop 2 is in view, where it keeps its zone.
        QVector<PhosphorEngine::ZoneAssignmentEntry> batch;
        wireResnapCommit(&batch);
        const auto result = m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));
        QVERIFY2(result.released.isEmpty(), "a kept zone in view is not reported away");
        QVERIFY2(batch.isEmpty(), "the window already shows the zone it keeps");
        QVERIFY(zonesOn(1, kWindow).isEmpty());
        QCOMPARE(zonesOn(2, kWindow), QStringList{m_zoneIds[1]});
        QVERIFY2(!persistedZonesByDesktop(kWindow).contains(1), "the forget must reach the record");
        QVERIFY(persistedZonesByDesktop(kWindow).contains(2));
    }

    // Unsnapping on one desktop forgets that desktop's persisted entry too:
    // "unsnapped here" is not representable in a capture (it names only
    // desktops with zones), so the forget is the only thing that keeps a
    // restart from snapping the window back into the zone it just left.
    void unsnapForgetsThatDesktopsPersistedEntry()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);
        QVERIFY(recordCapture(kWindow));
        QVERIFY(persistedZonesByDesktop(kWindow).contains(2));

        m_service->unassignWindow(kWindow); // the desktop in view is 2
        QVERIFY(zonesOn(2, kWindow).isEmpty());
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
        QVERIFY2(!persistedZonesByDesktop(kWindow).contains(2), "the unsnap must forget desktop 2's zone");
        QVERIFY(persistedZonesByDesktop(kWindow).contains(1));
    }

    // A closing window leaves EVERY member store, not the one in view: zone
    // occupancy is read across every store, so a leftover is a phantom
    // occupant of its zone on that desktop.
    void closingAMultiDesktopWindowClearsEveryStore()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        snapOn(1, kOther, m_zoneIds[2]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);

        m_engine->forgetWindow(kWindow);
        QVERIFY(zonesOn(2, kWindow).isEmpty());
        QVERIFY2(zonesOn(1, kWindow).isEmpty(), "the background store must drop the window");
        QVERIFY(!m_engine->heldKeyForWindow(kWindow).has_value());
        QVERIFY(!m_engine->isWindowTracked(kWindow));
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QVERIFY2(!m_service->windowsInZone(m_zoneIds[0]).contains(kWindow), "no phantom zone occupant");
        QCOMPARE(zonesOn(1, kOther), QStringList{m_zoneIds[2]});
    }

    // Discussion #1104, the second half: a window that is SNAPPED on the
    // desktop it is moved off keeps its slot on the desktop it moves to,
    // translated into the layout that desktop runs. Left alone it sat at the
    // rect of a layout the destination has not got — the "ghost layout".
    void movingToAnotherDesktopCarriesTheSnapIntoThatDesktopsLayout()
    {
        PhosphorZones::Layout* destination = addLayout(3);
        m_layoutManager->assignLayout(kScreen, 2, QString(), destination);
        QVector<PhosphorEngine::ZoneAssignmentEntry> batch;
        wireResnapCommit(&batch);

        snapOn(1, kWindow, m_zoneIds[1]); // the second zone, so position 2 carries
        QSignalSpy parkSpy(m_engine, &SnapEngine::windowDesktopMoveRequested);
        const auto result = m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));

        QVERIFY2(result.released.isEmpty(), "a window that is re-snapped on its new desktop is not a release");
        QCOMPARE(batch.size(), 1);
        QCOMPARE(batch.first().virtualDesktop, 2);
        QVERIFY2(batch.first().restatement, "a carry re-states the window's own slot (F461)");
        QCOMPARE(batch.first().targetZoneId, sortedZoneIds(destination).at(1));
        QCOMPARE(zonesOn(2, kWindow), QStringList{sortedZoneIds(destination).at(1)});
        QVERIFY2(zonesOn(1, kWindow).isEmpty(), "the desktop it left keeps nothing");
        // Desktop 2 is not in view, so the apply may never be acked; the effect
        // parks a window whose zone apply lands on a hidden desktop. The carry
        // asks for no desktop move, which would pin a window to one desktop.
        QVERIFY2(parkSpy.isEmpty(), "a carry to a hidden desktop asks for no desktop move");
    }

    // ...nor for a window on several desktops, whose desktop set a move would
    // replace.
    void aMultiDesktopWindowIsCarriedButNeverParked()
    {
        PhosphorZones::Layout* destination = addLayout(3);
        m_layoutManager->assignLayout(kScreen, 2, QString(), destination);
        QVector<PhosphorEngine::ZoneAssignmentEntry> batch;
        wireResnapCommit(&batch);

        snapOn(1, kWindow, m_zoneIds[1]);
        QSignalSpy parkSpy(m_engine, &SnapEngine::windowDesktopMoveRequested);
        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2, 3})));

        QCOMPARE(batch.size(), 1);
        QCOMPARE(batch.first().virtualDesktop, 2);
        QCOMPARE(zonesOn(2, kWindow), QStringList{sortedZoneIds(destination).at(1)});
        QVERIFY2(parkSpy.isEmpty(), "a park would pin this window to one of its desktops");
    }

    // The destination IS the desktop in view (KWin's move-to-desktop shortcut
    // follows the window). Its client is awake, so the geometry apply lands on
    // its own and there is nothing to park.
    void aCarryOntoTheDesktopInViewIsNotParked()
    {
        PhosphorZones::Layout* destination = addLayout(3);
        m_layoutManager->assignLayout(kScreen, 2, QString(), destination);
        QVector<PhosphorEngine::ZoneAssignmentEntry> batch;
        wireResnapCommit(&batch);

        snapOn(1, kWindow, m_zoneIds[1]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        QSignalSpy parkSpy(m_engine, &SnapEngine::windowDesktopMoveRequested);
        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));

        QCOMPARE(batch.size(), 1);
        QCOMPARE(zonesOn(2, kWindow), QStringList{sortedZoneIds(destination).at(1)});
        QVERIFY2(parkSpy.isEmpty(), "the window is on the desktop in view — nothing to wait for");
    }

    // Both desktops run the same layout: the same zone id, so the window does
    // not move — but the destination desktop now records it as the occupant,
    // which is what makes zone navigation and empty-zone detection right there.
    void aSharedLayoutCarriesTheWindowIntoTheSameZone()
    {
        QVector<PhosphorEngine::ZoneAssignmentEntry> batch;
        wireResnapCommit(&batch);

        snapOn(1, kWindow, m_zoneIds[2]);
        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));

        QCOMPARE(batch.size(), 1);
        QCOMPARE(batch.first().targetZoneId, m_zoneIds[2]);
        QCOMPARE(zonesOn(2, kWindow), QStringList{m_zoneIds[2]});
        QVERIFY2(zonesOn(1, kWindow).isEmpty(), "the same zone id, but only on the desktop the window is on now");
    }

    // The destination layout has no slot in the window's position (zone 3 of
    // three, moved onto a two-zone layout). The window floats at its pre-snap
    // rect there, the way a layout switch answers the same question, and the
    // release IS reported because it genuinely stopped being snapped.
    void aPositionTheDestinationLayoutHasNotFallsBackToThePreSnapRect()
    {
        PhosphorZones::Layout* destination = addLayout(2);
        m_layoutManager->assignLayout(kScreen, 2, QString(), destination);
        recordFreeGeometry(QRect(120, 80, 640, 480));
        QVector<PhosphorEngine::ZoneAssignmentEntry> batch;
        wireResnapCommit(&batch);

        snapOn(1, kWindow, m_zoneIds[2]); // position 3, which the destination has not got
        const auto result = m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));

        QCOMPARE(result.released.size(), 1);
        QCOMPARE(batch.size(), 1);
        QCOMPARE(batch.first().targetZoneId, QString(PhosphorEngine::RestoreSentinel));
        QCOMPARE(batch.first().targetGeometry, QRect(120, 80, 640, 480));
        QCOMPARE(batch.first().virtualDesktop, 2);
        QVERIFY(zonesOn(2, kWindow).isEmpty());
        QVERIFY(static_cast<SnapState*>(m_engine->stateForScreen(kScreen))->isFloating(kWindow));
    }

    // A destination desktop in a tiling mode is that engine's to place: the
    // snap pass gives the window up the way it always did.
    void aTilingDestinationIsLeftToItsOwnEngine()
    {
        PhosphorZones::AssignmentEntry tiling;
        tiling.mode = PhosphorZones::AssignmentEntry::Mode::Autotile;
        tiling.tilingAlgorithm = QStringLiteral("bsp");
        m_layoutManager->setAssignmentEntryDirect(kScreen, 2, QString(), tiling);
        QVector<PhosphorEngine::ZoneAssignmentEntry> batch;
        wireResnapCommit(&batch);

        snapOn(1, kWindow, m_zoneIds[0]);
        const auto result = m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));

        QCOMPARE(result.released.size(), 1);
        QVERIFY2(batch.isEmpty(), "snapping must not place a window on a desktop it does not run");
        QVERIFY(zonesOn(2, kWindow).isEmpty());
    }

    // The desktop the window LEFT runs a tiling mode now, so the zone it
    // holds there is frozen memory for a return to snapping and the window
    // was tiled, not snapped, when it moved. Nothing to carry.
    void aSnapFrozenOnATilingDesktopIsNotCarried()
    {
        PhosphorZones::Layout* destination = addLayout(3);
        m_layoutManager->assignLayout(kScreen, 2, QString(), destination);
        QVector<PhosphorEngine::ZoneAssignmentEntry> batch;
        wireResnapCommit(&batch);

        snapOn(1, kWindow, m_zoneIds[0]);
        PhosphorZones::AssignmentEntry tiling;
        tiling.mode = PhosphorZones::AssignmentEntry::Mode::Autotile;
        tiling.tilingAlgorithm = QStringLiteral("bsp");
        m_layoutManager->setAssignmentEntryDirect(kScreen, 1, QString(), tiling);

        const auto result = m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));

        QCOMPARE(result.released.size(), 1);
        QVERIFY2(batch.isEmpty(), "a frozen snap must not be carried onto another desktop");
        QVERIFY(zonesOn(2, kWindow).isEmpty());
    }

    // The read SnapAdaptor's desktop-arrival re-apply depends on. Its answer
    // becomes GEOMETRY, and zoneForWindow is the wrong question to ask for
    // that: it reports the window's PRIMARY membership, which falls back to
    // another desktop's store while the one in view holds nothing, and
    // zoneGeometry resolves a zone id against whichever layout owns it. The
    // pair hands back a valid rect of a layout this desktop does not run,
    // which is the ghost of discussion #1104. The store in view answers only
    // for its own desktop.
    void theStoreInViewAnswersForItsOwnDesktopWherePrimaryDoesNot()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);

        // The trap: the primary membership is still desktop 1's, so this names
        // a zone of a layout desktop 2 need not have.
        QCOMPARE(m_engine->zoneForWindow(kWindow), m_zoneIds[0]);
        QVERIFY2(zonesOn(2, kWindow).isEmpty(), "the store in view holds no zone for this window");

        // Snapped there too, and each store keeps answering for its own desktop.
        snapOn(2, kWindow, m_zoneIds[1]);
        QCOMPARE(zonesOn(2, kWindow), QStringList{m_zoneIds[1]});
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
        // And a float there is per store too: desktop 1 stays a live snap.
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->setWindowFloat(kWindow, true, kScreen);
        QVERIFY(static_cast<SnapState*>(m_engine->stateForScreen(kScreen))->isFloating(kWindow));
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QVERIFY2(!static_cast<SnapState*>(m_engine->stateForScreen(kScreen))->isFloating(kWindow),
                 "desktop 1 is still a live snap");
    }

    // A FLOATED window arrives floating, at its own frame, with no zone to
    // unfloat into there (float_home_cross_desktop, F608).
    void aFloatedWindowArrivesFloating()
    {
        PhosphorZones::Layout* destination = addLayout(3);
        m_layoutManager->assignLayout(kScreen, 2, QString(), destination);
        QVector<PhosphorEngine::ZoneAssignmentEntry> batch;
        wireResnapCommit(&batch);

        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setWindowFloat(kWindow, true, kScreen);
        const auto result = m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));

        QCOMPARE(result.released.size(), 1);
        QVERIFY(batch.isEmpty());
        QVERIFY(zonesOn(2, kWindow).isEmpty());
        const SnapState* there = static_cast<SnapState*>(m_engine->stateForScreen(kScreen));
        QVERIFY(there->isFloating(kWindow) && there->preFloatZones(kWindow).isEmpty());
    }

    // The aggregate queries see a window in every member store: it occupies
    // its zone on each desktop, and the flat list names it once however many
    // stores hold it.
    void aggregateQueriesSeeEveryMemberStoreOnce()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);

        QVERIFY(m_service->windowsInZone(m_zoneIds[0]).contains(kWindow));
        QVERIFY(m_service->windowsInZone(m_zoneIds[1]).contains(kWindow));
        QCOMPARE(m_service->snappedWindows().count(kWindow), 1);
    }

    // Restore: the record names a zone per desktop; the desktop the window
    // is being placed onto takes its own zone and the others it is still on
    // are seeded back into their stores, so switching to them after a
    // restart finds the window placed. The registry knows which desktops
    // those are (the effect stamps them before the resolve), and the restore
    // desktop is not necessarily the one the screen is showing. A desktop the
    // record names but the window is no longer on is forgotten, not seeded
    // (F263, F307).
    void restoreSeedsTheOtherDesktopsZones()
    {
        installRegistryFor({2, 3});
        WindowPlacement rec = multiDesktopRecord();
        rec.engines[WindowPlacement::snapEngineId()].zonesByDesktop.insert(3, {m_zoneIds[2]});
        m_service->placementStore().record(rec);

        // The screen shows desktop 1; the window is restored onto desktop 2.
        // The context gate is asked about the desktop being restored ONTO.
        int gatedDesktop = 0;
        m_engine->setShouldRestorePredicate([&gatedDesktop](const QString&, int desktop) {
            gatedDesktop = desktop;
            return true;
        });
        const auto clearGate = qScopeGuard([this] {
            m_engine->setShouldRestorePredicate({});
        });
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        const PhosphorEngine::SnapResult result = m_engine->resolveWindowRestore(kWindow, kScreen, false);
        QVERIFY(result.shouldSnap);
        QCOMPARE(result.zoneIds, QStringList{m_zoneIds[1]});
        QCOMPARE(result.virtualDesktop, 2);
        QCOMPARE(gatedDesktop, 2);
        // Desktop 3's zone came back into desktop 3's store, under a
        // membership, so the restore desktop's commit lands in its own.
        // Desktop 1, which the window left, is neither seeded nor kept.
        QCOMPARE(zonesOn(3, kWindow), QStringList{m_zoneIds[2]});
        QVERIFY(zonesOn(1, kWindow).isEmpty());
        QVERIFY(!persistedZonesByDesktop(kWindow).contains(1));
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        QVERIFY(m_engine->heldKeyForWindow(kWindow).has_value());

        // The commit the caller makes is pinned to the restore desktop, so
        // with the screen still showing desktop 1 it lands in desktop 2's
        // store and leaves desktop 3's zone alone.
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_service->assignWindowToZone(kWindow, result.zoneIds.first(), kScreen, result.virtualDesktop);
        QCOMPARE(zonesOn(2, kWindow), QStringList{m_zoneIds[1]});
        QCOMPARE(zonesOn(3, kWindow), QStringList{m_zoneIds[2]});
    }

    // The per-desktop restore decision. A record captured with the SNAPPED
    // desktop in view (state snapped, flat zoneIds z0, map {1: z0}) restored
    // onto desktop 2, where the window was unsnapped, must float there and
    // seed desktop 1, not re-snap z0 from the flat zoneIds; and a record
    // captured FLOATING on one desktop whose map names a zone for the
    // restore desktop must snap there.
    void restoreDecidesSnappedOrFloatedPerDesktop()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        auto captured = m_engine->capturePlacement(kWindow);
        QVERIFY(captured.has_value());
        QCOMPARE(captured->slotFor(WindowPlacement::snapEngineId()).state, WindowPlacement::stateSnapped());
        m_service->placementStore().record(*captured);
        m_engine->forgetWindow(kWindow);

        // On desktops 2 and 1, with 2 first: this fixture has no desktop
        // manager, so the restore desktop is the span's first.
        installRegistryFor({2, 1});

        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        PhosphorEngine::SnapResult result = m_engine->resolveWindowRestore(kWindow, kScreen, false);
        QVERIFY2(!result.shouldSnap, "desktop 2 was unsnapped at capture time");
        QVERIFY(m_engine->isFloating(kWindow));
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
        QVERIFY(zonesOn(2, kWindow).isEmpty());
        // No pre-float zone on desktop 2: the flat zoneIds are desktop 1's,
        // and Meta+F on desktop 2 must not unfloat into them.
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        QVERIFY(static_cast<SnapState*>(m_engine->stateForScreen(kScreen))->preFloatZones(kWindow).isEmpty());

        // The other leg: floating on desktop 1 at capture, so no zone there (the
        // store merges the map, so the first leg's is forgotten), z1 on desktop 2.
        m_engine->forgetWindow(kWindow);
        m_service->forgetDesktopZones(kWindow, WindowPlacement::snapEngineId(), 1);
        WindowPlacement floated = multiDesktopRecord();
        floated.engines[WindowPlacement::snapEngineId()].state = WindowPlacement::stateFloating();
        floated.engines[WindowPlacement::snapEngineId()].zonesByDesktop.remove(1);
        m_service->placementStore().record(floated);
        result = m_engine->resolveWindowRestore(kWindow, kScreen, false);
        QVERIFY2(result.shouldSnap, "the map names a zone on the restore desktop");
        QCOMPARE(result.zoneIds, QStringList{m_zoneIds[1]});
        QCOMPARE(result.virtualDesktop, 2);
        QVERIFY2(zonesOn(1, kWindow).isEmpty(), "desktop 1 was floating at capture");
    }

    // A restore whose registry span no longer covers a desktop the record
    // names seeds nothing there and drops the persisted entry, instead of
    // placing a phantom the membership pass would only release again.
    void restoreSkipsDesktopsTheWindowIsNoLongerOn()
    {
        installRegistryFor({2, 3});
        WindowPlacement rec = multiDesktopRecord();
        rec.engines[WindowPlacement::snapEngineId()].zonesByDesktop.insert(3, {m_zoneIds[2]});
        m_service->placementStore().record(rec);

        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        const PhosphorEngine::SnapResult result = m_engine->resolveWindowRestore(kWindow, kScreen, false);
        QVERIFY(result.shouldSnap);
        QCOMPARE(result.zoneIds, QStringList{m_zoneIds[1]});
        QVERIFY2(zonesOn(1, kWindow).isEmpty(), "desktop 1 is not in the window's span");
        QCOMPARE(zonesOn(3, kWindow), QStringList{m_zoneIds[2]});
        QVERIFY(!persistedZonesByDesktop(kWindow).contains(1));
    }

    // ...and a window on ONE desktop is a span of one, not unknown: restored
    // from a {1, 2} record onto desktop 2 alone, it seeds nothing on desktop 1
    // (F263).
    void restoreOfASingleDesktopWindowSeedsNoOtherDesktop()
    {
        installRegistryFor({2});
        m_service->placementStore().record(multiDesktopRecord());

        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        const PhosphorEngine::SnapResult result = m_engine->resolveWindowRestore(kWindow, kScreen, false);
        QVERIFY(result.shouldSnap);
        QCOMPARE(result.zoneIds, QStringList{m_zoneIds[1]});
        QVERIFY2(zonesOn(1, kWindow).isEmpty(), "the window is not on desktop 1");
        QVERIFY(!persistedZonesByDesktop(kWindow).contains(1));
    }

    // The floated leg: a record floating with a zone only on desktop 1,
    // restored onto desktop 2 alone, comes back floating, and the next
    // membership pass carries nothing into a zone (a seeded phantom on
    // desktop 1 was the stale key it carried from, F263).
    void restoreOfASingleDesktopFloatIsNotCarriedIntoAZone()
    {
        installRegistryFor({2});
        WindowPlacement rec = multiDesktopRecord();
        rec.engines[WindowPlacement::snapEngineId()].state = WindowPlacement::stateFloating();
        rec.engines[WindowPlacement::snapEngineId()].zonesByDesktop.remove(2);
        m_service->placementStore().record(rec);
        QVector<PhosphorEngine::ZoneAssignmentEntry> batch;
        wireResnapCommit(&batch);

        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        const PhosphorEngine::SnapResult result = m_engine->resolveWindowRestore(kWindow, kScreen, false);
        QVERIFY(!result.shouldSnap);
        m_engine->reconcileWindowMemberships(kWindow, spanOf(on({2})));

        QVERIFY2(batch.isEmpty(), "a floating window must not be carried into a zone");
        QVERIFY(zonesOn(2, kWindow).isEmpty());
    }

    // Floating on a desktop the window was adopted into but never snapped on
    // records the float's residence (screen and desktop) in that desktop's
    // store: a bare float bit would leave the capture screenless.
    void floatOnAnAdoptedDesktopRecordsItsResidence()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        m_engine->setWindowFloat(kWindow, true, kScreen);
        QVERIFY(m_engine->isFloating(kWindow));

        SnapState* d2 = static_cast<SnapState*>(m_engine->stateForScreen(kScreen));
        QVERIFY(d2 != nullptr);
        QCOMPARE(d2->screenForWindow(kWindow), kScreen);
        QCOMPARE(d2->desktopForWindow(kWindow), 2);
        // Read with desktop 2 still in view, where the float lives.
        QCOMPARE(m_engine->screenForTrackedWindow(kWindow), kScreen);
        const auto rec = m_engine->capturePlacement(kWindow);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->screenId, kScreen);
        // The other desktop's zone is untouched.
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
    }

    // The service's float path forgets the floated desktop's persisted entry
    // (its own unassign does the same), so a restart cannot seed the zone
    // back as a live snap on the desktop the user floated it on.
    void unsnapForFloatForgetsThatDesktopsPersistedEntry()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);
        QVERIFY(recordCapture(kWindow));
        QVERIFY(persistedZonesByDesktop(kWindow).contains(2));

        m_service->unsnapForFloat(kWindow); // the desktop in view is 2
        QVERIFY(zonesOn(2, kWindow).isEmpty());
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
        QVERIFY2(!persistedZonesByDesktop(kWindow).contains(2), "the float must forget desktop 2's zone");
        QVERIFY(persistedZonesByDesktop(kWindow).contains(1));
    }

    // A screen a tiling engine owns keeps its snap memberships as frozen
    // memory, and the membership pass must not re-commit them on a desktop
    // switch there: live, that re-commit fought the tiling engine's own
    // placement on every switch.
    void switchOnATilingScreenDoesNotReapplySnapZones()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);

        m_engine->setLiveModeResolver([](const QString&) {
            return PhosphorZones::AssignmentEntry::Mode::Autotile;
        });
        QSignalSpy resnapSpy(m_engine, &SnapEngine::resnapToNewLayoutRequested);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QVERIFY(m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky())).isEmpty());
        QCOMPARE(resnapSpy.count(), 0);
        // The memberships are kept for the return to snapping.
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
        QCOMPARE(zonesOn(2, kWindow), QStringList{m_zoneIds[1]});

        m_engine->setLiveModeResolver([](const QString&) {
            return PhosphorZones::AssignmentEntry::Mode::Snapping;
        });
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        QVERIFY2(resnapSpy.count() > 0, "back in snapping mode the switch re-applies the desktop's zone");
        const auto reapplied =
            PhosphorEngine::GeometryUtils::deserializeZoneAssignments(resnapSpy.last().at(0).toString(), nullptr);
        QVERIFY2(!reapplied.isEmpty() && reapplied.first().restatement, "the re-apply is a re-statement (F461)");
        m_engine->setLiveModeResolver({});
    }

    // #1151: a window shown on desktops 1 and 2 and snapped on desktop 1
    // while desktop 2 runs a tiling mode holds ONE snap membership, since a
    // tiling desktop grants none. The tiling engine placed it on desktop 2,
    // and it is one window, so back on desktop 1 it stands at the strip or
    // tile rect. The return re-applies its zone there even though no second
    // snap store holds it, for a sticky window and for one on a span of
    // desktops alike, and on every return, not only the first. The
    // single-desktop bystander snapped beside it is not re-committed.
    void aReturnFromATilingDesktopReappliesAWindowShownOnBoth_data()
    {
        QTest::addColumn<bool>("isSticky");
        QTest::newRow("sticky") << true;
        QTest::newRow("on desktops 1 and 2") << false;
    }
    void aReturnFromATilingDesktopReappliesAWindowShownOnBoth()
    {
        QFETCH(bool, isSticky);
        const PhosphorEngine::DesktopSpanQuery span = spanOf(isSticky ? sticky() : on({1, 2}));
        snapOn(1, kWindow, m_zoneIds[0]);
        snapOn(1, kOther, m_zoneIds[2]);

        QSignalSpy resnapSpy(m_engine, &SnapEngine::resnapToNewLayoutRequested);
        for (int cycle = 1; cycle <= 2; ++cycle) {
            const auto rowContext = qScopeGuard([cycle] {
                if (QTest::currentTestFailed()) {
                    qWarning() << "on return" << cycle << "from the tiling desktop";
                }
            });
            m_engine->setLiveModeResolver([](const QString&) {
                return PhosphorZones::AssignmentEntry::Mode::Autotile;
            });
            m_engine->setCurrentDesktopForScreen(kScreen, 2);
            // Asked of the pass itself: an empty store on desktop 2 cannot tell
            // no membership from an adopted one that holds nothing.
            QVERIFY2(m_engine->reconcileDesktopMemberships(kScreen, span).adopted.isEmpty(),
                     "a tiling desktop grants no snap membership");
            QVERIFY(zonesOn(2, kWindow).isEmpty());

            m_engine->setLiveModeResolver([](const QString&) {
                return PhosphorZones::AssignmentEntry::Mode::Snapping;
            });
            resnapSpy.clear();
            m_engine->setCurrentDesktopForScreen(kScreen, 1);
            QVERIFY(m_engine->reconcileDesktopMemberships(kScreen, span).isEmpty());
            QCOMPARE(resnapSpy.count(), 1);
            const auto entries =
                PhosphorEngine::GeometryUtils::deserializeZoneAssignments(resnapSpy.first().at(0).toString(), nullptr);
            QCOMPARE(entries.size(), 1);
            QCOMPARE(entries.first().windowId, kWindow);
            QCOMPARE(entries.first().targetZoneId, m_zoneIds[0]);
            QVERIFY(entries.first().restatement);
        }
        m_engine->setLiveModeResolver({});
    }

    // #1149: a window snapped to a different zone on each of its desktops
    // goes back to the entered desktop's zone on every switch, in both
    // directions. Exactly one entry per switch: the window is moved once, to
    // the zone of the desktop now shown, and a single-desktop bystander is not
    // re-committed. The entry is a re-statement, so a user maximize survives.
    // The setup leaves desktop 1 in view, so the rows start on desktop 2 and
    // each one is a real switch.
    void aSwitchReappliesOnlyTheEnteredDesktopsZone()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);
        snapOn(1, kOther, m_zoneIds[2]);

        QSignalSpy resnapSpy(m_engine, &SnapEngine::resnapToNewLayoutRequested);
        for (const auto& [desktop, zone] : {std::pair{2, m_zoneIds[1]}, std::pair{1, m_zoneIds[0]}}) {
            const int entered = desktop;
            const auto rowContext = qScopeGuard([entered] {
                if (QTest::currentTestFailed()) {
                    qWarning() << "on the switch to desktop" << entered;
                }
            });
            resnapSpy.clear();
            m_engine->setCurrentDesktopForScreen(kScreen, desktop);
            QVERIFY(m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky())).isEmpty());
            QCOMPARE(resnapSpy.count(), 1);
            const auto entries =
                PhosphorEngine::GeometryUtils::deserializeZoneAssignments(resnapSpy.first().at(0).toString(), nullptr);
            QCOMPARE(entries.size(), 1);
            QCOMPARE(entries.first().windowId, kWindow);
            QCOMPARE(entries.first().targetZoneId, zone);
            QVERIFY(entries.first().restatement);
        }
    }

    // #1149 on the activity axis (F220): a window on one desktop shown in two
    // activities, snapped to a different zone in each, goes back to the
    // entered activity's zone on every switch, in both directions. One entry
    // per switch, a re-statement, and the bystander snapped in one activity
    // alone is not re-committed. The setup leaves activity B in view, so the
    // rows start on A and each one is a real switch.
    void anActivitySwitchReappliesOnlyTheEnteredActivitysZone()
    {
        const QString a = QStringLiteral("act-a");
        const QString b = QStringLiteral("act-b");
        const PhosphorEngine::DesktopSpanQuery span = [a, b](const QString& windowId) {
            PhosphorEngine::DesktopSpan s;
            s.known = true;
            s.desktops = {1};
            s.activity = a;
            if (windowId == kWindow) {
                s.activities = QStringList{a, b};
            }
            return s;
        };
        showActivity(a);
        snapOn(1, kWindow, m_zoneIds[0]);
        snapOn(1, kOther, m_zoneIds[2]);
        showActivity(b);
        QCOMPARE(m_engine->reconcileDesktopMemberships(kScreen, span).adopted.size(), 1);
        snapOn(1, kWindow, m_zoneIds[1]);

        QSignalSpy resnapSpy(m_engine, &SnapEngine::resnapToNewLayoutRequested);
        for (const auto& [activity, zone] : {std::pair{a, m_zoneIds[0]}, std::pair{b, m_zoneIds[1]}}) {
            const QString entered = activity;
            const auto rowContext = qScopeGuard([entered] {
                if (QTest::currentTestFailed()) {
                    qWarning() << "on the switch to activity" << entered;
                }
            });
            resnapSpy.clear();
            showActivity(activity);
            QVERIFY(m_engine->reconcileDesktopMemberships(kScreen, span).isEmpty());
            QCOMPARE(resnapSpy.count(), 1);
            const auto entries =
                PhosphorEngine::GeometryUtils::deserializeZoneAssignments(resnapSpy.first().at(0).toString(), nullptr);
            QCOMPARE(entries.size(), 1);
            QCOMPARE(entries.first().windowId, kWindow);
            QCOMPARE(entries.first().targetZoneId, zone);
            QVERIFY(entries.first().restatement);
        }
    }

    // Discussion #1124: a snap committed on ANOTHER screen, as a keyboard move
    // across outputs makes, added a membership there beside the one on the
    // screen the window left. The old screen's stayed primary, so every read
    // answered with the zone the window had left, and the membership pass took
    // the pair for a multi-desktop window and re-applied that zone on each
    // screen, throwing the window back across monitors. The window moves with
    // the commit, and neither screen's pass re-applies anything.
    void aSnapOnAnotherScreenMovesTheWindowThere()
    {
        snapOn(1, kWindow, m_zoneIds[2]);
        m_engine->setCurrentDesktopForScreen(kScreen2, 1);
        m_service->assignWindowToZone(kWindow, m_zoneIds[0], kScreen2, 1);

        QVERIFY(m_engine->heldKeyForWindow(kWindow).has_value());
        QCOMPARE(m_engine->heldKeyForWindow(kWindow)->screenId, kScreen2);
        QCOMPARE(m_service->screenForWindow(kWindow), kScreen2);
        QCOMPARE(m_service->zoneForWindow(kWindow), m_zoneIds[0]);
        // Nothing is left behind on the screen it left.
        QVERIFY(zonesOn(1, kWindow).isEmpty());

        QSignalSpy resnapSpy(m_engine, &SnapEngine::resnapToNewLayoutRequested);
        QVERIFY(m_engine->reconcileDesktopMemberships(kScreen, spanOf(on({1}))).isEmpty());
        QVERIFY(m_engine->reconcileDesktopMemberships(kScreen2, spanOf(on({1}))).isEmpty());
        QCOMPARE(resnapSpy.count(), 0);
    }

    // A commit pinned to a desktop of the other screen that is NOT in view
    // there lands in exactly that context. Re-homing onto the screen's
    // current key first and then adding the pinned one left the window a
    // member of both. The desktop it resides on is the pinned one, and the
    // zone it left on the first screen is forgotten in the record too, or a
    // restart would seed it back.
    void aPinnedSnapOnAnotherScreenLandsInThatContextOnly()
    {
        snapOn(1, kWindow, m_zoneIds[2]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QVERIFY(recordCapture(kWindow));
        QVERIFY(persistedZonesByDesktop(kWindow).contains(1));
        QVERIFY(persistedZonesByDesktop(kWindow).contains(2));

        m_engine->setCurrentDesktopForScreen(kScreen2, 1);
        m_service->assignWindowToZone(kWindow, m_zoneIds[0], kScreen2, 2);

        const auto held = m_engine->heldKeyForWindow(kWindow);
        QVERIFY(held.has_value());
        QCOMPARE(held->screenId, kScreen2);
        QCOMPARE(held->desktop, 2);
        if (SnapState* inView = static_cast<SnapState*>(m_engine->stateForScreen(kScreen2))) {
            QVERIFY2(!m_engine->holdsWindowInState(kWindow, inView), "the desktop in view must not hold the window");
        }
        m_engine->setCurrentDesktopForScreen(kScreen2, 2);
        SnapState* pinned = static_cast<SnapState*>(m_engine->stateForScreen(kScreen2));
        QVERIFY(pinned);
        QCOMPARE(pinned->zonesForWindow(kWindow), QStringList{m_zoneIds[0]});
        QCOMPARE(pinned->desktopForWindow(kWindow), 2);
        QVERIFY(zonesOn(1, kWindow).isEmpty());
        QVERIFY(zonesOn(2, kWindow).isEmpty());
        QVERIFY2(persistedZonesByDesktop(kWindow).isEmpty(), "the zones left on the first screen must be forgotten");
    }

    // An all-activities window snapped on two desktops of one screen and moved
    // by keyboard to the other screen: the per-desktop map is activity-blind,
    // so a zone of the first screen surviving in it was adopted on the second
    // screen at the next activity switch and the window bounced between zones.
    void anActivitySwitchAfterAMoveAdoptsNothingFromTheScreenLeft()
    {
        snapOn(1, kWindow, m_zoneIds[1]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QVERIFY(recordCapture(kWindow));

        m_engine->setCurrentDesktopForScreen(kScreen2, 1);
        m_service->assignWindowToZone(kWindow, m_zoneIds[0], kScreen2, 1);
        QVERIFY(recordCapture(kWindow));
        const QHash<int, QStringList> persisted = persistedZonesByDesktop(kWindow);
        QVERIFY2(!persisted.value(1).contains(m_zoneIds[1]), "the first screen's desktop 1 zone must be forgotten");
        QVERIFY2(!persisted.value(2).contains(m_zoneIds[1]), "the first screen's desktop 2 zone must be forgotten");

        // Desktop 2 of the second screen in view: where the stale zone would land.
        m_engine->setCurrentDesktopForScreen(kScreen2, 2);
        m_engine->setCurrentActivity(QStringLiteral("activity-y"));
        m_engine->reconcileDesktopMemberships(kScreen2, spanOf(sticky()));
        SnapState* there = static_cast<SnapState*>(m_engine->stateForScreen(kScreen2));
        QVERIFY(there);
        QVERIFY(!there->zonesForWindow(kWindow).contains(m_zoneIds[1]));
        m_engine->setCurrentActivity(QString());
    }

    // A re-key on the SAME screen (a refused cross-mode move recovered onto
    // its source screen) keeps the window's other desktops there, zone and
    // persisted entry alike: they are not on a screen it left.
    void aSameScreenReKeyKeepsTheOtherDesktops()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        for (int desktop : {3, 4}) {
            m_engine->setCurrentDesktopForScreen(kScreen, desktop);
            m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
            snapOn(desktop, kWindow, m_zoneIds[desktop - 2]);
        }
        QVERIFY(recordCapture(kWindow));
        QCOMPARE(persistedZonesByDesktop(kWindow).size(), 3);

        // The primary moves onto desktop 2 with its zone; the other two stay.
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        QVERIFY(m_engine->migrateWindowToScreen(kWindow, kScreen));
        QCOMPARE(zonesOn(2, kWindow).size(), 1);
        int kept = 0;
        for (int desktop : {1, 3, 4}) {
            if (!zonesOn(desktop, kWindow).isEmpty()) {
                ++kept;
            }
        }
        QCOMPARE(kept, 2);
        QCOMPARE(persistedZonesByDesktop(kWindow).size(), 3);
    }

    // The leave primitive: every membership off the kept screen goes with
    // everything it held (zone, the last-used naming it, float bit, pre-float
    // home, persisted entry), while the kept screen's stay untouched. It is
    // silent: the caller decides what to announce.
    void releaseWindowOffScreenKeepsOnlyTheKeptScreen()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        SnapState* onOne = static_cast<SnapState*>(m_engine->stateForScreen(kScreen));
        onOne->restoreLastUsedZone(m_zoneIds[0], kScreen, QString(), 1);
        onOne->addPreFloatZone(kWindow, {m_zoneIds[2]});
        onOne->addPreFloatScreen(kWindow, kScreen);
        // A floating membership on the other screen.
        m_engine->setCurrentDesktopForScreen(kScreen2, 1);
        SnapState* onOther = m_engine->stateForWindowOnScreen(kWindow, kScreen2, 1);
        onOther->setFloatingOnScreen(kWindow, kScreen2, 1);
        QVERIFY(recordCapture(kWindow));

        QSignalSpy zoneSpy(m_service, &PhosphorPlacement::WindowTrackingService::windowZoneChanged);
        m_engine->releaseWindowOffScreen(kWindow, kScreen2);

        QVERIFY(zonesOn(1, kWindow).isEmpty());
        QVERIFY(zonesOn(2, kWindow).isEmpty());
        QVERIFY(onOne->lastUsedZoneId().isEmpty());
        QVERIFY(onOne->preFloatZones(kWindow).isEmpty());
        QVERIFY(!persistedZonesByDesktop(kWindow).contains(1));
        QVERIFY(!persistedZonesByDesktop(kWindow).contains(2));
        QVERIFY(m_engine->holdsWindowInState(kWindow, onOther));
        QVERIFY(onOther->isFloating(kWindow));
        QCOMPARE(zoneSpy.count(), 0);

        // Keeping nothing releases the rest, and the window is untracked.
        m_engine->releaseWindowOffScreen(kWindow, QString());
        QVERIFY(!m_engine->isWindowTracked(kWindow));
    }

    // A handoff to a tiling engine releases the CONTEXT it took the window
    // in, not the window: a membership on another desktop still in snapping
    // mode keeps its zone, so switching there finds the window snapped and
    // switching back finds it tiled. With no such context left, the window
    // is forgotten outright, as before.
    void handoffReleaseKeepsTheOtherSnappingDesktop()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);

        // Desktop 1 flips to tiling and the tiling engine takes the window there.
        m_layoutManager->assignLayoutById(kScreen, 1, QString(), QStringLiteral("autotile:bsp"));
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_engine->handoffRelease(kWindow);
        QVERIFY(zonesOn(1, kWindow).isEmpty());
        QCOMPARE(zonesOn(2, kWindow), QStringList{m_zoneIds[1]});
        QVERIFY(m_engine->isWindowTracked(kWindow));
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        QCOMPARE(m_engine->heldKeyForWindow(kWindow).value_or(kNoKey).desktop, 2);

        // Desktop 2 flips too: nothing snapping is left and the window goes.
        m_layoutManager->assignLayoutById(kScreen, 2, QString(), QStringLiteral("autotile:bsp"));
        m_engine->handoffRelease(kWindow);
        QVERIFY(zonesOn(2, kWindow).isEmpty());
        QVERIFY(!m_engine->isWindowTracked(kWindow));
        QVERIFY(!m_engine->heldKeyForWindow(kWindow).has_value());
    }

    // Adoption puts back the zone the durable record remembers for the
    // desktop entered: a screen that went to tiling and came back released
    // every context, and the record's per-desktop map is what survives.
    void adoptionRestoresTheRememberedZoneForThatDesktop()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);
        QVERIFY(recordCapture(kWindow));
        m_engine->forgetWindow(kWindow); // the handoff dropped every context

        // Re-snapped on desktop 1 (the return resnap); desktop 2 is entered.
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        const auto result = m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        QCOMPARE(result.adopted.size(), 1);
        QCOMPARE(zonesOn(2, kWindow), QStringList{m_zoneIds[1]});
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
    }

    // The remembered zone goes back only into the layout the desktop runs
    // now (discussion #1104): desktop 2 was given another snapping layout
    // while the window was away, so the zone remembered under the old one
    // stays out. Membership is still granted; the window is simply unsnapped
    // there until the user snaps it.
    void adoptionSkipsARememberedZoneFromALayoutTheDesktopNoLongerRuns()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);
        QVERIFY(recordCapture(kWindow));
        m_engine->forgetWindow(kWindow);

        PhosphorZones::Layout* other = addLayout(2);
        m_layoutManager->assignLayout(kScreen, 2, QString(), other);

        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        const auto result = m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        QCOMPARE(result.adopted.size(), 1);
        QVERIFY2(zonesOn(2, kWindow).isEmpty(), "a zone from the layout desktop 2 no longer runs must not come back");
        QCOMPARE(zonesOn(1, kWindow), QStringList{m_zoneIds[0]});
    }

    // Removing a desktop renumbers every bare desktop number the service
    // keeps, not only the persisted map: a resnap buffered across the removal
    // must land on the desktop it was buffered for, and a row on the removed
    // desktop falls back to 0, the all-desktops sentinel.
    void removingADesktopRenumbersTheResnapBuffer()
    {
        snapOn(3, kWindow, m_zoneIds[0]);
        snapOn(2, kOther, m_zoneIds[1]);
        m_service->populateResnapBufferForAllScreens({}, {kScreen});

        m_engine->renumberDesktopsAfterRemoval(2);
        QHash<QString, int> desktopByWindow;
        for (const PhosphorEngine::ResnapEntry& entry : m_service->takeResnapBuffer()) {
            desktopByWindow.insert(entry.windowId, entry.virtualDesktop);
        }
        QCOMPARE(desktopByWindow.value(kWindow, -1), 2);
        QCOMPARE(desktopByWindow.value(kOther, -1), 0);
    }

    // The close the daemon relays goes through the service, and has to reach
    // every member store the same way the engine's own forget does.
    void closeThroughTheServiceClearsEveryStore()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);

        m_service->windowClosed(kWindow);
        QVERIFY(zonesOn(2, kWindow).isEmpty());
        QVERIFY(zonesOn(1, kWindow).isEmpty());
        QVERIFY(!m_engine->heldKeyForWindow(kWindow).has_value());
        QVERIFY(!m_engine->isWindowTracked(kWindow));
    }

    // Without a registry answer the record's own desktop is the best
    // witness, ahead of whatever the screen happens to be showing.
    void restoreWithoutRegistryFollowsTheRecordsDesktop()
    {
        m_service->placementStore().record(multiDesktopRecord());
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        const PhosphorEngine::SnapResult result = m_engine->resolveWindowRestore(kWindow, kScreen, false);
        QVERIFY(result.shouldSnap);
        QCOMPARE(result.zoneIds, QStringList{m_zoneIds[0]});
        QCOMPARE(result.virtualDesktop, 1);
        QCOMPARE(zonesOn(2, kWindow), QStringList{m_zoneIds[1]});
    }

    // #1151: a layout-switch resnap puts a window back in the layout its span
    // was made in with the span, not its primary zone. A layout edited
    // meanwhile keeps the zones of the span it still holds (F39), and a switch
    // to another layout collapses the span to the zone at its primary
    // position (F34). The buffer is populated before the edit and the switch,
    // the order the daemon runs them in.
    void aResnapKeepsTheSpanOnlyInTheLayoutItWasMadeIn()
    {
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QSignalSpy resnapSpy(m_engine, &SnapEngine::resnapToNewLayoutRequested);
        const auto resnapEntries = [this, &resnapSpy] {
            resnapSpy.clear();
            m_engine->resnapToNewLayout(SnapEngine::ResnapFeedback::Silent);
            return resnapSpy.count() == 1
                ? PhosphorEngine::GeometryUtils::deserializeZoneAssignments(resnapSpy.first().at(0).toString(), nullptr)
                : QVector<PhosphorEngine::ZoneAssignmentEntry>{};
        };

        // The same layout: the span.
        m_service->assignWindowToZones(kWindow, {m_zoneIds[0], m_zoneIds[1]}, kScreen, 1);
        m_service->populateResnapBufferForAllScreens({}, {kScreen});
        auto entries = resnapEntries();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().targetZoneId, m_zoneIds[0]);
        QCOMPARE(entries.first().targetZoneIds, (QStringList{m_zoneIds[0], m_zoneIds[1]}));

        // The layout lost the span's third zone meanwhile: the two it holds.
        m_service->assignWindowToZones(kWindow, {m_zoneIds[0], m_zoneIds[1], m_zoneIds[2]}, kScreen, 1);
        m_service->populateResnapBufferForAllScreens({}, {kScreen});
        m_testLayout->removeZone(m_testLayout->zoneById(QUuid::fromString(m_zoneIds[2])));
        entries = resnapEntries();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().targetZoneId, m_zoneIds[0]);
        QCOMPARE(entries.first().targetZoneIds, (QStringList{m_zoneIds[0], m_zoneIds[1]}));

        // Another layout: the zone at the primary's position, no span.
        m_service->assignWindowToZones(kWindow, {m_zoneIds[0], m_zoneIds[1]}, kScreen, 1);
        m_service->populateResnapBufferForAllScreens({}, {kScreen});
        PhosphorZones::Layout* other = addLayout(3);
        m_layoutManager->assignLayout(kScreen, 1, QString(), other);
        entries = resnapEntries();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().targetZoneId, sortedZoneIds(other).first());
        QVERIFY(entries.first().targetZoneIds.isEmpty());
    }

    // #1151 on the activity axis (F151): a window on ONE desktop shown in two
    // activities, snapped in A while B runs a tiling mode, holds one snap
    // membership, since a tiling context grants none. Back in A it stands
    // where the tiling engine put it, so its zone is re-applied as for a
    // window shown on two desktops. So is a window on every activity, asked
    // of the activities the session has shown it. The bystander on A alone is
    // not.
    void aReturnFromATilingActivityReappliesAWindowShownInBoth_data()
    {
        QTest::addColumn<bool>("everyActivity");
        QTest::newRow("on activities A and B") << false;
        QTest::newRow("on every activity") << true;
    }
    void aReturnFromATilingActivityReappliesAWindowShownInBoth()
    {
        QFETCH(bool, everyActivity);
        const QString a = QStringLiteral("act-a");
        const QString b = QStringLiteral("act-b");
        const PhosphorEngine::DesktopSpanQuery span = [a, b, everyActivity](const QString& windowId) {
            PhosphorEngine::DesktopSpan s;
            s.known = true;
            s.desktops = {1};
            s.activity = a;
            if (windowId == kWindow) {
                s.activity = everyActivity ? QString() : a;
                s.activities = everyActivity ? QStringList{} : QStringList{a, b};
            }
            return s;
        };
        // B runs the tiling mode, as the daemon's context resolver answers
        // for a context in view or hidden.
        m_engine->setContextModeResolver([b](const QString&, int, const QString& activity) {
            return activity == b ? PhosphorZones::AssignmentEntry::Mode::Autotile
                                 : PhosphorZones::AssignmentEntry::Mode::Snapping;
        });
        showActivity(a);
        snapOn(1, kWindow, m_zoneIds[0]);
        snapOn(1, kOther, m_zoneIds[2]);
        m_engine->setLiveModeResolver([](const QString&) {
            return PhosphorZones::AssignmentEntry::Mode::Autotile;
        });
        showActivity(b);
        QVERIFY2(m_engine->reconcileDesktopMemberships(kScreen, span).adopted.isEmpty(),
                 "a tiling context grants no snap membership");

        m_engine->setLiveModeResolver([](const QString&) {
            return PhosphorZones::AssignmentEntry::Mode::Snapping;
        });
        QSignalSpy resnapSpy(m_engine, &SnapEngine::resnapToNewLayoutRequested);
        showActivity(a);
        m_engine->reconcileDesktopMemberships(kScreen, span);
        QCOMPARE(resnapSpy.count(), 1);
        const auto entries =
            PhosphorEngine::GeometryUtils::deserializeZoneAssignments(resnapSpy.first().at(0).toString(), nullptr);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().windowId, kWindow);
        QCOMPARE(entries.first().targetZoneId, m_zoneIds[0]);
        QVERIFY(entries.first().restatement);
    }

    // ...and only then. A session that runs no activities shows a window on
    // one desktop nowhere else, and a desktop switch re-commits nothing: the
    // churn the shown-elsewhere gate keeps out (F151).
    void aSingleDesktopWindowWithoutActivitiesIsNotReapplied()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        QSignalSpy resnapSpy(m_engine, &SnapEngine::resnapToNewLayoutRequested);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(on({1})));
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(on({1})));
        QCOMPARE(resnapSpy.count(), 0);
    }

    // ...nor in a session that runs activities, for a window on every
    // activity no other activity of which runs another mode: here the
    // session has shown one activity only, the common case. A zone is not
    // re-committed though the frame stops short of it (a size-increment
    // client), and a float is not re-stated.
    void aWindowOnEveryActivityIsNotReappliedOnADesktopSwitch_data()
    {
        QTest::addColumn<bool>("floating");
        QTest::newRow("snapped, its frame short of its zone") << false;
        QTest::newRow("floating") << true;
    }
    void aWindowOnEveryActivityIsNotReappliedOnADesktopSwitch()
    {
        QFETCH(bool, floating);
        showActivity(QStringLiteral("act-a"));
        // On desktop 1 of every activity: the span names no activity.
        const PhosphorEngine::DesktopSpanQuery span = spanOf(on({1}));
        if (floating) {
            m_engine->setCurrentDesktopForScreen(kScreen, 1);
            m_engine->stateForWindowOnScreen(kWindow, kScreen, 1)->setFloatingOnScreen(kWindow, kScreen, 1);
        } else {
            snapOn(1, kWindow, m_zoneIds[0]);
        }
        const QRect zoneRect = m_service->resolveZoneGeometry({m_zoneIds[0]}, kScreen);
        QVERIFY(zoneRect.isValid());
        m_nav.m_frames.insert(kWindow, zoneRect.adjusted(0, 0, -7, -9));
        m_engine->setNavigationStateProvider(&m_nav);
        QSignalSpy resnapSpy(m_engine, &SnapEngine::resnapToNewLayoutRequested);
        QSignalSpy floats(m_engine, &SnapEngine::windowFloatingChanged);

        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, span);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_engine->reconcileDesktopMemberships(kScreen, span);

        QCOMPARE(resnapSpy.count(), 0);
        QCOMPARE(floats.count(), 0);
    }

    // #1151 without the churn (F36): back on desktop 1 from the tiling
    // desktop, a sticky window whose frame still stands on its zone was not
    // moved there, and is not re-committed. One standing at the strip rect
    // is put back.
    void aReturnReappliesOnlyAWindowMovedOffItsZone_data()
    {
        QTest::addColumn<bool>("onItsZone");
        QTest::addColumn<int>("expected");
        QTest::newRow("frame on its zone") << true << 0;
        QTest::newRow("frame at the strip rect") << false << 1;
    }
    void aReturnReappliesOnlyAWindowMovedOffItsZone()
    {
        QFETCH(bool, onItsZone);
        QFETCH(int, expected);
        snapOn(1, kWindow, m_zoneIds[0]);
        snapOn(1, kOther, m_zoneIds[2]);
        m_engine->setLiveModeResolver([](const QString&) {
            return PhosphorZones::AssignmentEntry::Mode::Autotile;
        });
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        m_engine->setLiveModeResolver([](const QString&) {
            return PhosphorZones::AssignmentEntry::Mode::Snapping;
        });
        const QRect zoneRect = m_service->resolveZoneGeometry({m_zoneIds[0]}, kScreen);
        const QRect strip(0, 0, 600, 800);
        QVERIFY(zoneRect.isValid() && zoneRect != strip);
        m_nav.m_frames.insert(kWindow, onItsZone ? zoneRect : strip);
        m_engine->setNavigationStateProvider(&m_nav);
        QSignalSpy resnapSpy(m_engine, &SnapEngine::resnapToNewLayoutRequested);

        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));

        QCOMPARE(resnapSpy.count(), expected);
        if (expected > 0) {
            const auto entries =
                PhosphorEngine::GeometryUtils::deserializeZoneAssignments(resnapSpy.first().at(0).toString(), nullptr);
            QCOMPARE(entries.size(), 1);
            QCOMPARE(entries.first().windowId, kWindow);
            QCOMPARE(entries.first().targetZoneId, m_zoneIds[0]);
        }
    }

    // A sticky window floating on desktop 1 while desktop 2 runs a tiling
    // mode holds one snap membership. Back on desktop 1 the pass states the
    // float, since the tiling desktop told the effect the window was tiled
    // (F35), and puts the window back on its float-back when its frame is
    // the one the tile left it at, which is no snap zone (F470). A frame the
    // user moved is left where it is.
    void aReturnFromATilingDesktopRestatesAFloatShownOnBoth()
    {
        const QRect floatBack(100, 100, 500, 400);
        recordFreeGeometry(floatBack);
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        m_engine->stateForWindowOnScreen(kWindow, kScreen, 1)->setFloatingOnScreen(kWindow, kScreen, 1);
        m_nav.m_frames.insert(kWindow, QRect(0, 0, 600, 800));
        m_nav.m_settled.insert(kWindow); // the strip's apply settled there
        m_engine->setNavigationStateProvider(&m_nav);
        QSignalSpy floats(m_engine, &SnapEngine::windowFloatingChanged);
        QSignalSpy backs(m_engine, &SnapEngine::restatementGeometryRequested);
        const auto visitTheTilingDesktopAndReturn = [this] {
            m_engine->setLiveModeResolver([](const QString&) {
                return PhosphorZones::AssignmentEntry::Mode::Autotile;
            });
            m_engine->setCurrentDesktopForScreen(kScreen, 2);
            m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
            m_engine->setLiveModeResolver([](const QString&) {
                return PhosphorZones::AssignmentEntry::Mode::Snapping;
            });
            m_engine->setCurrentDesktopForScreen(kScreen, 1);
            m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        };

        visitTheTilingDesktopAndReturn();
        QCOMPARE(floats.count(), 1);
        QCOMPARE(floats.first().at(1).toBool(), true);
        QCOMPARE(backs.count(), 1);
        const QList<QVariant> args = backs.first();
        QCOMPARE(QRect(args.at(1).toInt(), args.at(2).toInt(), args.at(3).toInt(), args.at(4).toInt()), floatBack);

        // The user moved it since: no settled frame, so no float-back.
        m_nav.m_settled.clear();
        visitTheTilingDesktopAndReturn();
        QCOMPARE(floats.count(), 2);
        QCOMPARE(backs.count(), 1);
    }

    // restoreWindowsToZonesOnLogin off: a reopen does not put the window back
    // in the zone it remembers on the desktop it opens on, and the first
    // visit to its other desktop does not either, through the adoption's
    // remembered-zone restore. The record forgets both (F395).
    void aDeclinedRestoreForgetsEveryRememberedDesktop()
    {
        installRegistryFor({1, 2});
        m_service->placementStore().record(multiDesktopRecord());
        m_engine->setManagedRestorePredicate([](const QString&) {
            return false;
        });
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QVERIFY(!m_engine->resolveWindowRestore(kWindow, kScreen, false).shouldSnap);
        QVERIFY2(persistedZonesByDesktop(kWindow).isEmpty(), "a declined restore forgets every desktop's zone");

        // A membership on desktop 1 (the chain's float terminal gives it one,
        // stated here so the row does not rest on that), then desktop 2.
        m_engine->stateForWindowOnScreen(kWindow, kScreen, 1)->setFloatingOnScreen(kWindow, kScreen, 1);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        QCOMPARE(m_engine->reconcileDesktopMemberships(kScreen, spanOf(on({1, 2}))).adopted.size(), 1);
        QVERIFY2(zonesOn(2, kWindow).isEmpty(), "the adoption must not restore a declined zone");
    }

    // The floated leg: a record floating on desktop 1 and snapped on desktop 2
    // seeds desktop 2's zone on the restore itself, under the same gate. A
    // re-entry (the daemon's restart sweep) finds the window where the user
    // left it, and the gate does not apply to it.
    void aDeclinedFloatedRestoreSeedsNoOtherDesktop_data()
    {
        QTest::addColumn<int>("reason");
        QTest::addColumn<bool>("seeded");
        QTest::newRow("open") << static_cast<int>(PhosphorEngine::RestoreReason::Open) << false;
        QTest::newRow("restart sweep re-entry")
            << static_cast<int>(PhosphorEngine::RestoreReason::DaemonRestartSweep) << true;
    }
    void aDeclinedFloatedRestoreSeedsNoOtherDesktop()
    {
        QFETCH(int, reason);
        QFETCH(bool, seeded);
        installRegistryFor({1, 2});
        WindowPlacement rec = multiDesktopRecord();
        rec.engines[WindowPlacement::snapEngineId()].state = WindowPlacement::stateFloating();
        rec.engines[WindowPlacement::snapEngineId()].zonesByDesktop.remove(1);
        m_service->placementStore().record(rec);
        m_engine->setManagedRestorePredicate([](const QString&) {
            return false;
        });
        m_engine->setCurrentDesktopForScreen(kScreen, 1);

        const PhosphorEngine::SnapResult result =
            m_engine->resolveWindowRestore(kWindow, kScreen, false, PhosphorEngine::WindowKind::Unknown,
                                           static_cast<PhosphorEngine::RestoreReason>(reason));

        QVERIFY(!result.shouldSnap);
        if (seeded) {
            QCOMPARE(zonesOn(2, kWindow), QStringList{m_zoneIds[1]});
            QCOMPARE(persistedZonesByDesktop(kWindow).value(2), QStringList{m_zoneIds[1]});
        } else {
            QVERIFY2(zonesOn(2, kWindow).isEmpty(), "a declined restore seeds no other desktop");
            QVERIFY(persistedZonesByDesktop(kWindow).isEmpty());
        }
    }

    // A split rewrites each store's screen value to the virtual screen in
    // place. A membership adopted on another desktop holds nothing of its own
    // to go by, and is re-filed with the window's other memberships rather
    // than left under the monitor's old id (F432).
    void aRekeyCarriesAnAdoptedMembershipThatRecordsNoScreen()
    {
        const QString split = QStringLiteral("DP-1/vs:0");
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        QCOMPARE(m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky())).adopted.size(), 1);
        // The facade's in-place rewrite of desktop 1's screen value.
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        static_cast<SnapState*>(m_engine->stateForScreen(kScreen))
            ->assignWindowToZones(kWindow, {m_zoneIds[0]}, split, 1);

        m_engine->rekeyMembershipsToRecordedScreens(kScreen);

        int held = 0;
        for (SnapState* state : m_engine->allSnapStates()) {
            if (!m_engine->holdsWindowInState(kWindow, state)) {
                continue;
            }
            ++held;
            const auto key = m_engine->keyForState(state);
            QVERIFY(key.has_value());
            QCOMPARE(key->screenId, split);
        }
        QCOMPARE(held, 2);
    }

    // A zone left in a store the window is no member of is a leftover, not a
    // second home: a resnap of the current assignments reads the member store
    // alone and sends one entry for the window (F152).
    void aResnapIgnoresALeftoverInAStoreTheWindowIsNoMemberOf()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        SnapState* leftover = static_cast<SnapState*>(m_engine->stateForScreen(kScreen));
        QVERIFY(leftover);
        leftover->assignWindowToZones(kWindow, {m_zoneIds[1]}, kScreen, 2);
        QVERIFY2(!m_engine->holdsWindowInState(kWindow, leftover), "precondition: written with no membership");
        m_engine->setCurrentDesktopForScreen(kScreen, 1);
        QSignalSpy resnapSpy(m_engine, &SnapEngine::resnapToNewLayoutRequested);

        m_engine->resnapCurrentAssignments(kScreen, {kWindow}, SnapEngine::ResnapFeedback::Silent);

        QCOMPARE(resnapSpy.count(), 1);
        const auto entries =
            PhosphorEngine::GeometryUtils::deserializeZoneAssignments(resnapSpy.first().at(0).toString(), nullptr);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().targetZoneId, m_zoneIds[0]);
    }

    // Rotation turns a window held on two desktops from the zone of the
    // desktop in view only. Each store records desktop 0 for it (sticky or
    // unknown), which passes the recorded-desktop filter in both, so only the
    // store's own key tells them apart (F484).
    void aRotationTurnsAMultiDesktopWindowFromTheDesktopInViewOnly()
    {
        snapOn(1, kWindow, m_zoneIds[0]);
        m_engine->setCurrentDesktopForScreen(kScreen, 2);
        m_engine->reconcileDesktopMemberships(kScreen, spanOf(sticky()));
        snapOn(2, kWindow, m_zoneIds[1]);
        for (const auto& [desktop, zone] : {std::pair{1, m_zoneIds[0]}, std::pair{2, m_zoneIds[1]}}) {
            m_engine->setCurrentDesktopForScreen(kScreen, desktop);
            static_cast<SnapState*>(m_engine->stateForScreen(kScreen))
                ->assignWindowToZones(kWindow, {zone}, kScreen, 0);
        }
        m_engine->setCurrentDesktopForScreen(kScreen, 1);

        QStringList sources;
        for (const PhosphorEngine::ZoneAssignmentEntry& entry : m_engine->calculateRotation(true, kScreen)) {
            if (entry.windowId == kWindow) {
                sources.append(entry.sourceZoneId);
            }
        }
        QCOMPARE(sources, QStringList{m_zoneIds[0]});
    }

private:
    void showActivity(const QString& activity)
    {
        m_layoutManager->setCurrentActivity(activity);
        m_engine->setCurrentActivity(activity);
    }

    /// A record snapped in zone 0 on desktop 1 and zone 1 on desktop 2.
    WindowPlacement multiDesktopRecord() const
    {
        WindowPlacement rec;
        rec.windowId = kWindow;
        rec.appId = QStringLiteral("app");
        rec.screenId = kScreen;
        rec.virtualDesktop = 1;
        PhosphorEngine::EngineSlot slot;
        slot.state = WindowPlacement::stateSnapped();
        slot.zoneIds = {m_zoneIds[0]};
        slot.zonesByDesktop.insert(1, {m_zoneIds[0]});
        slot.zonesByDesktop.insert(2, {m_zoneIds[1]});
        rec.engines.insert(WindowPlacement::snapEngineId(), slot);
        return rec;
    }

    /// A registry that knows kWindow on @p desktops (the first is its
    /// virtualDesktop; one entry is a single-desktop window), installed on the
    /// engine and cleared in cleanup().
    void installRegistryFor(const QList<int>& desktops)
    {
        m_registry = std::make_unique<PhosphorEngine::WindowRegistry>();
        m_registry->canonicalizeWindowId(kWindow);
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.title = QStringLiteral("t");
        meta.virtualDesktop = desktops.value(0);
        if (desktops.size() > 1) {
            meta.virtualDesktops = desktops;
        }
        m_registry->upsert(QStringLiteral("11111111-2222-3333-4444-555555555555"), meta);
        m_engine->setWindowRegistry(m_registry.get());
    }

    void installFullResolver()
    {
        m_service->setSnapStateResolver(PhosphorPlacement::snapStateResolverFor(m_engine));
    }

    /// A second layout in the registry, so a desktop can be given one of its
    /// own.
    PhosphorZones::Layout* addLayout(int zoneCount)
    {
        PhosphorZones::Layout* layout = createTestLayout(zoneCount, m_layoutManager);
        m_layoutManager->addLayout(layout);
        return layout;
    }

    /// @p layout's zone ids in the order the position mapping counts them.
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

    /// Wire the engine's resnap batch the way SnapAdaptor does, so what the
    /// pass asks for is actually committed, and keep a copy of the entries.
    /// A payload that fails to parse is reported by cleanup().
    void wireResnapCommit(QVector<PhosphorEngine::ZoneAssignmentEntry>* seen)
    {
        QObject::connect(m_engine, &SnapEngine::resnapToNewLayoutRequested, m_engine,
                         [this, seen](const QString& payload) {
                             QString error;
                             const QVector<PhosphorEngine::ZoneAssignmentEntry> entries =
                                 PhosphorEngine::GeometryUtils::deserializeZoneAssignments(payload, &error);
                             if (!error.isEmpty()) {
                                 m_batchErrors.append(error);
                             }
                             *seen += entries;
                             m_engine->applyBatchAssignments(entries);
                         });
    }

    /// The pre-snap rect the float-back reads, recorded the way a capture does.
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

    /// Switches kScreen's view to @p desktop and snaps @p windowId into @p zoneId
    /// there through the service, the way a commit does. The view stays there.
    void snapOn(int desktop, const QString& windowId, const QString& zoneId)
    {
        m_engine->setCurrentDesktopForScreen(kScreen, desktop);
        m_service->assignWindowToZone(windowId, zoneId, kScreen, desktop);
    }

    /// Records @p windowId's capture; false when the engine captured nothing.
    bool recordCapture(const QString& windowId)
    {
        const auto rec = m_engine->capturePlacement(windowId);
        if (rec) {
            m_service->placementStore().record(*rec);
        }
        return rec.has_value();
    }

    /// Switches kScreen's view to @p desktop and returns the zones its store
    /// holds for @p windowId. The view stays there.
    QStringList zonesOn(int desktop, const QString& windowId)
    {
        m_engine->setCurrentDesktopForScreen(kScreen, desktop);
        SnapState* state = static_cast<SnapState*>(m_engine->stateForScreen(kScreen));
        return state ? state->zonesForWindow(windowId) : QStringList{};
    }

    QHash<int, QStringList> persistedZonesByDesktop(const QString& windowId) const
    {
        const auto rec = m_service->placementStore().peekExact(windowId);
        return rec ? rec->slotFor(WindowPlacement::snapEngineId()).zonesByDesktop : QHash<int, QStringList>{};
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;
    SnapEngine* m_engine = nullptr;
    std::unique_ptr<PhosphorEngine::WindowRegistry> m_registry;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
    QStringList m_batchErrors;
    SettledFrameNav m_nav;
};

QTEST_MAIN(TestSnapDesktopMembership)
#include "test_snap_desktop_membership.moc"
