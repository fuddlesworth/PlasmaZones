// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// buildCustomOrder() bridges the persisted priority order to the LayoutPreview
// id namespace used by sortPreviews(). Snapping layouts are keyed by bare UUID
// in both, but tiling algorithms are stored bare ("bsp") while their previews
// are keyed "autotile:bsp" — so the tiling ids must be prefixed or the priority
// order silently no-ops for the tiling view.

#include <QTest>

#include "core/utils/unifiedlayoutlist.h"

#include "helpers/StubSettings.h"

using PhosphorZones::LayoutUtils::buildCustomOrder;

class TestUnifiedLayoutOrder : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void tilingOrder_prefixedToPreviewNamespace()
    {
        PlasmaZones::StubSettings s;
        s.setTilingAlgorithmOrder({QStringLiteral("bsp"), QStringLiteral("theater")});

        const QStringList order = buildCustomOrder(&s, /*includeManual=*/false, /*includeAutotile=*/true);
        QCOMPARE(order, (QStringList{QStringLiteral("autotile:bsp"), QStringLiteral("autotile:theater")}));
    }

    void snappingOrder_keptAsBareUuid()
    {
        PlasmaZones::StubSettings s;
        s.setSnappingLayoutOrder({QStringLiteral("{11111111-1111-1111-1111-111111111111}"),
                                  QStringLiteral("{22222222-2222-2222-2222-222222222222}")});

        const QStringList order = buildCustomOrder(&s, /*includeManual=*/true, /*includeAutotile=*/false);
        QCOMPARE(order,
                 (QStringList{QStringLiteral("{11111111-1111-1111-1111-111111111111}"),
                              QStringLiteral("{22222222-2222-2222-2222-222222222222}")}));
    }

    void combined_snappingThenPrefixedTiling()
    {
        PlasmaZones::StubSettings s;
        s.setSnappingLayoutOrder({QStringLiteral("{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}")});
        s.setTilingAlgorithmOrder({QStringLiteral("bsp")});

        const QStringList order = buildCustomOrder(&s, /*includeManual=*/true, /*includeAutotile=*/true);
        QCOMPARE(
            order,
            (QStringList{QStringLiteral("{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}"), QStringLiteral("autotile:bsp")}));
    }

    void nullSettings_returnsEmpty()
    {
        QVERIFY(buildCustomOrder(nullptr, true, true).isEmpty());
    }
};

QTEST_MAIN(TestUnifiedLayoutOrder)
#include "test_unified_layout_order.moc"
