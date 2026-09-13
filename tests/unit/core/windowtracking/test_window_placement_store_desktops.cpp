// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_placement_store_desktops.cpp
 *
 * The per-desktop half of a snap slot: a window present on several desktops
 * carries one zone per desktop in EngineSlot::zonesByDesktop, and the store
 * has to keep that map honest across captures (which MERGE it), explicit
 * forgets, desktop removal (which renumbers it) and a save / load round trip.
 * Its own suite because test_window_placement_store.cpp sits at its size
 * baseline.
 */

#include <QTest>

#include <QJsonArray>
#include <QJsonObject>

#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowPlacementStore.h>

using PhosphorEngine::EngineSlot;
using PhosphorEngine::WindowPlacement;
using PhosphorEngine::WindowPlacementStore;

namespace {
const QString kApp = QStringLiteral("app");
const QString kWindow = QStringLiteral("app|11111111-2222-3333-4444-555555555555");
const QString kSibling = QStringLiteral("app|66666666-7777-8888-9999-000000000000");
const QString kScreen = QStringLiteral("DP-1");
const QString kZoneA = QStringLiteral("{aaaaaaaa-0000-0000-0000-000000000001}");
const QString kZoneB = QStringLiteral("{bbbbbbbb-0000-0000-0000-000000000002}");
const QString kZoneC = QStringLiteral("{cccccccc-0000-0000-0000-000000000003}");

WindowPlacement snappedOn(const QString& windowId, const QHash<int, QStringList>& zonesByDesktop,
                          const QStringList& zoneIds = {kZoneA})
{
    WindowPlacement p;
    p.windowId = windowId;
    p.appId = kApp;
    p.screenId = kScreen;
    p.virtualDesktop = 1;
    EngineSlot slot;
    slot.state = WindowPlacement::stateSnapped();
    slot.zoneIds = zoneIds;
    slot.zonesByDesktop = zonesByDesktop;
    p.engines.insert(WindowPlacement::snapEngineId(), slot);
    return p;
}

std::optional<WindowPlacement> recordFor(const WindowPlacementStore& store, const QString& windowId)
{
    for (const WindowPlacement& p : store.records()) {
        if (p.windowId == windowId) {
            return p;
        }
    }
    return std::nullopt;
}

QHash<int, QStringList> zonesByDesktopOf(const WindowPlacementStore& store, const QString& windowId)
{
    const auto rec = recordFor(store, windowId);
    return rec ? rec->slotFor(WindowPlacement::snapEngineId()).zonesByDesktop : QHash<int, QStringList>{};
}
} // namespace

class TestWindowPlacementStoreDesktops : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // A capture describes the context it ran in, so it can only name the
    // desktops the window is a member of AT THAT MOMENT. A later capture that
    // names fewer desktops must not erase the rest: re-snapping on one
    // desktop overwrites that desktop's entry and leaves the others alone.
    void record_mergesZonesByDesktopPerDesktop()
    {
        WindowPlacementStore store;
        QVERIFY(store.record(snappedOn(kWindow, {{1, {kZoneA}}, {2, {kZoneB}}})));
        // The next capture, taken while desktop 2 is in view and re-snapped
        // there, names desktop 2 only.
        store.record(snappedOn(kWindow, {{2, {kZoneC}}}, {kZoneC}));

        const QHash<int, QStringList> merged = zonesByDesktopOf(store, kWindow);
        QCOMPARE(merged.size(), 2);
        QCOMPARE(merged.value(1), QStringList{kZoneA});
        QCOMPARE(merged.value(2), QStringList{kZoneC});
        // The rest of the slot is a snapshot the incoming capture owns.
        QCOMPARE(recordFor(store, kWindow)->slotFor(WindowPlacement::snapEngineId()).zoneIds, QStringList{kZoneC});
    }

    // Because record() merges, forgetting a desktop is a deliberate act: the
    // only way a stale entry ever leaves the record. It counts as a content
    // change, so the record's own sequence moves with it.
    void forgetDesktopZones_dropsOneDesktopAndStampsTheRecord()
    {
        WindowPlacementStore store;
        store.record(snappedOn(kWindow, {{1, {kZoneA}}, {2, {kZoneB}}}));
        const auto before = recordFor(store, kWindow)->sequence;

        QVERIFY(store.forgetDesktopZones(kWindow, WindowPlacement::snapEngineId(), 2));
        QCOMPARE(zonesByDesktopOf(store, kWindow), (QHash<int, QStringList>{{1, {kZoneA}}}));
        QVERIFY2(recordFor(store, kWindow)->sequence > before, "a forget is a content change and must restamp");

        // Nothing to forget: no change, no restamp.
        const auto after = recordFor(store, kWindow)->sequence;
        QVERIFY(!store.forgetDesktopZones(kWindow, WindowPlacement::snapEngineId(), 9));
        QVERIFY(!store.forgetDesktopZones(kWindow, WindowPlacement::snapEngineId(), 0));
        QCOMPARE(recordFor(store, kWindow)->sequence, after);
        // A merge afterwards does not resurrect the forgotten desktop: the
        // incoming capture names desktop 1 only and the stored map has no
        // entry for 2 any more.
        store.record(snappedOn(kWindow, {{1, {kZoneA}}}));
        QCOMPARE(zonesByDesktopOf(store, kWindow).size(), 1);
    }

    // Plasma renumbers x11 desktops when one in the middle is deleted, and
    // the engines shift their live per-desktop stores to match. The persisted
    // map has to follow or a restart seeds a zone under a number that now
    // belongs to a different desktop.
    void renumberDesktopZones_dropsTheRemovedDesktopAndShiftsTheRest()
    {
        WindowPlacementStore store;
        store.record(snappedOn(kWindow, {{1, {kZoneA}}, {2, {kZoneB}}, {3, {kZoneC}}}));
        // An unrelated record without a map is untouched and not counted.
        store.record(snappedOn(kSibling, {}));

        QCOMPARE(store.renumberDesktopZones(2), 1);
        const QHash<int, QStringList> shifted = zonesByDesktopOf(store, kWindow);
        QCOMPARE(shifted.size(), 2);
        QCOMPARE(shifted.value(1), QStringList{kZoneA});
        QCOMPARE(shifted.value(2), QStringList{kZoneC});
        QVERIFY(!shifted.contains(3));
        QVERIFY(zonesByDesktopOf(store, kSibling).isEmpty());
        // Removing a desktop above every entry changes nothing.
        QCOMPARE(store.renumberDesktopZones(7), 0);
        QCOMPARE(store.renumberDesktopZones(0), 0);
    }

    // The record-level desktop indexes the same numbering as the map, so it
    // shifts with it; a record ON the removed desktop no longer knows where
    // it was.
    void renumberDesktopZones_shiftsTheRecordsOwnDesktop()
    {
        WindowPlacementStore store;
        WindowPlacement above = snappedOn(kWindow, {{3, {kZoneC}}});
        above.virtualDesktop = 3;
        store.record(above);
        WindowPlacement removed = snappedOn(kSibling, {});
        removed.virtualDesktop = 2;
        store.record(removed);

        QCOMPARE(store.renumberDesktopZones(2), 2);
        QCOMPARE(recordFor(store, kWindow)->virtualDesktop, 2);
        QCOMPARE(zonesByDesktopOf(store, kWindow).value(2), QStringList{kZoneC});
        QCOMPARE(recordFor(store, kSibling)->virtualDesktop, 0);
    }

    // The whole point of the map is surviving a restart.
    void serializeRoundTrip_keepsZonesByDesktop()
    {
        WindowPlacementStore store;
        store.record(snappedOn(kWindow, {{1, {kZoneA}}, {2, {kZoneB, kZoneC}}}));
        const QJsonObject saved = store.serialize();

        WindowPlacementStore loaded;
        loaded.deserialize(saved);
        const QHash<int, QStringList> map = zonesByDesktopOf(loaded, kWindow);
        QCOMPARE(map.size(), 2);
        QCOMPARE(map.value(1), QStringList{kZoneA});
        QCOMPARE(map.value(2), (QStringList{kZoneB, kZoneC}));
    }

    // session.json is on-disk input this process does not control. A
    // desktop key that is not a plausible desktop number is dropped rather
    // than minting a per-desktop store nothing prunes, the same bound the
    // record-level desktop already has.
    void fromJson_dropsImplausibleDesktopKeys()
    {
        QJsonObject obj = snappedOn(kWindow, {{2, {kZoneB}}}).toJson();
        QJsonObject engines = obj.value(QLatin1String("engines")).toObject();
        QJsonObject slot = engines.value(WindowPlacement::snapEngineId()).toObject();
        QJsonObject byDesktop = slot.value(QLatin1String("zonesByDesktop")).toObject();
        byDesktop.insert(QStringLiteral("0"), QJsonArray{kZoneA});
        byDesktop.insert(QStringLiteral("-3"), QJsonArray{kZoneA});
        byDesktop.insert(QStringLiteral("junk"), QJsonArray{kZoneA});
        byDesktop.insert(QString::number(PhosphorEngine::MAX_PLAUSIBLE_VIRTUAL_DESKTOP + 1), QJsonArray{kZoneA});
        byDesktop.insert(QStringLiteral("5"), QJsonArray{QString()}); // an empty zone id names nothing
        slot.insert(QLatin1String("zonesByDesktop"), byDesktop);
        engines.insert(WindowPlacement::snapEngineId(), slot);
        obj.insert(QLatin1String("engines"), engines);

        const WindowPlacement parsed = WindowPlacement::fromJson(kApp, obj);
        const QHash<int, QStringList> map = parsed.slotFor(WindowPlacement::snapEngineId()).zonesByDesktop;
        QCOMPARE(map.size(), 1);
        QCOMPARE(map.value(2), QStringList{kZoneB});
    }

    // A window floating on the desktop in view but snapped on another is a
    // managed placement: it must not be evicted as contentless residue, and
    // it must not be collapsed away as a pure float duplicate.
    void zonesByDesktop_countsAsRestorableContentAndIsNotAPureFloat()
    {
        WindowPlacement floatingHereSnappedThere;
        floatingHereSnappedThere.windowId = kWindow;
        floatingHereSnappedThere.appId = kApp;
        floatingHereSnappedThere.screenId = kScreen;
        EngineSlot slot;
        slot.state = WindowPlacement::stateFloating();
        slot.zonesByDesktop.insert(2, {kZoneB});
        floatingHereSnappedThere.engines.insert(WindowPlacement::snapEngineId(), slot);
        QVERIFY(floatingHereSnappedThere.hasRestorableContent());

        WindowPlacement plainFloat = floatingHereSnappedThere;
        plainFloat.engines[WindowPlacement::snapEngineId()].zonesByDesktop.clear();
        QVERIFY2(!plainFloat.hasRestorableContent(), "a geometry-less floated slot with no zones is residue");

        // The collapse keeps one pure-float record per app; the record that
        // still names a zone on another desktop is not a pure float and
        // survives beside the keeper.
        WindowPlacementStore store;
        WindowPlacement keeper;
        keeper.windowId = kSibling;
        keeper.appId = kApp;
        keeper.screenId = kScreen;
        keeper.freeGeometryByScreen.insert(kScreen, QRect(10, 20, 300, 400));
        store.record(keeper);
        floatingHereSnappedThere.freeGeometryByScreen.insert(kScreen, QRect(10, 20, 300, 400));
        store.record(floatingHereSnappedThere);
        store.collapsePureFloatSiblings(kApp, kSibling);
        QVERIFY(recordFor(store, kSibling).has_value());
        QVERIFY2(recordFor(store, kWindow).has_value(), "a record snapped on another desktop is not a float duplicate");
    }
};

QTEST_GUILESS_MAIN(TestWindowPlacementStoreDesktops)
#include "test_window_placement_store_desktops.moc"
