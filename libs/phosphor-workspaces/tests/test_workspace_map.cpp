// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWorkspaces/WorkspaceMap.h>

#include <QTest>

using PhosphorWorkspaces::WorkspaceEntry;
using PhosphorWorkspaces::WorkspaceMap;

namespace {
WorkspaceEntry entry(const QString& id, const QString& name = QString())
{
    WorkspaceEntry e;
    e.desktopId = id;
    e.name = name;
    return e;
}
}

class TestWorkspaceMap : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void insertRemove_ownerIndexInvariant();
    void insert_repairsDoubleOwnership();
    void reorderAndTransfer();
    void takeSlice_returnsOrderedAndForgets();
    void globalPositionForInsert_arithmetic();
    void screenOrder_keepsSliceHolders();
    void consistencyAndRepair();
    void serialization_roundTrip();
    void fromJson_rejectsBadVersionAndGarbage();
};

void TestWorkspaceMap::insertRemove_ownerIndexInvariant()
{
    WorkspaceMap map;
    map.setScreenOrder({QStringLiteral("A"), QStringLiteral("B")});
    map.insert(QStringLiteral("A"), 0, entry(QStringLiteral("{d1}")));
    map.insert(QStringLiteral("A"), 1, entry(QStringLiteral("{d2}")));
    map.insert(QStringLiteral("B"), 0, entry(QStringLiteral("{d3}")));

    QCOMPARE(map.ownerOf(QStringLiteral("{d1}")), QStringLiteral("A"));
    QCOMPARE(map.ownerOf(QStringLiteral("{d3}")), QStringLiteral("B"));
    QCOMPARE(map.sliceIndexOf(QStringLiteral("{d2}")), 1);
    QCOMPARE(map.allDesktopIds(),
             QStringList({QStringLiteral("{d1}"), QStringLiteral("{d2}"), QStringLiteral("{d3}")}));

    QVERIFY(map.remove(QStringLiteral("{d1}")));
    QVERIFY(map.ownerOf(QStringLiteral("{d1}")).isEmpty());
    QCOMPARE(map.sliceSize(QStringLiteral("A")), 1);
    QVERIFY(!map.remove(QStringLiteral("{d1}")));
}

void TestWorkspaceMap::insert_repairsDoubleOwnership()
{
    WorkspaceMap map;
    map.setScreenOrder({QStringLiteral("A"), QStringLiteral("B")});
    map.insert(QStringLiteral("A"), 0, entry(QStringLiteral("{d1}")));
    // Inserting the same id elsewhere removes it from the previous owner.
    map.insert(QStringLiteral("B"), 0, entry(QStringLiteral("{d1}")));
    QCOMPARE(map.ownerOf(QStringLiteral("{d1}")), QStringLiteral("B"));
    QCOMPARE(map.sliceSize(QStringLiteral("A")), 0);
}

void TestWorkspaceMap::reorderAndTransfer()
{
    WorkspaceMap map;
    map.setScreenOrder({QStringLiteral("A"), QStringLiteral("B")});
    map.insert(QStringLiteral("A"), 0, entry(QStringLiteral("{d1}")));
    map.insert(QStringLiteral("A"), 1, entry(QStringLiteral("{d2}"), QStringLiteral("chat")));
    map.insert(QStringLiteral("A"), 2, entry(QStringLiteral("{d3}")));

    QVERIFY(map.reorderWithinSlice(QStringLiteral("{d3}"), 0));
    QCOMPARE(map.slice(QStringLiteral("A")).first().desktopId, QStringLiteral("{d3}"));

    // Transfer keeps metadata (the named tag rides along).
    QVERIFY(map.transfer(QStringLiteral("{d2}"), QStringLiteral("B"), 0));
    QCOMPARE(map.ownerOf(QStringLiteral("{d2}")), QStringLiteral("B"));
    QCOMPARE(map.entryFor(QStringLiteral("{d2}")).name, QStringLiteral("chat"));
    QVERIFY(!map.transfer(QStringLiteral("{nope}"), QStringLiteral("B"), 0));
}

void TestWorkspaceMap::takeSlice_returnsOrderedAndForgets()
{
    WorkspaceMap map;
    map.setScreenOrder({QStringLiteral("A"), QStringLiteral("B")});
    map.insert(QStringLiteral("A"), 0, entry(QStringLiteral("{d1}")));
    map.insert(QStringLiteral("A"), 1, entry(QStringLiteral("{d2}")));

    const auto taken = map.takeSlice(QStringLiteral("A"));
    QCOMPARE(taken.size(), 2);
    QCOMPARE(taken.first().desktopId, QStringLiteral("{d1}"));
    QVERIFY(!map.hasScreen(QStringLiteral("A")));
    QVERIFY(map.ownerOf(QStringLiteral("{d1}")).isEmpty());
    QVERIFY(!map.screenOrder().contains(QStringLiteral("A")));
}

void TestWorkspaceMap::globalPositionForInsert_arithmetic()
{
    WorkspaceMap map;
    map.setScreenOrder({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});
    map.insert(QStringLiteral("A"), 0, entry(QStringLiteral("{a1}")));
    map.insert(QStringLiteral("A"), 1, entry(QStringLiteral("{a2}")));
    map.insert(QStringLiteral("B"), 0, entry(QStringLiteral("{b1}")));

    QCOMPARE(map.globalPositionForInsert(QStringLiteral("A"), 0), 0u);
    QCOMPARE(map.globalPositionForInsert(QStringLiteral("A"), 2), 2u);
    QCOMPARE(map.globalPositionForInsert(QStringLiteral("A"), 99), 2u); // clamped to slice size
    QCOMPARE(map.globalPositionForInsert(QStringLiteral("B"), 1), 3u);
    QCOMPARE(map.globalPositionForInsert(QStringLiteral("C"), 0), 3u);
}

void TestWorkspaceMap::screenOrder_keepsSliceHolders()
{
    WorkspaceMap map;
    map.setScreenOrder({QStringLiteral("A"), QStringLiteral("B")});
    map.insert(QStringLiteral("B"), 0, entry(QStringLiteral("{d1}")));
    // A reorder omitting a slice-holding screen appends it, never orphans it.
    map.setScreenOrder({QStringLiteral("A")});
    QVERIFY(map.screenOrder().contains(QStringLiteral("B")));
}

void TestWorkspaceMap::consistencyAndRepair()
{
    WorkspaceMap map;
    map.setScreenOrder({QStringLiteral("A")});
    map.insert(QStringLiteral("A"), 0, entry(QStringLiteral("{d1}")));
    map.insert(QStringLiteral("A"), 1, entry(QStringLiteral("{d2}")));

    QVERIFY(map.consistentWith({QStringLiteral("{d1}"), QStringLiteral("{d2}")}));
    QVERIFY(!map.consistentWith({QStringLiteral("{d1}")}));
    QVERIFY(!map.consistentWith({QStringLiteral("{d1}"), QStringLiteral("{d2}"), QStringLiteral("{d3}")}));

    // {d2} vanished, {d3} appeared: repair drops the former, reports the latter.
    const QStringList unowned = map.repairAgainst({QStringLiteral("{d1}"), QStringLiteral("{d3}")});
    QCOMPARE(unowned, QStringList({QStringLiteral("{d3}")}));
    QVERIFY(map.ownerOf(QStringLiteral("{d2}")).isEmpty());
    QCOMPARE(map.sliceSize(QStringLiteral("A")), 1);
}

void TestWorkspaceMap::serialization_roundTrip()
{
    WorkspaceMap map;
    map.setScreenOrder({QStringLiteral("A"), QStringLiteral("B")});
    map.insert(QStringLiteral("A"), 0, entry(QStringLiteral("{d1}"), QStringLiteral("chat")));
    map.insert(QStringLiteral("A"), 1, entry(QStringLiteral("{d2}")));
    map.insert(QStringLiteral("B"), 0, entry(QStringLiteral("{d3}")));
    map.setHomeScreen(QStringLiteral("{d3}"), QStringLiteral("C"));

    QHash<QString, int> current;
    current.insert(QStringLiteral("A"), 2);
    current.insert(QStringLiteral("B"), 3);
    const auto indexOf = [](const QString& id) {
        if (id == QStringLiteral("{d1}")) {
            return 1;
        }
        if (id == QStringLiteral("{d2}")) {
            return 2;
        }
        return 3;
    };

    const QString stateJson = map.toJson(7, current, indexOf, /*includeState=*/true);
    QVERIFY(stateJson.contains(QStringLiteral("\"generation\":7")));
    QVERIFY(stateJson.contains(QStringLiteral("\"current\":true")));
    QVERIFY(stateJson.contains(QStringLiteral("\"homeScreen\":\"C\"")));

    WorkspaceMap restored;
    QVERIFY(restored.fromJson(stateJson));
    QCOMPARE(restored.screenOrder(), map.screenOrder());
    QCOMPARE(restored.allDesktopIds(), map.allDesktopIds());
    QCOMPARE(restored.entryFor(QStringLiteral("{d1}")).name, QStringLiteral("chat"));
    QCOMPARE(restored.entryFor(QStringLiteral("{d3}")).homeScreenId, QStringLiteral("C"));
    QVERIFY(restored == map);

    // The wire flavour omits homeScreen.
    const QString wireJson = map.toJson(8, current, indexOf, /*includeState=*/false);
    QVERIFY(!wireJson.contains(QStringLiteral("homeScreen")));
}

void TestWorkspaceMap::fromJson_rejectsBadVersionAndGarbage()
{
    WorkspaceMap map;
    QVERIFY(!map.fromJson(QStringLiteral("not json")));
    QVERIFY(!map.fromJson(QStringLiteral("{\"v\":99,\"screenOrder\":[],\"slices\":{}}")));
    QVERIFY(map.fromJson(QStringLiteral("{\"v\":1,\"screenOrder\":[],\"slices\":{}}")));
    QVERIFY(map.allDesktopIds().isEmpty());
    // A duplicate id across slices is dropped on parse (first owner wins).
    QVERIFY(map.fromJson(QStringLiteral(
        "{\"v\":1,\"screenOrder\":[\"A\",\"B\"],\"slices\":{\"A\":[{\"id\":\"{x}\"}],\"B\":[{\"id\":\"{x}\"}]}}")));
    QCOMPARE(map.ownerOf(QStringLiteral("{x}")), QStringLiteral("A"));
    QCOMPARE(map.sliceSize(QStringLiteral("B")), 0);
}

QTEST_GUILESS_MAIN(TestWorkspaceMap)
#include "test_workspace_map.moc"
