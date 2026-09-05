// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWorkspaces/WorkspaceMap.h>

#include <QTest>

using PhosphorWorkspaces::WorkspaceEntry;
using PhosphorWorkspaces::WorkspaceMap;

namespace {
/// A desktop id in KWin's shape: a braced UUID. fromJson validates that shape
/// at the state-file boundary, so the fixtures have to speak it.
QString did(int n)
{
    return QStringLiteral("{00000000-0000-4000-8000-%1}").arg(n, 12, 10, QLatin1Char('0'));
}

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
    void fromJson_validatesIdShapeAndTrimsNames();
    void remove_erasesTheEmptiedSlice();
};

void TestWorkspaceMap::insertRemove_ownerIndexInvariant()
{
    WorkspaceMap map;
    map.setScreenOrder({QStringLiteral("A"), QStringLiteral("B")});
    map.insert(QStringLiteral("A"), 0, entry(did(1)));
    map.insert(QStringLiteral("A"), 1, entry(did(2)));
    map.insert(QStringLiteral("B"), 0, entry(did(3)));

    QCOMPARE(map.ownerOf(did(1)), QStringLiteral("A"));
    QCOMPARE(map.ownerOf(did(3)), QStringLiteral("B"));
    QCOMPARE(map.sliceIndexOf(did(2)), 1);
    QCOMPARE(map.allDesktopIds(), QStringList({did(1), did(2), did(3)}));

    QVERIFY(map.remove(did(1)));
    QVERIFY(map.ownerOf(did(1)).isEmpty());
    QCOMPARE(map.sliceSize(QStringLiteral("A")), 1);
    QVERIFY(!map.remove(did(1)));
}

void TestWorkspaceMap::insert_repairsDoubleOwnership()
{
    WorkspaceMap map;
    map.setScreenOrder({QStringLiteral("A"), QStringLiteral("B")});
    map.insert(QStringLiteral("A"), 0, entry(did(1)));
    // Inserting the same id elsewhere removes it from the previous owner.
    map.insert(QStringLiteral("B"), 0, entry(did(1)));
    QCOMPARE(map.ownerOf(did(1)), QStringLiteral("B"));
    QCOMPARE(map.sliceSize(QStringLiteral("A")), 0);
}

void TestWorkspaceMap::reorderAndTransfer()
{
    WorkspaceMap map;
    map.setScreenOrder({QStringLiteral("A"), QStringLiteral("B")});
    map.insert(QStringLiteral("A"), 0, entry(did(1)));
    map.insert(QStringLiteral("A"), 1, entry(did(2), QStringLiteral("chat")));
    map.insert(QStringLiteral("A"), 2, entry(did(3)));

    QVERIFY(map.reorderWithinSlice(did(3), 0));
    QCOMPARE(map.slice(QStringLiteral("A")).first().desktopId, did(3));

    // Transfer keeps metadata (the named tag rides along).
    QVERIFY(map.transfer(did(2), QStringLiteral("B"), 0));
    QCOMPARE(map.ownerOf(did(2)), QStringLiteral("B"));
    QCOMPARE(map.entryFor(did(2)).name, QStringLiteral("chat"));
    QVERIFY(!map.transfer(did(99), QStringLiteral("B"), 0));
}

void TestWorkspaceMap::takeSlice_returnsOrderedAndForgets()
{
    WorkspaceMap map;
    map.setScreenOrder({QStringLiteral("A"), QStringLiteral("B")});
    map.insert(QStringLiteral("A"), 0, entry(did(1)));
    map.insert(QStringLiteral("A"), 1, entry(did(2)));

    const auto taken = map.takeSlice(QStringLiteral("A"));
    QCOMPARE(taken.size(), 2);
    QCOMPARE(taken.first().desktopId, did(1));
    QVERIFY(!map.hasScreen(QStringLiteral("A")));
    QVERIFY(map.ownerOf(did(1)).isEmpty());
    QVERIFY(!map.screenOrder().contains(QStringLiteral("A")));
}

void TestWorkspaceMap::globalPositionForInsert_arithmetic()
{
    WorkspaceMap map;
    map.setScreenOrder({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});
    map.insert(QStringLiteral("A"), 0, entry(did(11)));
    map.insert(QStringLiteral("A"), 1, entry(did(12)));
    map.insert(QStringLiteral("B"), 0, entry(did(21)));

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
    map.insert(QStringLiteral("B"), 0, entry(did(1)));
    // A reorder omitting a slice-holding screen appends it, never orphans it.
    map.setScreenOrder({QStringLiteral("A")});
    QVERIFY(map.screenOrder().contains(QStringLiteral("B")));
}

void TestWorkspaceMap::consistencyAndRepair()
{
    WorkspaceMap map;
    map.setScreenOrder({QStringLiteral("A")});
    map.insert(QStringLiteral("A"), 0, entry(did(1)));
    map.insert(QStringLiteral("A"), 1, entry(did(2)));

    QVERIFY(map.consistentWith({did(1), did(2)}));
    QVERIFY(!map.consistentWith({did(1)}));
    QVERIFY(!map.consistentWith({did(1), did(2), did(3)}));

    // The second desktop vanished and a third appeared: repair drops the former
    // and reports the latter.
    const QStringList unowned = map.repairAgainst({did(1), did(3)});
    QCOMPARE(unowned, QStringList({did(3)}));
    QVERIFY(map.ownerOf(did(2)).isEmpty());
    QCOMPARE(map.sliceSize(QStringLiteral("A")), 1);
}

void TestWorkspaceMap::serialization_roundTrip()
{
    WorkspaceMap map;
    map.setScreenOrder({QStringLiteral("A"), QStringLiteral("B")});
    map.insert(QStringLiteral("A"), 0, entry(did(1), QStringLiteral("chat")));
    map.insert(QStringLiteral("A"), 1, entry(did(2)));
    map.insert(QStringLiteral("B"), 0, entry(did(3)));
    map.setHomeScreen(did(3), QStringLiteral("C"));

    QHash<QString, int> current;
    current.insert(QStringLiteral("A"), 2);
    current.insert(QStringLiteral("B"), 3);
    const auto indexOf = [](const QString& id) {
        if (id == did(1)) {
            return 1;
        }
        if (id == did(2)) {
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
    QCOMPARE(restored.entryFor(did(1)).name, QStringLiteral("chat"));
    QCOMPARE(restored.entryFor(did(3)).homeScreenId, QStringLiteral("C"));
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
    const QString duplicate = QStringLiteral(
                                  "{\"v\":1,\"screenOrder\":[\"A\",\"B\"],\"slices\":{\"A\":[{\"id\":\"%1\"}],"
                                  "\"B\":[{\"id\":\"%1\"}]}}")
                                  .arg(did(1));
    QVERIFY(map.fromJson(duplicate));
    QCOMPARE(map.ownerOf(did(1)), QStringLiteral("A"));
    QCOMPARE(map.sliceSize(QStringLiteral("B")), 0);
}

void TestWorkspaceMap::fromJson_validatesIdShapeAndTrimsNames()
{
    WorkspaceMap map;
    // The state file is a boundary. An id that is not a UUID can never name a
    // real KWin desktop, so it is refused at parse rather than carried until
    // the first settle drops it.
    const QString bogusId = QStringLiteral(
                                "{\"v\":1,\"screenOrder\":[\"A\"],\"slices\":{\"A\":[{\"id\":\"not-a-uuid\"},"
                                "{\"id\":\"%1\"}]}}")
                                .arg(did(1));
    QVERIFY(map.fromJson(bogusId));
    QCOMPARE(map.allDesktopIds(), QStringList({did(1)}));

    // A whitespace-only name is no name: kept as-is it would render as nothing
    // and still make the desktop destroy-exempt, with nothing on screen to
    // explain why the workspace never goes away.
    const QString blankName =
        QStringLiteral("{\"v\":1,\"screenOrder\":[\"A\"],\"slices\":{\"A\":[{\"id\":\"%1\",\"name\":\"   \"}]}}")
            .arg(did(2));
    QVERIFY(map.fromJson(blankName));
    QVERIFY(map.entryFor(did(2)).name.isEmpty());
}

void TestWorkspaceMap::remove_erasesTheEmptiedSlice()
{
    WorkspaceMap map;
    map.setScreenOrder({QStringLiteral("A"), QStringLiteral("B")});
    map.insert(QStringLiteral("A"), 0, entry(did(1)));

    // hasScreen() answers "holds a slice", so a screen whose last desktop just
    // left must answer false — an empty list left in the hash made it true.
    QVERIFY(map.hasScreen(QStringLiteral("A")));
    QVERIFY(map.remove(did(1)));
    QVERIFY(!map.hasScreen(QStringLiteral("A")));
    // Still part of the world, though: the screen order is untouched.
    QVERIFY(map.knowsScreen(QStringLiteral("A")));

    // The same invariant across a transfer, which is a remove plus an insert.
    map.insert(QStringLiteral("A"), 0, entry(did(2)));
    QVERIFY(map.transfer(did(2), QStringLiteral("B"), 0));
    QVERIFY(!map.hasScreen(QStringLiteral("A")));
    QVERIFY(map.hasScreen(QStringLiteral("B")));
}

QTEST_GUILESS_MAIN(TestWorkspaceMap)
#include "test_workspace_map.moc"
