// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// stripColumnsToVariantList — the QML wire shape behind the strip-mode zone
// selector's stripColumns property. Pins the key set, the active-column
// stamp, and the position contract (no filtering, no reordering).

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
        normal.relWidth = 0.5;
        normal.relHeight = 1.0;
        normal.tiles.append({QStringLiteral("a"), QRectF(0, 0, 1, 0.5), false, false, true});
        normal.tiles.append({QStringLiteral("b"), QRectF(), true, false, false}); // minimized, rect-less

        ScrollStripSnapshotColumn tabbedColumn;
        tabbedColumn.tabbed = true;
        tabbedColumn.relWidth = 0.33;
        tabbedColumn.relHeight = 1.0;
        tabbedColumn.tiles.append({QStringLiteral("c"), QRectF(0, 0, 1, 1), false, false, true});
        tabbedColumn.tiles.append({QStringLiteral("d"), QRectF(), false, true, false}); // hidden tab

        snap.columns.append(normal);
        snap.columns.append(tabbedColumn);

        const QVariantList list = PlasmaZones::stripColumnsToVariantList(snap);
        QCOMPARE(list.size(), 2);

        const QVariantMap first = list.at(0).toMap();
        QCOMPARE(first.value(QStringLiteral("tabbed")).toBool(), false);
        QCOMPARE(first.value(QStringLiteral("active")).toBool(), false);
        QCOMPARE(first.value(QStringLiteral("relWidth")).toReal(), 0.5);
        const QVariantList firstTiles = first.value(QStringLiteral("tiles")).toList();
        QCOMPARE(firstTiles.size(), 2);
        const QVariantMap tileA = firstTiles.at(0).toMap();
        QCOMPARE(tileA.value(QStringLiteral("windowId")).toString(), QStringLiteral("a"));
        QCOMPARE(tileA.value(QStringLiteral("height")).toReal(), 0.5);
        QCOMPARE(tileA.value(QStringLiteral("activeTab")).toBool(), true);
        const QVariantMap tileB = firstTiles.at(1).toMap();
        QCOMPARE(tileB.value(QStringLiteral("minimized")).toBool(), true);
        QCOMPARE(tileB.value(QStringLiteral("width")).toReal(), 0.0);

        const QVariantMap second = list.at(1).toMap();
        QCOMPARE(second.value(QStringLiteral("tabbed")).toBool(), true);
        QCOMPARE(second.value(QStringLiteral("active")).toBool(), true);
        const QVariantList secondTiles = second.value(QStringLiteral("tiles")).toList();
        QCOMPARE(secondTiles.at(1).toMap().value(QStringLiteral("hidden")).toBool(), true);
    }

    void emptySnapshotSerializesEmpty()
    {
        QCOMPARE(PlasmaZones::stripColumnsToVariantList(ScrollStripSnapshot{}).size(), 0);
    }
};

QTEST_APPLESS_MAIN(TestStripCardSerialize)
#include "test_stripcard_serialize.moc"
