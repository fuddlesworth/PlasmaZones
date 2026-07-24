// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Verifies that autotile algorithm capability flags flatten into the serialized
// LayoutPreview shape consumed by QML and D-Bus. Focused on reflowsOnFocus (the
// "Follows Focus" capability) since it is the newest flag, but the assertions
// also guard the shared writeAlgorithmFlat() path used by both projections.

#include <QJsonObject>
#include <QTest>
#include <QVariantMap>

#include <PhosphorLayoutApi/AlgorithmMetadata.h>
#include <PhosphorLayoutApi/LayoutPreview.h>

#include "common/layoutpreviewserialize.h"

using namespace PlasmaZones;

class TestLayoutPreviewSerialize : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void reflowsOnFocus_emittedInBothShapes()
    {
        PhosphorLayout::LayoutPreview preview;
        preview.id = QStringLiteral("autotile:theater");
        preview.displayName = QStringLiteral("Theater");
        PhosphorLayout::AlgorithmMetadata meta;
        meta.reflowsOnFocus = true;
        preview.algorithm = meta;

        // QVariantMap (QML) projection.
        const QVariantMap vm = toVariantMap(preview);
        QVERIFY(vm.contains(QStringLiteral("reflowsOnFocus")));
        QCOMPARE(vm.value(QStringLiteral("reflowsOnFocus")).toBool(), true);

        // QJsonObject (D-Bus) projection — must mirror the QVariantMap shape.
        const QJsonObject js = toJson(preview);
        QVERIFY(js.contains(QStringLiteral("reflowsOnFocus")));
        QCOMPARE(js.value(QStringLiteral("reflowsOnFocus")).toBool(), true);
    }

    void reflowsOnFocus_defaultsFalseForOrdinaryAlgorithm()
    {
        PhosphorLayout::LayoutPreview preview;
        preview.id = QStringLiteral("autotile:columns");
        preview.displayName = QStringLiteral("Columns");
        preview.algorithm = PhosphorLayout::AlgorithmMetadata{};

        QCOMPARE(toVariantMap(preview).value(QStringLiteral("reflowsOnFocus")).toBool(), false);
        QCOMPARE(toJson(preview).value(QStringLiteral("reflowsOnFocus")).toBool(), false);
    }

    void reflowsOnFocus_absentForManualLayout()
    {
        // A manual (non-autotile) preview carries no algorithm metadata, so the
        // flattened flags — including reflowsOnFocus — are not emitted at all.
        PhosphorLayout::LayoutPreview preview;
        preview.id = QStringLiteral("{00000000-0000-0000-0000-000000000000}");
        preview.displayName = QStringLiteral("Manual");
        QVERIFY(!preview.isAutotile());

        QVERIFY(!toVariantMap(preview).contains(QStringLiteral("reflowsOnFocus")));
        QVERIFY(!toJson(preview).contains(QStringLiteral("reflowsOnFocus")));
    }

    void masterCount_emittedInBothShapes()
    {
        // The renderer marks this many leading zones as masters, so the resolved
        // count the source computed the geometry with has to reach the wire. It
        // previously did not, pinning every preview's dot count to 1.
        PhosphorLayout::LayoutPreview preview;
        preview.id = QStringLiteral("autotile:master-stack");
        preview.displayName = QStringLiteral("Master Stack");
        PhosphorLayout::AlgorithmMetadata meta;
        meta.supportsMasterCount = true;
        preview.algorithm = meta;
        preview.masterCount = 3;

        QCOMPARE(toVariantMap(preview).value(QStringLiteral("masterCount")).toInt(), 3);
        QCOMPARE(toJson(preview).value(QStringLiteral("masterCount")).toInt(), 3);
    }

    void masterCount_absentForManualLayout()
    {
        // Manual layouts have no master concept, so the key stays off the wire
        // and the QML side keeps its `!== undefined` fallback to 1.
        PhosphorLayout::LayoutPreview preview;
        preview.id = QStringLiteral("{00000000-0000-0000-0000-000000000000}");
        preview.displayName = QStringLiteral("Manual");
        QVERIFY(!preview.isAutotile());

        QVERIFY(!toVariantMap(preview).contains(QStringLiteral("masterCount")));
        QVERIFY(!toJson(preview).contains(QStringLiteral("masterCount")));
    }
};

QTEST_MAIN(TestLayoutPreviewSerialize)
#include "test_layout_preview_serialize.moc"
