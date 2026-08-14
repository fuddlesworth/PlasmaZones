// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// stripColumnsToVariantList — the QML wire shape behind the strip-mode zone
// selector's stripColumns property. Pins the key set (explicitly, via the
// exact key list — a value compare against a coercion default cannot tell a
// present key from an absent one), the active-column stamp, and the position
// contract (no filtering, no reordering).

#include "common/stripcardserialize.h"

#include <PhosphorScrollEngine/ScrollEngineTypes.h>

#include <QtTest>

using namespace PhosphorScrollEngine;

class TestStripCardSerialize : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void emitsCanonicalShape()
    {
        ScrollStripSnapshot snap;
        snap.valid = true;
        snap.activeColumnIndex = 1;

        ScrollStripSnapshotColumn normal;
        // Non-default fraction so a dropped insert cannot pass as the
        // absent-key coercion value.
        normal.widthFraction = 0.4;
        // Non-zero origin and non-unit width so x, y and width all have
        // non-default expectations (a dropped insert cannot pass as the
        // absent-key coercion value).
        normal.tiles.append({.windowId = QStringLiteral("a"),
                             .relRect = QRectF(0.25, 0.5, 0.75, 0.5),
                             .minimized = false,
                             .hidden = false,
                             .activeTab = true});
        normal.tiles.append({.windowId = QStringLiteral("b"),
                             .relRect = QRectF(),
                             .minimized = false,
                             .hidden = false,
                             .activeTab = false});

        ScrollStripSnapshotColumn tabbedColumn;
        tabbedColumn.tabbed = true;
        tabbedColumn.tiles.append({.windowId = QStringLiteral("c"),
                                   .relRect = QRectF(0, 0, 1, 1),
                                   .minimized = false,
                                   .hidden = false,
                                   .activeTab = true});
        tabbedColumn.tiles.append({.windowId = QStringLiteral("d"),
                                   .relRect = QRectF(),
                                   .minimized = false,
                                   .hidden = true,
                                   .activeTab = false});

        snap.columns.append(normal);
        snap.columns.append(tabbedColumn);

        const QVariantList list = PlasmaZones::stripColumnsToVariantList(snap);
        QCOMPARE(list.size(), 2);

        // The exact key sets, so a dropped or renamed insert fails here
        // instead of surviving behind a coercion default.
        const QStringList columnKeys{QStringLiteral("active"), QStringLiteral("tabbed"), QStringLiteral("tiles"),
                                     QStringLiteral("widthFraction")};
        const QStringList tileKeys{QStringLiteral("activeTab"), QStringLiteral("height"), QStringLiteral("width"),
                                   QStringLiteral("x"), QStringLiteral("y")};

        const QVariantMap first = list.at(0).toMap();
        QCOMPARE(first.keys(), columnKeys);
        QCOMPARE(first.value(QStringLiteral("tabbed")).toBool(), false);
        QCOMPARE(first.value(QStringLiteral("active")).toBool(), false);
        QCOMPARE(first.value(QStringLiteral("widthFraction")).toReal(), 0.4);
        const QVariantList firstTiles = first.value(QStringLiteral("tiles")).toList();
        QCOMPARE(firstTiles.size(), 2);
        const QVariantMap tileA = firstTiles.at(0).toMap();
        QCOMPARE(tileA.keys(), tileKeys);
        QCOMPARE(tileA.value(QStringLiteral("x")).toReal(), 0.25);
        QCOMPARE(tileA.value(QStringLiteral("y")).toReal(), 0.5);
        QCOMPARE(tileA.value(QStringLiteral("width")).toReal(), 0.75);
        QCOMPARE(tileA.value(QStringLiteral("height")).toReal(), 0.5);
        QCOMPARE(tileA.value(QStringLiteral("activeTab")).toBool(), true);
        const QVariantMap tileB = firstTiles.at(1).toMap();
        QCOMPARE(tileB.value(QStringLiteral("width")).toReal(), 0.0);
        QCOMPARE(tileB.value(QStringLiteral("activeTab")).toBool(), false);

        const QVariantMap second = list.at(1).toMap();
        QCOMPARE(second.value(QStringLiteral("tabbed")).toBool(), true);
        QCOMPARE(second.value(QStringLiteral("active")).toBool(), true);
        const QVariantList secondTiles = second.value(QStringLiteral("tiles")).toList();
        QCOMPARE(secondTiles.size(), 2);
        QCOMPARE(secondTiles.at(0).toMap().value(QStringLiteral("activeTab")).toBool(), true);
        QCOMPARE(secondTiles.at(1).toMap().value(QStringLiteral("activeTab")).toBool(), false);
    }

    void emptySnapshotSerializesEmpty()
    {
        QCOMPARE(PlasmaZones::stripColumnsToVariantList(ScrollStripSnapshot{}).size(), 0);
    }

    /// An invalid snapshot serializes exactly like its columns say — the
    /// valid flag has no wire representation (the popup renders "no strip
    /// data" and "empty strip" identically on purpose; this pins that the
    /// serializer neither drops columns nor invents an error shape for it).
    void invalidSnapshotStillSerializesColumns()
    {
        ScrollStripSnapshot snap;
        snap.valid = false;
        ScrollStripSnapshotColumn column;
        column.tiles.append({.windowId = QStringLiteral("a"),
                             .relRect = QRectF(0, 0, 1, 1),
                             .minimized = false,
                             .hidden = false,
                             .activeTab = true});
        snap.columns.append(column);
        QCOMPARE(PlasmaZones::stripColumnsToVariantList(snap).size(), 1);
    }

    /// An out-of-range activeColumnIndex stamps no column active.
    void outOfRangeActiveIndexStampsNoColumn()
    {
        ScrollStripSnapshot snap;
        snap.valid = true;
        snap.activeColumnIndex = 5;
        for (int i = 0; i < 2; ++i) {
            ScrollStripSnapshotColumn column;
            column.tiles.append({.windowId = QStringLiteral("w"),
                                 .relRect = QRectF(0, 0, 1, 1),
                                 .minimized = false,
                                 .hidden = false,
                                 .activeTab = true});
            snap.columns.append(column);
        }
        const QVariantList list = PlasmaZones::stripColumnsToVariantList(snap);
        QCOMPARE(list.size(), 2);
        for (const QVariant& entry : list) {
            QCOMPARE(entry.toMap().value(QStringLiteral("active")).toBool(), false);
        }
    }
};

QTEST_APPLESS_MAIN(TestStripCardSerialize)
#include "test_stripcard_serialize.moc"
