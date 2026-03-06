// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layout_core.cpp
 * @brief Unit tests for Layout dirty tracking, batch modify, per-side gaps, copy constructor
 */

#include <QTest>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

#include "core/layout.h"
#include "core/zone.h"

using namespace PlasmaZones;

class TestLayoutCore : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════════
    // P0: Dirty tracking
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayout_dirtyTracking_newLayoutIsClean()
    {
        // A freshly constructed layout is NOT dirty -- it has never been modified.
        // LayoutManager::addLayout() calls markDirty() explicitly when adding.
        Layout layout(QStringLiteral("Fresh"));
        QVERIFY(!layout.isDirty());
    }

    void testLayout_dirtyTracking_clearDirtyAfterSave()
    {
        Layout layout(QStringLiteral("Test"));
        layout.setName(QStringLiteral("Changed"));
        QVERIFY(layout.isDirty());

        layout.clearDirty();
        QVERIFY(!layout.isDirty());
    }

    void testLayout_dirtyTracking_setterMarksDirty()
    {
        Layout layout(QStringLiteral("Test"));
        layout.clearDirty();
        QVERIFY(!layout.isDirty());

        layout.setName(QStringLiteral("NewName"));
        QVERIFY(layout.isDirty());

        layout.clearDirty();
        layout.setDescription(QStringLiteral("desc"));
        QVERIFY(layout.isDirty());

        layout.clearDirty();
        layout.setZonePadding(10);
        QVERIFY(layout.isDirty());

        layout.clearDirty();
        layout.setOuterGap(5);
        QVERIFY(layout.isDirty());

        layout.clearDirty();
        layout.setHiddenFromSelector(true);
        QVERIFY(layout.isDirty());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P0: Batch modify
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayout_batchModify_defersLayoutModifiedSignal()
    {
        Layout layout(QStringLiteral("Test"));
        QSignalSpy modifiedSpy(&layout, &Layout::layoutModified);

        layout.beginBatchModify();

        layout.setName(QStringLiteral("A"));
        layout.setDescription(QStringLiteral("B"));
        layout.setZonePadding(10);

        QCOMPARE(modifiedSpy.count(), 0);

        layout.endBatchModify();

        QCOMPARE(modifiedSpy.count(), 1);
    }

    void testLayout_batchModify_nestedDepth()
    {
        Layout layout(QStringLiteral("Test"));
        QSignalSpy modifiedSpy(&layout, &Layout::layoutModified);

        layout.beginBatchModify();
        layout.beginBatchModify();

        layout.setName(QStringLiteral("Nested"));
        QCOMPARE(modifiedSpy.count(), 0);

        layout.endBatchModify();
        QCOMPARE(modifiedSpy.count(), 0);

        layout.endBatchModify();
        QCOMPARE(modifiedSpy.count(), 1);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P1: Per-side outer gap overrides
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayout_perSideOuterGap_sentinel_minus1_isDefault()
    {
        Layout layout;
        QCOMPARE(layout.outerGapTop(), -1);
        QCOMPARE(layout.outerGapBottom(), -1);
        QCOMPARE(layout.outerGapLeft(), -1);
        QCOMPARE(layout.outerGapRight(), -1);
        QVERIFY(!layout.usePerSideOuterGap());
    }

    void testLayout_perSideOuterGap_setter_clampsBelow_minus1()
    {
        Layout layout;
        layout.setOuterGapTop(-5);
        QCOMPARE(layout.outerGapTop(), -1);

        layout.setOuterGapBottom(-100);
        QCOMPARE(layout.outerGapBottom(), -1);

        layout.setOuterGapLeft(-2);
        QCOMPARE(layout.outerGapLeft(), -1);

        layout.setOuterGapRight(-999);
        QCOMPARE(layout.outerGapRight(), -1);

        layout.setZonePadding(-3);
        QCOMPARE(layout.zonePadding(), -1);

        layout.setOuterGap(-50);
        QCOMPARE(layout.outerGap(), -1);
    }

    void testLayout_hasPerSideOuterGapOverride_allNegativeOneMeansFalse()
    {
        Layout layout;
        layout.setUsePerSideOuterGap(true);
        QVERIFY(!layout.hasPerSideOuterGapOverride());

        layout.setOuterGapTop(10);
        QVERIFY(layout.hasPerSideOuterGapOverride());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P1: Copy constructor
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayout_copyConstructor_deepCopiesZones()
    {
        Layout original(QStringLiteral("Original"));
        auto* zone = new Zone();
        zone->setRelativeGeometry(QRectF(0, 0, 0.5, 1));
        zone->setName(QStringLiteral("Zone1"));
        original.addZone(zone);

        Layout copy(original);
        QCOMPARE(copy.zoneCount(), 1);
        QCOMPARE(copy.zone(0)->name(), QStringLiteral("Zone1"));

        copy.zone(0)->setName(QStringLiteral("Modified"));
        QCOMPARE(original.zone(0)->name(), QStringLiteral("Zone1"));
        QCOMPARE(copy.zone(0)->name(), QStringLiteral("Modified"));

        QVERIFY(original.zone(0) != copy.zone(0));
    }

    void testLayout_copyConstructor_newId()
    {
        Layout original(QStringLiteral("Original"));
        Layout copy(original);

        QVERIFY(copy.id() != original.id());
        QVERIFY(!copy.id().isNull());
    }

    void testLayout_copyConstructor_noSourcePath()
    {
        Layout original(QStringLiteral("Original"));
        original.setSourcePath(QStringLiteral("/usr/share/plasmazones/layouts/test.json"));

        Layout copy(original);

        QVERIFY(copy.sourcePath().isEmpty());
    }
};

QTEST_MAIN(TestLayoutCore)
#include "test_layout_core.moc"
